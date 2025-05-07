#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "interface.h"

#define RETRY_INTERVAL_MS 10

static uint32_t sent_values[MAX_HISTORY] = {0};
static struct sockaddr_in server_addr;
static socklen_t addrlen = sizeof(server_addr);
static uint32_t seqn = 1;

static pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
static int ack_received = 0;

static void log_server_discovery()
{
    char timebuf[64];
    current_time(timebuf, sizeof(timebuf));
    printf("%s server_addr %s\n", timebuf, inet_ntoa(server_addr.sin_addr));
}

static int parse_args(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    return atoi(argv[1]);
}

static int setup_socket_and_discover(int port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
        perror("Erro ao configurar broadcast");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (discovery_send_broadcast(sock, port, &server_addr, addrlen) < 0)
    {
        perror("Falha na descoberta do servidor");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

static struct timespec timeout_in_ms(int ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += ms * 1000000;
    if (ts.tv_nsec >= 1000000000)
    {
        ts.tv_nsec -= 1000000000;
        ts.tv_sec++;
    }
    return ts;
}

static int send_with_retry_until_ack(int sock, packet *req)
{
    pthread_mutex_lock(&ack_lock);
    ack_received = 0;

    if (sendto(sock, req, sizeof(*req), 0, (struct sockaddr *)&server_addr, addrlen) < 0)
    {
        perror("Erro ao enviar pacote");
        pthread_mutex_unlock(&ack_lock);
        return -1;
    }

    while (!ack_received)
    {
        struct timespec timeout = timeout_in_ms(RETRY_INTERVAL_MS);
        pthread_cond_timedwait(&ack_cond, &ack_lock, &timeout);

        if (!ack_received)
        {
            if (sendto(sock, req, sizeof(*req), 0, (struct sockaddr *)&server_addr, addrlen) < 0)
            {
                perror("Erro ao reenviar pacote");
                pthread_mutex_unlock(&ack_lock);
                return -1;
            }
        }
    }

    pthread_mutex_unlock(&ack_lock);
    return 0;
}

static interface_args *create_interface_args(int sock)
{
    interface_args *args = malloc(sizeof(interface_args));
    if (!args)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    args->sock = sock;
    args->ack_lock = &ack_lock;
    args->ack_cond = &ack_cond;
    args->ack_received = &ack_received;
    args->seqn = &seqn;
    args->sent_values = sent_values;
    args->serveraddr = &server_addr;
    return args;
}

static pthread_t launch_interface_thread(interface_args *args)
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, (void *(*)(void *))show_data_on_interface, args) != 0)
    {
        perror("pthread_create");
        free(args);
        exit(EXIT_FAILURE);
    }
    return tid;
}

static void cleanup_interface_thread(pthread_t tid, interface_args *args)
{
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    free(args);
}

static void main_loop(int sock)
{
    while (1)
    {
        uint32_t value;
        if (scanf("%u", &value) != 1)
            break;

        if (seqn >= MAX_HISTORY)
        {
            fprintf(stderr, "Histórico cheio (MAX_HISTORY atingido).\n");
            break;
        }

        sent_values[seqn] = value;

        packet req = {
            .type = PACKET_TYPE_REQ,
            .seqn = seqn,
            .data.req.value = value,
        };

        if (send_with_retry_until_ack(sock, &req) == 0)
            seqn++;
    }
}

int main(int argc, char *argv[])
{
    int port = parse_args(argc, argv);
    int sock = setup_socket_and_discover(port);

    log_server_discovery();

    interface_args *args = create_interface_args(sock);
    pthread_t interface_tid = launch_interface_thread(args);

    main_loop(sock);

    close(sock);
    cleanup_interface_thread(interface_tid, args);
    return 0;
}
