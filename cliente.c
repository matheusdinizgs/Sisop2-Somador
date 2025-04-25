#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "discovery.h"

#define PORT 4000
#define MAX_BUFFER 1024
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_REQ_ACK 4
#define MAX_HISTORY 100000000

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

uint32_t valores_enviados[MAX_HISTORY] = {0};
int sock;
struct sockaddr_in servaddr, from;
struct hostent *server;
socklen_t addrlen = sizeof(servaddr);
uint32_t seqn = 1;

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

void current_time(char *buf, size_t len) {
    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

void *interface_thread(void *arg) {
    while (1) {
        packet ack;
        ssize_t len = recvfrom(sock, &ack, sizeof(ack), 0, NULL, NULL);
        if (len > 0 && ack.type == PACKET_TYPE_REQ_ACK) {
            pthread_mutex_lock(&ack_lock);
            if (ack.seqn == seqn) {
                char timebuf[64];
                current_time(timebuf, sizeof(timebuf));
                uint32_t value = valores_enviados[ack.seqn];
                printf("%s server %s id_req %u value %u num_reqs %u total_sum %lu\n",
                       timebuf, inet_ntoa(servaddr.sin_addr), ack.seqn, value, ack.data.ack.num_reqs,
                       ack.data.ack.total_sum);
                ack_recebido = 1;
                pthread_cond_signal(&ack_cond);
            }
            pthread_mutex_unlock(&ack_lock);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Pega a porta do argumento
    int port = atoi(argv[1]);

    // Cria o socket com domínio AF_INET e tipo SOCK_DGRAM (protocolo UDP)
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    // Cria o container genérico que permite que o SO identifique a família do endereço
    memset((char *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bzero(&(servaddr.sin_zero), 8);

    // Vincular o socket à porta
    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Cannot bind socket");
        close(sock);
        exit(EXIT_FAILURE);
    }
    // Configura o socket para enviar pacotes de broadcast
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Descoberta do servidor
    if (discovery_send_broadcast(sock, port, &servaddr) < 0) {
        fprintf(stderr, "Falha na descoberta do servidor\n");
        exit(EXIT_FAILURE);
    }

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s server_addr %s\n", timebuf, inet_ntoa(servaddr.sin_addr));

    pthread_t tid;
    pthread_create(&tid, NULL, interface_thread, NULL);

    while (1) {
        uint32_t value;
        if (scanf("%u", &value) != 1)
            break;

        valores_enviados[seqn] = value;

        packet req = {
            .type = PACKET_TYPE_REQ,
            .seqn = seqn,
        };
        req.data.req.value = value;

        pthread_mutex_lock(&ack_lock);
        ack_recebido = 0;
        sendto(sock, &req, sizeof(req), 0, (struct sockaddr *)&servaddr, addrlen);

        while (!ack_recebido) {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 10000000;  // 10ms = 10.000.000ns
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_nsec -= 1000000000;
                timeout.tv_sec++;
            }

            pthread_cond_timedwait(&ack_cond, &ack_lock, &timeout);

            if (!ack_recebido) {
                // Reenviar a requisição
                sendto(sock, &req, sizeof(req), 0, (struct sockaddr *)&servaddr, addrlen);
            }
        }
        pthread_mutex_unlock(&ack_lock);
        seqn++;
    }

    close(sock);
    pthread_cancel(tid);  // Encerrar a thread interface de maneira segura
    pthread_join(tid, NULL);
    return 0;
}
