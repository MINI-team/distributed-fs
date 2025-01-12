#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "client.h"
#include "pthread.h"

FILE * debugfd;
char* received_chunk_path = "test";

ChunkList *chunk_list_global;

void writeChunkFile(const char *filepat, uint8_t *data, int length)
{
    int fd, n;

    if ((fd = open(filepat, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    if ((n = write(fd, data, length)) == -1)
        err_n_die("read error");

    close(fd);
}

void debug_chunk_write(char *path, int id, uint8_t *data, int length)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "received_chunks/%s%d.chunk", 
        path, id);
    writeChunkFile(chunkname, data, length);
}

void *getChunk(void *voidPtr)
{
    int replicafd, net_len, ret = -1;
    struct sockaddr_in repladdr;
    size_t bytes_read, proto_len;
    argsThread_t *args = voidPtr;
    char file_buf[MAXLINE + 1]; // MAXLINE to be reconsidered!!!!
    uint8_t op_type;

    // if ((bytes_read = pread(args->filefd, file_buf, CHUNK_SIZE, args->offset)) < 0)
    //     err_n_die("putChunk read error");

    // int modulo = args->filesize % CHUNK_SIZE;
    // if (bytes_read != CHUNK_SIZE && bytes_read != modulo)
    //     err_n_die("for args->chunk_id: %d, putChunk pread bytes_read: %d, CHUNK_SIZE: %d\n, modulo: %d\n",
    //               args->chunk_id, bytes_read, CHUNK_SIZE, modulo);

    // file_buf[bytes_read] = '\0';

    ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
    chunkRequest.path = args->path;
    chunkRequest.chunk_id = args->chunk_id;

    uint32_t len_chunkRequest = chunk_request__get_packed_size(&chunkRequest);
    uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequest * sizeof(uint8_t));
    chunk_request__pack(&chunkRequest, proto_buf);

    // uint32_t len_chunkRequest = chunk_request__get_packed_size(chunk_list_global->chunks[args->chunk_id]);
    uint32_t payload_size = sizeof(uint8_t) + sizeof(uint32_t) + len_chunkRequest;
    payload_size = htonl(payload_size);
    op_type = 'r';

    // uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequest * sizeof(uint8_t));
    // chunk__pack(chunk_list_global->chunks[args->chunk_id], proto_buf);

    printf("aha\n");
    for (int i = 0; i < args->n_replicas; i++)
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            printf("i:%d\n", i);
            break;
        }

    if (ret < 0)
        err_n_die("each replica is dead");


    printf("hello\n");

    // setup_connection(&replicafd, args->ip, args->port);
    /*
        payload_size - 4 bytes
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
    */

    bulk_write(replicafd, &payload_size, sizeof(payload_size));

    write(replicafd, &op_type, 1);

    write_len_and_data(replicafd, len_chunkRequest, proto_buf);

    int32_t chunk_content_len = read_payload_size(replicafd);

    uint8_t *buffer = (uint8_t *)malloc(chunk_content_len * sizeof(uint8_t));
    if ((bytes_read = bulk_read(replicafd, buffer, chunk_content_len)) != chunk_content_len)
        err_n_die("bulk_read error for chunk_id: %d, bytes_written: %d, payload: %d\n", args->chunk_id, bytes_read , chunk_content_len);

    int pw_bytes_written;
    if ((pw_bytes_written = pwrite(args->filefd, buffer, chunk_content_len, args->offset)) < 0)
        err_n_die("pwrite error");

    close(replicafd);
}
// {
//     // printf("bull\n");
//     int serverfd, bytes_read;
//     struct sockaddr_in servaddr;
//     char chunk[CHUNK_SIZE+1];
//     uint8_t op_type[MAXLINE];

//     argsThread_t *args = voidPtr;
//     // printf("[tid: %lu] chunk_id: %d\n", pthread_self(), args->chunk_id);

//     strcpy(op_type, "read");

//     ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
//     chunkRequest.path = args->path;
//     chunkRequest.chunk_id = args->chunk_id;

//     int len = chunk_request__get_packed_size(&chunkRequest);
//     uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
//     chunk_request__pack(&chunkRequest, buffer);

//     // printf("bull2\n");
//     setup_connection(&serverfd, args->ip, args->port); 
//     // if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
//     //     err_n_die("socket error");

//     // memset(&servaddr, 0, sizeof(servaddr));
//     // servaddr.sin_family = AF_INET;
//     // servaddr.sin_port = htons(args->port);

//     // if (inet_pton(AF_INET, args->ip, &servaddr.sin_addr) <= 0)
//     //     err_n_die("inet_pton error for %s", args->ip);

//     // if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
//     //     err_n_die("connect error");
//     // printf("bull3\n");

//     int net_len = htonl(len); // refactoring!
//     write(serverfd, &net_len, sizeof(net_len));

//     write_len_and_data(serverfd, strlen(op_type) + 1, op_type);
//     write_len_and_data(serverfd, len, buffer);

//     free(buffer);
//     int32_t payload = read_payload_size(serverfd);

//     int32_t bytes_written;
//     buffer = (uint8_t *)malloc(payload * sizeof(uint8_t));
//     if ((bytes_written = bulk_read(serverfd, buffer, payload)) != payload)
//         err_n_die("bulk_read error for chunk_id: %d, bytes_written: %d, payload: %d\n", args->chunk_id, bytes_written, payload);
    
//     int pw_bytes_written;
//     if ((pw_bytes_written = pwrite(args->filefd, buffer, payload, args->offset)) < 0)
//         err_n_die("pwrite error");

//     close(serverfd);
//     // printf("bull4\n");
// }

void do_read(char *path)
{
    int                 serverfd, filefd, n, err, bytes_read;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

    if ((filefd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    FileRequestRead fileRequestRead = FILE_REQUEST_READ__INIT;
    fileRequestRead.path = path;

    /* Packing protbuf object and 'r' into the buffer */
    uint32_t len_fileRequestRead = file_request_read__get_packed_size(&fileRequestRead) + 1;
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestRead * sizeof(uint8_t));
    buffer[0] = 'r';
    file_request_read__pack(&fileRequestRead, buffer + 1);

    setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);

    write_len_and_data(serverfd, len_fileRequestRead, buffer);

    free(buffer);

    int32_t total_bytes_read = 0;
    int32_t payload = read_payload_size(serverfd);
    buffer = (uint8_t *)malloc(payload * sizeof(uint8_t));

    while (total_bytes_read < payload)
    {
        if ((bytes_read = read(serverfd, buffer + total_bytes_read, payload)) < 0)
            err_n_die("read error");
        total_bytes_read += bytes_read;
    }

    ChunkList *chunkList = chunk_list__unpack(NULL, payload, buffer);
    if (!chunkList)
        err_n_die("chunk_list is null");

    close(serverfd);

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunkList->n_chunks);

    int index = 0;
    bool work = true;
    int to_join = MAX_THREADS_COUNT;

    while(work)
    {
        for (int i = 0; i < MAX_THREADS_COUNT; i++)
        {
            printf("submiting chunk_id: %d\n", index);
            int n_replicas = chunkList->chunks[index]->n_replicas;

            threads[i].chunk_id = index;
            threads[i].path = path;
            // threads[i].ip = (char **)malloc(n_replicas * sizeof(char*));
            threads[i].n_replicas = chunkList->chunks[index]->n_replicas;
            threads[i].replicas = chunkList->chunks[index]->replicas;
            // threads[i].ip = chunkList->chunks[index]->replicas[0]->ip;
            // threads[i].port = chunkList->chunks[index]->replicas[0]->port;
            
            threads[i].offset = (int64_t)index * CHUNK_SIZE;
            threads[i].filefd = filefd;

            if ((err = pthread_create(&(threads[i].tid), NULL, getChunk, &threads[i])) != 0)
                err_n_die("couldn't create thread");
            
            if(++index == chunkList->n_chunks)
            {
                work = false;
                to_join = i + 1;
                break;
            }
        }

        for (int i = 0; i < to_join; i++)
            if ((err = pthread_join(threads[i].tid, NULL)) != 0)
                err_n_die("couldn't join thread");

    }

    free(threads);
}

void *putChunk(void *voidPtr)
{
    // printf("hello\n");
    int replicafd, net_len, ret = -1;
    struct sockaddr_in repladdr;
    size_t bytes_read, proto_len;
    argsThread_t *args = voidPtr;
    char file_buf[MAXLINE + 1]; // MAXLINE to be reconsidered!!!!
    uint8_t op_type;

    if ((bytes_read = pread(args->filefd, file_buf, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("putChunk read error");

    int modulo = args->filesize % CHUNK_SIZE;
    if (bytes_read != CHUNK_SIZE && bytes_read != modulo)
        err_n_die("for args->chunk_id: %d, putChunk pread bytes_read: %d, CHUNK_SIZE: %d\n, modulo: %d\n", 
            args->chunk_id, bytes_read, CHUNK_SIZE, modulo);

    file_buf[bytes_read] = '\0';

    uint32_t len_chunkRequestWrite = chunk__get_packed_size(chunk_list_global->chunks[args->chunk_id]);
    uint32_t payload_size = sizeof(uint8_t) + sizeof(uint32_t) + len_chunkRequestWrite
     + sizeof(uint32_t) + bytes_read;
    payload_size = htonl(payload_size);
    op_type = 'w';

    uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequestWrite * sizeof(uint8_t));
    chunk__pack(chunk_list_global->chunks[args->chunk_id], proto_buf);

    for (int i = 0; i < args->n_replicas; i++)
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            printf("i:%d\n", i);
            break;
        }

    if (ret < 0)
        err_n_die("each replica is dead");

    /*
        payload_size - 4 bytes
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
        chunk content length - 4 bytes
        chunk content - (chunk content length) bytes
    */

    bulk_write(replicafd, &payload_size, sizeof(payload_size));

    write(replicafd, &op_type, 1);
    
    printf("to sie nie uda\n");
    write_len_and_data(replicafd, len_chunkRequestWrite, proto_buf);

    write_len_and_data(replicafd, bytes_read, file_buf);

    close(replicafd);
}

void do_write(char *path)
{
    int err, filefd, serverfd;
    int bytes_read;

    printf("Opening file: %s\n", path);

    if ((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");

    FileRequestWrite fileRequestWrite = FILE_REQUEST_WRITE__INIT;
    fileRequestWrite.path = path;
    fileRequestWrite.size = file_size(filefd);

    /* Packing protbuf object and 'w' into the buffer */
    uint32_t len_fileRequestWrite = file_request_write__get_packed_size(&fileRequestWrite) + 1;
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestWrite * sizeof(uint8_t));
    buffer[0] = 'w';
    file_request_write__pack(&fileRequestWrite, buffer + 1);

    setup_connection(&serverfd, MASTER_SERVER_IP, MASTER_SERVER_PORT);
        
    write_len_and_data(serverfd, len_fileRequestWrite, buffer);

    free(buffer);


    int32_t payload;
    read_paylaod_and_data(serverfd, &buffer, &payload);

    ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
    if (!chunk_list)
        err_n_die("chunk_list is null");

    chunk_list_global = chunk_list;

    // close serverfd??

#ifdef DEBUG
    debug_log(debugfd, "chunk_list->n_chunks: %d\n", chunk_list->n_chunks);
    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        debug_log(debugfd, "chunk_list->chunks[i]->n_replicas: %ld\n" ,chunk_list->chunks[i]->n_replicas);
        for (int j = 0; j < chunk_list->chunks[i]->n_replicas; j++)
        {
            debug_log(debugfd, "chunk_list->chunks[i]->replicas[j]->ip: %s\n", chunk_list->chunks[i]->replicas[j]->ip);
            debug_log(debugfd, "chunk_list->chunks[i]->replicas[j]->port: %d\n", chunk_list->chunks[i]->replicas[j]->port);
        }
    }
#endif

    argsThread_t *threads = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);

    int index = 0;
    bool work = true;
    int to_join = MAX_THREADS_COUNT;

    while(work)
    {
        for (int i = 0; i < MAX_THREADS_COUNT; i++)
        {
            printf("submiting chunk_id: %d\n", index);
            threads[i].chunk_id = index;
            threads[i].path = path;
            threads[i].n_replicas = chunk_list->chunks[index]->n_replicas;
            threads[i].replicas = chunk_list->chunks[index]->replicas;

            threads[i].offset = (int64_t) index * CHUNK_SIZE;
            threads[i].filefd = filefd;
            threads[i].filesize = fileRequestWrite.size;

            if ((err = pthread_create(&(threads[i].tid), NULL, putChunk, &threads[i])) != 0)
                err_n_die("couldn't create thread");
            
            if(++index == chunk_list->n_chunks)
            {
                work = false;
                to_join = i + 1;
                break;
            }
        }

        for (int i = 0; i < to_join; i++)
            if ((err = pthread_join(threads[i].tid, NULL)) != 0)
                err_n_die("couldn't join thread");
    }

    //  free buffer

    free(threads);
}

int main(int argc, char **argv)
{
#ifdef DEBUG
    debugfd = fopen(CLIENT_DEBUG_PATH, "w");
    if (!debugfd)
        err_n_die("debugfd open error");
#endif
    if (argc != 3)
        err_n_die("usage: parameters error");

    if (strcmp(argv[1], "read") == 0)
        do_read(argv[2]);
    else if (strcmp(argv[1], "write") == 0)
        do_write(argv[2]);
    else
        err_n_die("usage: wrong client request");
}

int64_t file_size(int filefd)
{
    struct stat file_stat;

    if (fstat(filefd, &file_stat))
    {
        close(filefd);
        err_n_die("fstat error");
    }

    return (int64_t)file_stat.st_size;
}