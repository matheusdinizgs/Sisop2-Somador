#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
//#include <linux/time.h>
#include <sys/time.h>
#include <time.h>
//#include <bits/time.h>
#include "common.h"
#include "interfaceService.h"

#define MAX_BUFFER 1024
#define MAX_HISTORY 100000000

uint32_t valores_enviados[MAX_HISTORY] = {0};
int sock;
struct sockaddr_in serveraddr;
socklen_t addrlen = sizeof(serveraddr);
uint32_t seqn = 1;

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    sock = socket(AF_INET, SOCK_DGRAM, 0);

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in bcastaddr = {.sin_family = AF_INET,
                                    .sin_port = htons(port),
                                    .sin_addr.s_addr = inet_addr("255.255.255.255")};

    packet desc_pkt = {.type = PACKET_TYPE_DESC};

    sendto(sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&bcastaddr, sizeof(bcastaddr));

    // Esperar DESC_ACK
    recvfrom(sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&serveraddr, &addrlen);

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s server_addr %s\n", timebuf, inet_ntoa(serveraddr.sin_addr));

    // Create client context for the interface thread
    client_context_t ctx = {
        .sock = sock,
        .serveraddr = serveraddr,
        .addrlen = addrlen,
        .seqn_ptr = &seqn,
        .valores_enviados = valores_enviados,
        .ack_lock = &ack_lock,
        .ack_cond = &ack_cond,
        .ack_recebido_ptr = &ack_recebido
    };

    pthread_t tid;
    pthread_create(&tid, NULL, client_interface_thread, &ctx);

    while (1) {
        uint32_t value;
        if (scanf("%u", &value) != 1) break;

        valores_enviados[seqn] = value;

        packet req = {
            .type = PACKET_TYPE_REQ,
            .seqn = seqn,
        };
        req.data.req.value = value;

        pthread_mutex_lock(&ack_lock);
        ack_recebido = 0;
        sendto(sock, &req, sizeof(req), 0, (struct sockaddr *)&serveraddr, addrlen);
        
        while (!ack_recebido) {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 10000000; // 10ms = 10.000.000ns
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_nsec -= 1000000000;
                timeout.tv_sec++;
            }
        
            pthread_cond_timedwait(&ack_cond, &ack_lock, &timeout);
        
            if (!ack_recebido) {
                // p/ reenviar a requisição
                sendto(sock, &req, sizeof(req), 0, (struct sockaddr *)&serveraddr, addrlen);
            }
        }
        pthread_mutex_unlock(&ack_lock);
        seqn++;
    }

    close(sock);
    pthread_cancel(tid);  // p/ encerrar a thread interface de maneira segura
    pthread_join(tid, NULL);
    return 0;
}
