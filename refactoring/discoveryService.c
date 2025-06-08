#include "discoveryService.h"

int initServer(int argc, char *argv[]) {

    int numParameters = argc - 1; // Exclude the program name from the count
    int port = 0;
    int socketNumber = 0;
    struct sockaddr_in serverAddr;

    if (numParameters <= 0) {
        fprintf(stderr, "Error: the number of arguments is incorrect.\n"
                        "Expected 1 argument for the port number.\n"
                        "%s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]);

    if (port != PORT) {
        fprintf(stderr, "Error: Invalid port number. It must be %d}.\n", PORT);
        exit(EXIT_FAILURE);
    }

    if ((socketNumber = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Fills all memory bytes with zero in serverAddr
    // Ensures no leftover memory garbage
    // Prevents hard-to-find bugs in sockets, network memory, etc. This way, you can safely fill only the fields you need.
    memset((char *)&serverAddr, 0, sizeof(serverAddr)); 
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);
    
    if (bind(socketNumber, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Error binding socket");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }

    return socketNumber;
}

void endServer(int socketNumber) {
    
    if (close(socketNumber) < 0) {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

int initClient(int argc, char *argv[]) {
    
    int numParameters = argc - 1; // Exclude the program name from the count
    int port = 0;
    int socketNumber = 0; 
    int broadcast = 1;
    struct sockaddr_in broadcastAddr;
    packet desc_pkt = {.type = PACKET_TYPE_DESC};
    struct sockaddr_in serveraddr;
    socklen_t addrlen = sizeof(serveraddr);
    
    if (numParameters <= 0) {
        fprintf(stderr, "Error: the number of arguments is incorrect.\n"
                        "Expected 1 argument for the port number.\n"
                        "%s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]);

    if (port != PORT) {
        fprintf(stderr, "Error: Invalid port number. It must be %d}.\n", PORT);
        exit(EXIT_FAILURE);
    }

    if ((socketNumber = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(socketNumber, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("Error setting socket options");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }
    
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(port);
    broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    sendto(socketNumber, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));

    // Esperar DESC_ACK
    recvfrom(socketNumber, &desc_pkt, sizeof(desc_pkt), 0, (struct sockaddr *)&serveraddr, &addrlen);

    return socketNumber;
}