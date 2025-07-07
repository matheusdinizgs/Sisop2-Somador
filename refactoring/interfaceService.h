#ifndef interfaceService_h
#define interfaceService_h

#include "common.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "processingService.h"

// Client-side interface functions
typedef struct {
    int sock;
    struct sockaddr_in serveraddr;
    socklen_t addrlen;
    uint32_t *seqn_ptr;
    uint32_t *valores_enviados;
    pthread_mutex_t *ack_lock;
    pthread_cond_t *ack_cond;
    int *ack_recebido_ptr;
    // --- NOVOS CAMPOS ---
    uint32_t *current_leader_id_ptr;       // Ponteiro para o ID do líder atual
    struct sockaddr_in *current_leader_addr_ptr; // Ponteiro para o endereço do líder atual
    pthread_mutex_t *leader_addr_lock_ptr; // Ponteiro para o mutex do endereço do líder
} client_context_t;

void *client_interface_thread(void *arg);
void client_send_loop(client_context_t *ctx);
void print_client_server_addr(const char *server_addr_str);

// Server-side interface functions
void current_time(char *timeBuf, size_t len);

void print_server_state(char *timeBuf,
                        const char *clientAddr,
                        uint32_t seqn,
                        uint32_t value,
                        server_state *state,
                        int is_duplicate);

void getInitServerState(char *timeBuf, size_t len);                        

#endif // interfaceService_h
