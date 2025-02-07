#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"
#include "client.h"
#include "pthread.h"

int master_port = 9001;
char master_ip[IP_LENGTH] = "127.0.0.1";

FILE * debugfd;
char* received_chunk_path = "test";
int64_t debug_file_size;

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
    {
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            print_logs(2, "Chunk id %d, connected to replica %d\n",
                    args->chunk_id, args->replicas[i]->port);

            if ((ret = bulk_write(replicafd, &payload_size, sizeof(payload_size))) == -2 || 
                (ret = bulk_write(replicafd, &op_type, 1)) == -2 ||
                (ret = write_len_and_data(replicafd, len_chunkRequest, proto_buf)) == -2 ||
                (ret = read_payload_size(replicafd, NULL)) == -2)
            {
                print_logs(2, "Broken pipe, replica crashed, will try from another replica\n");
                continue;
            }

            if(ret == 0)
            {
                print_logs(2, "There is no such chunk on replica.\n");
                continue;
            }

            uint32_t chunk_content_len = ret;
            uint8_t *buffer = (uint8_t *)malloc(chunk_content_len * sizeof(uint8_t));
            if ((bytes_read = bulk_read(replicafd, buffer, chunk_content_len)) != chunk_content_len)
            {
                // err_n_die("bulk_read error for chunk_id: %d, bytes_read: %d, payload: %d\n", args->chunk_id, bytes_read , chunk_content_len);
                print_logs(1, "bytes_read: %d != chunk_content_len: %d, will try from another replica", bytes_read, chunk_content_len);
                ret = -2;
                continue;
            }

            int pw_bytes_written;
            if ((pw_bytes_written = pwrite(args->filefd, buffer, chunk_content_len, args->offset)) < 0)
                err_n_die("pwrite error");

            close(replicafd);

            free(proto_buf);
            free(buffer);

            print_logs(1, "Chunk id %d received from replica IP: %s port: %d\n",
                        args->chunk_id,  args->replicas[i]->ip, args->replicas[i]->port);
            
            break;
        }
    }

    if (ret < 0)
    {
        print_logs(0, "Each replica is dead, failed to read file\n");
        exit(1);
    }
}

void put_chunk(void *voidPtr)
{
    // sleep(1);
    int k;
    // print_logs(CLI_DEF_LVL, "hello\n");
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

    uint32_t len_chunkRequestWrite = chunk__get_packed_size(args->chunk_list_global->chunks[args->chunk_id]);
    uint32_t payload_size = sizeof(uint8_t) + sizeof(uint32_t) + len_chunkRequestWrite
     + sizeof(uint32_t) + bytes_read;
    payload_size = htonl(payload_size);
    op_type = 'w';

    uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequestWrite * sizeof(uint8_t));
    chunk__pack(args->chunk_list_global->chunks[args->chunk_id], proto_buf);

    for (int i = 0; i < args->n_replicas; i++)
    {
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            print_logs(2, "Chunk id %d, connected to replica %d\n",
                    args->chunk_id, args->replicas[i]->port);
            if ((ret = bulk_write(replicafd, &payload_size, sizeof(payload_size))) == -2 ||
                (ret = bulk_write(replicafd, &op_type, 1)) == -2 ||
                (ret = write_len_and_data(replicafd, len_chunkRequestWrite, proto_buf)) == -2 ||
                (ret = write_len_and_data(replicafd, bytes_read, file_buf)) == -2)
            {
                print_logs(2, "Broken pipe, replica crashed, will try from another replica\n");
                continue;
            }
            
            print_logs(1, "Chunk id %d written to replica IP: %s port: %d\n",
                args->chunk_id, args->replicas[i]->ip, args->replicas[i]->port);

            break;
        }
    }

    free(file_buf);
    free(proto_buf);
    close(replicafd);

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
}

void prepare_uncommitted_chunk(Replica **replicas_from_master, bool *replicas_ack, Chunk **uncommitted_chunks, int chunk_i, int chunk_id, char *path, int n_replicas)
{
    int fail_cnt = 0;
    for (int i = 0; i < n_replicas; i++)
    {
        // if (!replicas_ack[i] || (i != 0 && chunk_id == 53 && (strcmp(path, "1_2GB") == 0 || strcmp(path, "2_2GB") == 0))) // TEMPORARY, TODO: REMOVE || i == 0
        if (!replicas_ack[i])
            fail_cnt++;
    }

    print_logs(0, "prepare_uncommitted_chunk %d, fail_cnt: %d\n", chunk_i, fail_cnt);

    if(fail_cnt == 0)
        return;

    Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));
    chunk__init(chunk);
    uncommitted_chunks[chunk_i] = chunk;

    chunk->n_replicas = fail_cnt;
    chunk->replicas = (Replica **)malloc(fail_cnt * sizeof(Replica *));
    chunk->chunk_id = chunk_id;
    chunk->path = (char *)malloc(strlen(path) + 1);
    strcpy(chunk->path, path);

    int j = 0;
    for (int i = 0; i < n_replicas; i++)
    {
        // if (!replicas_ack[i] || (i != 0 && chunk_id == 53 && (strcmp(path, "1_2GB") == 0 || strcmp(path, "2_2GB") == 0))) // TEMPORARY, TODO: REMOVE || i == 0
        if (!replicas_ack[i])
        {
            Replica *replica = (Replica *)malloc(sizeof(Replica));
            replica__init(replica);
            chunk->replicas[j] = replica;

            replica->ip = (char *)malloc(IP_LENGTH * sizeof(char));
            strcpy(replica->ip, replicas_from_master[i]->ip);
            // replica->ip = replicas_from_master[i]->ip; TODO inspect if this is really wrong
            replica->port = replicas_from_master[i]->port;
            replica->id = replicas_from_master[i]->id;
            j++;
        }
    }

    print_logs(0, "prepare_uncommitted_chunk %d, fail_cnt: %d, after allocating the chunk and replicas\n", chunk_i, fail_cnt);
}

void put_chunk_commit(void *voidPtr)
{
    // sleep(1);
    int k;
    // print_logs(CLI_DEF_LVL, "hello\n");
    int replicafd, net_len, ret = -1;
    size_t bytes_read, proto_len;
    struct sockaddr_in repladdr;
    argsThread_t *args = voidPtr;
    uint8_t op_type;
    bool replicas_ack[REPLICATION_FACTOR] = {false}; // TODO: check if it's good; little wasteful, but should work OK
    // bool replicas_ack[args->chunk_list_global->chunks[args->chunk_i]->n_replicas] = {false}; // TODO: check if it's good

    uint8_t* file_buf = (uint8_t *)malloc(CHUNK_SIZE + 1); // +1 for the '\0'

    if ((bytes_read = pread(args->filefd, file_buf, CHUNK_SIZE, args->offset)) < 0)
        err_n_die("put_chunk read error");

    int modulo = debug_file_size % CHUNK_SIZE;
    if (bytes_read != CHUNK_SIZE && bytes_read != modulo)
        err_n_die("for args->chunk_id: %d, put_chunk pread bytes_read: %d, CHUNK_SIZE: %d\n, modulo: %d\n", 
            args->chunk_id, bytes_read, CHUNK_SIZE, modulo);

    file_buf[bytes_read] = '\0';

    /*
        Client Protocol - Details of the message sent to the replica:
        - Payload size: 4 bytes, total size of the entire message
        - Operation type: 1 byte
        - len_chunkRequestWrite: 4 bytes, specifies the length of the proto buffer
        - proto_buf: (len_chunkRequestWrite) bytes
        - bytes_read: 4 bytes, size of the chunk data
        - file_buf: (bytes_read) bytes, contains the actual file data
    */

    uint32_t len_chunkRequestWrite = chunk__get_packed_size(args->chunk_list_global->chunks[args->chunk_i]);
    uint32_t payload_size = sizeof(uint8_t) + sizeof(uint32_t) + len_chunkRequestWrite
     + sizeof(uint32_t) + bytes_read;
    payload_size = htonl(payload_size);
    op_type = 'w'; // TODO - change to x (have to change in replica as well)

    uint8_t *proto_buf = (uint8_t *)malloc(len_chunkRequestWrite * sizeof(uint8_t));
    chunk__pack(args->chunk_list_global->chunks[args->chunk_i], proto_buf);

    int expected_acks = args->n_replicas;

    for (int i = 0; i < args->n_replicas; i++)
    {
        if ((ret = setup_connection_retry(&replicafd, args->replicas[i]->ip, args->replicas[i]->port)) == 0)
        {
            print_logs(2, "Chunk id %d, connected to replica %d\n",
                    args->chunk_id, args->replicas[i]->port);
            if ((ret = bulk_write(replicafd, &payload_size, sizeof(payload_size))) == -2 ||
                (ret = bulk_write(replicafd, &op_type, 1)) == -2 ||
                (ret = write_len_and_data(replicafd, len_chunkRequestWrite, proto_buf)) == -2 ||
                (ret = write_len_and_data(replicafd, bytes_read, file_buf)) == -2)
            {
                print_logs(2, "Broken pipe, replica crashed, will try from another replica\n");
                continue;
            }
            
            print_logs(1, "Chunk id %d written to replica IP: %s port: %d\n",
                args->chunk_id, args->replicas[i]->ip, args->replicas[i]->port);

            break;
        }
        else
            expected_acks--;
    }

    free(file_buf);
    free(proto_buf);

    if (ret < 0) // <===> expected_acks == 0
    {
        prepare_uncommitted_chunk(args->replicas, replicas_ack, args->uncommitted_chunks, args->chunk_i, args->chunk_id, args->path, args->n_replicas);
        // err_n_die("each replica is dead"); // DONE: don't die send this to master
        return;
    }


    print_logs(0, "Waiting for commit for chunk_id %d\n", args->chunk_id);
    uint32_t payload;
    uint8_t *buffer;

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_MSEC;

    if (setsockopt(replicafd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
        err_n_die("setsockopt timeout error");

    for (int i = 0; i < REPLICATION_FACTOR; i++)
    {
        // TODO: use timeout = read_payload....;
        if(read_payload_and_data(replicafd, &buffer, &payload) == true)
        {
            print_logs(1, "Timeout for chunk %d replica %d\n", args->chunk_id, i);
            // err_n_die("was timeout");
            continue;
        }
        ChunkCommitReport *chunk_commit_report = chunk_commit_report__unpack(NULL, payload, buffer);
        if (chunk_commit_report->is_success)
        {
            print_logs(2, "Received chunk commit report for file %s%d, %d replica, success for IP: %s, port: %d\n",
                   args->path, args->chunk_id, i, chunk_commit_report->ip, chunk_commit_report->port);
            for (int j = 0; j < args->n_replicas; j++)
            {
                if (strcmp(args->replicas[j]->ip, chunk_commit_report->ip) == 0
                    && args->replicas[j]->port == chunk_commit_report->port)
                {
                    replicas_ack[j] = true;
                    break;
                }
            }
        }
        else // SHOULD HANDLE THIS - replica has too little disk space
        {
            err_n_die("Replica sent malicious message - Received chunk commit report for %d replica, FAIL for IP:%s, port: %d\n",
                   i, chunk_commit_report->ip, chunk_commit_report->port);
        }
        free(buffer);
        chunk_commit_report__free_unpacked(chunk_commit_report, NULL);
    }

    print_logs(0, "\n\n\nput_chunk_commit, before calling prepare_uncommitted_chunk\n");
    prepare_uncommitted_chunk(args->replicas, replicas_ack, args->uncommitted_chunks, args->chunk_i, args->chunk_id, args->path, args->n_replicas);

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
            print_logs(CLI_DEF_LVL, "ja ide spac\n");
            pthread_cond_wait(tp_args->cond_main_to_threads, tp_args->mutex); 
            print_logs(CLI_DEF_LVL, "obudzono\n");
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

void threads_process(argsThread_t *argsThread, thread_pool_args_t *thread_pool_args,
                     ChunkList *chunk_list, char *path, int filefd, 
                     Chunk **uncommitted_chunks)
{
    pthread_t tid[MAX_THREADS_COUNT];

    for (int i = 0; i < MAX_THREADS_COUNT; i++)
    {
        if (pthread_create(&tid[i], NULL, &thread_work, (void *)thread_pool_args) != 0)
            perror("pthread_create error"), exit(1);
    }
    
    for (int i = 0; i < chunk_list->n_chunks; i++)
    {
        argsThread[i].chunk_i = i;
        argsThread[i].chunk_id = chunk_list->chunks[i]->chunk_id;
        argsThread[i].path = path;
        argsThread[i].n_replicas = chunk_list->chunks[i]->n_replicas;
        argsThread[i].replicas = chunk_list->chunks[i]->replicas;

        argsThread[i].offset = (int64_t)chunk_list->chunks[i]->chunk_id * CHUNK_SIZE;
        argsThread[i].filefd = filefd;
        argsThread[i].uncommitted_chunks = uncommitted_chunks;

        argsThread[i].chunk_list_global = chunk_list;
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

    print_logs(2, "All threads joined \n");
}

void do_read(char *path)
{
    int                 serverfd, filefd, n, err, bytes_read;

    print_logs(1, "Reading file %s...\n\n", path);

    if ((filefd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    FileRequestRead fileRequestRead = FILE_REQUEST_READ__INIT;
    fileRequestRead.path = path;

    /* Packing protbuf object and 'r' into the buffer */
    uint32_t len_fileRequestRead = file_request_read__get_packed_size(&fileRequestRead) + sizeof(uint8_t);
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestRead * sizeof(uint8_t));
    buffer[0] = 'r';
    file_request_read__pack(&fileRequestRead, buffer + 1);

    setup_connection(&serverfd, master_ip, master_port);

    write_len_and_data(serverfd, len_fileRequestRead, buffer);
    free(buffer);

    uint32_t payload;
    read_payload_and_data(serverfd, &buffer, &payload);

    ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
    if (!chunk_list) // TODO
        err_n_die("chunk_list is null");

    if (!chunk_list->success)
        err_n_die("FAIL: file does not exist\n"); // TODO add abort with cleanup, free everything

    close(serverfd);

    argsThread_t *argsThread = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);
    

    thread_pool_args_t thread_pool_args = {&mutex, &cond_main_to_threads, &cond_thread_to_main,
                        -1, argsThread, true, false, get_chunk};

    threads_process(argsThread, &thread_pool_args, chunk_list, path, filefd, NULL);

    free(argsThread);

    print_logs(1, "\nFile %s read successfully and saved as %s\n", path, OUTPUT_PATH);
    print_logs(3, "Thread memory freed\n");
}

void do_write(char *path)
{
    int err, filefd, serverfd;
    int bytes_read;

    print_logs(1, "Writing file %s...\n\n", path);

    if ((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");

    debug_file_size = file_size(filefd);

    FileRequestWrite fileRequestWrite = FILE_REQUEST_WRITE__INIT;
    fileRequestWrite.path = path;
    fileRequestWrite.size = file_size(filefd);

    /* Packing protbuf object and 'w' into the buffer */
    uint32_t len_fileRequestWrite = file_request_write__get_packed_size(&fileRequestWrite) + sizeof(uint8_t);
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestWrite * sizeof(uint8_t));
    buffer[0] = 'w';
    file_request_write__pack(&fileRequestWrite, buffer + sizeof(uint8_t));

    setup_connection(&serverfd, master_ip, master_port);
        
    write_len_and_data(serverfd, len_fileRequestWrite, buffer);

    free(buffer);

    uint32_t payload;
    read_payload_and_data(serverfd, &buffer, &payload);

    ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
    if (!chunk_list)
        err_n_die("chunk_list is null");
    else
        print_logs(CLI_DEF_LVL, "chunk_list NOT null\n");

    close(serverfd);

    if (!chunk_list->success)
        err_n_die("FAIL: file already exists\n"); // TODO add abort with cleanup, free everything
    
    // chunk_list_global = chunk_list;


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

    threads_process(argsThread, &thread_pool_args, chunk_list, path, filefd, NULL);

    free(argsThread);

    print_logs(1, "\nFile %s written successfully\n", path);
}

ChunkList* prepare_commit_chunk_list(char *path, int n_chunks, Chunk **uncommited_chunks)
{
    ChunkList *commit_chunk_list = (ChunkList *)malloc(sizeof(ChunkList));
    chunk_list__init(commit_chunk_list);
    int uncommited_chunks_cnt = 0;

    for (int i = 0; i < n_chunks; i++)
        if (uncommited_chunks[i])
            uncommited_chunks_cnt++;

    commit_chunk_list->path = (char *)malloc((strlen(path) + 1) * sizeof(char));
    strcpy(commit_chunk_list->path, path);
    commit_chunk_list->n_chunks = uncommited_chunks_cnt; // may be 0, then it's success

    // if (true) // TEMPORARY, TODO: CHANGE THIS BACK
    if (uncommited_chunks_cnt == 0)
    {
        // commit_chunk_list->n_chunks = 0; // normally unnecessary, but need for testing (temporary)
        commit_chunk_list->success = true;
        commit_chunk_list->chunks = NULL;
        return commit_chunk_list;
    }

    commit_chunk_list->success = false;
    commit_chunk_list->chunks = (Chunk **)malloc(uncommited_chunks_cnt * sizeof(Chunk *));
    int it = 0;

    for (int i = 0; i < n_chunks; i++)
        if (uncommited_chunks[i])
            commit_chunk_list->chunks[it++] = uncommited_chunks[i];
    
    return commit_chunk_list;
}

void send_uncommitted_chunks(ChunkList *commit_chunk_list, int serverfd)
{
    uint32_t len_CommitChunkList = chunk_list__get_packed_size(commit_chunk_list) + sizeof(uint8_t);

    print_logs(0, "After chunk_list__get_packed_size\n");

    uint8_t *buffer = (uint8_t *)malloc(len_CommitChunkList * sizeof(uint8_t));

    print_logs(0, "After malloc\n");
    
    buffer[0] = 'c';
    chunk_list__pack(commit_chunk_list, buffer + sizeof(uint8_t));

    print_logs(0, "After chunk_list__pack\n");

    write_len_and_data(serverfd, len_CommitChunkList, buffer); // here i send the len_CommitChunkList and on the server i get client delcared invalid payload size
    
    print_logs(3, "sent %d payload size to master\n", len_CommitChunkList);
    
    free(buffer);
}

void do_write_commit(char *path)
{
    int err, filefd, serverfd;
    int bytes_read;

    print_logs(1, "Writing (write-committed) file %s...\n\n", path);

    if ((filefd = open(path, O_RDONLY)) < 0)
        err_n_die("filefd error");

    debug_file_size = file_size(filefd);

    FileRequestWrite fileRequestWrite = FILE_REQUEST_WRITE__INIT;
    fileRequestWrite.path = path;
    fileRequestWrite.size = file_size(filefd);

    /* Packing protbuf object and 'w' into the buffer */
    uint32_t len_fileRequestWrite = file_request_write__get_packed_size(&fileRequestWrite) + sizeof(uint8_t);
    uint8_t *buffer = (uint8_t *)malloc(len_fileRequestWrite * sizeof(uint8_t));
    buffer[0] = 'x';
    file_request_write__pack(&fileRequestWrite, buffer + sizeof(uint8_t));

    setup_connection(&serverfd, master_ip, master_port); // TODO ogarniecie jak master nie dziala
        
    write_len_and_data(serverfd, len_fileRequestWrite, buffer);

    free(buffer);
    
    uint32_t payload;
    bool committed = false;
    while(!committed)
    {
        read_payload_and_data(serverfd, &buffer, &payload);

        ChunkList *chunk_list = chunk_list__unpack(NULL, payload, buffer);
        if (!chunk_list)
            err_n_die("chunk_list is null");
        else
            print_logs(CLI_DEF_LVL, "chunk_list NOT null\n");

        close(serverfd);
        free(buffer);

        if (!chunk_list->success)
            err_n_die("FAIL: file already exists\n"); // TODO add abort with cleanup, free everything

        argsThread_t *argsThread = (argsThread_t *)malloc(sizeof(argsThread_t) * chunk_list->n_chunks);

        thread_pool_args_t thread_pool_args = {&mutex, &cond_main_to_threads, &cond_thread_to_main,
                                               -1, argsThread, true, false, put_chunk_commit};

        Chunk **uncommited_chunks = (Chunk **)malloc(chunk_list->n_chunks * sizeof(Chunk *));

        for (int i = 0; i < chunk_list->n_chunks; i++)
            uncommited_chunks[i] = NULL;

        threads_process(argsThread, &thread_pool_args, chunk_list, path, filefd, uncommited_chunks);
        free(argsThread);

        for (int i = 0; i < chunk_list->n_chunks; i++)
        {
            if (uncommited_chunks[i])
            {
                print_logs(1, "\nChunk %d is not fully commited\nFaulty replicas (%d):\n", 
                                chunk_list->chunks[i]->chunk_id, uncommited_chunks[i]->n_replicas);
                for (int j = 0; j < uncommited_chunks[i]->n_replicas; j++)
                {
                    print_logs(1, "IP: %s, Port: %d\n",
                               uncommited_chunks[i]->replicas[j]->ip, uncommited_chunks[i]->replicas[j]->port);
                }
            }
            else
            {
                print_logs(1, "\nChunk %d fully commited\n", chunk_list->chunks[i]->chunk_id);
            }
        }

        print_logs(0, "After prints\n");

        ChunkList *commit_chunk_list = prepare_commit_chunk_list(path, chunk_list->n_chunks, uncommited_chunks);

        print_logs(0, "After prepare_commit_chunk_list\n");
        
        committed = commit_chunk_list->success;

        setup_connection(&serverfd, master_ip, master_port);

        print_logs(0, "After setup_connection\n");

        send_uncommitted_chunks(commit_chunk_list, serverfd);

        print_logs(0, "After send_uncommitted_chunks\n");

        // for (int i = 0; i < commit_chunk_list->n_chunks; i++)
        //     free(commit_chunk_list->chunks[i]);
        free(commit_chunk_list->path);
        free(commit_chunk_list);

        print_logs(0, "After free(commit_chunk_list->path); free(commit_chunk_list);\n");

        for (int i = 0; i < chunk_list->n_chunks; i++)
        {
            if (uncommited_chunks[i])
            {
                // for(int j = 0; j < uncommited_chunks[i]->n_replicas; j++)
                // {
                //     free(uncommited_chunks[i]->replicas[j]->ip);
                //     free(uncommited_chunks[i]->replicas[j]);
                // }
                    
                free(uncommited_chunks[i]);

                print_logs(0, "After free(uncommited_chunks[%d])\n", i);
            }
                
        }
        free(uncommited_chunks);

        print_logs(0, "After free(uncommited_chunks)\n");

        chunk_list__free_unpacked(chunk_list, NULL);

        print_logs(0, "After chunk_list__free_unpacked\n");
    }

    print_logs(1, "\nFile %s written (write-committed) successfully\n", path);
}

void print_help()
{
    print_logs(0, "Usage:\n./client operation file   (when master ip is 127.0.0.1 and port 9001)\nOR\n./client master_ip master_port op file\n");
    print_logs(0, "operation is read | write | write-committed\n");
}

int main(int argc, char **argv)
{
    char operation[20]; //TODO refactor
    char path[256];

#ifdef RELEASE
    if (argc != 6)
        err_n_die("Error: Invalid parameters. Please provide the Master IP, Master port, operation type, file path.");
    strcpy(master_ip, argv[1]);
    master_port = atoi(argv[2]);
    strcpy(operation, argv[3]);
    strcpy(path, argv[4]);
#else
    if (argc == 5) // ./client master_ip master_port op file
    {
        strcpy(master_ip, argv[1]);
        master_port = atoi(argv[2]);
        strcpy(operation, argv[3]);
        strcpy(path, argv[4]);
    }
    else if (argc == 3) // ./client operation file
    {
        strcpy(operation, argv[1]);
        strcpy(path, argv[2]);
    }
    else if(argc == 2)
        print_help();
    else
        err_n_die("usage: parameters error");
#endif 

    signal(SIGPIPE, SIG_IGN);

    if (strcmp(operation, "read") == 0)
        do_read(path);
    else if (strcmp(operation, "write") == 0)
        do_write(path);
    else if(strcmp(operation, "write-committed") == 0)
        do_write_commit(path);
    else
        err_n_die("usage: wrong client request");

    print_logs(2, "\nClient application ended successfully\n");
}
