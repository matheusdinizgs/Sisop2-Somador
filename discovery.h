#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdint.h>
#include <arpa/inet.h>

// Define os tipos de pacotes de descoberta
#define PACKET_TYPE_DISCOVERY 1
#define PACKET_TYPE_DISCOVERY_ACK 2

// Estrutura do pacote de descoberta
typedef struct __discovery_packet
{
    uint16_t type;
} discovery_packet;

// Funções para o cliente
int discovery_send_broadcast(int sock, uint16_t port, struct sockaddr_in *serveraddr);

// Funções para o servidor
void discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len);

#endif // DISCOVERY_H