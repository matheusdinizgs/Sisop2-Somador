#ifndef discoveryService_h
#define discoveryService_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#define PORT 4000

int initServer(int argc, char *argv[]);

void endServer(int socketNumber);

#endif // discoveryService_h