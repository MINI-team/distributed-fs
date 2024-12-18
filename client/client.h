#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include <sys/stat.h>

char *OUTPUT_PATH = "output.txt";
#define OFFSET 13

typedef struct argsThread
{
    pthread_t tid;
    char *path;
    int chunk_id;

    char *ip;
    uint16_t port;

    int offset;
    int filefd;

} argsThread_t;

int file_size(int filefd);

void setup_connection(int *server_socket, char *ip, uint16_t port)
{
    struct sockaddr_in servaddr;
#ifdef DOCKER
    ip = resolve_host(ip);
    //printf("Heyy IP: %s, Port: %d\n", ip, port);
#endif
    
    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    //printf("After socket creation\n");
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    //printf("After inet_pton\n");
    if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
    {
        //printf("Heyy IP: %s, Port: %d\n", ip, port);
        err_n_die("connect error");
    }

    //printf("After connection\n");
}

#endif
