#include "discovery.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Envia uma mensagem de descoberta em broadcast (cliente)
int discovery_send_broadcast(int sock, uint16_t port, struct sockaddr_in *serveraddr)
{
    struct sockaddr_in bcastaddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr("192.168.2.255")}; // Ensure this is the correct broadcast address

    discovery_packet pkt = {.type = PACKET_TYPE_DISCOVERY};

    // Envia o pacote de descoberta
    if (sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&bcastaddr, sizeof(bcastaddr)) < 0)
    {
        perror("Erro ao enviar pacote de descoberta");
        return -1;
    }

    // Aguarda a resposta do servidor
    discovery_packet resp; // Separate buffer for the response
    socklen_t addrlen = sizeof(*serveraddr);
    if (recvfrom(sock, &resp, sizeof(resp), 0, (struct sockaddr *)serveraddr, &addrlen) < 0)
    {
        perror("Erro ao receber resposta do servidor");
        return -1;
    }

    // Verifica se o tipo do pacote recebido é PACKET_TYPE_DISCOVERY_ACK
    if (resp.type == PACKET_TYPE_DISCOVERY_ACK)
    {
        return 0;
    }

    fprintf(stderr, "Resposta inesperada do servidor: tipo %d\n", resp.type);
    return -1;
}

// Responde a uma mensagem de descoberta (servidor)
void discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len)
{
    discovery_packet resp = {.type = PACKET_TYPE_DISCOVERY_ACK};

    // Envia a resposta ao cliente
    if (sendto(sock, &resp, sizeof(resp), 0, (struct sockaddr *)cliaddr, len) < 0)
    {
        perror("Erro ao enviar resposta de descoberta");
    }
}