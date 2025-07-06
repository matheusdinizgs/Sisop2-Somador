DEPENDENCIES = discoveryService.c discoveryService.h interfaceService.h interfaceService.c processingService.c processingService.h common.h

all: server_testing client_testing

server_testing: server_testing.c ${DEPENDENCIES} 
	gcc -Wall -pthread -o server_testing server_testing.c discoveryService.c interfaceService.c processingService.c

client_testing: client_testing.c interfaceService.c interfaceService.h common.h discoveryService.c
	gcc -Wall -pthread -o client_testing client_testing.c interfaceService.c discoveryService.c

run_server_testing: server_testing
	./server_testing 4000

run_client_testing: client_testing
	./client_testing 4000	

clean:
	rm -f server_testing client_testing