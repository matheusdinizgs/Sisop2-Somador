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
#include <time.h>   // Para time()
#include "common.h" // Inclua o common.h atualizado!
#include "discoveryService.h"
#include "interfaceService.h"
#include "processingService.h"
#include <errno.h> // Para lidar com timeouts de recvfrom

// --- CONSTANTES ---
#define SERVER_RECV_TIMEOUT_SEC 1 // Timeout para recvfrom no loop principal do servidor

// --- FUNÇÃO DA THREAD DE GERENCIAMENTO DE LIDERANÇA (SERÁ IMPLEMENTADA EM processingService.c OU NOVO leadershipManager.c) ---
// Declaração para que main possa chamá-la.
void *manage_server_role_thread(void *arg);

// --- FUNÇÃO MAIN ---
int main(int argc, char *argv[])
{
    packet pkt; // Packet for receiving data
    struct sockaddr_in cliAddr;
    socklen_t len = sizeof(cliAddr);
    pthread_t request_handler_tid;    // Thread ID for handling client requests
    pthread_t leadership_manager_tid; // NOVO: Thread ID para gerenciar a liderança

    const size_t timeBufSize = 64;
    char timebuf[timeBufSize];
    server_state state; // O estado global do servidor

    // 1. Inicialização do estado do servidor
    init_server_state(&state); // Inicializa mutexes existentes e contadores

    // 2. Leitura dos argumentos de linha de comando: porta e ID do servidor
    // initServer já verifica se há 2 argumentos (porta e ID)
    int socketNumber = initServer(argc, argv); // initServer agora valida o número de args
    state.sock = socketNumber;

    // Extrair o server_id do segundo argumento
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <porta> <server_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    state.server_id = (uint32_t)atoi(argv[2]); // Atribui o ID único a este servidor
    state.is_leader = 0;                       // Por padrão, começa como seguidor
    state.current_leader_id = 0;               // Desconhecido no início
    state.election_in_progress = 0;
    state.num_known_servers = 0;
    state.last_leader_heartbeat_time = 0; // Inicializa com 0
    // Mutexes de replicação e de known_servers já inicializados em init_server_state

    getInitServerState(timebuf, timeBufSize); // Imprime estado inicial do servidor

    // NOVO: Configura timeout para recvfrom no loop principal do servidor
    // Para que a thread principal não bloqueie infinitamente e possa reagir a mudanças de estado.
    struct timeval tv;
    tv.tv_sec = SERVER_RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(socketNumber, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    // NOVO: Cria a thread de gerenciamento de liderança
    pthread_create(&leadership_manager_tid, NULL, manage_server_role_thread, &state);
    pthread_detach(leadership_manager_tid); // Permite que a thread libere seus recursos automaticamente

    // 3. Loop Principal do Servidor
    while (1)
    {
        // Tenta receber qualquer pacote
        int n = recvfrom(socketNumber, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliAddr, &len);

        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // Timeout, significa que não recebeu nada por SERVER_RECV_TIMEOUT_SEC.
                // A thread principal pode então verificar outras condições ou continuar.
                continue;
            }
            else
            {
                perror("Erro ao receber pacote no main do servidor");
                break; // Erro fatal, sai do loop
            }
        }

        if (n == 0)
        { // Conexão fechada, improvável em UDP mas bom para verificar
            continue;
        }

        // NOVO: Proteger acesso a variáveis de estado compartilhadas
        // É crucial travar o mutex 'state.lock' ao acessar ou modificar campos da 'state'
        // que são compartilhados entre a thread principal e as threads de tratamento de requisições,
        // e também os mutexes específicos para liderança e servers conhecidos.

        // Bloqueia o mutex geral, mas seja cauteloso para não causar deadlocks
        // com os mutexes da thread de liderança.
        // É melhor bloquear os mutexes mais específicos primeiro.
        pthread_mutex_lock(&state.known_servers_lock); // Trava a lista de servidores conhecidos
        pthread_mutex_lock(&state.lock);               // Trava o estado geral e a lista de clientes

        // --- Despacho de Pacotes ---
        switch (pkt.type)
        {
        case PACKET_TYPE_DESC:
            // Lógica de Descoberta: Servidor responde ao cliente.
            // NOVO: Apenas o líder deve responder com DESC_ACK.
            // Servidores secundários podem ignorar ou responder com REDIRECT.
            if (state.is_leader)
            {
                packet resp_desc_ack = {.type = PACKET_TYPE_DESC_ACK};
                // O DESC_ACK deve conter o server_info_data do líder.
                // Para isso, a struct 'requisicao_ack' em common.h precisar ter o 'server_info_data'.
                // Ou, crie uma nova struct 'desc_ack_data' na union 'packet'.
                // Vamos usar 'server_info_data' diretamente como exemplo para o DESC_ACK aqui.
                resp_desc_ack.data.server_info.server_id = state.server_id;
                resp_desc_ack.data.server_info.server_addr.sin_family = AF_INET;
                resp_desc_ack.data.server_info.server_addr.sin_port = htons(PORT);              // Porta do servidor
                resp_desc_ack.data.server_info.server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Ou o IP real da interface

                // Se o seu serverAddr em initServer já tem o IP da interface, use-o.
                // Ou o servidor pode descobrir seu próprio IP e armazenar em 'state'.
                // Para depuração, inet_ntoa(cliAddr.sin_addr) mostra o remetente,
                // mas para 'server_addr' no pacote, precisamos do IP deste servidor.

                // Assumindo que você tem acesso ao IP local do servidor:
                // get_local_ip_address(&resp_desc_ack.data.server_info.server_addr.sin_addr);

                sendto(socketNumber, &resp_desc_ack, sizeof(resp_desc_ack), 0, (struct sockaddr *)&cliAddr, len);
                find_or_add_client(&state, &cliAddr); // Adiciona o cliente à lista (se for líder)
                printf("Server (ID: %u, Líder): Respondeu DESC_ACK para %s:%d\n",
                       state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
            }
            else
            {
                // Seguidor: Opcional, pode redirecionar o cliente para o líder
                // Ou simplesmente ignora o DESC, esperando que o cliente encontre o COORDINATOR.
                // Se o cliente usa find_leader que espera COORDINATOR, isso é menos crítico.
                // Se o cliente enviar um REQ para um seguidor, o seguidor NÃO vai processar.
                // O cliente deve descobrir o líder via COORDINATOR.
                printf("Server (ID: %u, Seguidor): Ignorou DESC de %s:%d (não é o líder).\n",
                       state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
            }
            break;

        case PACKET_TYPE_REQ:
            // Lógica de Requisição de Cliente: Processa apenas se for o líder
            if (state.is_leader)
            {
                // Aloca memória para o contexto da requisição e cria uma thread para handle_request
                request_context *ctx = malloc(sizeof(request_context));
                if (ctx == NULL)
                {
                    perror("Failed to allocate request context");
                    // Lidar com erro: pode enviar um NACK ou ignorar
                    break;
                }
                ctx->pkt = pkt;
                ctx->addr = cliAddr;
                ctx->addrlen = len;
                ctx->sock = socketNumber;
                ctx->state = &state; // Passa o ponteiro para o estado global do servidor

                pthread_create(&request_handler_tid, NULL, handle_request, ctx);
                pthread_detach(request_handler_tid); // Thread libera seus recursos ao terminar
            }
            else
            {
                // Seguidor: Ignora ou redireciona a requisição.
                // O cliente espera que o líder processe.
                // Se um cliente persistir em enviar para um seguidor, ele não receberá ACK.
                printf("Server (ID: %u, Seguidor): Ignorou REQ %u de %s:%d (não é o líder).\n",
                       state.server_id, pkt.seqn, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
            }
            break;

            // --- NOVOS TIPOS DE PACOTES PARA GERENCIAMENTO DE CLUSTER/ELEIÇÃO ---

        case PACKET_TYPE_HEARTBEAT:
            // Recebido por seguidores do líder.
            // A thread 'manage_server_role_thread' monitorará isso.
            // Apenas registra o recebimento e atualiza a last_leader_heartbeat_time no estado.
            // Bloqueia o mutex do estado de liderança antes de atualizar.
            // Adiciona o servidor remetente à lista de conhecidos
            add_or_update_known_server(&state, pkt.data.server_info.server_id, &pkt.data.server_info.server_addr);

            pthread_mutex_lock(&state.leader_lock);
            if (pkt.data.server_info.server_id == state.current_leader_id)
            {
                state.last_leader_heartbeat_time = time(NULL);
            }
            pthread_mutex_unlock(&state.leader_lock);
            break;

        case PACKET_TYPE_ELECTION:
            // Recebido por qualquer servidor para iniciar uma eleição.
            // NOVO: Ignorar ELECTION enviado por mim mesmo
            if (pkt.data.server_info.server_id == state.server_id)
            {
                // Ignora ELECTION de si mesmo
                break;
            }

            printf("Server (ID: %u): Recebeu ELECTION de ID %u\n", state.server_id, pkt.data.server_info.server_id);

            if (state.server_id > pkt.data.server_info.server_id)
            {
                packet alive_resp = {.type = PACKET_TYPE_ALIVE};
                alive_resp.data.server_info.server_id = state.server_id;
                alive_resp.data.server_info.server_addr.sin_family = AF_INET;
                alive_resp.data.server_info.server_addr.sin_port = htons(PORT);
                sendto(socketNumber, &alive_resp, sizeof(alive_resp), 0, (struct sockaddr *)&cliAddr, len);
                printf("Server (ID: %u): Respondeu ALIVE para %u.\n", state.server_id, pkt.data.server_info.server_id);
            }
            break;

        case PACKET_TYPE_ALIVE:
            // Recebido em resposta a um ELECTION enviado por este servidor.
            // A thread 'manage_server_role_thread' monitorará isso para decidir se venceu a eleição.
            printf("Server (ID: %u): Recebeu ALIVE de ID %u\n", state.server_id, pkt.data.server_info.server_id);
            // A thread de gerenciamento de liderança precisa contar essas respostas.
            // pthread_mutex_lock(&state.leader_lock);
            // if (state.election_in_progress && pkt.data.server_info.server_id > state.server_id) {
            //     // Alguém maior está vivo, desiste da minha eleição
            //     state.election_in_progress = 0;
            //     // Pode atualizar current_leader_id para este maior temporariamente
            // }
            // pthread_mutex_unlock(&state.leader_lock);
            break;

        case PACKET_TYPE_COORDINATOR:
            pthread_mutex_lock(&state.leader_lock);

            // NOVO: Se estou em eleição e recebo COORDINATOR de ID maior, cancelo minha eleição
            if (state.election_in_progress && pkt.data.server_info.server_id > state.server_id)
            {
                printf("Server (ID: %u): Cancelando eleição. Líder (ID: %u) se anunciou.\n",
                       state.server_id, pkt.data.server_info.server_id);
                state.election_in_progress = 0;
            }

            if (state.current_leader_id != pkt.data.server_info.server_id)
            {
                state.current_leader_id = pkt.data.server_info.server_id;
                state.current_leader_addr = pkt.data.server_info.server_addr;
                state.is_leader = (state.server_id == state.current_leader_id);
                state.last_leader_heartbeat_time = time(NULL); // NOVO: Atualiza tempo do heartbeat

                printf("Server (ID: %u): NOVO COORDENADOR (Líder) é ID %u em %s:%d\n",
                       state.server_id, state.current_leader_id,
                       inet_ntoa(state.current_leader_addr.sin_addr),
                       ntohs(state.current_leader_addr.sin_port));
                state.election_in_progress = 0;
            }
            pthread_mutex_unlock(&state.leader_lock);
            break;

        case PACKET_TYPE_STATE_REPLICATION:
            // Recebido por seguidores do líder.
            // O seguidor aplica o estado e envia um ACK de volta.
            if (!state.is_leader)
            { // Somente seguidores devem receber isso
                // pthread_mutex_lock(&state.lock); // Proteger o estado ao aplicá-lo
                // Lógica para aplicar o estado replicado.
                // Isso é complexo, envolve copiar total_reqs, total_sum, e os client_entry.
                // Para simplificar agora, apenas os totais.
                // state->total_reqs = pkt.data.state_repl.total_reqs_at_leader;
                // state->total_sum = pkt.data.state_repl.total_sum_at_leader;
                // Lógica para replicar clientes: se a replicação for por snapshot, precisa copiar o array.
                // Se for por update incremental, aplicar as mudanças.

                // Enviar ACK de replicação de volta ao líder
                packet state_ack_resp = {.type = PACKET_TYPE_STATE_ACK};
                state_ack_resp.data.state_ack.server_id = state.server_id;
                state_ack_resp.data.state_ack.replicated_seqn = pkt.seqn; // ACK para a seqn da requisição replicada
                // O remetente do STATE_REPLICATION é o líder, então envia de volta para cliAddr.
                sendto(socketNumber, &state_ack_resp, sizeof(state_ack_resp), 0, (struct sockaddr *)&cliAddr, len);
                printf("Server (ID: %u, Seguidor): Estado replicado (req %u) e ACK enviado para o líder %s:%d\n",
                       state.server_id, pkt.seqn, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
                // pthread_mutex_unlock(&state.lock);
            }
            else
            {
                // Líder não deveria receber STATE_REPLICATION (apenas de um líder antigo ou erro)
                printf("Server (ID: %u, Líder): Recebeu STATE_REPLICATION inesperado de %s:%d\n",
                       state.server_id, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
            }
            break;

        case PACKET_TYPE_STATE_ACK:
            // Recebido por líder de um seguidor.
            // A thread 'handle_request' (ou uma função de replicação no líder) estará esperando por isso.
            // Ela usa 'state.replication_ack_lock' e 'state.replication_ack_cond'.
            pthread_mutex_lock(&state.replication_ack_lock);
            state.replication_acks_received++;
            // Sinaliza a variável de condição para "acordar" a thread que está esperando
            pthread_cond_signal(&state.replication_ack_cond);
            printf("Server (ID: %u, Líder): Recebeu STATE_ACK de ID %u. Total ACKs: %d\n",
                   state.server_id, pkt.data.state_ack.server_id, state.replication_acks_received);
            pthread_mutex_unlock(&state.replication_ack_lock);
            break;

        default:
            // Pacotes desconhecidos ou não tratados aqui
            printf("Server (ID: %u): Pacote de tipo desconhecido (%hu) de %s:%d\n",
                   state.server_id, pkt.type, inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
            break;
        }
        pthread_mutex_unlock(&state.lock);
        pthread_mutex_unlock(&state.known_servers_lock);
    }

    // Finalização
    // pthread_cancel(leadership_manager_tid); // Pode ser necessário cancelar a thread de gerenciamento
    pthread_join(leadership_manager_tid, NULL); // Esperar a thread de gerenciamento terminar
    endServer(socketNumber);
    return 0;
}