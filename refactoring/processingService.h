#ifndef processingService_h
#define processingService_h

#include "common.h"
#include <pthread.h>

typedef struct {
    client_entry clients[MAX_CLIENTS];
    int client_count;
    uint32_t total_reqs;
    uint64_t total_sum;
    pthread_mutex_t lock;
} server_state;

typedef struct {
    packet pkt;
    struct sockaddr_in addr;
    socklen_t addrlen;
    int sock;
    server_state *state;
} request_context;

void init_server_state(server_state *state);
int find_or_add_client(server_state *state, struct sockaddr_in *addr);
void *handle_request(void *arg);

#endif // processingService_h
