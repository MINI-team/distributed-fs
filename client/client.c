#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "pthread.h"

const char* SERVER_ADDRESS = "127.0.0.1";

char DEFAULT_PATH[MAXLINE+1];
char* OUTPUT_PATH = "output.txt";

#define OFFSET 13
#define CHUNK_SIZE 42

typedef struct argsThread
{
    pthread_t tid;
    char* path;
    int chunk_id;
    
    char *ip;
    uint16_t port;

    int offset;
    int filefd;
    
} argsThread_t;

void *getChunk(void *voidPtr)
{
    int                 serverfd, n;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE], op_type[MAXLINE];

    argsThread_t *args = voidPtr;
    printf("[tid: %lu] chunk_id: %d\n", pthread_self(), args->chunk_id);

    ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
    chunkRequest.path = args->path;
    chunkRequest.chunk_id = args->chunk_id;

    int len = chunk_request__get_packed_size(&chunkRequest);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    chunk_request__pack(&chunkRequest, buffer);

    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(args->port);

    if (inet_pton(AF_INET, args->ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", args->ip);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");

    int net_len = htonl(len); // TO FIX
    write(serverfd, &net_len, sizeof(net_len));

    strcpy(op_type, "read");

    net_len = htonl(strlen(op_type) + 1);

    write(serverfd, &net_len, sizeof(net_len));

    write(serverfd, op_type, strlen(op_type) + 1);

    net_len = htonl(len);
    write(serverfd, &net_len, sizeof(net_len));

    write(serverfd, buffer, len);

    free(buffer);

    memset(recvline, 0, MAXLINE);
    n = read(serverfd, recvline, MAXLINE);
    printf("[tid: %lu] received: %s\n", pthread_self(), recvline);
        
    if ((pwrite(args->filefd, recvline, n, args->offset)) < 0) {
        err_n_die("pwrite error");
    }

    close(serverfd);
}

void setFileRequest(int arc, char **arv, FileRequest *request) 
{
    // request->path = "test.txt";
    request->path = DEFAULT_PATH;
    request->offset = 0;
    request->size = 0;
}

void doRead(int argc, char **argv)
{
    int                 serverfd, filefd, n, err;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

    if ((filefd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    FileRequest request = FILE_REQUEST__INIT;
    
    setFileRequest(argc, argv, &request);

    size_t len = file_request__get_packed_size(&request);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    file_request__pack(&request, buffer);


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


    /* Sending file request */
    uint32_t net_len = htonl(len);
    write(serverfd, &net_len, sizeof(net_len));

    if (write(serverfd, buffer, len) != len)
        err_n_die("write error");

    free(buffer);


    /* Receiving ChunkList from the Master*/
    memset(recvline, 0, sizeof(recvline));
    n = read(serverfd, recvline, MAXLINE);

    ChunkList *chunkList = chunk_list__unpack(NULL, n, recvline);
    printf("n_chunks: %zu\n", chunkList->n_chunks);
    printf("\n");

    for(int i = 0; i < chunkList->n_chunks; i++){
        printf("chunk_id: %d \n", chunkList->chunks[i]->chunk_id);
        for(int j = 0; j < chunkList->chunks[i]->n_replicas; j++){
            printf("replica_name: %s \n", chunkList->chunks[i]->replicas[j]->name);
            printf("ip: %s \n", chunkList->chunks[i]->replicas[j]->ip);
            printf("port: %d \n", chunkList->chunks[i]->replicas[j]->port);
        }
        printf("\n");
    }

    close(serverfd);

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunkList->n_chunks);

    for (int i = 0; i < chunkList->n_chunks; i++) {

        threads[i].chunk_id = chunkList->chunks[i]->chunk_id;
        threads[i].path = DEFAULT_PATH;
        threads[i].ip = chunkList->chunks[i]->replicas[0]->ip;
        threads[i].port = chunkList->chunks[i]->replicas[0]->port;
        threads[i].offset = i * OFFSET;
        threads[i].filefd = filefd;

        if ((err = pthread_create(&(threads[i].tid), NULL, getChunk, &threads[i])) != 0) {
            err_n_die("couldn't create thread");
        }
    }

    for (int i = 0; i < chunkList->n_chunks; i++) {
        if ((err = pthread_join(threads[i].tid, NULL)) != 0) {
            err_n_die("couldn't join thread");
        }
    }

    free(threads);
}

void *putChunk(void *voidPtr)
{
    int replicafd, net_len;
    struct sockaddr_in repladdr;
    size_t bytes_read, proto_len;
    argsThread_t *args = voidPtr;
    char op_type[MAXLINE + 1], buffer[MAXLINE + 1];
    uint8_t *proto_buf;

    if ((bytes_read = pread(args->filefd, buffer, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("read error");

    buffer[bytes_read] = '\0';

    // printf("read from file %s:\n%s\n", args->path, buffer);
    printf("%s\n", buffer);

    ChunkRequest request = CHUNK_REQUEST__INIT;

    printf("chunk id is: %d\n", args->chunk_id);
    request.chunk_id = args->chunk_id;
    request.path = args->path;

    proto_len = chunk_request__get_packed_size(&request);
    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk_request__pack(&request, proto_buf);

    memset(&repladdr, 0, sizeof(repladdr));
    repladdr.sin_family = AF_INET;
    repladdr.sin_port = htons(args->port);

    if(inet_pton(AF_INET, args->ip, &repladdr.sin_addr) < 0)
        err_n_die("inet_pton error for %s", args->ip);
    
    if((replicafd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    if(connect(replicafd, (SA *)&repladdr, sizeof(repladdr)) < 0)
        err_n_die("connect error");
    
    net_len = htonl(CHUNK_SIZE);

    write(replicafd, &net_len, sizeof(net_len)); // this is supposed to be the size of the whole message, but idk why we would use that

    strcpy(op_type, "write");
    net_len = htonl(strlen(op_type) + 1);
    write(replicafd, &net_len, sizeof(net_len));

    write(replicafd, op_type, strlen(op_type) + 1);

    net_len = htonl(proto_len);
    write(replicafd, &net_len, sizeof(net_len));

    write(replicafd, proto_buf, proto_len);

    net_len = htonl(bytes_read);
    write(replicafd, &net_len, sizeof(net_len));

    write(replicafd, buffer, bytes_read);

    close(replicafd);
}

void doWrite()
{
    int n_threads = 2, err, filefd;
    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * n_threads);
    char path[2 * (MAXLINE + 1)];

    snprintf(path, sizeof(path), "data_client/%s", DEFAULT_PATH);

    printf("opening file %s\n", path);

    if((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");
    
    // printf("fd is %d\n", filefd);

    for (int i = 0; i < n_threads; i++) {

        threads[i].chunk_id = i;
        threads[i].path = path;
        // threads[i].ip = chunkList->chunks[i]->replicas[0]->ip;
        threads[i].ip = "127.0.0.1";
        // threads[i].port = chunkList->chunks[i]->replicas[0]->port;
        threads[i].port = 8080;
        threads[i].offset = i * CHUNK_SIZE;
        threads[i].filefd = filefd;

        if ((err = pthread_create(&(threads[i].tid), NULL, putChunk, &threads[i])) != 0) {
            err_n_die("couldn't create thread");
        }
    }

    for (int i = 0; i < n_threads; i++) {
        if ((err = pthread_join(threads[i].tid, NULL)) != 0) {
            err_n_die("couldn't join thread");
        }
    }

    free(threads);
}

int main(int argc, char **argv) 
{
    if (argc != 3)
        err_n_die("usage: parameters error");

    strcpy(DEFAULT_PATH, argv[2]);

    // printf("%s\n", argv[0]);
    // printf("|%s|\n", argv[1]);
    // printf("%s\n", argv[2]);

    if(strcmp(argv[1], "read") == 0)
        doRead(argc, argv);
    else if(strcmp(argv[1], "write") == 0)
        doWrite();
    else
        err_n_die("usage: wrong client request");
}