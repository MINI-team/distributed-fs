#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "pthread.h"

const char* SERVER_ADDRESS = "127.0.0.1";
const uint16_t SERVER_PORT = 9001;

// char* DEFAULT_PATH = "/home/vlada/Documents/thesis/distributed-fs/server/gfs.png";
char DEFAULT_PATH[MAXLINE+1];
char* OUTPUT_PATH = "output.txt";

#define OFFSET 13

typedef struct argsThread
{
    pthread_t tid;
    char* path;
    int chunk_id;
    
    char *ip;
    uint16_t port;

    int offset;
    int outputfd;
    
} argsThread_t;

void *getChunk(void *voidPtr)
{
    int                 serverfd, n;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

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

    int network_length = htonl(len);
    write(serverfd, &network_length, sizeof(network_length));

    write(serverfd, buffer, len);
    free(buffer);

    memset(recvline, 0, MAXLINE);
    n = read(serverfd, recvline, MAXLINE);
    printf("[tid: %lu] received: %s\n", pthread_self(), recvline);
        
    if ((pwrite(args->outputfd, recvline, n, args->offset)) < 0) {
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

int main(int argc, char **argv) 
{
    int                 serverfd, outputfd, n, err;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

    if (argc != 3)
        err_n_die("usage: parameters error");

    strcpy(DEFAULT_PATH, argv[2]);

    if ((outputfd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("outputfd error");

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
        threads[i].outputfd = outputfd;

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