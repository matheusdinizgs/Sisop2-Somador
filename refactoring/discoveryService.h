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

int initClient(int port, int *sock, struct sockaddr_in *serveraddr, socklen_t *addrlen);

#endif // discoveryService_h