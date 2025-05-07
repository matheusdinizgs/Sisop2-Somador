#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>

struct requisicao
{
    uint32_t value; // Valor da requsição
};

struct requisicao_ack
{
    uint32_t seqn;      // Número de sequência que está sendo feito o ack
    uint32_t num_reqs;  // Quantidade de requisições
    uint64_t total_sum; // Valor da soma agregada até o momento
};

typedef struct __packet
{
    uint16_t type; // Tipo do pacote (DESC | REQ | DESC_ACK | REQ_ACK )
    uint32_t seqn; // Número de sequência de uma requisição
    union
    {
        struct requisicao req;
        struct requisicao_ack ack;
    } data;
} packet;

typedef struct
{
    struct sockaddr_in addr; // end ip do cliente
    uint32_t last_req;       // quantidade de requisições que ja foram enviadas por esse cliente e processadas pelo servidor
    uint64_t last_sum;       // soma acumulada
} client_entry;

void current_time(char *buf, size_t len);