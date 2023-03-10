#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <string>

#define SERVER_PORT 8554
#define SERVER_RTP_PORT 55532
#define SERVER_RTCP_PORT 55533

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
            } else if (strstr(line, "CSeq") != NULL) {
                if (sscanf(line, "CSeq: %d\r\n", &CSeq) != 1) {
                    //error
                }
            } else if (strncmp(line, "Transport:", strlen("Transport:")) >= 0) {
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
        } else if (strcmp(method, "DESCRIBE") == 0) {
            if (handle_cmd_DESCRIBE(write_buffer, CSeq, url) != 0) {
                printf("failed to handle DESCRIBE\n");
                break;
            }
        } else if (strcmp(method, "SETUP") == 0) {
            if (handle_cmd_SETUP(write_buffer, CSeq, client_rtp_port) != 0) {
                printf("failed to handle SETUP\n");
                break;
            }
        } else if (strcmp(method, "PLAY") == 0) {
            if (handle_cmd_PLAY(write_buffer, CSeq) != 0) {
                printf("failed to handle PLAY\n");
                break;
            }
        } else {
            printf("invalid method\n");
            break;
        }
        printf("<<<<<<<<<<<<<<<<<<<<<<<\n");
        printf("%s write_buffer: %s \n", __FUNCTION__, write_buffer);
        send(clientfd, write_buffer, strlen(write_buffer), 0);

        if (strcmp(method, "PLAY") == 0) {
            printf("start play\n");
            printf("client ip: %s\n", client_ip);
            printf("client port: %d\n", client_rtp_port);

            while (true) { sleep(40); }
            break;
        }

        bzero(method, sizeof(method));
        bzero(url, sizeof(url));
        CSeq = 0;
    }

    close(clientfd);
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
