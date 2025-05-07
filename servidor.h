
#include <arpa/inet.h>

int find_or_add_client(struct sockaddr_in *addr);
void *handle_request(void *arg);