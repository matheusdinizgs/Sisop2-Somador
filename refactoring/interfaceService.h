#ifndef interfaceService_h
#define interfaceService_h

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "processingService.h"
#include "common.h"

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
