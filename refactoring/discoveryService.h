#ifndef discoveryService_h
#define discoveryService_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#define PORT 4000
#define PACKET_TYPE_DESC 1
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_DESC_ACK 3
#define PACKET_TYPE_REQ_ACK 4

struct requisicao {
    uint32_t value;
};

struct requisicao_ack {
    uint32_t seqn;
    uint32_t num_reqs;
    uint64_t total_sum;
};

typedef struct __packet {
    uint16_t type;
    uint32_t seqn;
    union {
        struct requisicao req;
        struct requisicao_ack ack;
    } data;
} packet;

int initServer(int argc, char *argv[]);

//int find_or_add_client(struct sockaddr_in *addr);

void endServer(int socketNumber);

int initClient(int argc, char *argv[]);

#endif // discoveryService_h