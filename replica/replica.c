#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"

// const char *SERVER_ADDRESS = "127.0.0.1";
// #define REPLICA_PORT 8080
int REPLICA_PORT = 8080;

char *DEFAULT_PATH = "ala.txt";

int connect_with_master()
{
    int serverfd;
    struct sockaddr_in servaddr;
    /* Connecting with the server */
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(MASTER_SERVER_PORT);

    char* master_ip = resolve_host(MASTER_SERVER_IP);

    if (inet_pton(AF_INET, master_ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for connecting with master");

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");
    
    return serverfd;
}

void readChunkFile(const char *chunkname, int connfd)
{
    int fd;
    size_t bytes_read;
    char buffer[MAXLINE + 1];

    if ((fd = open(chunkname, O_RDONLY)) == -1)
        err_n_die("open error");

    if ((bytes_read = read(fd, buffer, MAXLINE)) == -1)
        err_n_die("read error");

    buffer[bytes_read] = '\0';

    close(fd);

    if (write(connfd, buffer, bytes_read) == -1)
        err_n_die("write error");
}

void processRequest(char *path, int id, int connfd)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/%d/chunks/%s%d.chunk", REPLICA_PORT, path, id);
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

int forwardChunk(Chunk *chunk, uint8_t *data, int data_len)
{
    int success = 1;
    int replicafd, net_len, msg_len;
    struct sockaddr_in repladdr;
    size_t proto_len;
    // argsThread_t *args = voidPtr;
    char op_type[MAXLINE + 1], recvchar;
    uint8_t *proto_buf;

    strcpy(op_type, "write");

    proto_len = chunk__get_packed_size(chunk);
    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk__pack(chunk, proto_buf);

    for(int i = 0; i < chunk->n_replicas; i++)
    {
        if (chunk->replicas[i]->port == REPLICA_PORT) // SIMPLE HEURISTIC FOR NOW,
        // should add ip as well
            continue;
        memset(&repladdr, 0, sizeof(repladdr));
        repladdr.sin_family = AF_INET;
        repladdr.sin_port = htons(chunk->replicas[i]->port);

        if (inet_pton(AF_INET, chunk->replicas[i]->ip, &repladdr.sin_addr) < 0)
            err_n_die("inet_pton error for %s", chunk->replicas[i]->ip);

        if ((replicafd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            err_n_die("socket error");

        if (connect(replicafd, (SA *)&repladdr, sizeof(repladdr)) < 0)
        {
            printf("Unable to connect to another replica (%d), but continuing to work\n",
                chunk->replicas[i]->port);
            continue;
            // err_n_die("connect error");
        }

        net_len = htonl(CHUNK_SIZE);
        write(replicafd, &net_len, sizeof(net_len)); // this is supposed to be the size of the whole message, but idk why we would use that

        write_len_and_data(replicafd, strlen(op_type) + 1, op_type);

        write_len_and_data(replicafd, proto_len, proto_buf);

        write_len_and_data(replicafd, data_len, data);

        // read(replicafd, &msg_len, sizeof(msg_len));

        read(replicafd, &recvchar, 1);
        
        if(recvchar == 'y')
            printf("received acknowledgement of receiving chunk, %c\n", recvchar);
        else
            success = 0;

        close(replicafd);
    }
    return success;
}

void processWriteRequest(char *path, int id, uint8_t *data, int length, Chunk *chunk)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/%d/chunks/%s%d.chunk", 
        REPLICA_PORT, path, id);
    printf("chunkname: %s\n", chunkname);
    writeChunkFile(chunkname, data, length);
}

int main(int argc, char **argv)
{
    int listenfd, connfd, n, res, masterfd;
    struct sockaddr_in servaddr;
    uint8_t operation_type_buff[MAXLINE + 1];
    uint8_t recvline[MAXLINE + 1];
    uint8_t *proto_buf;
    size_t proto_len;

    if (argc >= 2)
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

    if ((bind(listenfd, (SA *)&servaddr, sizeof(servaddr))) < 0)
        err_n_die("bind error");

    if (listen(listenfd, 10) < 0)
        err_n_die("listen error");

    for (;;)
    {
        struct sockaddr_in addr;
        socklen_t addrlen;

        printf("Waiting for a connection on port %d\n", REPLICA_PORT);
        fflush(stdout);
        connfd = accept(listenfd, (SA *)NULL, NULL);

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

        if (strcmp(operation_type_buff, "read") == 0)
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
        else if (strcmp(operation_type_buff, "write_primary") == 0 || strcmp(operation_type_buff, "write") == 0)
        {
            printf("received %s request\n", operation_type_buff);
            n = read(connfd, &proto_len, sizeof(proto_len));
            proto_len = ntohl(proto_len);
            printf("proto_len: %d\n", proto_len);

            memset(recvline, 0, MAXLINE);
            n = read(connfd, recvline, proto_len);
            Chunk *chunk = chunk__unpack(NULL, n, recvline);

            // printf("chunk->path: %s\n", chunk->path);
            printf("chunk->chunk_id: %d\n", chunk->chunk_id);
            printf("chunk->n_replicas: %ld\n", chunk->n_replicas);

            for (int i = 0; i < chunk->n_replicas; i++)
            {
                printf("Name: %s IP: %s Port: %d Is_primary: %d\n",
                       chunk->replicas[i]->name, chunk->replicas[i]->ip,
                       chunk->replicas[i]->port, chunk->replicas[i]->is_primary);
            }

            n = read(connfd, &buf_len, sizeof(buf_len));
            buf_len = ntohl(buf_len);
            printf("buf_len: %d\n", buf_len);

            memset(recvline, 0, MAXLINE);
            n = read(connfd, recvline, buf_len);

            printf("received chunk:\n%s\n", recvline);

            // processWriteRequest("dummypath", chunk->chunk_id, recvline, buf_len, chunk);
            processWriteRequest(chunk->path, chunk->chunk_id, recvline, buf_len, chunk);

            if (strcmp(operation_type_buff, "write_primary") == 0)
            {
                res = forwardChunk(chunk, recvline, buf_len);
                CommitChunk commit = COMMIT_CHUNK__INIT;
                commit.success = res;
                commit.chunk_id = chunk->chunk_id;
                
                if(res)
                    commit.replicas_success = chunk->replicas;
                else
                    commit.replicas_fail = chunk->replicas;
                
                msg_len = commit_chunk__get_packed_size(&commit);
                proto_buf = (uint8_t *)malloc(msg_len * sizeof(uint8_t));
                commit_chunk__pack(&commit, proto_buf);

                // REPLACE THIS
                // masterfd = connect_with_master();
                // write_len_and_data(masterfd, msg_len, proto_buf);
                // close(masterfd);
            }
            if (strcmp(operation_type_buff, "write") == 0)
            {
                char send_char = 'y';
                n = write(connfd, &send_char, 1);
                printf("sent acknowledgement of receiving chunk\n");
            }
        }
        else
            err_n_die("wrong operation type");

        close(connfd);

        printf("---------------------------------------------\n");
    }
}