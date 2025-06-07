#include "discoveryService.h"

void initServer(int argc, char *argv[]) {
    
    //printf("Starting server...\n");

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

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number. It must be between 1 and 65535.\n");
        exit(EXIT_FAILURE);
    }

    if ((socketNumber = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);
    
    // Fills all memory bytes with zero in serverAddr
    // Ensures no leftover memory garbage
    // Prevents hard-to-find bugs in sockets, network memory, etc. This way, you can safely fill only the fields you need.
    memset((char *)&serverAddr, 0, sizeof(serverAddr)); 
    
    if (bind(socketNumber, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Error binding socket");
        close(socketNumber);
        exit(EXIT_FAILURE);
    }

    //printf("Server initialized on port %d. SocketNumber: %d\n", port, socketNumber);
}

void initClient(int argc, char *argv[]) {
    // This function is not implemented in the original code.
    // You can implement it as needed for your application.
    fprintf(stderr, "Client initialization is not implemented.\n");
    exit(EXIT_FAILURE);
}