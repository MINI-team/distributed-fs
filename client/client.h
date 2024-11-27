#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"
#include <sys/stat.h>

const char *SERVER_ADDRESS = "127.0.0.1";

int file_size(int filefd);

void setup_connection(int *server_socket)
{
    struct sockaddr_in servaddr;
    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_ADDRESS, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");
}

#endif