#pragma once

#define PORT 4000
#define MAX_BUFFER 1024
#define PACKET_TYPE_REQ 2
#define PACKET_TYPE_REQ_ACK 4
#define MAX_HISTORY 100000000

void *interface_thread(void *arg);
int init_socket_and_find_server(int port);
