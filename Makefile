CC=gcc
CFLAGS=-Wall -pthread
LIB_OBJS=discovery.o utils.o interface.o processing.o

all: servidor cliente

servidor: servidor.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o servidor servidor.c $(LIB_OBJS)

cliente: cliente.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o cliente cliente.c $(LIB_OBJS)

discovery.o: discovery.c discovery.h
	$(CC) $(CFLAGS) -c discovery.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

interface.o: interface.c interface.h
	$(CC) $(CFLAGS) -c interface.c

processing.o: processing.c processing.h
	$(CC) $(CFLAGS) -c processing.c

clean:
	rm -f servidor cliente *.o
