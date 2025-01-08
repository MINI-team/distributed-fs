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
    int serverfd, bytes_read;
    struct sockaddr_in servaddr;
    char chunk[CHUNK_SIZE+1];
    uint8_t op_type[MAXLINE];

    argsThread_t *args = voidPtr;
    // printf("[tid: %lu] chunk_id: %d\n", pthread_self(), args->chunk_id);

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

    if (inet_pton(AF_INET, args->ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", args->ip);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");

    int net_len = htonl(len); // refactoring!
    write(serverfd, &net_len, sizeof(net_len));

    write_len_and_data(serverfd, strlen(op_type) + 1, op_type);
    write_len_and_data(serverfd, len, buffer);

    free(buffer);

    int32_t payload = read_payload_size(serverfd);
    int32_t bytes_written;
    buffer = (uint8_t *)malloc(payload * sizeof(uint8_t));
    if ((bytes_written = bulk_read(serverfd, buffer, payload)) != payload)
        err_n_die("bulk_read error for chunk_id: %d, bytes_written: %d, payload: %d\n", args->chunk_id, bytes_written, payload);
    

    int pw_bytes_written;
    if ((pw_bytes_written = pwrite(args->filefd, buffer, payload, args->offset)) < 0)
        err_n_die("pwrite error");

    close(serverfd);
}

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
            threads[i].chunk_id = index;
            threads[i].path = path;
            threads[i].ip = chunkList->chunks[index]->replicas[0]->ip;
            threads[i].port = chunkList->chunks[index]->replicas[0]->port;
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
    // printf("p\n");
    int replicafd, net_len;
    struct sockaddr_in repladdr;
    size_t bytes_read, proto_len;
    argsThread_t *args = voidPtr;
    char op_type[MAXLINE + 1], buffer[MAXLINE + 1]; // MAXLINE to be reconsidered!!!!
    uint8_t *proto_buf;

    if ((bytes_read = pread(args->filefd, buffer, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("putChunk read error");

    int modulo = args->filesize % CHUNK_SIZE;
    if (bytes_read != CHUNK_SIZE && bytes_read != modulo)
        err_n_die("for args->chunk_id: %d, putChunk pread bytes_read: %d, CHUNK_SIZE: %d\n, modulo: %d\n", 
            args->chunk_id, bytes_read, CHUNK_SIZE, modulo);

    buffer[bytes_read] = '\0';

    strcpy(op_type, "write_primary");

    proto_len = chunk__get_packed_size(chunk_list_global->chunks[args->chunk_id]);
    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk__pack(chunk_list_global->chunks[args->chunk_id], proto_buf);

    setup_connection(&replicafd, args->ip, args->port);

    /// this has to be refactored
    net_len = htonl(CHUNK_SIZE);
    write(replicafd, &net_len, sizeof(net_len));

    write_len_and_data(replicafd, strlen(op_type) + 1, op_type);

    write_len_and_data(replicafd, proto_len, proto_buf);

    write_len_and_data(replicafd, bytes_read, buffer);

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
            threads[i].ip = chunk_list->chunks[index]->replicas[0]->ip;
            threads[i].port = chunk_list->chunks[index]->replicas[0]->port;
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