#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"

const char* SERVER_ADDRESS = "127.0.0.1";
#define REPLICA_PORT 8080

char* DEFAULT_PATH = "ala.txt";

void readChunkFile(const char *chunkname, int connfd)
{   int     fd;
    size_t  bytes_read;
    char    buffer[MAXLINE + 1];

    if ((fd = open(chunkname, O_RDONLY)) == -1)
        err_n_die("open error");
    
    if ((bytes_read = read(fd, buffer, MAXLINE)) == -1)
        err_n_die("read error");

    buffer[bytes_read] = '\0';

    close(fd);

    if (write(connfd, buffer, bytes_read) == -1)
        err_n_die("write error");
}

void processRequest(char* path, int id, int connfd)
{
    char chunkname[MAXLINE+1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/chunks/%s%d.chunk", path, id);
    printf("chunkname: %s\n", chunkname);
    readChunkFile(chunkname, connfd);
}
int main()
{
    int                 listenfd, connfd, n;
    struct sockaddr_in  servaddr;
    uint8_t             buff[MAXLINE + 1];
    uint8_t             recvline[MAXLINE + 1];

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    int option = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        err_n_die("setsockopt error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(REPLICA_PORT);

    if ((bind(listenfd, (SA *) &servaddr, sizeof(servaddr))) < 0)
        err_n_die("bind error");

    if (listen(listenfd, 10) < 0)
        err_n_die("listen error");

    for (; ; ) {
        struct sockaddr_in addr;
        socklen_t addrlen;
        
        printf("Waiting for a connection on port %d\n", REPLICA_PORT);
        fflush(stdout);
        connfd = accept(listenfd, (SA *) NULL, NULL);

        
        int message_length;
        n = read(connfd, &message_length, sizeof(message_length));
        message_length = ntohl(message_length);
        printf("message_length: %d\n", message_length);
        printf("n: %d\n", n);

        memset(recvline, 0, MAXLINE);
        n = read(connfd, recvline, MAXLINE);
        ChunkRequest *chunkRequest = chunk_request__unpack(NULL, n, recvline);

        printf("chunkRequest->path: %s\n", chunkRequest->path);
        printf("chunkRequest->chunk_id: %d\n", chunkRequest->chunk_id);

        processRequest(chunkRequest->path, chunkRequest->chunk_id, connfd);

        close(connfd);
    }
}