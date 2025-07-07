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
#define LEADER_DISCOVERY_RETRIES 5      // Quantas vezes tentar descobrir o líder
#define LEADER_DISCOVERY_INTERVAL_US 1000000 // Intervalo entre tentativas (1 segundo)


uint32_t valores_enviados[MAX_HISTORY] = {0};

pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_recebido = 0;

// NOVO: Mutex para proteger o endereço do líder no contexto do cliente
pthread_mutex_t leader_addr_lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[]) {
    
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int sock;
    struct sockaddr_in serveraddr; // Endereço do servidor com o qual nos comunicamos
    socklen_t addrlen = sizeof(serveraddr);
    uint32_t seqn = 1;

    //initClient(port, &sock, &serveraddr, &addrlen);


    // NOVO: Variáveis para rastrear o líder atual
    uint32_t current_leader_id = 0;
    struct sockaddr_in current_leader_addr; // O endereço do líder que o cliente usará para enviar reqs
    
    // 1. Criar o socket UDP do cliente (agora mais genérico, sem descoberta imediata do líder)
    // initClient pode ser uma função simples de socket(AF_INET, SOCK_DGRAM, 0)
    // ou você pode mover essa lógica para cá.
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creating client socket");
        exit(EXIT_FAILURE);
    }

    // NOVO: Tentar encontrar o líder no cluster
    // Esta função agora lida com o broadcast, recebimento de DESC_ACKs,
    // e possivelmente REDIRECTs para encontrar o líder real.
    // Ela vai preencher current_leader_addr e current_leader_id.
    if (find_leader(sock, port, &current_leader_addr, &addrlen, &current_leader_id) != 0) {
        fprintf(stderr, "Erro: Não foi possível encontrar um servidor líder ativo.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    //printf("%s server_addr %s\n", timebuf, inet_ntoa(serveraddr.sin_addr));
    printf("%s Cliente conectado ao líder (ID: %u) em %s:%d\n",
           timebuf, current_leader_id, inet_ntoa(current_leader_addr.sin_addr), ntohs(current_leader_addr.sin_port));


    // Create client context for the interface thread
    client_context_t ctx = {
        .sock = sock,
        .serveraddr = serveraddr,
        .addrlen = addrlen,
        .seqn_ptr = &seqn,
        .valores_enviados = valores_enviados,
        .ack_lock = &ack_lock,
        .ack_cond = &ack_cond,
        .ack_recebido_ptr = &ack_recebido,
        // --- NOVOS PONTEIROS PARA O CONTEXTO ---
        .current_leader_id_ptr = &current_leader_id,
        .current_leader_addr_ptr = &current_leader_addr,
        .leader_addr_lock_ptr = &leader_addr_lock // Passa o mutex
    };

    

    pthread_t tid;
    // A client_interface_thread será responsável por receber ACKs,
    // mas também por receber REDIRECTs e COORDINATORs para atualizar o líder.
    pthread_create(&tid, NULL, client_interface_thread, &ctx);


    // O loop de envio agora é mais robusto, tentando re-descobrir o líder se houver falha
    client_send_loop(&ctx);

    close(sock);
    // pthread_cancel(tid); // Considerar remoção ou tratamento mais robusto de cancelamento
    pthread_join(tid, NULL); // Esperar a thread de interface terminar
    pthread_mutex_destroy(&ack_lock); // Libera o mutex
    pthread_cond_destroy(&ack_cond); // Libera a cond
    pthread_mutex_destroy(&leader_addr_lock); // Libera o novo mutex
    return 0;
}
