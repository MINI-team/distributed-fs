#include <stdio.h>
#include "common.h"
#include "pthread.h"

const char* SERVER_ADDRESS = "127.0.0.1";

int main(int argc, char **argv) 
{
    int                 serverfd, outputfd, n, err;
    struct sockaddr_in  servaddr;
    size_t              bytes_sent;
    char                recvline[MAXLINE];


    /* Connecting with the server */
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_ADDRESS, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", argv[1]);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");

    char* msg1 = "Hello";

    if ((bytes_sent = write(serverfd, msg1, strlen(msg1))) < 0)
        err_n_die("write error");

    printf("bytes sent: %zu\n", bytes_sent);
    sleep(5);
}