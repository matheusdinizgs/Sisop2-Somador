#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include "discoveryService.h"
#include "interfaceService.h"
#include "processingService.h"
#include "common.h"

int main(int argc, char* argv[]) {
    
    packet pkt;
    packet resp = {.type = PACKET_TYPE_DESC_ACK};
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    pthread_t tid;
    const size_t timeBufSize = 64;
    char timebuf[timeBufSize];

    server_state state;
    init_server_state(&state);

    int socketNumber = initServer(argc, argv);
    getInitServerState(timebuf, timeBufSize);

    while (1) {
        
        recvfrom(socketNumber, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliaddr, &len);

        if (pkt.type == PACKET_TYPE_DESC) {
            
            sendto(socketNumber, &resp, sizeof(resp), 0, (struct sockaddr *)&cliaddr, len);
            pthread_mutex_lock(&state.lock);
            find_or_add_client(&state, &cliaddr);
            pthread_mutex_unlock(&state.lock);

        } else if (pkt.type == PACKET_TYPE_REQ) {
            
            void *ctx = malloc(sizeof(packet) + sizeof(cliaddr) + sizeof(socklen_t) + sizeof(int) + sizeof(server_state *));
            memcpy(ctx, &(struct { packet pkt; struct sockaddr_in addr; socklen_t addrlen; int sock; server_state *state; }){pkt, cliaddr, len, socketNumber, &state}, sizeof(packet) + sizeof(cliaddr) + sizeof(socklen_t) + sizeof(int) + sizeof(server_state *));
            pthread_create(&tid, NULL, handle_request, ctx);
            pthread_detach(tid);
        }
    }
    
    endServer(socketNumber);
    return 0;
}