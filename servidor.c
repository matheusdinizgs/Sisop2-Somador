#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "discovery.h"
#include "processing.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    int sock;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        // CRIA SOCKET UDP
        perror("cannot create socket");
        return 0;
    }

    struct sockaddr_in servaddr;

    memset((char *)&servaddr, 0, sizeof(servaddr)); // zera todos os bytes de memoria de servadrr
    // garante que nao fique lixo de memoria antigo
    // evita bugs difíceis de achar em sockets, memória de rede, etc. Assim você pode preencher apenas os campos que precisa, com segurança.

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)))
    {
        perror("bind failed");
        return 0;
    }

    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s num_reqs 0 total_sum 0\n", timebuf);

    while (1)
    {
        packet pkt;
        struct sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);

        recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&cliaddr, &len);

        if (pkt.type == PACKET_TYPE_DESC)
        {
            client_count = discovery_handle_request(sock, &cliaddr, len, clients, lock, client_count);
        }
        else if (pkt.type == PACKET_TYPE_REQ)
        {
            maybe_handle_request(pkt, cliaddr, len, sock);
        }
    }
    close(sock);
    return 0;
}