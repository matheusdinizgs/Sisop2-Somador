#include <stdint.h>
#include <stdio.h>

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

void current_time(char *buf, size_t len);