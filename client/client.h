#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include <sys/stat.h>

#define OUTPUT_PATH "output.txt"
#define CLIENT_DEBUG_PATH "client.log"

typedef struct argsThread
{
    pthread_t tid;
    char *path;
    int chunk_id;

    char *ip;
    uint16_t port;

    int64_t offset;
    int filefd;

    int64_t filesize; // for deubug reasons, to be deleted

} argsThread_t;

int64_t file_size(int filefd);

#endif
