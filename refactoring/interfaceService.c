#include "interfaceService.h"

void current_time(char *buf, size_t len) {

    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

void getInitServerState(char *timeBuf, size_t len) {
    
    current_time(timeBuf, len);
    printf("%s num_reqs 0 total_sum 0\n", timeBuf);
}

void getServerState(char *timeBuf,
                    const char *clientAddr,
                    uint32_t seqn,
                    uint32_t value,
                    uint32_t totalReqs,
                    uint64_t totalSum) {
    current_time(timeBuf, sizeof(timeBuf));
    printf("%s client %s id_req %u value %u num_reqs %u total_sum %lu\n",
           timeBuf, clientAddr, seqn, value, totalReqs, totalSum);
}

void getServerDupState(char *timeBuf,
                       const char *clientAddr,
                       uint32_t seqn,
                       uint32_t value,
                       uint32_t totalReqs,
                       uint64_t totalSum) {
    current_time(timeBuf, sizeof(timeBuf));                        
    printf("%s client %s DUP!! id_req %u value %u num_reqs %u total_sum %lu\n",
           timeBuf, clientAddr, seqn, value, totalReqs, totalSum);
}