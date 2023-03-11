#include <arpa/inet.h>
#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "rtp.h"

#define SERVER_PORT 8554
#define SERVER_RTP_PORT 55532
#define SERVER_RTCP_PORT 55533
#define BUFFER_MAX_SIZE (1024 * 1024)
#define ACC_FILE_NAME "/home/llz/CPP/data/test-long.aac"

static int create_tcp_socket() {
    int sockfd;
    int on = 1;
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    return sockfd;
}

static int create_udp_socket() {
    int sockfd;
    int on = 1;
    sockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    return sockfd;
}

static int bind_socket_addr(int sockfd, const char* ip, int port) {
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        return -1;
    }
    return 0;
}

static int accept_client(int sockfd, char* ip, int* port) {
    int client_sockfd;
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    socklen_t addr_len;
    addr_len = sizeof(addr);
    client_sockfd = accept(sockfd, (struct sockaddr*) &addr, &addr_len);
    if (client_sockfd < 0) {
        return -1;
    }
    strcpy(ip, inet_ntoa(addr.sin_addr));
    *port = ntohs(addr.sin_port);
    return client_sockfd;
}

static int handle_cmd_OPTIONS(char* result, int cseq) {
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY\r\n"
            "\r\n",
            cseq);
    return 0;
}

static int handle_cmd_DESCRIBE(char* result, int cseq, char* url) {
    char sdp[500];
    char local_ip[100];
    sscanf(url, "rtsp://%[^:]:", local_ip);
    sprintf(sdp,
            "v=0\r\n"
            "o-= 9%ld 1 IN IP4 %s\r\n"
            "t=0 0\r\n"
            "a=control:*\r\n"
            "m=audio 0 RTP/AVP 97\r\n"
            "a=rtpmap:97 mpeg-generic/44100/2\r\n"
            "a=fmtp:97 profile-level-id=1;mode=AAC=hbr;sizelength=13;"
            "indexdeltalength=3;config=1210;\r\n"
            "a=control:track0\r\n",
            time(nullptr), local_ip);
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Content-Base: %s\r\n"
            "Content-type: application/sdp\r\n"
            "Content-length: %zu\r\n"
            "\r\n"
            "%s",
            cseq, url, strlen(sdp), sdp);
    return 0;
}

static int handle_cmd_SETUP(char* result, int cseq, int client_rtp_port) {
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
            "Session: 66334873\r\n"
            "\r\n",
            cseq, client_rtp_port, client_rtp_port + 1, SERVER_RTP_PORT,
            SERVER_RTCP_PORT);
    return 0;
}

static int handle_cmd_PLAY(char* result, int cseq) {
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Range: npt=0.000-\r\n"
            "Session: 66334873; timeout=10\r\n"
            "\r\n",
            cseq);
    return 0;
}

struct AdtsHeader {
    unsigned int syncword; // 12bit同步字，'1111.1111.1111'表示一个ADTS帧的开始
    uint8_t id; // 1bit 0表示MPEG-4，1表示MPEG-2
    uint8_t layer; // 2bit 必须为0
    uint8_t protection_absent; // 1bit 1表示没有CRC 0表示有CRC
    uint8_t profile; // 1bit AAC级别（MPEG-2定义了3中profile
                     // MPEG-4中定义了6种profile）
    uint8_t sampling_frequency_index; // 4bit 采样率
    uint8_t private_bit; // 1bit 编码时设置为0，解码时忽略
    uint8_t channel_cfg; // 3bit 声道数量
    uint8_t original_copy; // 1bit 编码时设置为0，解码时忽略
    uint8_t home; // 1bit 编码时设置为0，解码时忽略
    unsigned int aac_frame_length; // 13bit 一个ADTS帧的长度包含ADTS头和AAC原始流
    unsigned int adts_buffer_fullness; // 11bit 缓冲区充满度, 0x7FF说明是码率可变
    // 的码流，不需要此字段，CBR可能需要此字段，不同编码器的使用情况不同。
    /*
     * number_of_raw_data_blocks_in_frame
     * 表示ADTS帧有number_of_raw_data_blocks_in_frame + 1个AAC原始帧
     * 所以说该字段=0说明ADTS帧中有一个AAC数据块，并不是说没有
     * 一个AAC原始帧包含一段时间内1024个采样及相关数据*
     */
    uint8_t number_of_raw_data_blocks_in_frame; // 2bit
};

static int parse_adts_header(uint8_t* in, struct AdtsHeader* res) {
    static int frame_number;
    bzero(res, sizeof(*res));
    if (in[0] == 0xFF && (in[1] & 0xF0) == 0xF0) {
        // 符合同步字
        res->id = ()
    }
}
