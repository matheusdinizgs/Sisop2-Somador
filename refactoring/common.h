#ifndef common_h
#define common_h

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 4000
#define MAX_CLIENTS 100
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

typedef struct {
    struct sockaddr_in addr;
    uint32_t last_req;
    uint64_t last_sum;
} client_entry;

#endif // common_h