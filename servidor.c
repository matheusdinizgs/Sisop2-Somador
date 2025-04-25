#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#include "discovery.h"

#define PORT 4000
#define MAX_CLIENTS 100
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_REQ_ACK 4

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

typedef struct
{
    struct sockaddr_in addr;
    uint32_t last_req;
    uint64_t last_sum;
} client_entry;

client_entry clients[MAX_CLIENTS];
int client_count = 0;
uint32_t total_reqs = 0;
uint64_t total_sum = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int find_or_add_client(struct sockaddr_in *addr)
{
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr)
            return i;
    }
    clients[client_count].addr = *addr;
    clients[client_count].last_req = 0;
    clients[client_count].last_sum = 0;
    return client_count++;
}

void current_time(char *buf, size_t len)
{
    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

void *handle_request(void *arg)
{
    struct
    {
        packet pkt;
        struct sockaddr_in addr;
        socklen_t addrlen;
        int sock;
    } *ctx = arg;

    packet *pkt = &ctx->pkt;
    char timebuf[64];
    int idx = find_or_add_client(&ctx->addr);

    pthread_mutex_lock(&lock);
    if (pkt->seqn == clients[idx].last_req + 1)
    {
        total_reqs++;
        total_sum += pkt->data.req.value;
        clients[idx].last_req = pkt->seqn;
        clients[idx].last_sum = total_sum;
        current_time(timebuf, sizeof(timebuf));
        printf("%s client %s id_req %u value %u num_reqs %u total_sum %lu\n",
               timebuf, inet_ntoa(ctx->addr.sin_addr), pkt->seqn,
               pkt->data.req.value, total_reqs, total_sum);
    }
    else
    {
        current_time(timebuf, sizeof(timebuf));
        printf("%s client %s DUP!! id_req %u value %u num_reqs %u total_sum %lu\n",
               timebuf, inet_ntoa(ctx->addr.sin_addr), pkt->seqn,
               pkt->data.req.value, total_reqs, total_sum);
    }

    packet ack = {
        .type = PACKET_TYPE_REQ_ACK,
        .seqn = clients[idx].last_req,
    };
    ack.data.ack.num_reqs = total_reqs;
    ack.data.ack.total_sum = clients[idx].last_sum;
    sendto(ctx->sock, &ack, sizeof(ack), 0,
           (struct sockaddr *)&ctx->addr, ctx->addrlen);
    pthread_mutex_unlock(&lock);

    free(ctx);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr = {.sin_family = AF_INET,
                                   .sin_addr.s_addr = INADDR_ANY,
                                   .sin_port = htons(port)};

    bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr));

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s num_reqs 0 total_sum 0\n", timebuf);

    while (1)
    {
        packet pkt;
        struct sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);

        recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliaddr, &len);

        if (pkt.type == PACKET_TYPE_DISCOVERY)
        {
            discovery_handle_request(sock, &cliaddr, len);
        }
        else if (pkt.type == PACKET_TYPE_REQ)
        {
            pthread_t tid;
            void *ctx = malloc(sizeof(packet) + sizeof(cliaddr) + sizeof(socklen_t) + sizeof(int));
            memcpy(ctx, &(struct { packet pkt; struct sockaddr_in addr; socklen_t addrlen; int sock; }){pkt, cliaddr, len, sock}, sizeof(packet) + sizeof(cliaddr) + sizeof(socklen_t) + sizeof(int));
            pthread_create(&tid, NULL, handle_request, ctx);
            pthread_detach(tid);
        }
    }
    close(sock);
    return 0;
}