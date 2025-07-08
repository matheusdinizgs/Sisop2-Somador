#include "discoveryService.h"
#include "common.h"     // Inclua common.h para os novos tipos de pacotes e estruturas
#include <stdio.h>      // Para funções de I/O (printf, fprintf, perror)
#include <stdlib.h>     // Para exit, atoi
#include <string.h>     // Para memset
#include <unistd.h>     // Para close
#include <arpa/inet.h>  // Para inet_addr, htonl, htons, inet_ntoa
#include <sys/socket.h> // Para funções de socket
#include <sys/time.h>   // Para struct timeval (timeout do socket)
#include <errno.h>      // Para EWOULDBLOCK, EAGAIN (timeouts)

// --- FUNÇÕES DE SERVIDOR ---

// Modificação: initServer agora aceitará o ID do servidor como argumento
// O protótipo em discoveryService.h deve ser: int initServer(int argc, char *argv[], uint32_t *server_id_ptr);
// O ID do servidor é passado para a struct server_state no main do server_testing.c
int initServer(int argc, char *argv[])
{ // Removi uint32_t *server_id_ptr daqui, o main que cuida disso.
    int numParameters = argc - 1;
    int port = 0;
    int socketNumber = 0;
    struct sockaddr_in serverAddr;

    // NOVO: Verifica o número de argumentos para porta E ID do servidor
    if (numParameters < 2)
    { // Espera <porta> <server_id>
        fprintf(stderr, "Uso: %s <porta> <server_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]); // Porta
    // O server_id será lido e atribuído no main do server_testing.c

    // Verifica se a porta é a definida em common.h (opcional, mas bom para consistência)
    if (port != PORT)
    {
        fprintf(stderr, "Error: Invalid port number. It must be %d.\n", PORT);
        exit(EXIT_FAILURE);
    }

    if ((socketNumber = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Habilitar SO_REUSEADDR para permitir que a porta seja reutilizada rapidamente
    int reuse = 1;
    if (setsockopt(socketNumber, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("Error setting SO_REUSEADDR option");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }

    // --- NOVO/CRUCIAL: Habilitar SO_BROADCAST para o socket do servidor ---
    // Isso permite que este socket envie mensagens para endereços de broadcast,
    // como o PACKET_TYPE_COORDINATOR para clientes e outros servidores.
    int broadcast_enable = 1;
    if (setsockopt(socketNumber, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0)
    {
        perror("Error setting SO_BROADCAST option");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }

    // Fills all memory bytes with zero in serverAddr
    memset((char *)&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // Escuta em todas as interfaces disponíveis
    serverAddr.sin_port = htons(port);

    if (bind(socketNumber, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Error binding socket");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }

    return socketNumber;
}

void endServer(int socketNumber)
{
    if (close(socketNumber) < 0)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

// --- FUNÇÃO DE CLIENTE (find_leader) MOVIDA E AJUSTADA ---

// Retorna 0 em sucesso, -1 em falha
int find_leader(
    int sock,
    int port,
    struct sockaddr_in *leader_addr,
    socklen_t *addrlen,
    uint32_t *leader_id_ptr)
{
#define LEADER_DISCOVERY_RETRIES 10
#define LEADER_DISCOVERY_TIMEOUT_SEC 3

    packet p_recv;
    struct sockaddr_in sender_addr;
    socklen_t sender_addrlen = sizeof(sender_addr);
    int retries = 0;

    // CORREÇÃO: Cliente deve escutar na MESMA PORTA que o servidor (4000)
    struct sockaddr_in client_bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port), // MUDANÇA: usa a porta do servidor (4000)
        .sin_addr.s_addr = htonl(INADDR_ANY)};

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Habilita recepção de broadcast
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
        perror("Error setting SO_BROADCAST on client socket");
        return -1;
    }

    if (bind(sock, (struct sockaddr *)&client_bind_addr, sizeof(client_bind_addr)) < 0)
    {
        perror("Error binding client socket to port 4000");
        return -1;
    }

    // Verifica a porta atribuída
    socklen_t addr_len = sizeof(client_bind_addr);
    getsockname(sock, (struct sockaddr *)&client_bind_addr, &addr_len);
    printf("Cliente escutando na porta: %d\n", ntohs(client_bind_addr.sin_port));

    // Configura timeout para recvfrom
    struct timeval tv;
    tv.tv_sec = LEADER_DISCOVERY_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    while (retries < LEADER_DISCOVERY_RETRIES)
    {
        char timebuf[64];
        sprintf(timebuf, "[%ld]", time(NULL));

        printf("%s Cliente aguardando anúncio do líder... (Tentativa %d/%d)\n",
               timebuf, retries + 1, LEADER_DISCOVERY_RETRIES);

        int n = recvfrom(sock, &p_recv, sizeof(p_recv), 0, (struct sockaddr *)&sender_addr, &sender_addrlen);

        if (n > 0)
        {
            if (p_recv.type == PACKET_TYPE_COORDINATOR)
            {
                *leader_id_ptr = p_recv.data.server_info.server_id;
                *leader_addr = p_recv.data.server_info.server_addr;
                *addrlen = sender_addrlen;

                printf("%s Líder (ID: %u) encontrado via COORDINATOR em %s:%d\n",
                       timebuf, *leader_id_ptr, inet_ntoa(leader_addr->sin_addr), ntohs(leader_addr->sin_port));
                return 0;
            }
            else
            {
                printf("%s Pacote inesperado recebido (Tipo: %hu). Ignorando...\n", timebuf, p_recv.type);
            }
        }
        else if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                printf("%s Timeout na espera por COORDINATOR. Tentando novamente...\n", timebuf);
            }
            else
            {
                perror("Erro ao receber no find_leader");
                return -1;
            }
        }
        retries++;
        usleep(500000); // 500ms entre tentativas
    }

    return -1; // Não encontrou o líder
}

// --- REMOÇÃO DA ANTIGA initClient ---
// A função initClient original não é mais usada pelo main do cliente.
// A funcionalidade de descoberta de líder foi transferida para find_leader.
// Você pode remover ou comentar a implementação abaixo:
/*
int initClient(int port, int *sock, struct sockaddr_in *serveraddr, socklen_t *addrlen) {
    *sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int broadcast = 1;
    if (setsockopt(*sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("Error setting socket options");
        close(*sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in bcastaddr = {.sin_family = AF_INET,
                                     .sin_port = htons(port),
                                     .sin_addr.s_addr = inet_addr("255.255.255.255")};

    packet desc_pkt = {.type = PACKET_TYPE_DESC};

    sendto(*sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&bcastaddr, sizeof(bcastaddr));

    // Esperar DESC_ACK
    recvfrom(*sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)serveraddr, addrlen);

    return 0; // Success
}
*/