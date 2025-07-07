#ifndef common_h
#define common_h

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>        // Necessário para time_t
#include <pthread.h>     // Necessário para pthread_mutex_t, pthread_cond_t

// --- CONSTANTES ---
#define PORT 4000
#define MAX_CLIENTS 100
#define MAX_HISTORY 100000000
#define MAX_SERVERS 5 // Número máximo de servidores no cluster

// --- TIPOS DE PACOTES ---
#define PACKET_TYPE_DESC            1
#define PACKET_TYPE_REQ             2
#define PACKET_TYPE_DESC_ACK        3
#define PACKET_TYPE_REQ_ACK         4
#define PACKET_TYPE_HEARTBEAT       5  // Líder envia para seguidores
#define PACKET_TYPE_ELECTION        6  // Seguidor inicia uma eleição
#define PACKET_TYPE_ALIVE           7  // Resposta a um pacote ELECTION
#define PACKET_TYPE_COORDINATOR     8  // Novo líder anuncia sua liderança
#define PACKET_TYPE_STATE_REPLICATION 9  // Líder envia estado para seguidores
#define PACKET_TYPE_STATE_ACK       10 // Seguidor confirma recebimento e aplicação do estado
#define PACKET_TYPE_REDIRECT        11 // Seguidor redireciona cliente para o líder

// --- ESTRUTURAS DE DADOS ---

// Estrutura para uma requisição de cliente (valor numérico)
struct requisicao {
    uint32_t value;
};

// Estrutura para um ACK de requisição de cliente
// (Você pode querer adicionar server_info_data aqui também, se o ACK do REQ
// sempre incluir a info do líder, mas por agora mantemos como está).
struct requisicao_ack {
    uint32_t seqn;
    uint32_t num_reqs;
    uint64_t total_sum;
};

// NOVO: Estrutura genérica para informações de um servidor
// Usada em Heartbeat, ALIVE, COORDINATOR, REDIRECT, e DESC_ACK
typedef struct {
    uint32_t server_id;       // ID único do servidor
    struct sockaddr_in server_addr; // Endereço completo (IP e Porta) do servidor
} server_info_data;

// NOVO: Estrutura para o ACK de Descoberta (DESC_ACK)
// Agora inclui 'server_info_data' para informar ao cliente quem está respondendo.
typedef struct {
    server_info_data responding_server_info; // Informações do servidor que enviou o DESC_ACK
    // Você pode adicionar mais campos aqui se o DESC_ACK precisar
    // informar sobre o líder atual (ex: server_info_data current_leader_info_known_by_responder;)
    // Mas para o cliente escutando COORDINATOR, isso pode não ser necessário.
} desc_ack_data;

// NOVO: Estrutura para dados de Replicação de Estado
// Inclui os totais globais do servidor.
// Replicar o array de clients[] é mais complexo;
// esta estrutura pode precisar de mais campos ou de um mecanismo incremental.
typedef struct {
    uint32_t last_processed_seqn_by_leader; // Última seq. global processada pelo líder
    uint32_t total_reqs_at_leader;           // Total de requisições no líder
    uint64_t total_sum_at_leader;            // Somatório total no líder
    // Se for replicar clientes via snapshot:
    // client_entry clients_snapshot[MAX_CLIENTS]; // Cuidado com o tamanho do pacote UDP
    // Ou, para updates incrementais de clientes, defina aqui.
} state_replication_data;

// NOVO: Estrutura para ACK de Replicação de Estado
typedef struct {
    uint32_t server_id;       // ID do servidor que está confirmando a replicação
    uint32_t replicated_seqn; // A última sequência de requisição que ele replicou com sucesso
} state_ack_data;

// A estrutura 'packet' agora contém uma union de todas as possíveis cargas de pacotes.
typedef struct __packet {
    uint16_t type; // Tipo de pacote (PACKET_TYPE_...)
    uint32_t seqn; // Número de sequência (útil para REQ, REQ_ACK, e replicação)
    union {
        struct requisicao req;
        struct requisicao_ack ack;
        desc_ack_data desc_ack;             // Para PACKET_TYPE_DESC_ACK
        server_info_data server_info;       // Para Heartbeat, Election, Alive, Coordinator, Redirect
        state_replication_data state_repl;  // Para PACKET_TYPE_STATE_REPLICATION
        state_ack_data state_ack;           // Para PACKET_TYPE_STATE_ACK
    } data;
} packet;

// Estrutura para informações de um cliente no servidor
typedef struct {
    struct sockaddr_in addr;
    uint32_t last_req;
    uint64_t last_sum;
} client_entry;

// Estrutura para representar outro servidor no cluster (conhecido por este servidor)
typedef struct {
    uint32_t id;                // ID único deste servidor
    struct sockaddr_in addr;    // Endereço IP e porta
    time_t last_heartbeat_recv; // Para o líder verificar se este seguidor está vivo
    int is_alive;               // Flag explícita para o estado (vivo/morto) - DESCOMENTADA!
} server_entry;

// Estrutura para o estado geral de um servidor (seja ele líder ou seguidor)
// Contém todas as informações necessárias para o papel do servidor no cluster.
typedef struct {
    client_entry clients[MAX_CLIENTS];
    int client_count;
    uint32_t total_reqs;
    uint64_t total_sum; // Use uint64_t para total_sum para evitar overflow

    pthread_mutex_t lock; // Mutex para proteger o estado geral (total_reqs, total_sum, clients)

    // ===== CAMPOS PARA LIDERANÇA E REPLICAÇÃO =====
    uint32_t server_id;                 // ID único desta instância do servidor
    int is_leader;                      // 1 se for líder, 0 se for seguidor
    uint32_t current_leader_id;         // ID do servidor que é o líder atual
    struct sockaddr_in current_leader_addr; // Endereço do líder atual
    pthread_mutex_t leader_lock;

    time_t last_leader_heartbeat_time;  // Tempo do último heartbeat recebido do líder (para seguidores)

    // Para o Algoritmo do Valentão e gerenciamento da eleição
    int election_in_progress;           // Flag: 1 se uma eleição estiver em andamento
    time_t election_start_time;         // Quando esta instância iniciou sua eleição
    // NOTA: 'election_responses_expected' e 'election_responses_received'
    // são melhor gerenciados localmente na thread de eleição, não aqui no estado global.
    // O critério de vitória no Bully é "não receber ALIVE de ID maior em um timeout".

    // Lista de outros servidores no cluster (usado pelo líder para replication/heartbeats;
    // por todos para eleição e rastreamento de pares)
    server_entry known_servers[MAX_SERVERS];
    int num_known_servers;
    pthread_mutex_t known_servers_lock; // Protege a lista e campos relacionados a servers

    // Para Sincronização de Replicação (no LÍDER)
    pthread_mutex_t replication_ack_lock;     // Protege os contadores de ACKs de replicação
    pthread_cond_t replication_ack_cond;      // Condição para esperar por ACKs de replicação
    int replication_acks_needed;              // Número de ACKs de replicação esperados (quorum)
    int replication_acks_received;            // Número de ACKs de replicação recebidos para a transação atual

    // O socket do servidor (útil para que as threads internas possam enviar mensagens)
    int sock; 
} server_state;


#endif // common_h