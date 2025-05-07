#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include "utils.h"

// Define os tipos de pacotes de descoberta
#define PACKET_TYPE_DISCOVERY 1
#define PACKET_TYPE_DISCOVERY_ACK 2

// Funções para o cliente
int discovery_send_broadcast(int sock, uint16_t port, struct sockaddr_in *serveraddr, socklen_t addrlen);

// Funções para o servidor
int discovery_handle_request(int sock, struct sockaddr_in *cliaddr, socklen_t len, client_entry *clients, pthread_mutex_t lock, int client_count);
int find_or_add_client(struct sockaddr_in *addr, client_entry *clients, int client_count);

#endif // DISCOVERY_H