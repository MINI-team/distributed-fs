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

    int offset;
    int filefd;

    int64_t filesize; // for deubug reasons, to be deleted

} argsThread_t;

int64_t file_size(int filefd);

void setup_connection(int *server_socket, char *ip, uint16_t port)
{
    struct sockaddr_in servaddr;
#ifdef DOCKER
    ip = resolve_host(ip);
#endif
    
    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("IP: %s, Port: %d\n", ip, port);
        err_n_die("connect error");
    }
}

#endif
