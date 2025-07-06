#ifndef interfaceService_h
#define interfaceService_h

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include "processingService.h"

void current_time(char *timeBuf, size_t len);

void print_server_state(char *timeBuf,
                        const char *clientAddr,
                        uint32_t seqn,
                        uint32_t value,
                        server_state *state,
                        int is_duplicate);

void getInitServerState(char *timeBuf, size_t len);                        

#endif // interfaceService_h
