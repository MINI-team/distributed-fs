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

void setupReplicas(Replica **replicas)
{
    // char* replica_ip = resolve_host(REPLICA_ADDRESS);
    replicas[0] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[0]);
    replicas[0]->ip = "127.0.0.1";
    // replicas[0]->ip = replica_ip;
    replicas[0]->port = 8080;

    replicas[1] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[1]);
    replicas[1]->ip = "127.0.0.1";
    // replicas[1]->ip = replica_ip;
    replicas[1]->port = 8081;
    
    replicas[2] = (Replica *)malloc(sizeof(Replica));
    replica__init(replicas[2]);
    // replicas[2]->ip = "127.0.0.1";
    replicas[2]->port = 8082;
}
void handle_new_connection(int epoll_fd, int server_socket)
{   
    int client_socket;
    printf("New client connected\n");
    if ((client_socket = accept(server_socket, (SA *)NULL, NULL)) < 0)
    {
        printf("Server couldnt accept client\n");
        return;
    }

    event_data_t *client_event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!client_event_data)
        err_n_die("malloc error");

    client_data_t *client_data = (client_data_t *)malloc(sizeof(client_data_t));
    if (!client_data)
        err_n_die("malloc error");

    client_data->client_socket = client_socket;
    client_data->buffer = (uint8_t *)malloc((SINGLE_CLIENT_BUFFER_SIZE + 1) * sizeof(uint8_t));
    client_data->payload_size = 0;
    client_data->bytes_stored = 0;
    client_data->space_left = SINGLE_CLIENT_BUFFER_SIZE;
    client_data->reading_started = false;
    
    client_event_data->is_server = 0;
    client_event_data->client_data = client_data;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = client_event_data; 

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event))
        err_n_die("epoll_ctl error"); 

    set_fd_nonblocking(client_socket);
}

void add_file(char* path, int64_t size, replica_info_t **all_replicas, GHashTable *hash_table)
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
        chunk->n_replicas = 3;
        chunk->replicas = (Replica **)malloc(3 * sizeof(Replica *));

        // Replica *replicas[3];
        for (int j = 0; j < 3; j++)
        {
            
            Replica *replica = (Replica *)malloc(sizeof(Replica));
            replica__init(replica);
            
            int rand_ind;
            rand_ind = 0;
            // if (j == 0)
            //     rand_ind = 0;
            // else
            //     rand_ind = rand() % (REPLICAS_COUNT - 1) + 1;
            
            // printf("rand_ind: %d \n", rand_ind);
            replica->ip = (char *)malloc(IP_LENGTH * sizeof(char));
            strcpy(replica->ip, all_replicas[rand_ind]->ip);
            replica->port = all_replicas[rand_ind]->port;
            replica->is_primary = (j == 0);

            chunk->replicas[j] = replica;
        }
        
        chunk_list->chunks[i] = chunk;
    }

    g_hash_table_insert(hash_table, strdup(path), chunk_list);
}

void process_request(int epoll_fd, event_data_t *event_data, replica_info_t **all_replicas, GHashTable *hash_table)
{
    char request_type = event_data->client_data->buffer[0];
    if (request_type == 'w')
    {
        printf("write request detected \n");
        printf("event_data->client_data->payload_size - 1: %d\n", event_data->client_data->payload_size - 1);
        printf("halo\n");
        FileRequestWrite *fileRequestWrite = file_request_write__unpack(
            NULL,
            event_data->client_data->payload_size - 1,
            event_data->client_data->buffer + 1
        );
        if (!fileRequestWrite)
            err_n_die("ups");
        printf("fileRequestWrite->path: %s\n", fileRequestWrite->path);
        printf("fileRequestWrite->size: %ld\n", fileRequestWrite->size);
        add_file(fileRequestWrite->path, fileRequestWrite->size, all_replicas, hash_table);

        ChunkList* chunk_list = g_hash_table_lookup(hash_table, fileRequestWrite->path);
        if (chunk_list)
        {
            printf("oho i hit client: %d\n", event_data->client_data->client_socket);

            int32_t chunk_list_len = chunk_list__get_packed_size(chunk_list);
            printf("I will send this client chunk_list_len=%lu bytes\n", sizeof(chunk_list_len));
            uint8_t *buffer = (uint8_t *)malloc(chunk_list_len * sizeof(uint8_t));
            chunk_list__pack(chunk_list, buffer);

            int32_t net_chunk_list_len = ntohl(chunk_list_len);
            if (write(event_data->client_data->client_socket, &net_chunk_list_len, sizeof(net_chunk_list_len)) != sizeof(net_chunk_list_len))
                err_n_die("write len error");

            printf("i will send this client chunk_list_len=%d bytes\n", chunk_list_len);

            int32_t total_bytes_written = 0;
            int32_t bytes_written;
            while (total_bytes_written < chunk_list_len)
            {
                bytes_written = write(event_data->client_data->client_socket, buffer + total_bytes_written, chunk_list_len - total_bytes_written);
                if (bytes_written < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // printf("EAGAIN/EWOULDBLOK\n");
                        continue;
                    }
                    err_n_die("read error");
                }

                    total_bytes_written += bytes_written;
                    printf("bytes_written=:%d\n", bytes_written);
                    printf("total_bytes_written:%d\n", total_bytes_written);
                }
            printf("server sent the response for write request\n");
        }
        else
        {
            printf("not found \n");
        }
    }
    else if (request_type == 'r')
    {
        printf("read request detected \n"); 
        FileRequestRead *FileRequestRead = file_request_read__unpack(NULL, event_data->client_data->payload_size - 1, event_data->client_data->buffer + 1);
        printf("fileRequest->path: %s\n", FileRequestRead->path);
        ChunkList* chunk_list = g_hash_table_lookup(hash_table, FileRequestRead->path);
        if (chunk_list)
        {
            size_t chunk_list_len = chunk_list__get_packed_size(chunk_list);
            printf("I will send this client chunk_list_len=%lu bytes\n", sizeof(chunk_list_len)); // not needed
            uint8_t *buffer = (uint8_t *)malloc(chunk_list_len * sizeof(uint8_t));
            chunk_list__pack(chunk_list, buffer);

            // possibly bulk this
            int32_t net_chunk_list_len = ntohl(chunk_list_len);
            if (write(event_data->client_data->client_socket, &net_chunk_list_len, sizeof(net_chunk_list_len)) != sizeof(net_chunk_list_len))
                err_n_die("write error");

            printf("i will send this client chunk_list_len=%d bytes\n", chunk_list_len);

            int32_t total_bytes_written = 0;
            int32_t bytes_written;
            while (total_bytes_written < chunk_list_len) // should be bulk_write
            {
                bytes_written = write(event_data->client_data->client_socket, buffer + total_bytes_written, chunk_list_len - total_bytes_written);
                if (bytes_written < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // printf("EAGAIN/EWOULDBLOK\n");
                        continue;
                    }
                    err_n_die("read error");
                }
                    total_bytes_written += bytes_written;
                    printf("bytes_written=:%d\n", bytes_written);
                    printf("total_bytes_written:%d\n", total_bytes_written);
            }

            printf("server sent the response for read request\n");
        }
        else
        {
            printf("not found \n");
        }
    }
    else
    {
        printf("request rejected \n");
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_data->client_data->client_socket, NULL))
            err_n_die("epoll_ctl error");
        close(event_data->client_data->client_socket);
        free(event_data->client_data->buffer);
        free(event_data->client_data);
        free(event_data);
        return;
    }

    // FileRequest *fileRequest = file_request__unpack(NULL, event_data->client_data->payload_size - 1, event_data->client_data->buffer + 1);
    // printf("fileRequest->path: %s\n", fileRequest->path);
}

void disconnect_client(int epoll_fd, event_data_t *event_data, int client_socket)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL))
        err_n_die("epoll_ctl error");

    free(event_data->client_data->buffer);
    free(event_data->client_data);
    free(event_data);
    close(client_socket);
}

void handle_new_client_payload_declaration(int epoll_fd, event_data_t *event_data)
{
    int client_socket = event_data->client_data->client_socket;
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
    event_data->client_data->reading_started = true;
    event_data->client_data->payload_size = ntohl(network_payload_size);
    
    if (event_data->client_data->payload_size <= 0 ||
        event_data->client_data->payload_size > SINGLE_CLIENT_BUFFER_SIZE)
    {
        printf("Client declared invalid payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    printf("Client configured, declared payload size: %d bytes\n", event_data->client_data->payload_size);
}

void handle_client(int epoll_fd, event_data_t *event_data, replica_info_t **all_replicas, GHashTable *hash_table)
{
    int client_socket = event_data->client_data->client_socket;
    int bytes_read;

    if (event_data->client_data->reading_started == false)
    {
        handle_new_client_payload_declaration(epoll_fd, event_data);
        return;
    }

    printf("handle_client, client_socket: %d, payload: %d\n", client_socket, event_data->client_data->payload_size);

    bytes_read = read(client_socket, event_data->client_data->buffer + event_data->client_data->bytes_stored,
        event_data->client_data->space_left);

    if (bytes_read == 0)
    {
        printf("Client disconnected \n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    printf("handle_client, bytes_read: %d\n", bytes_read);

    event_data->client_data->space_left -= bytes_read;
    event_data->client_data->bytes_stored += bytes_read;

    if (event_data->client_data->bytes_stored == event_data->client_data->payload_size)
        process_request(epoll_fd, event_data, all_replicas, hash_table);
    else if (event_data->client_data->bytes_stored > event_data->client_data->payload_size)    /*multi-queries clients - TODO*/
        err_n_die("undefined");
}

int main()
{   
    srand(time(NULL));
    GHashTable          *hash_table = g_hash_table_new(g_str_hash, g_str_equal);
    int                 server_socket;
    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];
    replica_info_t      *all_replicas[5]; // these are all replicas master knows


    initialize_demo_replicas(all_replicas);
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
                printf("client event\n");
                handle_client(epoll_fd, event_data, all_replicas, hash_table);                
            }
        }
    }
    close(epoll_fd);
    close(server_socket);
}

void initialize_demo_replicas(replica_info_t **all_replicas)
{
    for (int i = 0; i < 5; i++) {
        all_replicas[i] = (replica_info_t *)malloc(sizeof(replica_info_t));
        all_replicas[i]->ip = (char *)malloc(16 * sizeof(char)); // Allocating memory for IP
    }

#ifdef DOCKER
    strcpy(all_replicas[0]->ip, resolve_host(REPLICA_SERVER_IP_0));
    all_replicas[0]->port = REPLICA_SERVER_PORT_0;

    strcpy(all_replicas[1]->ip, resolve_host(REPLICA_SERVER_IP_1));
    all_replicas[1]->port = REPLICA_SERVER_PORT_1;

    strcpy(all_replicas[2]->ip, resolve_host(REPLICA_SERVER_IP_2));
    all_replicas[2]->port = REPLICA_SERVER_PORT_2;

    strcpy(all_replicas[3]->ip, resolve_host(REPLICA_SERVER_IP_3));
    all_replicas[3]->port = REPLICA_SERVER_PORT_3;

    strcpy(all_replicas[4]->ip, resolve_host(REPLICA_SERVER_IP_4));
    all_replicas[4]->port = REPLICA_SERVER_PORT_4;
#else
    strcpy(all_replicas[0]->ip, REPLICA_SERVER_IP_0);
    all_replicas[0]->port = REPLICA_SERVER_PORT_0;

    strcpy(all_replicas[1]->ip, REPLICA_SERVER_IP_1);
    all_replicas[1]->port = REPLICA_SERVER_PORT_1;

    strcpy(all_replicas[2]->ip, REPLICA_SERVER_IP_2);
    all_replicas[2]->port = REPLICA_SERVER_PORT_2;

    strcpy(all_replicas[3]->ip, REPLICA_SERVER_IP_3);
    all_replicas[3]->port = REPLICA_SERVER_PORT_3;

    strcpy(all_replicas[4]->ip, REPLICA_SERVER_IP_4);
    all_replicas[4]->port = REPLICA_SERVER_PORT_4;
#endif
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
