#include "processingService.h"
#include "interfaceService.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

void init_server_state(server_state *state) {
    state->client_count = 0;
    state->total_reqs = 0;
    state->total_sum = 0;
    pthread_mutex_init(&state->lock, NULL);
}

int find_or_add_client(server_state *state, struct sockaddr_in *addr) {
    pthread_mutex_lock(&state->lock);
    for (int i = 0; i < state->client_count; i++) {
        if (state->clients[i].addr.sin_port == addr->sin_port &&
            state->clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr) {
            pthread_mutex_unlock(&state->lock);
            return i;
        }
    }
    state->clients[state->client_count].addr = *addr;
    state->clients[state->client_count].last_req = 0;
    state->clients[state->client_count].last_sum = 0;
    int new_client_idx = state->client_count++;
    pthread_mutex_unlock(&state->lock);
    return new_client_idx;
}

void *handle_request(void *arg) {
    request_context *ctx = (request_context *)arg;

    packet *pkt = &ctx->pkt;
    char timebuf[64];
    int idx = find_or_add_client(ctx->state, &ctx->addr);

    pthread_mutex_lock(&ctx->state->lock);
    if (pkt->seqn == ctx->state->clients[idx].last_req + 1) {
        ctx->state->total_reqs++;
        ctx->state->total_sum += pkt->data.req.value;
        ctx->state->clients[idx].last_req = pkt->seqn;
        ctx->state->clients[idx].last_sum = ctx->state->total_sum;
        print_server_state(timebuf, inet_ntoa(ctx->addr.sin_addr), pkt->seqn, pkt->data.req.value, ctx->state, 0);
    } else {
        print_server_state(timebuf, inet_ntoa(ctx->addr.sin_addr), pkt->seqn, pkt->data.req.value, ctx->state, 1);
    }

    packet ack = {
        .type = PACKET_TYPE_REQ_ACK,
        .seqn = ctx->state->clients[idx].last_req,
    };
    ack.data.ack.num_reqs = ctx->state->total_reqs;
    ack.data.ack.total_sum = ctx->state->clients[idx].last_sum;
    sendto(ctx->sock, &ack, sizeof(ack), 0,
           (struct sockaddr *)&ctx->addr, ctx->addrlen);
    pthread_mutex_unlock(&ctx->state->lock);

    free(ctx);
    return NULL;
}
