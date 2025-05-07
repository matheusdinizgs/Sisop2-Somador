#include "interface.h"

void show_data_on_interface(interface_args *args)
{
    while (1)
    {
        packet ack;
        ssize_t len;
        if ((len = recvfrom(args->sock, &ack, sizeof(ack), 0, NULL, NULL)) < 0)
        {
            perror("recvfrom");
            continue;
        }

        if (ack.type == PACKET_TYPE_REQ_ACK)
        {
            pthread_mutex_lock(args->ack_lock);
            if (ack.seqn == *(args->seqn))
            {
                char timebuf[64];
                current_time(timebuf, sizeof(timebuf));
                uint32_t value = args->sent_values[ack.seqn];
                printf(
                    "%s server %s id_req %u value %u num_reqs %u total_sum %lu\n",
                    timebuf,
                    inet_ntoa(args->serveraddr->sin_addr),
                    ack.seqn,
                    value,
                    ack.data.ack.num_reqs,
                    ack.data.ack.total_sum);
                *(args->ack_received) = 1;
                pthread_cond_signal(args->ack_cond);
            }
            pthread_mutex_unlock(args->ack_lock);
        }
    }
}