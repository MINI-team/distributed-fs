#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

#define OUTPUT_PATH "output.txt"
#define CLIENT_DEBUG_PATH "client.log"

typedef struct argsThread
{
    pthread_t tid;
    char *path;
    int chunk_id;
    int chunk_i;

    // Replicas data
    int n_replicas;
    char *ip;
    uint16_t port;
    Replica **replicas;

    int64_t offset;
    int filefd;

    int64_t filesize; // for debug reasons, to be deleted

    Chunk **uncommitted_chunks; // array of Chunk pointers, each by default set to NULL

    ChunkList *chunk_list_global;

} argsThread_t;

typedef struct thread_pool_args
{
    pthread_mutex_t *mutex;
    pthread_cond_t *cond_main_to_threads;
    pthread_cond_t *cond_thread_to_main;   

    int current_chunk;
    argsThread_t *argsThread;
    bool work_taken;
    bool work_finished;
    void (*process_chunk)(void *); // put_chunk or get_chunk or put_chunk_committed

} thread_pool_args_t;

int64_t file_size(int filefd);

#endif
