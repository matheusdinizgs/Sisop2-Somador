#include "common.h" // Inclua common.h para os novos tipos de pacotes e server_info_data
#include "interfaceService.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h> // Adicionado para inet_ntoa, que é usado em client_interface_thread
#include <errno.h>     // Adicionado para lidar com erros de socket, como timeouts

// --- FUNÇÕES DE UTILIDADE E IMPRESSÃO (Sem alterações significativas aqui) ---
void current_time(char *buf, size_t len) {
    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

void getInitServerState(char *timeBuf, size_t len) {
    current_time(timeBuf, len);
    printf("%s num_reqs 0 total_sum 0\n", timeBuf);
}

void print_server_state(char *timeBuf,
                        const char *clientAddr,
                        uint32_t seqn,
                        uint32_t value,
                        server_state *state, // Certifique-se que server_state está definido em common.h
                        int is_duplicate) {
    current_time(timeBuf, 64);
    if (is_duplicate) {
        printf("%s client %s DUP!! id_req %u value %u num_reqs %u total_sum %lu\n",
               timeBuf, clientAddr, seqn, value, state->total_reqs, (unsigned long)state->total_sum); // Cast para unsigned long
    } else {
        printf("%s client %s id_req %u value %u num_reqs %u total_sum %lu\n",
               timeBuf, clientAddr, seqn, value, state->total_reqs, (unsigned long)state->total_sum); // Cast para unsigned long
    }
}

void print_client_server_addr(const char *server_addr_str) {
    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s server_addr %s\n", timebuf, server_addr_str);
}

// --- FUNÇÃO client_interface_thread (Recebimento de Mensagens do Cliente) ---
void *client_interface_thread(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    
    // Configura um timeout para recvfrom, para não bloquear indefinidamente.
    // Isso é útil para que a thread possa verificar alguma condição ou ser cancelada.
    struct timeval tv;
    tv.tv_sec = 1;  // 1 segundo de timeout
    tv.tv_usec = 0;
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (1) {
        packet p_recv;
        struct sockaddr_in sender_addr;
        socklen_t sender_addrlen = sizeof(sender_addr);

        // Recebe qualquer pacote do socket
        ssize_t len = recvfrom(ctx->sock, &p_recv, sizeof(p_recv), 0, (struct sockaddr *)&sender_addr, &sender_addrlen);
        
        if (len < 0) {
            // Lidar com erros ou timeouts
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Timeout, apenas continua o loop
                continue;
            } else {
                perror("Erro ao receber no client_interface_thread");
                break; // Sair do loop em caso de erro grave
            }
        }
        
        if (len > 0) {
            char timebuf[64];
            current_time(timebuf, sizeof(timebuf));

            switch (p_recv.type) {
                case PACKET_TYPE_REQ_ACK:
                    // --- Lógica Existente de ACK ---
                    pthread_mutex_lock(ctx->ack_lock);
                    // Verifica se o ACK é para a última sequência esperada
                    if (p_recv.seqn == *(ctx->seqn_ptr)) { // Isso só funciona se `seqn_ptr` aponta para a próxima req a ser enviada.
                                                           // Idealmente, ack.seqn deveria ser igual à última req enviada.
                                                           // (Verificar lógica do client_send_loop)
                        uint32_t value = ctx->valores_enviados[p_recv.seqn]; // Use p_recv.seqn para indexar
                        printf("%s Cliente recebeu ACK do servidor %s:%d para id_req %u (valor %u). num_reqs %u total_sum %lu\n",
                               timebuf, inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                               p_recv.seqn, value, p_recv.data.ack.num_reqs, (unsigned long)p_recv.data.ack.total_sum); // Cast
                        *(ctx->ack_recebido_ptr) = 1;
                        pthread_cond_signal(ctx->ack_cond);
                    } else {
                        // ACK para uma sequência não esperada ou já processada. Ignorar.
                        printf("%s Cliente recebeu ACK inesperado (seq %u). Atual esperada: %u. Ignorando.\n",
                               timebuf, p_recv.seqn, *(ctx->seqn_ptr));
                    }
                    pthread_mutex_unlock(ctx->ack_lock);
                    break;

                case PACKET_TYPE_COORDINATOR:
                    // --- NOVO: Recebimento de Anúncio do Líder ---
                    pthread_mutex_lock(ctx->leader_addr_lock_ptr); // Protege o endereço do líder
                    
                    // Verifica se o ID do líder anunciado é diferente do que o cliente conhece
                    if (*(ctx->current_leader_id_ptr) != p_recv.data.server_info.server_id) {
                        *(ctx->current_leader_id_ptr) = p_recv.data.server_info.server_id;
                        *(ctx->current_leader_addr_ptr) = p_recv.data.server_info.server_addr; // Atualiza o endereço
                        
                        printf("%s Cliente recebeu ANÚNCIO DE COORDENADOR. Novo líder: ID %u em %s:%d\n",
                               timebuf, *(ctx->current_leader_id_ptr),
                               inet_ntoa(ctx->current_leader_addr_ptr->sin_addr),
                               ntohs(ctx->current_leader_addr_ptr->sin_port));
                    }
                    pthread_mutex_unlock(ctx->leader_addr_lock_ptr);
                    break;

                case PACKET_TYPE_REDIRECT:
                    // --- NOVO: Recebimento de Redirecionamento (se um seguidor responder a um REQ) ---
                    // Este caso pode ocorrer se um cliente enviar uma REQ para um servidor secundário,
                    // e este secundário responde com um REDIRECT para o líder.
                    // ATENÇÃO: A lógica atual de send_loop tenta apenas para o líder.
                    // Este REDIRECT seria mais provável se o cliente não souber quem é o líder
                    // e enviar para qualquer um que responda ao DESC inicial.
                    pthread_mutex_lock(ctx->leader_addr_lock_ptr);
                    if (*(ctx->current_leader_id_ptr) != p_recv.data.server_info.server_id) {
                         *(ctx->current_leader_id_ptr) = p_recv.data.server_info.server_id;
                         *(ctx->current_leader_addr_ptr) = p_recv.data.server_info.server_addr;
                         printf("%s Cliente REDIRECIONADO para o líder (ID: %u) em %s:%d\n",
                                timebuf, *(ctx->current_leader_id_ptr),
                                inet_ntoa(ctx->current_leader_addr_ptr->sin_addr),
                                ntohs(ctx->current_leader_addr_ptr->sin_port));
                    }
                    pthread_mutex_unlock(ctx->leader_addr_lock_ptr);
                    // O send_loop deve retransmitir a requisição com o novo endereço
                    // Para isso, você pode sinalizar o send_loop ou ele re-tentar automaticamente
                    // com o endereço atualizado.

                    break;

                default:
                    printf("%s Cliente recebeu pacote de tipo desconhecido: %hu\n", timebuf, p_recv.type);
                    break;
            }
        }
    }
    return NULL;
}

// --- FUNÇÃO client_send_loop (Envio de Mensagens do Cliente) ---
void client_send_loop(client_context_t *ctx) {
    const int MAX_RETRANSMISSIONS = 3; // Número máximo de retransmissões antes de assumir falha
    const long TIMEOUT_NS = 100000000; // 100ms = 100.000.000ns (timeout para ACK)


    char timebuf[64];

    while (1) {
        uint32_t value;
        if (scanf("%u", &value) != 1) {
            // Se o stdin for fechado ou entrada inválida, sai do loop
            break;
        }

        // Armazena o valor enviado para verificação futura
        // (Assumindo que MAX_HISTORY é grande o suficiente para não estourar)
        if (*(ctx->seqn_ptr) < MAX_HISTORY) {
            ctx->valores_enviados[*(ctx->seqn_ptr)] = value;
        } else {
            fprintf(stderr, "Erro: Histórico de valores enviados excedido. seqn = %u\n", *(ctx->seqn_ptr));
            // Pode resetar seqn_ptr ou lidar de outra forma se apropriado
            continue; // Pula para a próxima iteração
        }

        packet req = {
            .type = PACKET_TYPE_REQ,
            .seqn = *(ctx->seqn_ptr),
        };
        req.data.req.value = value;

        int retransmissions = 0;
        int ack_received_for_current_req = 0; // Flag local para a requisição atual

        pthread_mutex_lock(ctx->ack_lock); // Trava o mutex para gerenciar o ACK

        // Loop de envio e espera por ACK com retransmissões
        while (!ack_received_for_current_req && retransmissions < MAX_RETRANSMISSIONS) {
            
            // Declare timebuf here, inside the loop's scope
            current_time(timebuf, sizeof(timebuf)); // <-- POPULATE timebuf HERE
            
            
            pthread_mutex_lock(ctx->leader_addr_lock_ptr); // Trava o mutex do líder para pegar o endereço atual
            struct sockaddr_in target_addr = *(ctx->current_leader_addr_ptr); // Pega o endereço do líder atual
            pthread_mutex_unlock(ctx->leader_addr_lock_ptr); // Libera o mutex do líder

            // Envia a requisição para o ENDEREÇO ATUAL DO LÍDER
            sendto(ctx->sock, &req, sizeof(req), 0, (struct sockaddr *)&target_addr, ctx->addrlen);
            
            printf("%s Cliente enviou req %u (valor %u) para líder %s:%d (Tentativa %d/%d)\n",
                   timebuf, req.seqn, req.data.req.value, inet_ntoa(target_addr.sin_addr), ntohs(target_addr.sin_port),
                   retransmissions + 1, MAX_RETRANSMISSIONS);

            // Reseta a flag global de ACK recebido para esta requisição
            *(ctx->ack_recebido_ptr) = 0;

            // Espera pelo ACK
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += TIMEOUT_NS;
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_nsec -= 1000000000;
                timeout.tv_sec++;
            }
            
            // pthread_cond_timedwait retorna 0 se sinalizado, ETIMEDOUT se timeout
            int wait_status = pthread_cond_timedwait(ctx->ack_cond, ctx->ack_lock, &timeout);

            if (*(ctx->ack_recebido_ptr)) { // Verifica a flag global que client_interface_thread setará
                ack_received_for_current_req = 1; // ACK recebido para esta requisição
            } else if (wait_status == ETIMEDOUT) {
                printf("%s Cliente: Timeout esperando ACK para req %u. Reenviando...\n", timebuf, req.seqn);
                retransmissions++;
            } else {
                // Outro erro ou sinal inesperado, re-tenta
                printf("%s Cliente: Erro ou sinal inesperado esperando ACK para req %u. Reenviando...\n", timebuf, req.seqn);
                retransmissions++;
            }
        } // Fim do loop de retransmissão

        if (!ack_received_for_current_req) {
            // Se o ACK não foi recebido após todas as retransmissões
            printf("%s Cliente: Não foi possível receber ACK para req %u após %d tentativas. Líder pode ter caído ou há problemas de rede.\n",
                   timebuf, req.seqn, MAX_RETRANSMISSIONS);
            // Aqui, você pode implementar uma lógica de "falha do líder" do lado do cliente:
            // - Pode tentar chamar find_leader() novamente.
            // - Pode apenas imprimir um erro e continuar (se o sistema for tolerante a perdas de requisições).
            // - Pode sair.

            // Para um cliente mais robusto:
            // Se o líder não responde, o cliente PRECISA re-executar find_leader()
            // ou ter um mecanismo passivo para obter o novo líder.
            // Uma solução simples aqui é, se houver falha, tentar re-encontrar o líder
            // antes de enviar a próxima requisição.
            printf("%s Cliente: Tentando encontrar novo líder antes da próxima requisição...\n", timebuf);
            pthread_mutex_unlock(ctx->ack_lock); // Libera o lock antes de chamar find_leader
            // Nota: find_leader pode bloquear, e o seqn_ptr já foi incrementado.
            // Poderia ser melhor ter um loop externo ou um estado "aguardando líder"
            // que impeça o envio de novas requisições.
            // Por simplicidade, chamamos aqui, mas em um sistema real isso exigiria mais refino.
            
            // Para não bloquear o envio de mensagens (apenas o ack), podemos simplificar a retry aqui.
            // A client_interface_thread irá atualizar o leader_addr e id se um novo COORDINATOR chegar.
            // Se o leader atual não responder, a próxima tentativa de sendto simplesmente usará o
            // novo leader_addr (se atualizado pela interface thread).
            // A ação mais direta para o cliente aqui é simplesmente parar e esperar
            // que a interface_thread detecte um novo COORDINATOR.

            // Para o momento, vamos apenas avisar e sair.
            // Em uma versão mais avançada, haveria um loop externo que tentaria reconectar.
            // exit(EXIT_FAILURE); // Pode ser muito agressivo
            // Continua o loop para a próxima entrada do usuário, que talvez seja enviada ao novo líder.
            // A interface_thread é quem atualizaria ctx->current_leader_addr_ptr.
        } else {
            // ACK recebido com sucesso.
        }
        pthread_mutex_unlock(ctx->ack_lock); // Libera o mutex do ACK

        // AQUI (FORA DO MUTEX), INCREMENTA O NÚMERO DE SEQUÊNCIA
        (*(ctx->seqn_ptr))++;
        usleep(100000); // Pequeno atraso para não sobrecarregar
    }
}