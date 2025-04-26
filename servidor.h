
#include <arpa/inet.h>

typedef struct
{
    struct sockaddr_in addr;
    uint32_t last_req;
    uint64_t last_sum;
} client_entry;

int find_or_add_client(struct sockaddr_in *addr);
void *handle_request(void *arg);