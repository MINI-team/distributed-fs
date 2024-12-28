#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "client.h"
#include "pthread.h"

// const char *MASTER_ADDRESS = "127.0.0.1";

char DEFAULT_PATH[MAXLINE + 1];

ChunkList *chunk_list_global;
char* master_ip;

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
    int replicafd, n;
    struct sockaddr_in servaddr;
    char recvline[MAXLINE], op_type[MAXLINE];
    int i, err = -1;

    argsThread_t *args = voidPtr;
    // printf("[tid: %lu] chunk_id: %d\n", pthread_self(), args->chunk_id);
    // printf("chunk_id: %d, ip[0]: %s port[0]: %d, N_REPLICAS: %d\n", 
    //     args->chunk_id, args->ip[0], args->port[0], args->n_replicas);

    strcpy(op_type, "read");

    ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
    chunkRequest.path = args->path;
    chunkRequest.chunk_id = args->chunk_id;

    int len = chunk_request__get_packed_size(&chunkRequest);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    chunk_request__pack(&chunkRequest, buffer);

    for (i = 0; err < 0 && i < args->n_replicas; i++)
    {
        printf("trying to setup connection to: ip[%d]: %s port[%d]: %d\n", i, args->ip[0], i, args->port[0]);
        err = setup_connection(&replicafd, args->ip[i], args->port[i]);
    }

    if (err < 0)
        err_n_die("Client (read) couldn't connect to any replica");

    int net_len = htonl(len); // TO FIX
    write(replicafd, &net_len, sizeof(net_len));

    write_len_and_data(replicafd, strlen(op_type) + 1, op_type);
    write_len_and_data(replicafd, len, buffer);

    free(buffer);

    memset(recvline, 0, MAXLINE);
    n = read(replicafd, recvline, MAXLINE);
    // printf("[tid: %lu] received: %s\n", pthread_self(), recvline);
    printf("received: %s\n", recvline);
    fflush(stdout);

    if ((pwrite(args->filefd, recvline, n, args->offset)) < 0)
        err_n_die("pwrite error");

    close(replicafd);
}

void setFileRequest(int arc, char **arv, FileRequest *request)
{
    // request->path = "test.txt";
    request->path = DEFAULT_PATH;
    request->offset = 0;
    request->size = 0;
}

// void createThreads(char mode, argsThread_t *threads, ChunkList *chunkList, char *path, int filefd, void *fun)
// {
//     int err;
//     threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunkList->n_chunks);

//     for (int i = 0; i < chunkList->n_chunks; i++)
//     {
//         Chunk *cur_chunk = chunkList->chunks[i];
//         if(mode == 'w')
//             threads[i].chunk_id = i;
//         else
//             threads[i].chunk_id = chunkList->chunks[i]->chunk_id;
//         threads[i].path = path;
//         // threads[i].ip = chunkList->chunks[i]->replicas[0]->ip;
//         // threads[i].port = chunkList->chunks[i]->replicas[0]->port;
//         threads[i].offset = i * CHUNK_SIZE;
//         threads[i].filefd = filefd;

//         threads[i].ip = (char **)malloc(cur_chunk->n_replicas * sizeof(char));
//         threads[i].port = (uint16_t *)malloc(cur_chunk->n_replicas * sizeof(uint16_t));
//         threads[i].n_replicas = cur_chunk->n_replicas;

//         for (int j = 0; j < cur_chunk->n_replicas; j++)
//         {
//             threads[i].ip[j] = cur_chunk->replicas[j]->ip;
//             threads[i].port[j] = cur_chunk->replicas[j]->port;
//         }

//         if ((err = pthread_create(&(threads[i].tid), NULL, fun, &threads[i])) != 0)
//         {
//             err_n_die("couldn't create thread");
//         }
//     }
// }

void doRead(int argc, char **argv)
{
    int                 serverfd, filefd, n, err;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

    char output_path[256];
    snprintf(output_path, sizeof(output_path), "%s_output.txt", argv[2]);

    if ((filefd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    FileRequest request = FILE_REQUEST__INIT;

    setFileRequest(argc, argv, &request);

    size_t len = file_request__get_packed_size(&request);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    file_request__pack(&request, buffer);

    /* Connecting with the server */

    master_ip = strdup(argv[3]);
    setup_connection(&serverfd, master_ip, MASTER_SERVER_PORT);
    //setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);
    
    printf("tu sie nam polaczylo\n");
    printf("len: %ld \n", len);
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
    printf("received n_chunks: %zu\n", chunkList->n_chunks);
    fflush(stdout);
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

    // argsThread_t *threads;
    // createThreads('r', threads, chunkList, DEFAULT_PATH, filefd, getChunk);

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunkList->n_chunks);

    for (int i = 0; i < chunkList->n_chunks; i++)
    {
        Chunk *cur_chunk = chunkList->chunks[i];
        threads[i].chunk_id = chunkList->chunks[i]->chunk_id;
        threads[i].path = DEFAULT_PATH;
        threads[i].offset = i * CHUNK_SIZE;
        threads[i].filefd = filefd;

        threads[i].ip = (char **)malloc(cur_chunk->n_replicas * sizeof(char));
        threads[i].port = (uint16_t *)malloc(cur_chunk->n_replicas * sizeof(uint16_t));
        threads[i].n_replicas = cur_chunk->n_replicas;

        for (int j = 0; j < cur_chunk->n_replicas; j++)
        {
            threads[i].ip[j] = master_ip;
            //threads[i].ip[j] = cur_chunk->replicas[j]->ip;
            threads[i].port[j] = cur_chunk->replicas[j]->port;
        }

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
    int err = -1;
    int i;

    if ((bytes_read = pread(args->filefd, buffer, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("read error");

    buffer[bytes_read] = '\0';

    // printf("read from file %s:\n%s\n", args->path, buffer);
    printf("%s\n", buffer);

    fflush(stdout);

    strcpy(op_type, "write_primary");

    // proto_len = chunk__get_packed_size(chunks[args->chunk_id]);
    proto_len = chunk__get_packed_size(chunk_list_global->chunks[args->chunk_id]);
    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk__pack(chunk_list_global->chunks[args->chunk_id], proto_buf);

    // for(i = 0; err < 0; i = (i + 1) % args->n_replicas)
    for(i = 0; err < 0 && i < args->n_replicas; i++)
    {
        err = setup_connection(&replicafd, args->ip[i], args->port[i]);
    }

    // printf("err is %d\n", err);

    if(err < 0)
        err_n_die("Client (write) couldn't connect to any replica");

    // setup_connection(&replicafd, args->ip, args->port);

    net_len = htonl(CHUNK_SIZE);
    write(replicafd, &net_len, sizeof(net_len)); // this is supposed to be the size of the whole message, but idk why we would use that

    write_len_and_data(replicafd, strlen(op_type) + 1, op_type);

    write_len_and_data(replicafd, proto_len, proto_buf);

    write_len_and_data(replicafd, bytes_read, buffer);

    close(replicafd);

    printf("sent %s to replica\n", buffer);
}

void doWrite(char **argv)
{
    char *_path = argv[2];
    int err, filefd, serverfd;
    char path[2 * (MAXLINE + 1)];
    struct sockaddr_in  servaddr;
    int bytes_read;
   
    snprintf(path, sizeof(path), "%s", _path);

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

    char* master_ip = strdup(argv[3]);
    setup_connection(&serverfd, master_ip, MASTER_SERVER_PORT);
    //setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);
        
    printf("len_fileRequestWrite: %u \n", len_fileRequestWrite);

    uint32_t net_len_fileRequestWrite_plus1 = htonl(len_fileRequestWrite + 1);
    write(serverfd, &net_len_fileRequestWrite_plus1, sizeof(net_len_fileRequestWrite_plus1));

    uint8_t request_type = 'w';
    write(serverfd, &request_type, 1); // this sends fine

    if (write(serverfd, buffer, len_fileRequestWrite) != len_fileRequestWrite) // this sends fine as well
        err_n_die("write error");

    printf("fileRequestWrite->path: %s\n", fileRequestWrite.path);
    printf("fileRequestWrite->size: %ld\n", fileRequestWrite.size);

    free(buffer);
    // printf("fd is %d\n", filefd);

    uint8_t *buffer2 = (uint8_t *)malloc((MAXLINE+1) * sizeof(uint8_t));
    if ((bytes_read = read(serverfd, buffer2, MAXLINE)) < 0) // this breaksread error (errno = 14) : Bad address  
        err_n_die("read error");
    printf("bytes_read=:%d\n", bytes_read); 
    ChunkList *chunk_list = chunk_list__unpack(NULL, bytes_read, buffer2);
    printf("write: n_chunks: %d\n",chunk_list->n_chunks);

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

    fflush(stdout);
    
    // argsThread_t *threads; // = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);
    // createThreads('w', threads, chunk_list, path, filefd, putChunk);

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);

    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        Chunk *cur_chunk = chunk_list->chunks[i];
        threads[i].chunk_id = i;
        threads[i].path = path;
        // threads[i].ip = chunk_list->chunks[i]->replicas[0]->ip;
        // threads[i].port = chunk_list->chunks[i]->replicas[0]->port;
        threads[i].offset = i * CHUNK_SIZE;
        threads[i].filefd = filefd;
        
        threads[i].ip = (char **)malloc(cur_chunk->n_replicas * sizeof(char));
        threads[i].port = (uint16_t *)malloc(cur_chunk->n_replicas * sizeof(uint16_t));
        threads[i].n_replicas = cur_chunk->n_replicas;

        for(int j = 0; j < cur_chunk->n_replicas; j++)
        {
            //threads[i].ip[j] = cur_chunk->replicas[j]->ip;
            threads[i].ip[j] = master_ip;
            threads[i].port[j] = cur_chunk->replicas[j]->port;
        }

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
    if (argc != 4)
        err_n_die("usage: parameters error");

    strcpy(DEFAULT_PATH, argv[2]);
    if (strcmp(argv[1], "read") == 0)
        doRead(argc, argv);
    else if (strcmp(argv[1], "write") == 0)
        doWrite(argv);
    else
        err_n_die("usage: wrong client request");

    free(master_ip);
    return 0;
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