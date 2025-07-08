// client_testing.c

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

// --- NOVAS CONSTANTES PARA O CLIENTE ---
#define LEADER_DISCOVERY_RETRIES 10      // Quantas vezes tentar descobrir o líder
#define LEADER_DISCOVERY_TIMEOUT_SEC 3   // Tempo de espera por um COORDINATOR por tentativa (3 segundos)

// A constante MAX_HISTORY deve estar em common.h. Se não estiver, adicione-a lá.
// #define MAX_HISTORY 100000000

uint32_t valores_enviados[MAX_HISTORY] = {0};

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

// Mutex para proteger o endereço do líder no contexto do cliente
pthread_mutex_t leader_addr_lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int sock;
    // REMOVA ESTA LINHA: struct sockaddr_in serveraddr; // <--- REMOVER!
    socklen_t addrlen = sizeof(struct sockaddr_in); // <--- Ajustar o sizeof

    uint32_t seqn = 1;

    // Variáveis para rastrear o líder atual
    uint32_t current_leader_id = 0;
    struct sockaddr_in current_leader_addr; // O endereço do líder que o cliente usará para enviar reqs

    // Criar o socket UDP do cliente
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creating client socket");
        exit(EXIT_FAILURE);
    }

    // Tentar encontrar o líder no cluster
    if (find_leader(sock, port, &current_leader_addr, &addrlen, &current_leader_id) != 0) {
        fprintf(stderr, "Erro: Não foi possível encontrar um servidor líder ativo após %d tentativas.\n", LEADER_DISCOVERY_RETRIES);
        close(sock);
        exit(EXIT_FAILURE);
    }

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s Cliente conectado ao líder (ID: %u) em %s:%d\n",
           timebuf, current_leader_id, inet_ntoa(current_leader_addr.sin_addr), ntohs(current_leader_addr.sin_port));

    // Create client context for the interface thread
    client_context_t ctx = {
        .sock = sock,
        .serveraddr = current_leader_addr, // <--- CORREÇÃO: use 'current_leader_addr' AQUI!
        .addrlen = addrlen,
        .seqn_ptr = &seqn,
        .valores_enviados = valores_enviados,
        .ack_lock = &ack_lock,
        .ack_cond = &ack_cond,
        .ack_recebido_ptr = &ack_recebido,
        // --- NOVOS PONTEIROS PARA O CONTEXTO ---
        .current_leader_id_ptr = &current_leader_id,
        .current_leader_addr_ptr = &current_leader_addr,
        .leader_addr_lock_ptr = &leader_addr_lock
    };

    pthread_t tid;
    pthread_create(&tid, NULL, client_interface_thread, &ctx);

    client_send_loop(&ctx);

    close(sock);
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&ack_lock);
    pthread_cond_destroy(&ack_cond);
    pthread_mutex_destroy(&leader_addr_lock);
    return 0;
}