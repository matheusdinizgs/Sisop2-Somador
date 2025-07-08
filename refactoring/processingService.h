#ifndef processingService_h
#define processingService_h

#include "common.h"  // Inclua common.h, que agora define a struct server_state COMPLETA
#include <pthread.h> // Ainda necessário para pthread_t, etc.

// NOVO: A struct server_state NÃO é mais definida aqui.
// Ela é importada do common.h, que agora contém todos os campos
// para liderança, replicação e gerenciamento de servidores.

typedef struct
{
    packet pkt;
    struct sockaddr_in addr;
    socklen_t addrlen;
    int sock;
    server_state *state; // Ponteiro para a struct server_state definida em common.h
} request_context;

// --- DECLARAÇÕES DE FUNÇÃO ---
void init_server_state(server_state *state);
int find_or_add_client(server_state *state, struct sockaddr_in *addr);

// NOVO: Protótipo para a função que adiciona/atualiza servidores conhecidos
void add_or_update_known_server(server_state *state, uint32_t server_id, struct sockaddr_in *server_addr);

// NOVO: Protótipo para a nova thread de gerenciamento de liderança
void *manage_server_role_thread(void *arg);

void *handle_request(void *arg); // Este protótipo já existia, mas sua implementação mudou.
int get_local_ip_address(struct in_addr *addr);

#endif // processingService_h