#include "processingService.h"
#include "interfaceService.h"
#include "common.h" // Inclua common.h para os novos tipos de pacotes e structs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>   // Para usleep
#include <sys/time.h> // Para struct timeval e gettimeofday
#include <time.h>     // Para time()
#include <errno.h>    // Para ETIMEDOUT
#include <ifaddrs.h>

// --- CONSTANTES DE LIDERANÇA ---
#define HEARTBEAT_INTERVAL_SEC 1  // Líder envia heartbeats a cada 1 segundo
#define LEADER_TIMEOUT_SEC 5      // Seguidor espera 5 segundos sem heartbeat antes de iniciar eleição
#define ELECTION_TIMEOUT_SEC 3    // Servidor espera 3 segundos por respostas ALIVE após iniciar eleição
#define REPLICATION_TIMEOUT_SEC 2 // Líder espera 2 segundos por ACKs de replicação

// --- FUNÇÕES DE ESTADO DO SERVIDOR ---

void init_server_state(server_state *state)
{
    state->client_count = 0;
    state->total_reqs = 0;
    state->total_sum = 0;
    pthread_mutex_init(&state->lock, NULL); // Mutex para o estado geral (total_reqs, total_sum, clients)

    // --- NOVAS INICIALIZAÇÕES PARA LIDERANÇA E REPLICAÇÃO ---
    state->is_leader = 0;         // Inicialmente não é líder
    state->server_id = 0;         // Será definido no main do server_testing.c
    state->current_leader_id = 0; // Desconhecido
    memset(&state->current_leader_addr, 0, sizeof(state->current_leader_addr));
    state->election_in_progress = 0;
    state->last_leader_heartbeat_time = 0;

    state->num_known_servers = 0;
    pthread_mutex_init(&state->known_servers_lock, NULL); // Mutex para a lista known_servers

    pthread_mutex_init(&state->leader_lock, NULL); // Mutex para variáveis de estado de liderança (is_leader, current_leader_id, etc.)

    // Mutex e cond para replicação
    pthread_mutex_init(&state->replication_ack_lock, NULL);
    pthread_cond_init(&state->replication_ack_cond, NULL);
    state->replication_acks_needed = 0;
    state->replication_acks_received = 0;
}

int find_or_add_client(server_state *state, struct sockaddr_in *addr)
{
    pthread_mutex_lock(&state->lock); // Protege o array de clientes
    for (int i = 0; i < state->client_count; i++)
    {
        if (state->clients[i].addr.sin_port == addr->sin_port &&
            state->clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr)
        {
            pthread_mutex_unlock(&state->lock);
            return i;
        }
    }
    if (state->client_count < MAX_CLIENTS)
    { // Verifica limite
        state->clients[state->client_count].addr = *addr;
        state->clients[state->client_count].last_req = 0;
        state->clients[state->client_count].last_sum = 0;
        int new_client_idx = state->client_count++;
        pthread_mutex_unlock(&state->lock);
        return new_client_idx;
    }
    pthread_mutex_unlock(&state->lock);
    return -1; // Erro: limite de clientes atingido
}

// NOVO: Adiciona/atualiza um servidor na lista de servidores conhecidos
void add_or_update_known_server(server_state *state, uint32_t server_id, struct sockaddr_in *server_addr)
{
    pthread_mutex_lock(&state->known_servers_lock);
    for (int i = 0; i < state->num_known_servers; i++)
    {
        if (state->known_servers[i].id == server_id)
        {
            state->known_servers[i].addr = *server_addr; // Atualiza endereço
            pthread_mutex_unlock(&state->known_servers_lock);
            return;
        }
    }
    if (state->num_known_servers < MAX_SERVERS)
    {
        state->known_servers[state->num_known_servers].id = server_id;
        state->known_servers[state->num_known_servers].addr = *server_addr;
        state->num_known_servers++;
        printf("Server (ID: %u): Adicionado/Atualizado servidor conhecido: ID %u em %s:%d. Total: %d\n",
               state->server_id, server_id, inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port),
               state->num_known_servers);
    }
    else
    {
        fprintf(stderr, "Server (ID: %u): Limite de servidores conhecidos atingido. Não foi possível adicionar ID %u.\n",
                state->server_id, server_id);
    }
    pthread_mutex_unlock(&state->known_servers_lock);
}

// --- FUNÇÃO DE TRATAMENTO DE REQUISIÇÕES (handle_request) ---

void *handle_request(void *arg)
{
    request_context *ctx = (request_context *)arg;

    packet *pkt = &ctx->pkt;
    char timebuf[64];
    int idx = find_or_add_client(ctx->state, &ctx->addr); // Garante que o cliente está na lista

    if (idx == -1)
    { // Erro ao adicionar cliente
        fprintf(stderr, "Server (ID: %u): Erro ao processar requisição, cliente não adicionado.\n", ctx->state->server_id);
        free(ctx);
        return NULL;
    }

    pthread_mutex_lock(&ctx->state->lock); // Trava o estado geral do servidor

    int is_duplicate = 0;
    if (pkt->seqn == ctx->state->clients[idx].last_req + 1)
    {
        ctx->state->total_reqs++;
        ctx->state->total_sum += pkt->data.req.value;
        ctx->state->clients[idx].last_req = pkt->seqn;
        ctx->state->clients[idx].last_sum = ctx->state->total_sum; // Atualiza last_sum do cliente
    }
    else
    {
        is_duplicate = 1;
    }
    print_server_state(timebuf, inet_ntoa(ctx->addr.sin_addr), pkt->seqn, pkt->data.req.value, ctx->state, is_duplicate);

    // --- NOVO: REPLICAÇÃO DO ESTADO PARA SEGUIDORES (SOMENTE SE FOR LÍDER) ---
    // Esta lógica deve ser executada APENAS se o servidor for o líder.
    // O main.c já garante que handle_request é chamado apenas pelo líder.
    pthread_mutex_lock(&ctx->state->leader_lock);
    int is_current_server_leader = ctx->state->is_leader;
    pthread_mutex_unlock(&ctx->state->leader_lock);

    if (is_current_server_leader && !is_duplicate)
    { // Apenas replica se for líder e não for duplicata
        pthread_mutex_lock(&ctx->state->replication_ack_lock);
        ctx->state->replication_acks_received = 0; // Zera contagem de ACKs para esta replicação

        // Contar quantos seguidores existem (excluindo a si mesmo)
        pthread_mutex_lock(&ctx->state->known_servers_lock);
        int num_followers = 0;
        for (int i = 0; i < ctx->state->num_known_servers; i++)
        {
            if (ctx->state->known_servers[i].id != ctx->state->server_id)
            {
                num_followers++;
            }
        }
        ctx->state->replication_acks_needed = num_followers; // Espera ACKs de todos os seguidores
        pthread_mutex_unlock(&ctx->state->known_servers_lock);

        if (num_followers > 0)
        {
            packet repl_pkt = {
                .type = PACKET_TYPE_STATE_REPLICATION,
                .seqn = pkt->seqn, // Usa a seqn da requisição original para rastrear a replicação
            };
            // Replica o estado atual do líder
            repl_pkt.data.state_repl.total_reqs_at_leader = ctx->state->total_reqs;
            repl_pkt.data.state_repl.total_sum_at_leader = ctx->state->total_sum;
            // Para replicar 'clients' seria mais complexo, talvez um array de client_entry ou um mecanismo incremental.
            // Por enquanto, focamos nos totais.

            // Envia o pacote de replicação para todos os seguidores conhecidos
            pthread_mutex_lock(&ctx->state->known_servers_lock); // Trava novamente para iterar
            for (int i = 0; i < ctx->state->num_known_servers; i++)
            {
                if (ctx->state->known_servers[i].id != ctx->state->server_id)
                { // Não envia para si mesmo
                    sendto(ctx->sock, &repl_pkt, sizeof(repl_pkt), 0,
                           (struct sockaddr *)&ctx->state->known_servers[i].addr, sizeof(struct sockaddr_in));
                    printf("Server (ID: %u, Líder): Enviou REPLICAÇÃO (req %u) para seguidor ID %u em %s:%d\n",
                           ctx->state->server_id, pkt->seqn, ctx->state->known_servers[i].id,
                           inet_ntoa(ctx->state->known_servers[i].addr.sin_addr),
                           ntohs(ctx->state->known_servers[i].addr.sin_port));
                }
            }
            pthread_mutex_unlock(&ctx->state->known_servers_lock);

            // Esperar pelos ACKs de replicação
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += REPLICATION_TIMEOUT_SEC; // Espera por 2 segundos

            // Loop para esperar por todos os ACKs esperados
            while (ctx->state->replication_acks_received < ctx->state->replication_acks_needed)
            {
                int wait_status = pthread_cond_timedwait(&ctx->state->replication_ack_cond, &ctx->state->replication_ack_lock, &timeout);
                if (wait_status == ETIMEDOUT)
                {
                    printf("Server (ID: %u, Líder): Timeout esperando ACKs de replicação para req %u. Recebidos: %d/%d\n",
                           ctx->state->server_id, pkt->seqn, ctx->state->replication_acks_received, ctx->state->replication_acks_needed);
                    break; // Sai do loop de espera se houver timeout
                }
            }
        }
        else
        {
            printf("Server (ID: %u, Líder): NENHUM seguidor para replicar o estado.\n", ctx->state->server_id);
        }
        pthread_mutex_unlock(&ctx->state->replication_ack_lock); // Libera o lock de ACK de replicação
    }

    // --- Envia ACK para o Cliente ---
    packet ack = {
        .type = PACKET_TYPE_REQ_ACK,
        .seqn = ctx->state->clients[idx].last_req,
    };
    ack.data.ack.num_reqs = ctx->state->total_reqs;
    ack.data.ack.total_sum = ctx->state->clients[idx].last_sum;
    sendto(ctx->sock, &ack, sizeof(ack), 0,
           (struct sockaddr *)&ctx->addr, ctx->addrlen);
    printf("Server (ID: %u, Líder): Enviou ACK para cliente %s:%d para req %u\n",
           ctx->state->server_id, inet_ntoa(ctx->addr.sin_addr), ntohs(ctx->addr.sin_port), pkt->seqn);

    pthread_mutex_unlock(&ctx->state->lock); // Libera o mutex do estado geral

    free(ctx);
    return NULL;
}

// --- NOVO: THREAD DE GERENCIAMENTO DE LIDERANÇA (manage_server_role_thread) ---
// Esta thread é o coração da lógica de cluster e eleição.
void *manage_server_role_thread(void *arg)
{
    server_state *state = (server_state *)arg;
    char timebuf[64];
    time_t last_heartbeat_sent_time = 0; // Para o líder
    time_t election_start_time = 0;      // Para o seguidor/candidato
    int initial_election_done = 0;       // NOVO: Flag para eleição inicial

    // NOVO: Adicionar a si mesmo à lista de servidores conhecidos
    // Isso é importante para que o líder saiba para quem replicar, e seguidores para quem enviar ELECTION.
    struct sockaddr_in self_addr;
    memset(&self_addr, 0, sizeof(self_addr));
    self_addr.sin_family = AF_INET;
    self_addr.sin_port = htons(PORT);
    // Para obter o IP real do servidor, você precisaria de uma função get_local_ip_address.
    // Por simplicidade, usaremos INADDR_ANY ou um IP de loopback para testes.
    // self_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    // Ou, se você tem o IP real, use inet_addr("192.168.1.X")
    // Para um servidor real, é crucial que este seja o IP acessível por outros servidores.

    // Uma forma de obter o IP real (exige mais código, mas é robusto):
    // char ip_str[INET_ADDRSTRLEN];
    // if (get_local_ip_address_str(ip_str, sizeof(ip_str)) == 0) {
    //     self_addr.sin_addr.s_addr = inet_addr(ip_str);
    // } else {
    //     self_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Fallback
    // }
    // Por enquanto, vamos usar INADDR_ANY para simplificar e focar na lógica.
    // self_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Assumindo que outros servers podem alcançar este IP
    if (get_local_ip_address(&self_addr.sin_addr) != 0)
    {
        self_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Fallback
    }

    add_or_update_known_server(state, state->server_id, &self_addr);
    sleep(2);

    while (1)
    {
        current_time(timebuf, sizeof(timebuf));
        time_t now = time(NULL);

        pthread_mutex_lock(&state->leader_lock); // Trava o estado de liderança

        // Eleição inicial se ainda não houve uma
        if (!initial_election_done)
        {
            printf("%s Server (ID: %u): Iniciando eleição inicial...\n", timebuf, state->server_id);

            packet election_pkt = {.type = PACKET_TYPE_ELECTION};
            election_pkt.data.server_info.server_id = state->server_id;
            election_pkt.data.server_info.server_addr = self_addr;

            pthread_mutex_lock(&state->known_servers_lock);

            // Verifica se há servidores com ID maior
            int has_higher_id_server = 0;
            for (int i = 0; i < state->num_known_servers; i++)
            {
                if (state->known_servers[i].id > state->server_id)
                {
                    has_higher_id_server = 1;
                    break;
                }
            }

            if (!has_higher_id_server && state->num_known_servers <= 1)
            {
                // Apenas eu mesmo na lista, ou ninguém com ID maior
                printf("%s Server (ID: %u): Nenhum servidor com ID maior encontrado. EU SOU O LÍDER!\n",
                       timebuf, state->server_id);
                state->is_leader = 1;
                state->current_leader_id = state->server_id;
                state->current_leader_addr = self_addr;
                state->election_in_progress = 0;
                last_heartbeat_sent_time = now - HEARTBEAT_INTERVAL_SEC; // Força envio imediato
            }
            else
            {
                // Há outros servidores, envia ELECTION por broadcast
                struct sockaddr_in broadcast_addr;
                memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                broadcast_addr.sin_family = AF_INET;
                broadcast_addr.sin_port = htons(PORT);
                broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

                sendto(state->sock, &election_pkt, sizeof(election_pkt), 0,
                       (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
                printf("%s Server (ID: %u): Enviou ELECTION por broadcast\n", timebuf, state->server_id);

                state->election_in_progress = 1;
                election_start_time = now;
            }

            pthread_mutex_unlock(&state->known_servers_lock);
            initial_election_done = 1;
        }
        // --- Lógica do Líder ---
        if (state->is_leader)
        {
            // Enviar HEARTBEATs periodicamente
            if (now - last_heartbeat_sent_time >= HEARTBEAT_INTERVAL_SEC)
            {
                packet hb_pkt = {.type = PACKET_TYPE_HEARTBEAT};
                hb_pkt.data.server_info.server_id = state->server_id;
                hb_pkt.data.server_info.server_addr = self_addr; // O endereço do próprio líder

                // Enviar HEARTBEAT para todos os servidores conhecidos (incluindo a si mesmo, mas não é problema)
                // E também enviar PACKET_TYPE_COORDINATOR por BROADCAST para clientes e outros servidores

                // Enviar HEARTBEAT para servidores conhecidos (unicast)
                pthread_mutex_lock(&state->known_servers_lock);
                for (int i = 0; i < state->num_known_servers; i++)
                {
                    if (state->known_servers[i].id != state->server_id)
                    { // Não envia heartbeat para si mesmo
                        sendto(state->sock, &hb_pkt, sizeof(hb_pkt), 0,
                               (struct sockaddr *)&state->known_servers[i].addr, sizeof(struct sockaddr_in));
                    }
                }
                pthread_mutex_unlock(&state->known_servers_lock);

                // --- NOVO: Líder envia PACKET_TYPE_COORDINATOR por BROADCAST ---
                // Para isso, o socket precisa ter a opção SO_BROADCAST habilitada.
                // Isso deve ser feito uma vez na inicialização do socket (no initServer).
                // E o endereço de destino deve ser o IP de broadcast (255.250.255.255)
                // ou o endereço de broadcast da sub-rede local.

                packet coord_pkt = {.type = PACKET_TYPE_COORDINATOR};
                coord_pkt.data.server_info.server_id = state->server_id;
                coord_pkt.data.server_info.server_addr = self_addr;

                struct sockaddr_in broadcast_addr;
                memset(&broadcast_addr, 0, sizeof(broadcast_addr));
                broadcast_addr.sin_family = AF_INET;
                broadcast_addr.sin_port = htons(PORT);                    // A mesma porta que clientes e servidores escutam
                broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255

                // Certifique-se que o socket 'state->sock' tem a opção SO_BROADCAST ativada.
                // Isso deve ser feito em initServer.
                sendto(state->sock, &coord_pkt, sizeof(coord_pkt), 0,
                       (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

                printf("%s Server (ID: %u, LÍDER): Enviou HEARTBEATs e COORDINATOR por broadcast.\n", timebuf, state->server_id);
                last_heartbeat_sent_time = now;
            }
        }
        // --- Lógica do Seguidor (e Candidato) ---
        else
        { // Se não é líder
            // Detecção de falha do líder (se houver um líder conhecido)
            if (state->current_leader_id != 0 && now - state->last_leader_heartbeat_time >= LEADER_TIMEOUT_SEC)
            {
                printf("%s Server (ID: %u, Seguidor): Líder (ID: %u) não responde. Iniciando eleição!\n",
                       timebuf, state->server_id, state->current_leader_id);
                state->election_in_progress = 1;
                election_start_time = now;
                // election_responses_received = 0; // Zera contagem de respostas ALIVE

                // Iniciar eleição (Algoritmo do Valentão)
                // Enviar ELECTION para todos os servidores com ID maior
                packet election_pkt = {.type = PACKET_TYPE_ELECTION};
                election_pkt.data.server_info.server_id = state->server_id;
                election_pkt.data.server_info.server_addr = self_addr;

                pthread_mutex_lock(&state->known_servers_lock);
                int sent_election_to_higher = 0;
                for (int i = 0; i < state->num_known_servers; i++)
                {
                    if (state->known_servers[i].id > state->server_id)
                    {
                        sendto(state->sock, &election_pkt, sizeof(election_pkt), 0,
                               (struct sockaddr *)&state->known_servers[i].addr, sizeof(struct sockaddr_in));
                        printf("%s Server (ID: %u): Enviou ELECTION para ID %u\n",
                               timebuf, state->server_id, state->known_servers[i].id);
                        sent_election_to_higher = 1;
                    }
                }
                pthread_mutex_unlock(&state->known_servers_lock);

                if (!sent_election_to_higher)
                {
                    // Se não há ninguém com ID maior, eu sou o novo líder!
                    printf("%s Server (ID: %u): Ninguém com ID maior. EU SOU O NOVO LÍDER!\n", timebuf, state->server_id);
                    state->is_leader = 1;
                    state->current_leader_id = state->server_id;
                    state->current_leader_addr = self_addr;
                    state->election_in_progress = 0;
                    last_heartbeat_sent_time = now - HEARTBEAT_INTERVAL_SEC; // Força o envio imediato de heartbeat/coordinator
                }
            }
            // Lógica para quando uma eleição está em progresso
            else if (state->election_in_progress)
            {
                // Se o tempo de eleição expirou e não recebi ALIVE de ninguém maior, eu sou o líder
                if (now - election_start_time >= ELECTION_TIMEOUT_SEC)
                {
                    // NOTA: A contagem de ALIVEs é feita na thread principal, que recebe os pacotes.
                    // Para que esta thread saiba, precisaríamos de um mecanismo de comunicação entre threads,
                    // como uma fila de mensagens ou um contador protegido por mutex.
                    // Por simplicidade, vamos assumir que se o timeout ocorrer e não houver ALIVE de ID maior,
                    // este servidor se declara líder.
                    // A contagem de 'election_responses_received' deveria ser de ALIVEs de IDs MAIORES.
                    // Isso é um ponto de refinamento.

                    // Se eu não recebi ALIVE de ninguém maior que eu, eu sou o líder.
                    // (Isso requer que a thread principal atualize uma flag ou contador aqui)
                    // Por enquanto, vamos simplificar: se o timeout da eleição passar,
                    // e *eu não tiver recebido um COORDINATOR de um ID maior*, eu me torno líder.
                    // A lógica de recebimento de ALIVEs e COORDINATORs está na thread principal.
                    // A thread de gerenciamento precisa verificar o current_leader_id.

                    // Se o current_leader_id não foi atualizado para um ID maior que o meu
                    // durante a eleição, e o timeout passou:
                    pthread_mutex_lock(&state->leader_lock); // Trava novamente para verificar o líder
                    if (state->current_leader_id != 0 && state->current_leader_id > state->server_id)
                    {
                        // Um líder maior já se anunciou, então eu desisto da eleição.
                        state->election_in_progress = 0;
                        printf("%s Server (ID: %u): Eleição encerrada. Líder (ID: %u) já se anunciou.\n",
                               timebuf, state->server_id, state->current_leader_id);
                    }
                    else
                    {
                        // Ninguém maior se anunciou, eu sou o líder.
                        printf("%s Server (ID: %u): Timeout de eleição. Ninguém maior respondeu. EU SOU O NOVO LÍDER!\n",
                               timebuf, state->server_id);
                        state->is_leader = 1;
                        state->current_leader_id = state->server_id;
                        state->current_leader_addr = self_addr;
                        state->election_in_progress = 0;
                        last_heartbeat_sent_time = now - HEARTBEAT_INTERVAL_SEC; // Força envio imediato
                    }
                    pthread_mutex_unlock(&state->leader_lock);
                }
            }
        }
        pthread_mutex_unlock(&state->leader_lock); // Libera o estado de liderança

        usleep(100000); // Pequeno atraso para evitar CPU busy-wait (100ms)
    }
    return NULL;
}

int get_local_ip_address(struct in_addr *addr)
{
    struct ifaddrs *ifaddr, *ifa;
    int family;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET)
        {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;

            // Pula loopback
            if (sa->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
            {
                continue;
            }

            *addr = sa->sin_addr;
            freeifaddrs(ifaddr);
            return 0;
        }
    }

    freeifaddrs(ifaddr);
    return -1;
}