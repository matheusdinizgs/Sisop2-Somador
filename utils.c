#include "utils.h"
#include <time.h>

void current_time(char *buf, size_t len)
{
    time_t now = time(NULL);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
}