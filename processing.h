#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include "discovery.h"

#include <pthread.h>
#include <netinet/in.h>

typedef struct
{
    packet pkt;
    struct sockaddr_in addr;
    socklen_t addrlen;
    int sock;
    pthread_mutex_t *lock;
} request_context;

extern client_entry clients[MAX_CLIENTS];
extern int client_count;
extern uint64_t total_sum;
extern uint32_t total_reqs;

void *handle_request(void *arg);
void maybe_handle_request(packet pkt, struct sockaddr_in cliaddr, socklen_t len, int sock, pthread_mutex_t *lock);
void log_request(packet *pkt, struct sockaddr_in *addr, int is_dup);
void process_packet(packet *pkt, struct sockaddr_in *addr, int sock, socklen_t addrlen, pthread_mutex_t *lock);

#endif // REQUEST_HANDLER_H
