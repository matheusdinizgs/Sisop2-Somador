#ifndef discoveryService_h
#define discoveryService_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "common.h"

int initServer(int argc, char *argv[]);

//int find_or_add_client(struct sockaddr_in *addr);

void endServer(int socketNumber);
int find_leader(int sock, int port, struct sockaddr_in *leader_addr, socklen_t *addrlen, uint32_t *leader_id_ptr);
//int initClient(int port, int *sock, struct sockaddr_in *serveraddr, socklen_t *addrlen);

#endif // discoveryService_h