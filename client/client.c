#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "client.h"
#include "pthread.h"

FILE * debugfd;
char* received_chunk_path = "test";
int64_t debug_file_size;

ChunkList *chunk_list_global;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_main_to_threads = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_thread_to_main = PTHREAD_COND_INITIALIZER;

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
    char chunkname[MAX_FILENAME_LENGTH + 1];
    snprintf(chunkname, sizeof(chunkname), "received_chunks/%s%d.chunk", 
        path, id);
    writeChunkFile(chunkname, data, length);
}

void get_chunk(void *voidPtr)
{
    int replicafd, ret = -1;
    struct sockaddr_in repladdr;
    size_t bytes_read;
    argsThread_t *args = voidPtr;
    uint8_t op_type;

    ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
    chunkRequest.path = args->path;
    chunkRequest.chunk_id = args->chunk_id;

    uint32_t len_chunkRequest = chunk_request__get_packed_size(&chunkRequest);
    uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequest * sizeof(uint8_t));
    chunk_request__pack(&chunkRequest, proto_buf);

    uint32_t payload_size = sizeof(uint8_t) + sizeof(uint32_t) + len_chunkRequest;
    payload_size = htonl(payload_size);
    op_type = 'r';

    for (int i = 0; i < args->n_replicas; i++)
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            printf("\n=============================\nConnected to replica %d\n===================================\n",
             args->replicas[i]->port);
            break;
        }

    if (ret < 0)
        err_n_die("each replica is dead");

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
        err_n_die("bulk_read error for chunk_id: %d, bytes_read: %d, payload: %d\n", args->chunk_id, bytes_read , chunk_content_len); // here my app crashes

    int pw_bytes_written;
    if ((pw_bytes_written = pwrite(args->filefd, buffer, chunk_content_len, args->offset)) < 0)
        err_n_die("pwrite error");

    close(replicafd);

    free(proto_buf);
    free(buffer);
}

void put_chunk(void *voidPtr)
{
    int k;
    // printf("hello\n");
    int replicafd, net_len, ret = -1;
    size_t bytes_read, proto_len;
    struct sockaddr_in repladdr;
    argsThread_t *args = voidPtr;
    uint8_t op_type;
    // char file_buf[CHUNK_SIZE + 1]; 

    uint8_t* file_buf = (uint8_t *)malloc(CHUNK_SIZE * sizeof(uint8_t));

    if ((bytes_read = pread(args->filefd, file_buf, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("put_chunk read error");

    int modulo = debug_file_size % CHUNK_SIZE;
    if (bytes_read != CHUNK_SIZE && bytes_read != modulo)
        err_n_die("for args->chunk_id: %d, put_chunk pread bytes_read: %d, CHUNK_SIZE: %d\n, modulo: %d\n", 
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
    printf("to sie niby udalo\n");

    write(replicafd, &op_type, 1);
    
    write_len_and_data(replicafd, len_chunkRequestWrite, proto_buf);
    printf("to sie nie uda\n");

    write_len_and_data(replicafd, bytes_read, file_buf);

    // close(replicafd);

    free(file_buf);
    free(proto_buf);

    printf("Waiting for commit\n");
    uint32_t payload;
    uint8_t *buffer;
    // alert(5);

    // for (int i = 0; i < REPLICATION_FACTOR; i++) BRING THIS BACK <-------------------------------------
    // --------------------------------------------------------------------------
    // --------------------------------------------------------------------------
    for (int i = 0; i < REPLICATION_FACTOR; i++)
    {
        read_paylaod_and_data(replicafd, &buffer, &payload);
        ChunkCommitReport *chunk_commit_report = chunk_commit_report__unpack(NULL, payload, buffer);
        if (chunk_commit_report->is_success)
            printf("Received chunk commit report for %d replica, success for IP: %s, port: %d\n",
                   i, chunk_commit_report->ip, chunk_commit_report->port);
        else
            printf("Received chunk commit report for %d replica, fail for IP:%s, port: %d\n",
                   i, chunk_commit_report->ip, chunk_commit_report->port);
    }
    // w tym momencie, u repliki bedzie striggerowany EPOLLIN dla klienta i bytes_read = 0
    close(replicafd);
}


void *thread_work(void *data)
{
    thread_pool_args_t *tp_args = (thread_pool_args_t *)data;

    while(1)
    {   
        pthread_mutex_lock(tp_args->mutex);
        while (tp_args->work_taken && !tp_args->work_finished)
        {
            printf("ja ide spac\n");
            pthread_cond_wait(tp_args->cond_main_to_threads, tp_args->mutex); 
            printf("obudzono\n");
        }
        if (tp_args->work_finished)
        {
            pthread_mutex_unlock(tp_args->mutex);
            break;
        }

        int index = tp_args->current_chunk;
        tp_args->work_taken = true;

        pthread_cond_signal(tp_args->cond_thread_to_main);
        pthread_mutex_unlock(tp_args->mutex);

        if (tp_args->process_chunk)
            tp_args->process_chunk((void*)(&(tp_args->argsThread[index])));
        else
            err_n_die("no process_chunk configured");
    }
    return NULL;
}

void threads_process(argsThread_t *argsThread, thread_pool_args_t *thread_pool_args, ChunkList *chunk_list, char *path, int filefd)
{
        pthread_t tid[MAX_THREADS_COUNT];

    for (int i = 0; i < MAX_THREADS_COUNT; i++)
    {
        if (pthread_create(&tid[i], NULL, &thread_work, (void *)thread_pool_args) != 0)
            perror("pthread_create error"), exit(1);
    }
    
    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        argsThread[i].chunk_id = i;
        argsThread[i].path = path;
        argsThread[i].n_replicas = chunk_list->chunks[i]->n_replicas;
        argsThread[i].replicas = chunk_list->chunks[i]->replicas;

        argsThread[i].offset = (int64_t) i * CHUNK_SIZE;
        argsThread[i].filefd = filefd;
    }

    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        pthread_mutex_lock(&mutex);
        while (thread_pool_args->work_taken == false)
            pthread_cond_wait(&cond_thread_to_main, &mutex);
        
        thread_pool_args->current_chunk = i;
        thread_pool_args->work_taken = false;
        pthread_cond_signal(&cond_main_to_threads);
        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_lock(&mutex);
        while (thread_pool_args->work_taken == false)
            pthread_cond_wait(&cond_thread_to_main, &mutex);

    thread_pool_args->work_finished = true;

    pthread_cond_broadcast(&cond_main_to_threads);
    pthread_mutex_unlock(&mutex);


    for (int i = 0; i < MAX_THREADS_COUNT; i++)
        if (pthread_join(tid[i], NULL) != 0)
            err_n_die("pthread_join error");

    printf("All threads joined \n");
}

void do_read(char *path)
{
    int                 serverfd, filefd, n, err, bytes_read;

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

    uint32_t payload;
    read_paylaod_and_data(serverfd, &buffer, &payload);

    ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
    if (!chunk_list)
        err_n_die("chunk_list is null");

    close(serverfd);

    argsThread_t *argsThread = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);
    

    thread_pool_args_t thread_pool_args = {&mutex, &cond_main_to_threads, &cond_thread_to_main,
                        -1, argsThread, true, false, get_chunk};

    threads_process(argsThread, &thread_pool_args, chunk_list, path, filefd);

    free(argsThread);   
}

void do_write(char *path)
{
    int err, filefd, serverfd;
    int bytes_read;

    printf("Opening file: %s\n", path);

    if ((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");

    debug_file_size = file_size(filefd);

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

    uint32_t payload;
    read_paylaod_and_data(serverfd, &buffer, &payload);

    ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
    if (!chunk_list)
        err_n_die("chunk_list is null");
    else
        printf("chunk_list NOT null\n");

    chunk_list_global = chunk_list;

    close(serverfd);

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

    argsThread_t *argsThread = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);
    
    thread_pool_args_t thread_pool_args = {&mutex, &cond_main_to_threads, &cond_thread_to_main,
                        -1, argsThread, true, false, put_chunk};

    threads_process(argsThread, &thread_pool_args, chunk_list, path, filefd);

    free(argsThread);
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
