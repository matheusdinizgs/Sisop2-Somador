
#include "discovery.h"

#include <pthread.h>

typedef struct
{
    int sock;
    pthread_mutex_t *ack_lock;
    pthread_cond_t *ack_cond;
    int *ack_received;
    uint32_t *seqn;
    uint32_t *sent_values;
    struct sockaddr_in *serveraddr;
} interface_args;

void show_data_on_interface(interface_args *args);