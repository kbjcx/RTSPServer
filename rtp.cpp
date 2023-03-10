#include "rtp.h"

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>

void rtp_header_init(struct RtpPacket* rtp_packet, uint8_t csrc_len,
                     uint8_t extension, uint8_t padding, uint8_t version,
                     uint8_t payload_type, uint8_t marker, uint16_t seq,
                     uint32_t timestamp, uint32_t ssrc) {
    rtp_packet->rtp_header.csrc_len = csrc_len;
    rtp_packet->rtp_header.extension = extension;
    rtp_packet->rtp_header.padding = padding;
    rtp_packet->rtp_header.version = version;
    rtp_packet->rtp_header.payload_type = payload_type;
    rtp_packet->rtp_header.marker = marker;
    rtp_packet->rtp_header.seq = seq;
    rtp_packet->rtp_header.timestamp = timestamp;
    rtp_packet->rtp_header.ssrc = ssrc;
}

int rtp_send_packet_over_tcp(int client_sockfd, struct RtpPacket* rtp_packet,
                             uint32_t data_size) {
    rtp_packet->rtp_header.seq = htons(rtp_packet->rtp_header.seq);
    rtp_packet->rtp_header.timestamp = htonl(rtp_packet->rtp_header.timestamp);
    rtp_packet->rtp_header.ssrc = htonl(rtp_packet->rtp_header.ssrc);
    
    uint32_t rtp_size = RTP_HEADER_SIZE + data_size;
    char* temp_buffer = (char*) malloc(4 + rtp_size);
    temp_buffer[0] = 0x24;
    temp_buffer[1] = 0x00;
    temp_buffer[2] = (uint8_t) ((rtp_size & 0xFF00) >> 8);
    temp_buffer[3] = (uint8_t) (rtp_size & 0xFF);
    memcpy(temp_buffer + 4, rtp_packet, rtp_size);
    
    int ret = send(client_sockfd, temp_buffer, rtp_size + 4, 0);
    
    rtp_packet->rtp_header.seq = ntohs(rtp_packet->rtp_header.seq);
    rtp_packet->rtp_header.timestamp = ntohl(rtp_packet->rtp_header.timestamp);
    rtp_packet->rtp_header.ssrc = ntohl(rtp_packet->rtp_header.ssrc);
    
    free(temp_buffer);
    temp_buffer = nullptr;
    
    return ret;
}

int rtp_send_packet_over_udp(int server_rtp_sockfd, const char* ip,
                             int16_t port, struct RtpPacket* rtp_packet,
                             uint32_t data_size) {
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    int ret;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
    
    rtp_packet->rtp_header.seq = htons(rtp_packet->rtp_header.seq);
    rtp_packet->rtp_header.timestamp = htonl(rtp_packet->rtp_header.timestamp);
    rtp_packet->rtp_header.ssrc = htonl(rtp_packet->rtp_header.ssrc);
    
    ret = sendto(server_rtp_sockfd, (char*) rtp_packet,
                 data_size + RTP_HEADER_SIZE, 0, (struct sockaddr*) &addr,
                 sizeof(addr));
    
    rtp_packet->rtp_header.seq = ntohs(rtp_packet->rtp_header.seq);
    rtp_packet->rtp_header.timestamp = ntohl(rtp_packet->rtp_header.timestamp);
    rtp_packet->rtp_header.ssrc = ntohl(rtp_packet->rtp_header.ssrc);
    
    return ret;
}
