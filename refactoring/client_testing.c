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
#define LEADER_DISCOVERY_RETRIES 5           // Quantas vezes tentar descobrir o líder
#define LEADER_DISCOVERY_INTERVAL_US 1000000 // Intervalo entre tentativas (1 segundo)

uint32_t valores_enviados[MAX_HISTORY] = {0};

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

// NOVO: Mutex para proteger o endereço do líder no contexto do cliente
pthread_mutex_t leader_addr_lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int discovery_sock, comm_sock; // NOVO: Dois sockets separados
    struct sockaddr_in serveraddr;
    socklen_t addrlen = sizeof(serveraddr);
    uint32_t seqn = 1;

    uint32_t current_leader_id = 0;
    struct sockaddr_in current_leader_addr;

    // 1. Socket para descoberta
    discovery_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_sock < 0)
    {
        perror("Error creating discovery socket");
        exit(EXIT_FAILURE);
    }

    // 2. Encontrar o líder
    if (find_leader(discovery_sock, port, &current_leader_addr, &addrlen, &current_leader_id) != 0)
    {
        fprintf(stderr, "Erro: Não foi possível encontrar um servidor líder ativo.\n");
        close(discovery_sock);
        exit(EXIT_FAILURE);
    }

    // 3. Fechar socket de descoberta e criar novo para comunicação
    close(discovery_sock);

    comm_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (comm_sock < 0)
    {
        perror("Error creating communication socket");
        exit(EXIT_FAILURE);
    }

    // 4. Configurar endereço do servidor encontrado
    serveraddr = current_leader_addr;

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s Cliente conectado ao líder (ID: %u) em %s:%d\n",
           timebuf, current_leader_id, inet_ntoa(current_leader_addr.sin_addr), ntohs(current_leader_addr.sin_port));

    // 5. Criar contexto com o socket de comunicação
    client_context_t ctx = {
        .sock = comm_sock, // MUDANÇA: usar socket de comunicação
        .serveraddr = serveraddr,
        .addrlen = addrlen,
        .seqn_ptr = &seqn,
        .valores_enviados = valores_enviados,
        .ack_lock = &ack_lock,
        .ack_cond = &ack_cond,
        .ack_recebido_ptr = &ack_recebido,
        .current_leader_id_ptr = &current_leader_id,
        .current_leader_addr_ptr = &current_leader_addr,
        .leader_addr_lock_ptr = &leader_addr_lock};

    pthread_t tid;
    pthread_create(&tid, NULL, client_interface_thread, &ctx);

    client_send_loop(&ctx);

    close(comm_sock);
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&ack_lock);
    pthread_cond_destroy(&ack_cond);
    pthread_mutex_destroy(&leader_addr_lock);
    return 0;
}
