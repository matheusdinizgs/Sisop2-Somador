#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include "common.h"
#include "interfaceService.h"
#include "discoveryService.h"

#define MAX_HISTORY 100000000

uint32_t valores_enviados[MAX_HISTORY] = {0};

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

int main(int argc, char *argv[]) {
    
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int sock;
    struct sockaddr_in serveraddr;
    socklen_t addrlen = sizeof(serveraddr);
    uint32_t seqn = 1;

    initClient(port, &sock, &serveraddr, &addrlen);

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

    client_send_loop(&ctx);

    close(sock);
    pthread_cancel(tid);  // p/ encerrar a thread interface de maneira segura
    pthread_join(tid, NULL);
    return 0;
}
