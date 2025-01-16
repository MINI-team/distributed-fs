#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <limits.h>
#include <string.h>
#include "common.h"
#include "dfs.pb-c.h"

#include "server.h"

#define MAX_EVENTS 10

void handle_new_connection(int epoll_fd, int server_socket)
{   
    int client_socket;
    printf("New client connected\n");
    if ((client_socket = accept(server_socket, (SA *)NULL, NULL)) < 0)
    {
        // printf("Server couldnt accept client\n");
        err_n_die("Server couldnt accept client");
        // return;
    }

    event_data_t *client_event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!client_event_data)
        err_n_die("malloc error");

    peer_data_t *peer_data = (peer_data_t *)malloc(sizeof(peer_data_t));
    if (!peer_data)
        err_n_die("malloc error");

    peer_data->client_socket = client_socket;
    peer_data->buffer = (uint8_t *)malloc((SINGLE_CLIENT_BUFFER_SIZE + 1) * sizeof(uint8_t));
    peer_data->payload_size = 0;
    peer_data->bytes_stored = 0;
    peer_data->space_left = SINGLE_CLIENT_BUFFER_SIZE;
    peer_data->reading_started = false;
    
    client_event_data->is_server = 0;
    client_event_data->peer_data = peer_data;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = client_event_data; 

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event))
        err_n_die("epoll_ctl error"); 

    set_fd_nonblocking(client_socket);
}

void setup_outbound(int epoll_fd, event_data_t *event_data, ChunkList *chunk_list)
{
    uint32_t chunk_list_len = chunk_list__get_packed_size(chunk_list);
    printf("I will send this client chunk_list_len=%lu bytes\n", sizeof(chunk_list_len));
    uint8_t *buffer = (uint8_t *)malloc(chunk_list_len * sizeof(uint8_t));
    chunk_list__pack(chunk_list, buffer);

    uint32_t chunk_list_net_len = htonl(chunk_list_len);
    uint32_t out_payload_size = sizeof(uint32_t) + chunk_list_len;
    printf("chunk_list_len: %d\n", chunk_list_len);

    event_data->peer_data->out_payload_size = out_payload_size;
    event_data->peer_data->out_buffer = (uint8_t *)malloc(out_payload_size * sizeof(uint8_t));
    memcpy(event_data->peer_data->out_buffer, &chunk_list_net_len, sizeof(uint32_t));
    memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t), buffer, chunk_list_len);
    event_data->peer_data->bytes_sent = 0;
    event_data->peer_data->left_to_send = out_payload_size;

    event_data->is_server = 0;

    struct epoll_event event;
    event.events = EPOLLOUT;
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
        err_n_die("unable to add EPOLLOUT");

    printf("from now on it's EPOLLOUT\n");

    free(buffer);
}

void add_file(char* path, int64_t size, int replicas_count, replica_info_t **all_replicas, GHashTable *hash_table)
{
    int chunks_number = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    printf("add_file, chunks_number=%d\n", chunks_number);

    ChunkList *chunk_list = (ChunkList *)malloc(sizeof(ChunkList));
    chunk_list__init(chunk_list);
    chunk_list->success = 1;
    chunk_list->n_chunks = chunks_number;
    chunk_list->chunks = (Chunk **)malloc(chunks_number * sizeof(Chunk *));

    for (int i = 0; i < chunks_number; i++)
    {
        Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));
        chunk__init(chunk);
        chunk->chunk_id = i;
        chunk->path = (char *)malloc(MAX_FILENAME_LENGTH * sizeof(char)); // to be modified (we shouldnt be using constant length)
        strcpy(chunk->path, path);
        chunk->n_replicas = REPLICATION_FACTOR;
        chunk->replicas = (Replica **)malloc(REPLICATION_FACTOR * sizeof(Replica *));

        // Replica *replicas[REPLICATION_FACTOR];
        for (int j = 0; j < REPLICATION_FACTOR; j++)
        {
            
            Replica *replica = (Replica *)malloc(sizeof(Replica));
            replica__init(replica);
            
            int rand_ind;
            // rand_ind = 0;
            // rand_ind = i % 2;

            // if (j == 0 || j == 1)
            //     rand_ind = i % 2;
            // if (j == 2)
            //     rand_ind = (i+1) % 2;
            
            // if (j == 0 || j == 1)
            //     rand_ind = 0;
            // if (j == 2)
            //     rand_ind = 1;

            // NIE JEBANY RAND - MOŻE ON DAĆ TĘ SAMĄ REPLIKĘ JAKO WSZYSTKIE REPLIKI DANEGO CHUNKU
            if (j == 0)
                rand_ind = i % replicas_count;
            if (j == 1)
                rand_ind = (i + 1) % replicas_count;

            replica->ip = (char *)malloc(IP_LENGTH * sizeof(char));
            strcpy(replica->ip, all_replicas[rand_ind]->ip);
            replica->port = all_replicas[rand_ind]->port;
            replica->is_primary = (j == 0);

            chunk->replicas[j] = replica;
            printf("Chunkowi %d przeydzielono replike %d\n", i, all_replicas[rand_ind]->port);
        }
        
        chunk_list->chunks[i] = chunk;
    }

    g_hash_table_insert(hash_table, strdup(path), chunk_list);
}

void process_request(int epoll_fd, event_data_t *event_data, int *replicas_count, replica_info_t **all_replicas, GHashTable *hash_table)
{
    char request_type = event_data->peer_data->buffer[0];

    printf("request type: %c\n", request_type);

    if (request_type == 'w')
    {
        printf("write request detected \n");
        printf("event_data->peer_data->payload_size - 1: %d\n", event_data->peer_data->payload_size - 1);
        printf("halo\n");
        FileRequestWrite *fileRequestWrite = file_request_write__unpack(
            NULL,
            event_data->peer_data->payload_size - 1,
            event_data->peer_data->buffer + 1
        );
        if (!fileRequestWrite)
            err_n_die("ups");
        printf("fileRequestWrite->path: %s\n", fileRequestWrite->path);
        printf("fileRequestWrite->size: %ld\n", fileRequestWrite->size);
        add_file(fileRequestWrite->path, fileRequestWrite->size, *replicas_count, all_replicas, hash_table);

        ChunkList* chunk_list = g_hash_table_lookup(hash_table, fileRequestWrite->path);
        if (chunk_list)
        {
            printf("oho i hit client: %d\n", event_data->peer_data->client_socket);

            setup_outbound(epoll_fd, event_data, chunk_list);
        }
        else
        {
            printf("not found \n");
        }
    }
    else if (request_type == 'r')
    {
        printf("read request detected \n"); 
        FileRequestRead *FileRequestRead = file_request_read__unpack(NULL, event_data->peer_data->payload_size - 1, event_data->peer_data->buffer + 1);
        printf("fileRequest->path: %s\n", FileRequestRead->path);
        ChunkList* chunk_list = g_hash_table_lookup(hash_table, FileRequestRead->path);
        
        if (chunk_list)
        {
            setup_outbound(epoll_fd, event_data, chunk_list);
        }
        else
        {
            printf("not found \n");
        }
    }
    else if(request_type == 'n')
    {
        printf("new replica request detected \n");
        NewReplica *replica = new_replica__unpack(NULL, event_data->peer_data->payload_size - 1, event_data->peer_data->buffer + 1);

        printf("ip: %s\n", replica->ip);
        printf("port: %d\n", replica->port);
        // calloc()
        all_replicas[*replicas_count] = (replica_info_t *)malloc(sizeof(replica_info_t));
        all_replicas[*replicas_count]->ip = (char *)malloc(IP_LENGTH * sizeof(char)); // Allocating memory for IP

        strcpy(all_replicas[*replicas_count]->ip, replica->ip);
        all_replicas[*replicas_count]->port = replica->port;
        
        (*replicas_count)++;

        printf("new replica added\n");
    }
    else
    {
        printf("request rejected \n");
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_data->peer_data->client_socket, NULL))
            err_n_die("epoll_ctl error");
        close(event_data->peer_data->client_socket);
        free(event_data->peer_data->buffer);
        free(event_data->peer_data);
        free(event_data);
        return;
    }

    // FileRequest *fileRequest = file_request__unpack(NULL, event_data->peer_data->payload_size - 1, event_data->peer_data->buffer + 1);
    // printf("fileRequest->path: %s\n", fileRequest->path);
}

void disconnect_client(int epoll_fd, event_data_t *event_data, int client_socket)
{
    printf("DISCONNECT\n");
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL))
        err_n_die("epoll_ctl error");

    free(event_data->peer_data->buffer);
    if (event_data->peer_data->out_buffer)
    {
        free(event_data->peer_data->out_buffer);
        event_data->peer_data->out_buffer = NULL;
    }
    free(event_data->peer_data);
    free(event_data);
    close(client_socket);
}

void write_to_client(int epoll_fd, event_data_t *client_event_data)
{
    int bytes_written = bulk_write_nonblock(client_event_data->peer_data->client_socket,
        client_event_data->peer_data->out_buffer,
        &(client_event_data->peer_data->bytes_sent),
        &(client_event_data->peer_data->left_to_send)
    );

    // int bytes_written = bulk_write_nonblock(client_event_data->peer_data);

    if (bytes_written == -1)
        return;
    if (bytes_written == client_event_data->peer_data->out_payload_size)
        disconnect_client(epoll_fd, client_event_data, client_event_data->peer_data->client_socket);
    else
        err_n_die("NIGGA WHAAT THE FUUUUUUUUUUUUUUUUUUCK");
}

void handle_new_client_payload_declaration(int epoll_fd, event_data_t *event_data)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;
    int32_t network_payload_size;
    
    bytes_read = read(client_socket, &network_payload_size, sizeof(network_payload_size));
    printf("handle_new_client_payload_declaration, bytes_read: %d\n", bytes_read);
    if (bytes_read < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            printf("EAGAIN/EWOULDBLOK\n");
            return;
        }
        err_n_die("read error");
    }
    if (bytes_read < sizeof(network_payload_size))
    {
        /*
            Disconnect the client if the payload size cannot be fully read
            Typically, a zero-byte read indicates the client has disconnected
        */
        printf("Client disconnected or sent incomplete payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    /* At this point we know we have a new client who declared their payload */
    event_data->peer_data->reading_started = true;
    event_data->peer_data->payload_size = ntohl(network_payload_size);
    
    if (event_data->peer_data->payload_size <= 0 ||
        event_data->peer_data->payload_size > SINGLE_CLIENT_BUFFER_SIZE)
    {
        printf("Client declared invalid payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    printf("Client configured, declared payload size: %d bytes\n", event_data->peer_data->payload_size);
}

void handle_client(int epoll_fd, event_data_t *event_data, int *replicas_count, replica_info_t **all_replicas, GHashTable *hash_table)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;

    if (event_data->peer_data->reading_started == false)
    {
        handle_new_client_payload_declaration(epoll_fd, event_data);
        return;
    }

    printf("handle_client, client_socket: %d, payload: %d\n", client_socket, event_data->peer_data->payload_size);

    bytes_read = read(client_socket, event_data->peer_data->buffer + event_data->peer_data->bytes_stored,
        event_data->peer_data->space_left);

    if (bytes_read == 0)
    {
        printf("Client disconnected \n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    printf("handle_client, bytes_read: %d\n", bytes_read);

    event_data->peer_data->space_left -= bytes_read;
    event_data->peer_data->bytes_stored += bytes_read;

    if (event_data->peer_data->bytes_stored == event_data->peer_data->payload_size)
        process_request(epoll_fd, event_data, replicas_count, all_replicas, hash_table);
    else if (event_data->peer_data->bytes_stored > event_data->peer_data->payload_size)    /*multi-queries clients - TODO*/
        err_n_die("undefined");
}

int main()
{   
    srand(time(NULL));
    GHashTable          *hash_table = g_hash_table_new(g_str_hash, g_str_equal);
    int                 server_socket;
    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];
    int                 replicas_count = 0;
    replica_info_t      *all_replicas[1000]; // these are all replicas master knows


    // initialize_demo_replicas(all_replicas);
    server_setup(&server_socket, &epoll_fd, &event);

    while (running) 
    {
        printf("\n Server polling for events \n");
        
        // MAX_EVENTS: 1000, przyjdzie na raz 30 
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); //connect
        printf("Ready events: %d \n", event_count);

        for (int i = 0; i < event_count; i++) 
        {
            event_data_t *event_data = (event_data_t *)events[i].data.ptr;

            if (event_data->is_server)
            {   
                printf("server event\n");
                handle_new_connection(epoll_fd, server_socket);
            }
            else
            {
                
                if (events[i].events & EPOLLIN)
                {
                    printf("client event EPOLLIN triggered\n");
                    handle_client(epoll_fd, event_data, &replicas_count, all_replicas, hash_table);
                }
                else if (events[i].events & EPOLLOUT)
                {
                    printf("client event EPOLLOUT triggered\n");
                    write_to_client(epoll_fd, event_data);
                } 
                else
                {
                    err_n_die("SHOULDNT HAPPEN!!!");
                }
            }
        }
    }
    close(epoll_fd);
    close(server_socket);
}

void server_setup(int *server_socket, int *epoll_fd, struct epoll_event *event)
{
    struct sockaddr_in servaddr;

    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    int option = 1;
    if (setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        err_n_die("setsockopt error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(MASTER_SERVER_PORT);

    if (bind(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("bind error");
    
    if (listen(*server_socket, 4096) < 0)
        err_n_die("listen error");

    set_fd_nonblocking(*server_socket);
    
    if ((*epoll_fd = epoll_create1(0)) < 0)
        err_n_die("epoll_create1 error");

    event_data_t *server_event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!server_event_data)
        err_n_die("malloc error");

    server_event_data->is_server = 1;
    server_event_data->server_socket = *server_socket;

    event->events = EPOLLIN;
    event->data.ptr = server_event_data;

    if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, *server_socket, event) < 0)
        err_n_die("epoll_ctl error");
}
