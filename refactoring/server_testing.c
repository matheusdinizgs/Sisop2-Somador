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
    
    packet pkt; // Packet for receiving data
    packet resp = {.type = PACKET_TYPE_DESC_ACK}; // Packet for sending discovery acknowledgements
    struct sockaddr_in cliAddr; 
    socklen_t len = sizeof(cliAddr);
    pthread_t tid; // Thread ID for handling requests
    const size_t timeBufSize = 64;
    char timebuf[timeBufSize];
    server_state state; 

    init_server_state(&state);
    int socketNumber = initServer(argc, argv);
    getInitServerState(timebuf, timeBufSize);

    while (1) {
        
        // Wait for a packet from a client
        recvfrom(socketNumber, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliAddr, &len);

        // Handle discovery packets
        if (pkt.type == PACKET_TYPE_DESC) {
            
            // Send a discovery acknowledgement to the client
            sendto(socketNumber, &resp, sizeof(resp), 0, (struct sockaddr *)&cliAddr, len);
            
            pthread_mutex_lock(&state.lock);
            
            // Add the client to the list of known clients
            find_or_add_client(&state, &cliAddr);
            pthread_mutex_unlock(&state.lock);

        // Handle request packets
        } else if (pkt.type == PACKET_TYPE_REQ) {
            
            void *ctx = malloc(sizeof(packet) + sizeof(cliAddr) + sizeof(socklen_t) + sizeof(int) + sizeof(server_state *));
            
            // Copy the packet, client address, and server state to the context
            memcpy(ctx, 
                &(struct { packet pkt; struct sockaddr_in addr; socklen_t addrlen; int sock; server_state *state; }){pkt, cliAddr, len, socketNumber, &state}, 
                sizeof(packet) + sizeof(cliAddr) + sizeof(socklen_t) + sizeof(int) + sizeof(server_state *));
            pthread_create(&tid, NULL, handle_request, ctx);
            pthread_detach(tid);
        }
    }
    
    // Close the server socket
    endServer(socketNumber);
    return 0;
}
