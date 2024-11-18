#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"

const char* SERVER_ADDRESS = "127.0.0.1";
// #define REPLICA_PORT 8080
int REPLICA_PORT = 8080;

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

void writeChunkFile(const char *filepat, uint8_t *data, int length)
{
    int fd, n;

    if ((fd = open(filepat, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    if ((n = write(fd, data, length)) == -1)
        err_n_die("read error");

    close(fd);
}

void processWriteRequest(char *path, int id, uint8_t *data, int length)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/chunks/%d.chunk", id);
    printf("chunkname: %s\n", chunkname);
    writeChunkFile(chunkname, data, length);
}

int main(int argc, char **argv)
{
    int                 listenfd, connfd, n;
    struct sockaddr_in  servaddr;
    uint8_t             operation_type_buff[MAXLINE + 1];
    uint8_t             recvline[MAXLINE + 1];

    if(argc >= 2)
    {
        REPLICA_PORT = atoi(argv[1]);
    }

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
        
        int msg_len, op_type_len, proto_len, buf_len;
        n = read(connfd, &msg_len, sizeof(msg_len));
        msg_len = ntohl(msg_len);
        printf("WHOLE msg_len: %d\n", msg_len);
        // printf("n: %d\n", n);

        n = read(connfd, &op_type_len, sizeof(op_type_len));
        op_type_len = ntohl(op_type_len);
        printf("op_type_len: %d\n", op_type_len);

        memset(operation_type_buff, 0, MAXLINE);
        n = read(connfd, operation_type_buff, op_type_len);

        printf("operation_type: %s\n", operation_type_buff);

        if(strcmp(operation_type_buff, "read") == 0)
        {
            printf("received read request\n");
            n = read(connfd, &proto_len, sizeof(proto_len));
            proto_len = ntohl(proto_len);
            printf("proto_len: %d\n", proto_len);

            memset(recvline, 0, MAXLINE);
            n = read(connfd, recvline, MAXLINE); // proto_len instead of MAXLINE?
            ChunkRequest *chunkRequest = chunk_request__unpack(NULL, n, recvline);

            printf("chunkRequest->path: %s\n", chunkRequest->path);
            printf("chunkRequest->chunk_id: %d\n", chunkRequest->chunk_id);

            processRequest(chunkRequest->path, chunkRequest->chunk_id, connfd);
        }
        else if(strcmp(operation_type_buff, "write") == 0)
        {
            printf("received write request\n");
            n = read(connfd, &proto_len, sizeof(proto_len));
            proto_len = ntohl(proto_len);
            printf("proto_len: %d\n", proto_len);

            memset(recvline, 0, MAXLINE);
            n = read(connfd, recvline, proto_len);
            ChunkRequest *chunkRequest = chunk_request__unpack(NULL, n, recvline);

            printf("chunkRequest->path: %s\n", chunkRequest->path);
            printf("chunkRequest->chunk_id: %d\n", chunkRequest->chunk_id);

            n = read(connfd, &buf_len, sizeof(buf_len));
            buf_len = ntohl(buf_len);
            printf("buf_len: %d\n", buf_len);

            memset(recvline, 0, MAXLINE);
            n = read(connfd, recvline, buf_len);

            printf("received chunk:\n%s\n", recvline);

            processWriteRequest(chunkRequest->path, chunkRequest->chunk_id, recvline, buf_len);
        }
        else
            err_n_die("wrong operation type");

        close(connfd);

        printf("---------------------------------------------\n");
    }
}