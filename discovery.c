#include "discovery.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Envia uma mensagem de descoberta em broadcast (cliente)
int discovery_send_broadcast(int sock, uint16_t port, struct sockaddr_in *serveraddr, socklen_t addrlen)
{
    struct sockaddr_in bcastaddr = {.sin_family = AF_INET,
                                    .sin_port = htons(port),
                                    .sin_addr.s_addr = inet_addr("255.255.255.255")};

    packet desc_pkt = {.type = PACKET_TYPE_DESC};

    // Envia o pacote de descoberta
    if (sendto(sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&bcastaddr, sizeof(bcastaddr)) < 0)
    {
        perror("Erro ao enviar pacote de descoberta");
        return -1;
    }

    // Esperar DESC_ACK
    if (recvfrom(sock, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)serveraddr, &addrlen) < 0)
    {
        perror("Erro ao receber pacote de descoberta");
        return -1;
    }

    return 0;
}

// Responde a uma mensagem de descoberta(servidor) void discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len, discovery_packet *req)
int discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len, client_entry *clients, int client_count, pthread_mutex_t *lock)
{
    packet resp = {.type = PACKET_TYPE_DESC_ACK};
    if (sendto(sock, &resp, sizeof(resp), 0, (struct sockaddr *)cliaddr, len) < 0)
    {
        perror("Erro ao enviar pacote de descoberta");
        return -1;
    }

    client_count = find_or_add_client(cliaddr, clients, client_count, lock);
    return client_count;
}

// Encontra ou adiciona um cliente na lista de clientes
int find_or_add_client(struct sockaddr_in *addr, client_entry *clients, int client_count, pthread_mutex_t *lock)
{
    pthread_mutex_lock(lock);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].addr.sin_port == addr->sin_port &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr)
            return i;
    }
    clients[client_count].addr = *addr;
    clients[client_count].last_req = 0;
    clients[client_count].last_sum = 0;
    return ++client_count;
    pthread_mutex_unlock(lock);
}