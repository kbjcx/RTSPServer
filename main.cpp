#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <ctime>
#include <unistd.h>

#include <string>

#include "rtp.h"

#define SERVER_PORT 8554
#define SERVER_RTP_PORT 55532
#define SERVER_RTCP_PORT 55533

#define H264_FILE_NAME "../data/test.h264"
#define BUFFER_MAX_SIZE (1024 * 1024)

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
    /*
    ip: accept取出的已经建立连接的ip地址
    port：accept取出的已经建立连接的端口
    */
    int clientfd;
    struct sockaddr_in client_addr;
    bzero(&client_addr, sizeof(client_addr));
    socklen_t client_addr_len;
    client_addr_len = sizeof(client_addr);
    
    clientfd =
            accept(sockfd, (struct sockaddr*) &client_addr, &client_addr_len);
    if (clientfd < 0) {
        return -1;
    }
    
    strcpy(ip, inet_ntoa(client_addr.sin_addr));
    *port = ntohs(client_addr.sin_port);
    return clientfd;
}

static int handle_cmd_OPTIONS(char* result, int cseq) {
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, Play\r\n"
            "\r\n",
            cseq);
    return 0;
}

static inline int start_code_3(const char* buffer) {
    if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 1) {
        return 1;
    }
    else {
        return 0;
    }
}

static inline int start_code_4(const char* buffer) {
    if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 1) {
        return 1;
    }
    else {
        return 0;
    }
}

static char* find_next_start_code(char* buffer, int len) {
    int i;
    
    if (len < 3) {
        return nullptr;
    }
    
    for (i = 0; i < len - 3; ++i) {
        if (start_code_3(buffer) || start_code_4(buffer)) {
            return buffer;
        }
        ++buffer;
    }
    if (start_code_3(buffer)) {
        return buffer;
    }
    return nullptr;
}

static int get_frame_from_H264_file(FILE* fp, char* frame, int size) {
    int read_size, frame_size;
    char* next_start_code;
    
    if (fp < 0) {
        return -1;
    }
    
    read_size = fread(frame, 1, size, fp);
    
    if (!start_code_3(frame) && !start_code_4(frame)) {
        return -1;
    }
    
    next_start_code = find_next_start_code(frame + 3, read_size - 3);
    if (!next_start_code) {
        // lseek(fd, 0, SEEK_SET);
        // frame_size = read_Size;
        return -1;
    }
    else {
        frame_size = next_start_code - frame;
        fseek(fp, frame_size - read_size, SEEK_CUR);
    }
    
    return frame_size;
}

static int rtp_send_H264_frame(int server_rtp_sockfd, const char* ip,
                               int16_t port, struct RtpPacket* rtp_packet,
                               char* frame, uint32_t frame_size) {
    uint8_t nalu_first_byte;
    int send_bytes = 0;
    int ret;
    
    nalu_first_byte = frame[0];
    
    printf("frame size = %d \n", frame_size);
    if (frame_size <= RTP_MAX_PKT_SIZE) {
        // 单NALU模式
        //*   0 1 2 3 4 5 6 7 8 9
        //*  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //*  |F|NRI|  Type   | a single NAL unit ... |
        //*  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        memcpy(rtp_packet->payload, frame, frame_size);
        ret = rtp_send_packet_over_udp(server_rtp_sockfd, ip, port, rtp_packet,
                                       frame_size);
        if (ret < 0) {
            return -1;
        }
        
        ++rtp_packet->rtp_header.seq;
        send_bytes += ret;
        if ((nalu_first_byte & 0x1F) == 7 || (nalu_first_byte & 0x1F) == 8) {
            // 如果是SPS或者PPS类型，就不需要加时间戳
            goto out;
        }
    }
    else {
        // NALU包长度大于最大包长，分片模式
        //*  0                   1                   2
        //*  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
        //* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        //* | FU indicator  |   FU header   |   FU payload   ...  |
        //* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        
        
        
        //*     FU Indicator
        //*    0 1 2 3 4 5 6 7
        //*   +-+-+-+-+-+-+-+-+
        //*   |F|NRI|  Type   |
        //*   +---------------+
        
        
        
        //*      FU Header
        //*    0 1 2 3 4 5 6 7
        //*   +-+-+-+-+-+-+-+-+
        //*   |S|E|R|  Type   |
        //*   +---------------+
        
        int packet_num = frame_size / RTP_MAX_PKT_SIZE; // 完整的包的数量
        int remain_packet_size = frame_size % RTP_MAX_PKT_SIZE; // 剩余的不完整的包的大小
        int i, pos = 1;
        // 发送完整的包
        for (i = 0; i < packet_num; ++i) {
            rtp_packet->payload[0] = (nalu_first_byte & 0x60) | 28;
            rtp_packet->payload[1] = nalu_first_byte & 0x1F;
            if (i == 0) {
                // 第一包数据
                rtp_packet->payload[1] |= 0x80;
            }
            else if (remain_packet_size == 0 && i == packet_num - 1) {
                // 最后一包数据
                rtp_packet->payload[1] |= 0x40;
            }
            
            memcpy(rtp_packet->payload + 2, frame + pos, RTP_MAX_PKT_SIZE);
            ret = rtp_send_packet_over_udp(server_rtp_sockfd, ip, port,
                                           rtp_packet, RTP_MAX_PKT_SIZE + 2);
            if (ret < 0) {
                return -1;
            }
            ++rtp_packet->rtp_header.seq;
            send_bytes += ret;
            pos += RTP_MAX_PKT_SIZE;
        }
        // 发送剩余的内容
        if (remain_packet_size > 0) {
            rtp_packet->payload[0] = (nalu_first_byte & 0x60) | 28;
            rtp_packet->payload[1] = nalu_first_byte & 0x1F;
            rtp_packet->payload[1] |= 0x40;
            
            memcpy(rtp_packet->payload + 2,
                   frame + pos,
                   remain_packet_size + 2);
            ret = rtp_send_packet_over_udp(server_rtp_sockfd, ip, port,
                                           rtp_packet, remain_packet_size + 2);
            if (ret < 0) {
                return -1;
            }
            ++rtp_packet->rtp_header.seq;
            send_bytes += ret;
        }
    }
    rtp_packet->rtp_header.timestamp += 90000 / 25;
    out:
    
    return send_bytes;
}

static int handle_cmd_DESCRIBE(char* result, int cseq, char* url) {
    char sdp[500];
    char local_ip[100];
    
    sscanf(url, "rtsp://%[^:]:", local_ip);
    
    sprintf(sdp,
            "v=0\r\n"
            "o=- 9%ld 1 IN IP4 %s\r\n"
            "t=0 0\r\n"
            "a=control:*\r\n"
            "m=video 0 RTP/AVP 96\r\n"
            "a=rtpmap:96 H264/90000\r\n"
            "a=control:track0\r\n",
            time(nullptr),
            local_ip);
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Content-Base: %s\r\n"
            "Content-type: application/sdp\r\n"
            "Content-length: %zu\r\n"
            "\r\n"
            "%s",
            cseq,
            url,
            strlen(sdp),
            sdp);
    return 0;
}

static int handle_cmd_SETUP(char* result, int cseq, int client_rtp_port) {
    sprintf(result,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
            "Session: 66334873\r\n"
            "\r\n",
            cseq,
            client_rtp_port,
            client_rtp_port + 1,
            SERVER_RTP_PORT,
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

static void do_client(int clientfd, const char* client_ip, int client_port) {
    char method[40];
    char url[100];
    char version[40];
    int CSeq;
    
    int server_rtp_sockfd = -1, server_rtcp_sockfd = -1;
    int client_rtp_port, client_rtcp_port;
    char* read_buffer = (char*) malloc(10000);
    char* write_buffer = (char*) malloc(10000);
    
    while (true) {
        int recv_len;
        recv_len = recv(clientfd, read_buffer, 2000, 0);
        if (recv_len <= 0) {
            break;
        }
        
        read_buffer[recv_len] = '\0';
        std::string recv_str = read_buffer;
        printf(">>>>>>>>>>>>>>>>>>>>>>\n");
        printf("%s read_buffer = %s \n", __FUNCTION__, read_buffer);
        
        const char* sep = "\n";
        char* line = strtok(read_buffer, sep);
        while (line) {
            if (strstr(line, "OPTIONS") != NULL ||
                strstr(line, "DESCRIBE") != NULL ||
                strstr(line, "SETUP") != NULL || strstr(line, "PLAY") != NULL) {
                if (sscanf(line, "%s %s %s\r\n", method, url, version) != 3) {
                    //error
                }
            }
            else if (strstr(line, "CSeq") != NULL) {
                if (sscanf(line, "CSeq: %d\r\n", &CSeq) != 1) {
                    //error
                }
            }
            else if (strncmp(line, "Transport:", strlen("Transport:")) >= 0) {
                // Transport: RTP/AVP/UDP;unicast;client_port=13358-13359
                // Transport: RTP/AVP;unicast;client_port=13358-13359
                if (sscanf(
                        line,
                        "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
                        &client_rtp_port,
                        &client_rtcp_port) != 2) {
                    // error
                    printf("parse Transport error\n");
                }
            }
            line = strtok(nullptr, sep);
        }
        
        if (strcmp(method, "OPTIONS") == 0) {
            if (handle_cmd_OPTIONS(write_buffer, CSeq) != 0) {
                printf("failed to handle OPTIONS\n");
                break;
            }
        }
        else if (strcmp(method, "DESCRIBE") == 0) {
            if (handle_cmd_DESCRIBE(write_buffer, CSeq, url) != 0) {
                printf("failed to handle DESCRIBE\n");
                break;
            }
        }
        else if (strcmp(method, "SETUP") == 0) {
            if (handle_cmd_SETUP(write_buffer, CSeq, client_rtp_port) != 0) {
                printf("failed to handle SETUP\n");
                break;
            }
            server_rtp_sockfd = create_udp_socket();
            server_rtcp_sockfd = create_udp_socket();
            if (server_rtp_sockfd < 0 || server_rtcp_sockfd < 0) {
                printf("failed to create udp socket\n");
                break;
            }
            if (bind_socket_addr(server_rtp_sockfd, "0.0.0.0",
                                 SERVER_RTP_PORT) < 0 ||
                bind_socket_addr(server_rtcp_sockfd, "0.0.0.0",
                                 SERVER_RTCP_PORT) < 0) {
                printf("failed to bind addr\n");
                break;
            }
        }
        else if (strcmp(method, "PLAY") == 0) {
            if (handle_cmd_PLAY(write_buffer, CSeq) != 0) {
                printf("failed to handle PLAY\n");
                break;
            }
        }
        else {
            printf("invalid method\n");
            break;
        }
        printf("<<<<<<<<<<<<<<<<<<<<<<<\n");
        printf("%s write_buffer: %s \n", __FUNCTION__, write_buffer);
        send(clientfd, write_buffer, strlen(write_buffer), 0);
        // 开始播放，发送RTP包
        if (strcmp(method, "PLAY") == 0) {
            int frame_size, start_code;
            char* frame = (char*) malloc(500000);
            struct RtpPacket* rtp_packet = (struct RtpPacket*) malloc(500000);
            FILE* fp = fopen(H264_FILE_NAME, "rb");
            if (!fp) {
                printf("读取 %s 失败\n", H264_FILE_NAME);
                break;
            }
            rtp_header_init(rtp_packet, 0, 0, 0, RTP_VERSION,
                            RTP_PAYLOAD_TYPE_H264, 0, 0, 0, 0x88923423);
            printf("start play\n");
            printf("client ip: %s\n", client_ip);
            printf("client port: %d\n", client_rtp_port);
            
            while (true) {
                frame_size = get_frame_from_H264_file(fp, frame, 500000);
                if (frame_size < 0) {
                    printf("读取 %s 结束，frame size = %d \n", H264_FILE_NAME,
                           frame_size);
                    break;
                }
                if (start_code_3(frame)) {
                    start_code = 3;
                }
                else {
                    start_code = 4;
                }
                frame_size -= start_code;
                rtp_send_H264_frame(server_rtp_sockfd, client_ip,
                                    client_rtp_port, rtp_packet,
                                    frame + start_code, frame_size);
                
                sleep(40);
            }
            free(frame);
            free(rtp_packet);
            
            break;
        }
        
        bzero(method, sizeof(method));
        bzero(url, sizeof(url));
        CSeq = 0;
    }
    
    close(clientfd);
    if (server_rtp_sockfd) {
        close(server_rtp_sockfd);
    }
    if (server_rtcp_sockfd) {
        close(server_rtcp_sockfd);
    }
    free(read_buffer);
    free(write_buffer);
}

int main() {
    int server_sockfd;
    server_sockfd = create_tcp_socket();
    if (server_sockfd == -1) {
        printf("failed to create socket\n");
        return -1;
    }
    
    if (bind_socket_addr(server_sockfd, "0.0.0.0", SERVER_PORT) == -1) {
        printf("failed to bind\n");
        return -1;
    }
    
    if (listen(server_sockfd, 5) == -1) {
        printf("failed to listen\n");
        return -1;
    }
    
    printf("%s rtsp://127.0.0.1:%d\n", __FILE__, SERVER_PORT);
    while (true) {
        int client_sockfd;
        int client_port;
        char client_ip[40];
        
        client_sockfd = accept_client(server_sockfd, client_ip, &client_port);
        if (client_sockfd == -1) {
            printf("failed to accept\n");
            return -1;
        }
        printf("accept client: client ip: %s client port: %d\n",
               client_ip,
               client_port);
        do_client(client_sockfd, client_ip, client_port);
    }
    close(server_sockfd);
    return 0;
}
