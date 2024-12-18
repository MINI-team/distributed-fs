#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "client.h"
#include "pthread.h"

// const char *MASTER_ADDRESS = "127.0.0.1";

char DEFAULT_PATH[MAXLINE + 1];

ChunkList *chunk_list_global;


// void setupReplicas(Replica **replicas)
// {
//     // char* replica_ip = resolve_host(REPLICA_ADDRESS);
//     replicas[0] = (Replica *)malloc(sizeof(Replica));
//     replica__init(replicas[0]);
//     replicas[0]->ip = "127.0.0.1";
//     // replicas[0]->ip = replica_ip;
//     replicas[0]->port = 8080;
//     replicas[0]->name = "Replica A";

//     replicas[1] = (Replica *)malloc(sizeof(Replica));
//     replica__init(replicas[1]);
//     replicas[1]->ip = "127.0.0.1";
//     // replicas[1]->ip = replica_ip;
//     replicas[1]->port = 8081;
//     replicas[1]->name = "Replica B";
// }

void *getChunk(void *voidPtr)
{
    int serverfd, n;
    struct sockaddr_in servaddr;
    char recvline[MAXLINE], op_type[MAXLINE];

    argsThread_t *args = voidPtr;
    printf("[tid: %lu] chunk_id: %d\n", pthread_self(), args->chunk_id);

    strcpy(op_type, "read");

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

    //printf("?????? IP: %s, Port: %d\n", args->ip, args->port);

    if (inet_pton(AF_INET, args->ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", args->ip);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");

    int net_len = htonl(len); // TO FIX
    write(serverfd, &net_len, sizeof(net_len));

    write_len_and_data(serverfd, strlen(op_type) + 1, op_type);
    write_len_and_data(serverfd, len, buffer);

    free(buffer);

    memset(recvline, 0, MAXLINE);
    n = read(serverfd, recvline, MAXLINE);
    printf("[tid: %lu] received: %s\n", pthread_self(), recvline);

    if ((pwrite(args->filefd, recvline, n, args->offset)) < 0)
        err_n_die("pwrite error");

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
    setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);

    printf("tu sie nam polaczylo\n");
    printf("len: %d \n", len);
    /* Sending file request */
    uint32_t net_len = htonl(len + 1);
    write(serverfd, &net_len, sizeof(net_len));
    
    char request_type = 'r';
    write(serverfd, &request_type, 1);

    if (write(serverfd, buffer, len) != len)
        err_n_die("write error");

    free(buffer);

    /* Receiving ChunkList from the Master*/
    memset(recvline, 0, sizeof(recvline));
    n = read(serverfd, recvline, MAXLINE);

    ChunkList *chunkList = chunk_list__unpack(NULL, n, recvline);
    // printf("n_chunks: %zu\n", chunkList->n_chunks);
    // printf("\n");

    // for (int i = 0; i < chunkList->n_chunks; i++)
    // {
    //     printf("chunk_id: %d \n", chunkList->chunks[i]->chunk_id);
    //     for (int j = 0; j < chunkList->chunks[i]->n_replicas; j++)
    //     {
    //         printf("replica_name: %s \n", chunkList->chunks[i]->replicas[j]->name);
    //         printf("ip: %s \n", chunkList->chunks[i]->replicas[j]->ip);
    //         printf("port: %d \n", chunkList->chunks[i]->replicas[j]->port);
    //     }
    //     printf("\n");
    // }

    close(serverfd);

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunkList->n_chunks);

    for (int i = 0; i < chunkList->n_chunks; i++)
    {

        threads[i].chunk_id = chunkList->chunks[i]->chunk_id;
        threads[i].path = DEFAULT_PATH;
        threads[i].ip = chunkList->chunks[i]->replicas[0]->ip;
        threads[i].port = chunkList->chunks[i]->replicas[0]->port;
        threads[i].offset = i * CHUNK_SIZE;
        threads[i].filefd = filefd;

        if ((err = pthread_create(&(threads[i].tid), NULL, getChunk, &threads[i])) != 0)
        {
            err_n_die("couldn't create thread");
        }
    }

    for (int i = 0; i < chunkList->n_chunks; i++)
    {
        if ((err = pthread_join(threads[i].tid, NULL)) != 0)
        {
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

    strcpy(op_type, "write_primary");

    // proto_len = chunk__get_packed_size(chunks[args->chunk_id]);
    proto_len = chunk__get_packed_size(chunk_list_global->chunks[args->chunk_id]);
    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk__pack(chunk_list_global->chunks[args->chunk_id], proto_buf);

    setup_connection(&replicafd, args->ip, args->port);

    // memset(&repladdr, 0, sizeof(repladdr));
    // repladdr.sin_family = AF_INET;
    // repladdr.sin_port = htons(args->port);

    // if (inet_pton(AF_INET, args->ip, &repladdr.sin_addr) < 0)
    //     err_n_die("inet_pton error for %s", args->ip);

    // if ((replicafd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    //     err_n_die("socket error");

    // if (connect(replicafd, (SA *)&repladdr, sizeof(repladdr)) < 0)
    // {
    //     printf("IP: %s, Port: %d\n", args->ip, args->port);
    //     err_n_die("connect error");
    // }

    net_len = htonl(CHUNK_SIZE);
    write(replicafd, &net_len, sizeof(net_len)); // this is supposed to be the size of the whole message, but idk why we would use that

    write_len_and_data(replicafd, strlen(op_type) + 1, op_type);

    write_len_and_data(replicafd, proto_len, proto_buf);

    write_len_and_data(replicafd, bytes_read, buffer);

    close(replicafd);

    printf("sent %s to replica %d\n", buffer, args->port);
}

void doWrite(char *_path)
{
    // int n_threads = 2, 
    int err, filefd, serverfd;
    char path[2 * (MAXLINE + 1)];
    struct sockaddr_in  servaddr;
    int bytes_read;
   
    // snprintf(path, sizeof(path), "data_client/%s", _path);
    snprintf(path, sizeof(path), "%s", _path);
    // snprintf(path, sizeof(path), "%s", argv[2]);
    // snprintf(path, sizeof(path), "data_client/%s", DEFAULT_PATH);

    printf("opening file: %s\n", path);

    if ((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");

    printf("file size: %d bytes\n", file_size(filefd));

    FileRequestWrite fileRequestWrite = FILE_REQUEST_WRITE__INIT;
    fileRequestWrite.path = path;
    fileRequestWrite.size = file_size(filefd);

    int len_fileRequestWrite = file_request_write__get_packed_size(&fileRequestWrite);
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestWrite * sizeof(uint8_t));
    file_request_write__pack(&fileRequestWrite, buffer);

    setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);
        
    printf("len_fileRequestWrite: %zu \n", len_fileRequestWrite);

    uint32_t net_len_fileRequestWrite_plus1 = htonl(len_fileRequestWrite + 1);
    write(serverfd, &net_len_fileRequestWrite_plus1, sizeof(net_len_fileRequestWrite_plus1));

    uint8_t request_type = 'w';
    write(serverfd, &request_type, 1); // this sends fine

    if (write(serverfd, buffer, len_fileRequestWrite) != len_fileRequestWrite) // this sends fine as well
        err_n_die("write error");

    printf("fileRequestWrite->path: %s\n", fileRequestWrite.path);
    printf("fileRequestWrite->size: %d\n", fileRequestWrite.size);

    free(buffer);
    // printf("fd is %d\n", filefd);

    uint8_t *buffer2 = (uint8_t *)malloc((MAXLINE+1) * sizeof(uint8_t));
    if ((bytes_read = read(serverfd, buffer2, MAXLINE)) < 0) // this breaksread error (errno = 14) : Bad address  
        err_n_die("read error");
    printf("bytes_read=:%d\n", bytes_read); 
    ChunkList *chunk_list = chunk_list__unpack(NULL, bytes_read, buffer2);
    //printf("chunk_list->n_chunks: %d\n",chunk_list->n_chunks);

    chunk_list_global = chunk_list;

    // for (int i = 0; i < chunk_list->n_chunks; i++)
    // {
    //     printf("chunk_list->chunks[i]->n_replicas: %d\n",chunk_list->chunks[i]->n_replicas);
    //     for (int j = 0; j < chunk_list->chunks[i]->n_replicas; j++)
    //     {
    //         printf("chunk_list->chunks[i]->replicas[j]->ip: %s\n", chunk_list->chunks[i]->replicas[j]->ip);
    //         printf("chunk_list->chunks[i]->replicas[j]->port: %d\n", chunk_list->chunks[i]->replicas[j]->port);
    //     }
    // }

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);

    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        threads[i].chunk_id = i;
        threads[i].path = path;
        threads[i].ip = chunk_list->chunks[i]->replicas[0]->ip;
        // threads[i].ip = "127.0.0.1"; // <------------------------------- REPLACE THIS (ip of client)
        threads[i].port = chunk_list->chunks[i]->replicas[0]->port;
        // threads[i].port = 8080; // <------------------------------- REPLACE THIS (port of client)
        threads[i].offset = i * CHUNK_SIZE;
        threads[i].filefd = filefd;

        if ((err = pthread_create(&(threads[i].tid), NULL, putChunk, &threads[i])) != 0)
        {
            err_n_die("couldn't create thread");
        }
    }

    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        if ((err = pthread_join(threads[i].tid, NULL)) != 0)
        {
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

    if (strcmp(argv[1], "read") == 0)
        doRead(argc, argv);
    else if (strcmp(argv[1], "write") == 0)
        doWrite(argv[2]);
    else
        err_n_die("usage: wrong client request");
}

int file_size(int filefd)
{
    struct stat file_stat;

    if (fstat(filefd, &file_stat))
    {
        close(filefd);
        err_n_die("fstat error");
    }

    return (int)file_stat.st_size;
}