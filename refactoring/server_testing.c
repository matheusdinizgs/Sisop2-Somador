#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h> // Para time()
#include "discoveryService.h"
#include "interfaceService.h"
#include "processingService.h"
#include "common.h" // Inclua o common.h atualizado!
#include <errno.h> // Para lidar com timeouts de recvfrom

// --- CONSTANTES ---
#define SERVER_RECV_TIMEOUT_SEC 1 // Timeout para recvfrom no loop principal do servidor

// --- FUNÇÃO DA THREAD DE GERENCIAMENTO DE LIDERANÇA (IMPLEMENTADA EM processingService.c) ---
void *manage_server_role_thread(void *arg);


// --- FUNÇÃO MAIN ---
int main(int argc, char* argv[]) {
    packet pkt; // Packet for receiving data
    struct sockaddr_in cliAddr; 
    socklen_t len = sizeof(cliAddr);
    pthread_t request_handler_tid; // Thread ID for handling client requests
    pthread_t leadership_manager_tid; // Thread ID para gerenciar a liderança

    const size_t timeBufSize = 64;
    char timebuf[timeBufSize];
    server_state state; // O estado global do servidor

    // 1. Inicialização do estado do servidor
    init_server_state(&state); // Inicializa mutexes existentes e contadores

    // 2. Leitura dos argumentos de linha de comando: porta, ID do servidor e flag de líder
    if (argc < 4) { // Espera <porta> <server_id> <1_ou_0_para_lider>
        fprintf(stderr, "Uso: %s <porta> <server_id> <1_ou_0_para_lider>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]); // Porta
    state.server_id = (uint32_t)atoi(argv[2]); // ID único deste servidor
    int initial_is_leader_flag = atoi(argv[3]); // Flag para iniciar como líder ou seguidor

    // Verifica se a porta é a definida em common.h (opcional)
    if (port != PORT) {
        fprintf(stderr, "Error: Invalid port number. It must be %d.\n", PORT);
        exit(EXIT_FAILURE);
    }

    // Passa o socketNumber para a struct state.
    // Lembre-se que initServer agora retorna o socket.
    int socketNumber = initServer(argc, argv); // initServer agora valida args de porta E BIND PARA 127.0.0.1
    state.sock = socketNumber; // Atribui o socket principal à server_state

    // --- NOVO: Atribui o papel inicial do servidor com base no argumento ---
    if (initial_is_leader_flag == 1) {
        state.is_leader = 1; // Inicia como líder
        state.current_leader_id = state.server_id; // Ele é o líder
        // Define o endereço do líder para 127.0.0.1 para teste local
        state.current_leader_addr.sin_family = AF_INET;
        state.current_leader_addr.sin_port = htons(PORT);
        state.current_leader_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
        printf("Server (ID: %u): Iniciado como LÍDER.\n", state.server_id);
    } else {
        state.is_leader = 0; // Inicia como seguidor
        state.current_leader_id = 0; // Ainda não conhece o líder
        printf("Server (ID: %u): Iniciado como SEGUIDOR.\n", state.server_id);
    }
    // Outros campos da state são inicializados em init_server_state ou aqui
    state.election_in_progress = 0;
    state.num_known_servers = 0;
    state.last_leader_heartbeat_time = 0;

    getInitServerState(timebuf, timeBufSize); // Imprime estado inicial do servidor

    // Configura timeout para recvfrom no loop principal do servidor
    struct timeval tv;
    tv.tv_sec = SERVER_RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(socketNumber, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Cria a thread de gerenciamento de liderança
    pthread_create(&leadership_manager_tid, NULL, manage_server_role_thread, &state);
    pthread_detach(leadership_manager_tid);

    // 3. Loop Principal do Servidor
    while (1) {
        int n = recvfrom(socketNumber, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliAddr, &len);

        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue; 
            } else {
                perror("Erro ao receber pacote no main do servidor");
                break;
            }
        }
        
        if (n == 0) {
            continue;
        }
        
        pthread_mutex_lock(&state.known_servers_lock);
        pthread_mutex_lock(&state.lock); 

        switch (pkt.type) {
            case PACKET_TYPE_DESC:
                if (state.is_leader) {
                    packet resp_desc_ack = {.type = PACKET_TYPE_DESC_ACK};
                    resp_desc_ack.data.desc_ack.responding_server_info.server_id = state.server_id;
                    resp_desc_ack.data.desc_ack.responding_server_info.server_addr.sin_family = AF_INET;
                    resp_desc_ack.data.desc_ack.responding_server_info.server_addr.sin_port = htons(PORT);
                    // --- PONTO AJUSTADO PARA LOCAL: ANUNCIAR 127.0.0.1 NO DESC_ACK ---
                    resp_desc_ack.data.desc_ack.responding_server_info.server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    
                    sendto(socketNumber, &resp_desc_ack, sizeof(resp_desc_ack), 0, (struct sockaddr *)&cliAddr, len);
                    find_or_add_client(&state, &cliAddr);
                    printf("Server (ID: %u, Líder): Respondeu DESC_ACK para %s:%d (anunciando %s:%d)\n",
                           state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port),
                           inet_ntoa(resp_desc_ack.data.desc_ack.responding_server_info.server_addr.sin_addr),
                           ntohs(resp_desc_ack.data.desc_ack.responding_server_info.server_addr.sin_port));
                } else {
                    printf("Server (ID: %u, Seguidor): Ignorou DESC de %s:%d (não é o líder).\n",
                           state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                }
                break;

            case PACKET_TYPE_REQ:
                if (state.is_leader) {
                    request_context *ctx = malloc(sizeof(request_context));
                    if (ctx == NULL) {
                        perror("Failed to allocate request context");
                        break;
                    }
                    ctx->pkt = pkt;
                    ctx->addr = cliAddr;
                    ctx->addrlen = len;
                    ctx->sock = socketNumber;
                    ctx->state = &state;

                    pthread_create(&request_handler_tid, NULL, handle_request, ctx);
                    pthread_detach(request_handler_tid);
                } else {
                    printf("Server (ID: %u, Seguidor): Ignorou REQ %u de %s:%d (não é o líder).\n",
                           state.server_id, pkt.seqn, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                }
                break;

            case PACKET_TYPE_HEARTBEAT:
                pthread_mutex_lock(&state.leader_lock);
                // NOVO: Adiciona o servidor do heartbeat na lista de conhecidos, se não estiver.
                add_or_update_known_server(&state, pkt.data.server_info.server_id, &pkt.data.server_info.server_addr);
                
                if (pkt.data.server_info.server_id == state.current_leader_id) {
                    state.last_leader_heartbeat_time = time(NULL);
                    printf("Server (ID: %u): Recebeu HEARTBEAT do líder (ID: %u).\n",
                            state.server_id, state.current_leader_id);
                }
                pthread_mutex_unlock(&state.leader_lock);
                break;

            case PACKET_TYPE_ELECTION:
                pthread_mutex_lock(&state.leader_lock); // Para proteger election_in_progress
                // NOVO: Adiciona o servidor que enviou a eleição na lista de conhecidos
                add_or_update_known_server(&state, pkt.data.server_info.server_id, &pkt.data.server_info.server_addr);

                printf("Server (ID: %u): Recebeu ELECTION de ID %u\n", state.server_id, pkt.data.server_info.server_id);
                
                if (state.server_id > pkt.data.server_info.server_id) {
                    packet alive_resp = {.type = PACKET_TYPE_ALIVE};
                    alive_resp.data.server_info.server_id = state.server_id;
                    alive_resp.data.server_info.server_addr.sin_family = AF_INET;
                    alive_resp.data.server_info.server_addr.sin_port = htons(PORT);
                    // --- PONTO AJUSTADO PARA LOCAL: ANUNCIAR 127.0.0.1 NO ALIVE ---
                    alive_resp.data.server_info.server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    
                    sendto(socketNumber, &alive_resp, sizeof(alive_resp), 0, (struct sockaddr *)&cliAddr, len);
                    printf("Server (ID: %u): Respondeu ALIVE para ID %u (anunciando %s:%d).\n", 
                           state.server_id, pkt.data.server_info.server_id,
                           inet_ntoa(alive_resp.data.server_info.server_addr.sin_addr),
                           ntohs(alive_resp.data.server_info.server_addr.sin_port));

                    // Se eu sou maior e respondi ALIVE, eu devo iniciar minha própria eleição se não estiver em uma
                    if (!state.election_in_progress) {
                        state.election_in_progress = 1;
                        state.election_start_time = time(NULL);
                        printf("Server (ID: %u): Iniciando minha própria eleição.\n", state.server_id);
                    }
                }
                pthread_mutex_unlock(&state.leader_lock);
                break;

            case PACKET_TYPE_ALIVE:
                pthread_mutex_lock(&state.leader_lock); // Para proteger election_in_progress, current_leader_id
                // NOVO: Adiciona o servidor que enviou ALIVE na lista de conhecidos
                add_or_update_known_server(&state, pkt.data.server_info.server_id, &pkt.data.server_info.server_addr);

                printf("Server (ID: %u): Recebeu ALIVE de ID %u\n", state.server_id, pkt.data.server_info.server_id);

                // Se eu estou em eleição e recebi ALIVE de alguém com ID maior, desisto da minha eleição.
                if (state.election_in_progress && pkt.data.server_info.server_id > state.server_id) {
                    state.election_in_progress = 0; // Desiste da eleição
                    // Pode ser que este ALIVE seja do novo líder que ainda não enviou COORDINATOR.
                    // Atualize current_leader_id para este ID maior temporariamente.
                    // A confirmação virá com um COORDINATOR.
                    state.current_leader_id = pkt.data.server_info.server_id;
                    state.current_leader_addr = pkt.data.server_info.server_addr;
                    printf("Server (ID: %u): Recebeu ALIVE de ID maior (%u). Desistindo da eleição.\n",
                           state.server_id, pkt.data.server_info.server_id);
                }
                pthread_mutex_unlock(&state.leader_lock);
                break;

            case PACKET_TYPE_COORDINATOR:
                pthread_mutex_lock(&state.leader_lock);
                // NOVO: Adiciona o servidor que enviou COORDINATOR na lista de conhecidos
                add_or_update_known_server(&state, pkt.data.server_info.server_id, &pkt.data.server_info.server_addr);

                if (state.current_leader_id != pkt.data.server_info.server_id) {
                    state.current_leader_id = pkt.data.server_info.server_id;
                    state.current_leader_addr = pkt.data.server_info.server_addr;
                    state.is_leader = (state.server_id == state.current_leader_id); // Atualiza meu próprio status
                    printf("Server (ID: %u): NOVO COORDENADOR (Líder) é ID %u em %s:%d\n",
                           state.server_id, state.current_leader_id,
                           inet_ntoa(state.current_leader_addr.sin_addr), ntohs(state.current_leader_addr.sin_port));
                    state.election_in_progress = 0; // Eleição terminada (se estava em uma)
                }
                pthread_mutex_unlock(&state.leader_lock);
                break;
            
            case PACKET_TYPE_STATE_REPLICATION:
                if (!state.is_leader) {
                    state.total_reqs = pkt.data.state_repl.total_reqs_at_leader;
                    state.total_sum = pkt.data.state_repl.total_sum_at_leader;
                    // REPLICAÇÃO DE CLIENTES: Isso é complexo e não está na state_replication_data atual.
                    // Se você precisar, precisará de uma forma de serializar e deserializar 'clients'.

                    packet state_ack_resp = {.type = PACKET_TYPE_STATE_ACK};
                    state_ack_resp.data.state_ack.server_id = state.server_id;
                    state_ack_resp.data.state_ack.replicated_seqn = pkt.seqn;
                    sendto(socketNumber, &state_ack_resp, sizeof(state_ack_resp), 0, (struct sockaddr *)&cliAddr, len);
                    printf("Server (ID: %u, Seguidor): Estado replicado (req %u) e ACK enviado para o líder %s:%d\n",
                           state.server_id, pkt.seqn, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                } else {
                    printf("Server (ID: %u, Líder): Recebeu STATE_REPLICATION inesperado de %s:%d\n",
                           state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                }
                break;

            case PACKET_TYPE_STATE_ACK:
                pthread_mutex_lock(&state.replication_ack_lock);
                state.replication_acks_received++;
                pthread_cond_signal(&state.replication_ack_cond);
                printf("Server (ID: %u, Líder): Recebeu STATE_ACK de ID %u. Total ACKs: %d\n",
                       state.server_id, pkt.data.state_ack.server_id, state.replication_acks_received);
                pthread_mutex_unlock(&state.replication_ack_lock);
                break;

            default:
                printf("Server (ID: %u): Pacote de tipo desconhecido (%hu) de %s:%d\n",
                       state.server_id, pkt.type, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                break;
        }
        pthread_mutex_unlock(&state.lock);
        pthread_mutex_unlock(&state.known_servers_lock);
    }
    
    // Finalização
    pthread_join(leadership_manager_tid, NULL);
    endServer(socketNumber);
    return 0;
}