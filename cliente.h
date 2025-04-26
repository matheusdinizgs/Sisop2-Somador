#pragma once

#define PORT 4000
#define MAX_BUFFER 1024
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_REQ_ACK 4
#define MAX_HISTORY 100000000

struct requisicao
{
    uint32_t value;
};

struct requisicao_ack
{
    uint32_t seqn;
    uint32_t num_reqs;
    uint64_t total_sum;
};

typedef struct __packet
{
    uint16_t type;
    uint32_t seqn;
    union
    {
        struct requisicao req;
        struct requisicao_ack ack;
    } data;
} packet;

void *interface_thread(void *arg);
int init_socket_and_find_server(int port);
