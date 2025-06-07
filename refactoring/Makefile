DEPENDENCIES = discoveryService.c discoveryService.h interfaceService.h interfaceService.c

all: server_testing client_testing

server_testing: server_testing.c ${DEPENDENCIES} 
	gcc -Wall -pthread -o server_testing server_testing.c discoveryService.c interfaceService.c 

client_testing: client_testing.c #${DEPENDENCIES}
	gcc -Wall -pthread client_testing.c -o client_testing

run_server_testing: server_testing
	./server_testing 4000

run_client_testing: client_testing
	./client_testing 4000	

clean:
	rm -f server_testing client_testing