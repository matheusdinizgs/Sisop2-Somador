#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "processing.h"

client_entry clients[MAX_CLIENTS];
int client_count = 0;
uint32_t total_reqs = 0;
uint64_t total_sum = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *handle_request(void *arg)
{
    request_context *ctx = (request_context *)arg;
    process_packet(&ctx->pkt, &ctx->addr, ctx->sock, ctx->addrlen);
    free(ctx);
    return NULL;
}

void log_request(packet *pkt, struct sockaddr_in *addr, int is_dup)
{
    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s client %s %s id_req %u value %u num_reqs %u total_sum %lu\n",
           timebuf, inet_ntoa(addr->sin_addr), is_dup ? "DUP!!" : "",
           pkt->seqn, pkt->data.req.value, total_reqs, total_sum);
}

void process_packet(packet *pkt, struct sockaddr_in *addr, int sock, socklen_t addrlen)
{
    int idx = find_or_add_client(addr, clients, client_count);

    pthread_mutex_lock(&lock);
    int is_dup = pkt->seqn != clients[idx].last_req + 1;

    if (!is_dup)
    {
        total_reqs++;
        total_sum += pkt->data.req.value;
        clients[idx].last_req = pkt->seqn;
        clients[idx].last_sum = total_sum;
    }

    log_request(pkt, addr, is_dup);

    packet ack = {
        .type = PACKET_TYPE_REQ_ACK,
        .seqn = clients[idx].last_req,
        .data.ack = {
            .num_reqs = total_reqs,
            .total_sum = clients[idx].last_sum}};

    sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)addr, addrlen);
    pthread_mutex_unlock(&lock);
}

void maybe_handle_request(packet pkt, struct sockaddr_in cliaddr, socklen_t len, int sock)
{
    request_context *ctx = malloc(sizeof(request_context));
    if (!ctx)
    {
        perror("malloc failed");
        return;
    }
    *ctx = (request_context){pkt, cliaddr, len, sock};

    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_request, ctx) != 0)
    {
        perror("pthread_create failed");
        free(ctx);
        return;
    }
    pthread_detach(tid);
}
