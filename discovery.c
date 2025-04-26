#include "discovery.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int get_broadcast_address(char *broadcast_ip, size_t len)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;

            // Skip loopback interface
            if (addr->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
            {
                continue;
            }

            if (addr && netmask)
            {
                uint32_t ip = addr->sin_addr.s_addr;
                uint32_t mask = netmask->sin_addr.s_addr;
                uint32_t broadcast = ip | ~mask;
                inet_ntop(AF_INET, &broadcast, broadcast_ip, len);
                freeifaddrs(ifaddr);
                return 0;
            }
        }
    }

    freeifaddrs(ifaddr);
    return -1;
}

// Envia uma mensagem de descoberta em broadcast (cliente)
int discovery_send_broadcast(int sock, uint16_t port, struct sockaddr_in *serveraddr)
{
    char broadcast_ip[INET_ADDRSTRLEN]; // Buffer to store the broadcast address
    if (get_broadcast_address(broadcast_ip, sizeof(broadcast_ip)) == 0)
    {
        printf("Broadcast address: %s\n", broadcast_ip);
    }
    else
    {
        fprintf(stderr, "Failed to get broadcast address\n");
    }

    struct sockaddr_in bcastaddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(broadcast_ip)};

    uint32_t my_id = getpid();
    discovery_packet pkt = {.type = PACKET_TYPE_DISCOVERY, .client_id = my_id};

    // Envia o pacote de descoberta
    if (sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&bcastaddr, sizeof(bcastaddr)) < 0)
    {
        perror("Erro ao enviar pacote de descoberta");
        return -1;
    }

    while (1)
    {
        // Aguarda a resposta do servidor
        discovery_packet resp; // Separate buffer for the response
        socklen_t addrlen = sizeof(*serveraddr);
        if (recvfrom(sock, &resp, sizeof(resp), 0, (struct sockaddr *)serveraddr, &addrlen) < 0)
        {
            perror("Erro ao receber resposta do servidor");
        }

        if (resp.type == PACKET_TYPE_DISCOVERY_ACK && resp.client_id == my_id)
        {
            printf("Received Packet: type=%d, client_id=%u\n", resp.type, resp.client_id);
            return 0;
        }

        continue;
    }
}

// Responde a uma mensagem de descoberta (servidor)
void discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len, discovery_packet *req)
{
    discovery_packet resp = {
        .type = PACKET_TYPE_DISCOVERY_ACK,
        .client_id = req->client_id};

    printf("ACK packet: type=%d, client_id=%u\n", resp.type, resp.client_id);
    printf("Enviando ACK de descoberta para %s:%d\n",
           inet_ntoa(cliaddr->sin_addr), ntohs(cliaddr->sin_port));

    // Envia a resposta ao cliente
    if (sendto(sock, &resp, sizeof(resp), 0, (struct sockaddr *)cliaddr, len) < 0)
    {
        perror("Erro ao enviar resposta de descoberta");
    }
}