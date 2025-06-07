all: server_testing server_testing_debug

server_testing: server_testing.c discoveryService.c discoveryService.h
	gcc -Wall -pthread server_testing.c discoveryService.c -o server_testing

server_testing_debug: server_testing.c discoveryService.c discoveryService.h
	gcc -Wall -g -O0 -pthread server_testing.c discoveryService.c -o server_testing_debug

run_server_testing: server_testing
	./server_testing 4000

run_server_testing_debug: server_testing_debug
	./server_testing 4000

clean:
	rm -f server_testing server_testing_debug