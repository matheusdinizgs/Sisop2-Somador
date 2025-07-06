#include "interfaceService.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

void client_send_loop(client_context_t *ctx) {
    while (1) {
        uint32_t value;
        if (scanf("%u", &value) != 1) break;

        ctx->valores_enviados[*(ctx->seqn_ptr)] = value;

        packet req = {
            .type = PACKET_TYPE_REQ,
            .seqn = *(ctx->seqn_ptr),
        };
        req.data.req.value = value;

        pthread_mutex_lock(ctx->ack_lock);
        *(ctx->ack_recebido_ptr) = 0;
        sendto(ctx->sock, &req, sizeof(req), 0, (struct sockaddr *)&(ctx->serveraddr), ctx->addrlen);
        
        while (!*(ctx->ack_recebido_ptr)) {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 10000000; // 10ms = 10.000.000ns
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_nsec -= 1000000000;
                timeout.tv_sec++;
            }
        
            pthread_cond_timedwait(ctx->ack_cond, ctx->ack_lock, &timeout);
        
            if (!*(ctx->ack_recebido_ptr)) {
                // p/ reenviar a requisição
                sendto(ctx->sock, &req, sizeof(req), 0, (struct sockaddr *)&(ctx->serveraddr), ctx->addrlen);
            }
        }
        pthread_mutex_unlock(ctx->ack_lock);
        (*(ctx->seqn_ptr))++;
    }
}

