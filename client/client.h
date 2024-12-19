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

    size_t n_replicas;
    char **ip;
    uint16_t *port;

    int offset;
    int filefd;

} argsThread_t;

int file_size(int filefd);

int setup_connection(int *server_socket, char *ip, uint16_t port)
{
    int err;
    struct sockaddr_in servaddr;
#ifdef DOCKER
    ip = resolve_host(ip);
    if(ip == NULL)
        return -1;
#endif

    fflush(stdout);
    printf("Trying to connect to IP: %s, Port: %d\n", ip, port);
    fflush(stdout);

    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    //printf("After socket creation\n");
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    if ((err = connect(*server_socket, (SA *)&servaddr, sizeof(servaddr))) < 0)
    // if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
    {
        fflush(stdout);
        printf("Unable to connect to IP: %s, Port: %d, trying a different replica...\n", ip, port);
        // err_n_die("connect error");
        fflush(stdout);
        return err;
    }

    printf("Succesfully connected to IP: %s, Port: %d\n", ip, port);

    return 0;
}

#endif
