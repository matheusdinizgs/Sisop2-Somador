CC=gcc
CFLAGS=-Wall -pthread
LIB_OBJS=discovery.o utils.o

all: servidor cliente

servidor: servidor.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o servidor servidor.c $(LIB_OBJS)

cliente: cliente.c cliente.h $(LIB_OBJS)
	$(CC) $(CFLAGS) -o cliente cliente.c $(LIB_OBJS)

discovery.o: discovery.c discovery.h
	$(CC) $(CFLAGS) -c discovery.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

clean:
	rm -f servidor cliente *.o
