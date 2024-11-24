#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <sys/epoll.h>

#include "common.h"
#include "dfs.pb-c.h"

#define MAX_EVENTS 5
#define READ_SIZE 1024

#define MAX_REPLICAS 10
#define MAX_CHUNKS 10

void initialize_replicas(replica_info **all_replicas)
{
    all_replicas[0] = (replica_info *)malloc(sizeof(replica_info));
    replica__init(all_replicas[0]);
    all_replicas[0]->ip = "127.0.0.1";
    all_replicas[0]->port = 8080;

    all_replicas[1] = (replica_info *)malloc(sizeof(replica_info));
    replica__init(all_replicas[1]);
    all_replicas[1]->ip = "127.0.0.1";
    all_replicas[1]->port = 8081;

    all_replicas[2] = (replica_info *)malloc(sizeof(replica_info));
    replica__init(all_replicas[2]);
    all_replicas[2]->ip = "127.0.0.1";
    all_replicas[2]->port = 8082;

    all_replicas[3] = (replica_info *)malloc(sizeof(replica_info));
    replica__init(all_replicas[3]);
    all_replicas[3]->ip = "127.0.0.1";
    all_replicas[3]->port = 8083;

    all_replicas[4] = (replica_info *)malloc(sizeof(replica_info));
    replica__init(all_replicas[4]);
    all_replicas[4]->ip = "127.0.0.1";
    all_replicas[4]->port = 8084;
}

void setupReplicas(Replica **replicas)
{
    replicas[0] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[0]);
    replicas[0]->ip = "127.0.0.1";
    replicas[0]->port = 8080;

    replicas[1] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[1]);
    replicas[1]->ip = "127.0.0.1";
    replicas[1]->port = 8081;
    
    replicas[2] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[2]);
    replicas[2]->ip = "127.0.0.1";
    replicas[2]->port = 8082;
}

void setupChunks(Chunk **chunks, Replica **replicas)
{
    chunks[0] = (Chunk *)malloc(sizeof(Chunk));
    chunk__init(chunks[0]);
    chunks[0]->chunk_id = 1;
    chunks[0]->replicas = replicas;
    chunks[0]->n_replicas = 2;

    chunks[1] = (Chunk *)malloc(sizeof(Chunk));
    chunk__init(chunks[1]);
    chunks[1]->chunk_id = 2;
    chunks[1]->replicas = replicas;
    chunks[1]->n_replicas = 2;
}

int main()
{   
    GHashTable *hash_table = g_hash_table_new(g_str_hash, g_str_equal);
    int                 server_socket, client_socket, n;
    struct sockaddr_in  servaddr;
    size_t              bytes_read;
    uint8_t             read_buffer[READ_SIZE + 1];        

    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];

    replica_info        *all_replicas[MAX_REPLICAS]; // this are all replicas master knows

    Replica             *replicas[3]; // this an array of replicas we send tot the client
    int                 replicas_count = 3;

    Chunk               *chunks[2];
    int                 chunks_count = 2;

    ChunkList           chunkList = CHUNK_LIST__INIT;

    


    initialize_replicas(all_replicas);
    // setupReplicas(replicas);
    setupChunks(chunks, replicas);    

    chunkList.success = 1;
    chunkList.chunks = chunks;
    chunkList.n_chunks = chunks_count;


    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    int option = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        err_n_die("setsockopt error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("bind error");
    
    if (listen(server_socket, 10) < 0)
        err_n_die("listen error");
    
    if ((epoll_fd = epoll_create1(0)) < 0)
        err_n_die("epoll_create1 error");

    event.events = EPOLLIN;
    event.data.fd = server_socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &event) < 0)
        err_n_die("epoll_ctl error");

    event.data.fd = 0;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 0, &event) < 0)
        err_n_die("epoll_ctl error");

    while (running) {
        printf("Server polling for events \n");
        
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        printf("Ready events: %d \n", event_count);

        for (int i = 0; i < event_count; i++) {
            printf("Reading file descriptor: %d\n", events[i].data.fd);

            if (events[i].data.fd == server_socket)
            {   
                /* New client handling */


                uint32_t net_len;
                
                read(client_socket, &net_len, sizeof(net_len));

                uint32_t len = ntohl(net_len);

                uint8_t *client_read_buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
                if(!client_read_buffer) {
                    perror("malloc failed");
                    close(client_socket);
                    continue;
                }

                read(client_socket, client_read_buffer, len);

                FileRequest *fileRequest = file_request__unpack(NULL, len, client_read_buffer);

                printf("fileRequest->path: %s\n", fileRequest->path);
                free(client_read_buffer);

                size_t chunkList_len = chunk_list__get_packed_size(&chunkList);
                uint8_t *buffer = (uint8_t *)malloc(chunkList_len * sizeof(uint8_t));
                chunk_list__pack(&chunkList, buffer);

                if (write(client_socket, buffer, chunkList_len) != chunkList_len)
                    err_n_die("write error");

                close(client_socket);
            }
            else if (events[i].data.fd != 0)
            {
                
            }
            else if (events[i].data.fd == 0)
            {
                running = 0;
                break;
            }
        }
    }
    close(epoll_fd);
    close(server_socket);
}