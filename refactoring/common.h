#ifndef common_h
#define common_h

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>        // Adicione para usar time_t
#include <pthread.h>     // Adicione para pthread_mutex_t, pthread_cond_t

//CONSTANTES
#define PORT 4000
#define MAX_CLIENTS 100
#define MAX_SERVERS 5 // Novo: Número máximo de servidores no cluster

// Tipos de pacotes existentes
#define PACKET_TYPE_DESC 1
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_DESC_ACK 3
#define PACKET_TYPE_REQ_ACK 4

// --- NOVOS TIPOS DE PACOTES PARA ELEIÇÃO E REPLICAÇÃO ---
#define PACKET_TYPE_HEARTBEAT       5 // Líder envia para seguidores
#define PACKET_TYPE_ELECTION        6 // Seguidor inicia uma eleição
#define PACKET_TYPE_ALIVE           7 // Resposta a um pacote ELECTION
#define PACKET_TYPE_COORDINATOR     8 // Novo líder anuncia sua liderança
#define PACKET_TYPE_STATE_REPLICATION 9 // Líder envia estado para seguidores
#define PACKET_TYPE_STATE_ACK       10 // Seguidor confirma recebimento e aplicação do estado
#define PACKET_TYPE_REDIRECT        11 // Seguidor redireciona cliente para o líder

struct requisicao {
    uint32_t value;
};

struct requisicao_ack {
    uint32_t seqn;
    uint32_t num_reqs;
    uint64_t total_sum;
};


// --- NOVAS ESTRUTURAS PARA DADOS DE PACOTES (Eleição e Replicação) ---
// Dados para Heartbeat, ALIVE, COORDINATOR, REDIRECT
typedef struct {
    uint32_t server_id;     // ID único do servidor remetente/alvo
    struct sockaddr_in server_addr; // Endereço completo (IP e Porta) do servidor
} server_info_data;


// Dados para Replicação de Estado
// IMPORTANTE: Esta estrutura deve refletir o estado que você quer replicar.
// Para o seu caso, é o total_reqs, total_sum e o estado de CADA cliente.
// Isso pode ser grande. Você pode precisar replicar incrementalmente ou em partes.
// Para começar, vamos tentar replicar o estado completo de server_state relevante.
// NOTA: A replicação do array client_entry pode ser complexa via struct simples.
// Considere um mecanismo que envia as atualizações de clientes individualmente ou um snapshot.
typedef struct {
    uint32_t last_processed_seqn_by_leader; // Última seq. global processada pelo líder
    uint32_t total_reqs_at_leader;           // Total de requisições no líder
    uint64_t total_sum_at_leader;            // Somatório total no líder
    // Você precisará de uma forma de replicar 'client_entry's.
    // Para simplificar agora, apenas os totais globais.
    // Para clientes, você pode enviar updates específicos ou um snapshot completo.
    // Ex: client_entry clients_snapshot[MAX_CLIENTS]; // Muito grande para pacote UDP
    // Melhor: uma estrutura que indica qual cliente e sua 'last_req' e 'last_sum'
} state_replication_data;

// Estrutura para ACK de Replicação
typedef struct {
    uint32_t server_id; // ID do servidor que está confirmando a replicação
    uint32_t replicated_seqn; // A última seqn que ele replicou com sucesso
} state_ack_data;



typedef struct __packet {
    uint16_t type;
    uint32_t seqn;
    union {
        struct requisicao req;
        struct requisicao_ack ack;
        // --- NOVOS CAMPOS NA UNION ---
        server_info_data server_info;       // Para Heartbeat, Election, Alive, Coordinator, Redirect
        state_replication_data state_repl;  // Para Replicação de Estado
        state_ack_data state_ack;           // Para ACK de Replicação de Estado
    } data;
} packet;

typedef struct {
    struct sockaddr_in addr;
    uint32_t last_req;
    uint64_t last_sum;
} client_entry;

// NOVO: Estrutura para representar um servidor secundário conhecido pelo líder
typedef struct {
    uint32_t id;                // ID único deste servidor secundário
    struct sockaddr_in addr;    // Endereço IP e porta
    time_t last_heartbeat_recv; // Para o líder verificar se este seguidor está vivo
    int is_alive;               // Flag para controlar o estado (vivo/morto) (talvez nao usar)
} server_entry;


// --- ESTRUTURA `server_state` ATUALIZADA ---
// Ela agora gerencia o papel do servidor (líder/seguidor) e o estado do cluster
typedef struct {
    client_entry clients[MAX_CLIENTS];
    int client_count;
    uint32_t total_reqs;
    uint64_t total_sum; // Use uint64_t para total_sum para evitar overflow

    pthread_mutex_t lock; // Mutex existente para proteger o estado (total_reqs, total_sum, clients)

    // ===== NOVOS CAMPOS PARA LIDERANÇA E REPLICAÇÃO =====
    uint32_t server_id;                 // ID único desta instância do servidor
    int is_leader;                      // 1 se for líder, 0 se for seguidor
    uint32_t current_leader_id;         // ID do servidor que é o líder atual
    struct sockaddr_in current_leader_addr; // Endereço do líder atual

    time_t last_leader_heartbeat_time;  // Tempo do último heartbeat recebido do líder (para seguidores)

    // Para o Algoritmo do Valentão e gerenciamento da eleição
    int election_in_progress;           // Flag: 1 se uma eleição estiver em andamento
    time_t election_start_time;         // Quando esta instância iniciou sua eleição
    int election_responses_expected;    // Usado no bully: quantos "Alive" espero de maiores ID
    int election_responses_received;    // Usado no bully: quantos "Alive" recebi

    // Lista de outros servidores no cluster (seguidores no líder; todos os outros no seguidor)
    server_entry known_servers[MAX_SERVERS];
    int num_known_servers;
    pthread_mutex_t known_servers_lock; // Protege a lista e campos relacionados a servers

    // Para Sincronização de Replicação (no LÍDER)
    pthread_mutex_t replication_ack_lock;     // Protege os contadores de ACKs de replicação
    pthread_cond_t replication_ack_cond;      // Condição para esperar por ACKs de replicação
    int replication_acks_needed;              // Número de ACKs de replicação esperados (quorum)
    int replication_acks_received;            // Número de ACKs de replicação recebidos para a transação atual

    // Variável para armazenar o último estado replicado com sucesso (para o líder)
    // Isso é útil se precisar reenviar o estado para um seguidor que estava offline.
    // struct server_state_snapshot last_replicated_state; // Se for replicar snapshots
    
} server_state;


#endif // common_h