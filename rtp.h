#ifndef RTSPSERVER_RTP_H
#define RTSPSERVER_RTP_H

#include <cstdint>

#define RTP_VERSION 2

#define RTP_PAYLOAD_TYPE_H264 96
#define RTP_PAYLOAD_TYPE_AAC 97

#define RTP_HEADER_SIZE 12
#define RTP_MAX_PKT_SIZE 1400

/*
 *    0                   1                   2                   3
 *    7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0|7 6 5 4 3 2 1 0
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                           timestamp                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |           synchronization source (SSRC) identifier            |
 *   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *   |            contributing source (CSRC) identifiers             |
 *   :                             ....                              :
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

struct RtpHeader {
    // byte 0 位域分配位数是从低位到高位
    // version padding extension csrc_len
    uint8_t csrc_len: 4; // CSRC计数器, 占4位, 指示CSRC标识符的个数，低位数据
    uint8_t extension: 1; // 占1位, if X = 1, 则有扩展头部
    uint8_t padding: 1; // 填充标志，在该报文的尾部添加一个或多个额外的八位组，他们不是有效载荷的一部分
    uint8_t version: 2; // RTP协议的版本号，高位数据
    // byte 1
    uint8_t payload_type: 7; // 有效载荷类型
    uint8_t marker: 1; // 标记位, 对于不同的有效载荷有不同的含义
    // byte 2-3
    uint16_t seq; // 报文序列号，超过1字节需要在发送时转为大端序
    // byte 4 - 7
    uint32_t timestamp; // 时间戳
    // byte 8 - 11
    uint32_t ssrc; // 标记同步信源
    // 0 - 15个特约信源（CSRC）标识符, 每个占32位
};

struct RtpPacket {
    struct RtpHeader rtp_header;
    uint8_t payload[0];
};

void rtp_header_init(struct RtpPacket* rtp_packet, uint8_t csrc_len,
                     uint8_t extension, uint8_t padding, uint8_t version,
                     uint8_t payload_type, uint8_t marker, uint16_t seq,
                     uint32_t timestamp, uint32_t ssrc);

int rtp_send_packet_over_tcp(int client_sockfd, struct RtpPacket* rtp_packet,
                             uint32_t data_size);
int rtp_send_packet_over_udp(int server_rtp_sockfd, const char* ip,
                             int16_t port, struct RtpPacket* rtp_packet,
                             uint32_t data_size);
#endif
