#include "interfaceService.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Global variables from client_testing.c that need to be accessed by client_interface_thread
extern uint32_t valores_enviados[];
extern int sock;
extern struct sockaddr_in serveraddr;
extern uint32_t seqn;
extern pthread_mutex_t ack_lock;
extern pthread_cond_t ack_cond;
extern int ack_recebido;

void current_time(char *buf, size_t len) {

    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

void getInitServerState(char *timeBuf, size_t len) {
    
    current_time(timeBuf, len);
    printf("%s num_reqs 0 total_sum 0\n", timeBuf);
}

void print_server_state(char *timeBuf,
                       const char *clientAddr,
                       uint32_t seqn,
                       uint32_t value,
                       server_state *state,
                       int is_duplicate) {
    current_time(timeBuf, 64);
    if (is_duplicate) {
        printf("%s client %s DUP!! id_req %u value %u num_reqs %u total_sum %lu\n",
               timeBuf, clientAddr, seqn, value, state->total_reqs, state->total_sum);
    } else {
        printf("%s client %s id_req %u value %u num_reqs %u total_sum %lu\n",
               timeBuf, clientAddr, seqn, value, state->total_reqs, state->total_sum);
    }
}

void *client_interface_thread(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    while (1) {
        packet ack;
        ssize_t len = recvfrom(ctx->sock, &ack, sizeof(ack), 0, NULL, NULL);
        if (len > 0 && ack.type == PACKET_TYPE_REQ_ACK) {
            pthread_mutex_lock(ctx->ack_lock);
            if (ack.seqn == *(ctx->seqn_ptr)) {
                char timebuf[64];
                current_time(timebuf, sizeof(timebuf));
                uint32_t value = ctx->valores_enviados[ack.seqn];
                printf("%s server %s id_req %u value %u num_reqs %u total_sum %lu\n",
                    timebuf, inet_ntoa(ctx->serveraddr.sin_addr), ack.seqn, value, ack.data.ack.num_reqs,
                        ack.data.ack.total_sum);
                *(ctx->ack_recebido_ptr) = 1;
                pthread_cond_signal(ctx->ack_cond);
            }
            pthread_mutex_unlock(ctx->ack_lock);
        }
    }
    return NULL;
}

void print_client_server_addr(const char *server_addr_str) {
    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s server_addr %s\n", timebuf, server_addr_str);
}

