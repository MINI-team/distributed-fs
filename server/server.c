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

// int replica_robin_index = 0;
// int total_alive_replicas = 0;

int master_port = 9001;
char master_ip[IP_LENGTH] = "127.0.0.1";

void handle_new_connection(int epoll_fd, int server_socket)
{   
    int client_socket;
    print_logs(1, "\n============================================================================\n");
    print_logs(1, "New client connected\n");
    if ((client_socket = accept(server_socket, (SA *)NULL, NULL)) < 0)
    {
        err_n_die("Server couldnt accept client");
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
    if (!chunk_list)
    {
        print_logs(MAS_DEF_LVL, "Sending empty chunk_list\n");
        chunk_list = (ChunkList *)malloc(sizeof(ChunkList));
        chunk_list__init(chunk_list);
        chunk_list->success  = false;
        chunk_list->n_chunks = 0;
        chunk_list->chunks = NULL;   
    }
    
    uint32_t chunk_list_len = chunk_list__get_packed_size(chunk_list);
    print_logs(MAS_DEF_LVL, "Will send client chunk_list_len=%lu bytes\n", sizeof(chunk_list_len));
    uint8_t *buffer = (uint8_t *)malloc(chunk_list_len * sizeof(uint8_t));
    chunk_list__pack(chunk_list, buffer);

    uint32_t chunk_list_net_len = htonl(chunk_list_len);
    uint32_t out_payload_size = sizeof(uint32_t) + chunk_list_len;
    print_logs(MAS_DEF_LVL, "chunk_list_len: %d\n", chunk_list_len);

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

    print_logs(MAS_DEF_LVL, "from now on it EPOLLOUT\n");

    if (!chunk_list->success)
        free(chunk_list);
    free(buffer);
}

void disconnect_client(int epoll_fd, event_data_t *event_data, int client_socket)
{
    print_logs(MAS_DEF_LVL, "DISCONNECT\n");
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

int round_robin(replicas_data_t *replicas_data)
{
    int replicas_count = replicas_data->replicas_count;
    int total_alive_replicas = replicas_data->total_alive_replicas;
    int *replica_robin_index = &(replicas_data->replica_robin_index);
    bool *is_alive = replicas_data->is_alive;
    bool *already_used = replicas_data->already_used;
    Replica **all_replicas = replicas_data->all_replicas;

    if (total_alive_replicas < REPLICATION_FACTOR)
    {
        // TODO disconnect the client
        print_logs(1, "total_alive_replicas: %d < REPLICATION_FACTOR: %d\nCan't allocate chunks for this file\n",
                        total_alive_replicas, REPLICATION_FACTOR);
        return -1;
    }

    int rand_ind = *replica_robin_index;

    while (!is_alive[rand_ind] || already_used[rand_ind])
    {
        if (!is_alive[rand_ind])
            print_logs(3, "Replica %d is dead, incrementing rand_ind\n", all_replicas[rand_ind]->port);
        if (already_used[rand_ind])
            print_logs(3, "Replica %d is already_used, incrementing rand_ind\n", all_replicas[rand_ind]->port);
        rand_ind = (rand_ind + 1) % replicas_count;
    }

    print_logs(3, "\nReplica %d assigned\n\n", all_replicas[rand_ind]->port);

    *replica_robin_index = (*replica_robin_index + 1) % replicas_count;

    return rand_ind;
}

void add_file(char* path, int64_t size, replicas_data_t *replicas_data, GHashTable *hash_table, bool committed)
{
    int replicas_count = replicas_data->replicas_count;
    int total_alive_replicas = replicas_data->total_alive_replicas;
    int *replica_robin_index = &(replicas_data->replica_robin_index);
    Replica **all_replicas = replicas_data->all_replicas;
    bool *is_alive = replicas_data->is_alive;

    if (total_alive_replicas < REPLICATION_FACTOR)
    {
        // TODO disconnect the client 
        print_logs(1, "total_alive_replicas: %d < REPLICATION_FACTOR: %d\nCan't allocate chunks for this file\n", total_alive_replicas, REPLICATION_FACTOR);
        return;
    }
    int chunks_number = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    print_logs(MAS_DEF_LVL, "add_file, chunks_number=%d\n", chunks_number);

    ChunkList *chunk_list = (ChunkList *)malloc(sizeof(ChunkList));
    chunk_list__init(chunk_list);
    chunk_list->success = true;
    chunk_list->committed = committed;
    chunk_list->n_chunks = chunks_number;
    chunk_list->chunks = (Chunk **)malloc(chunks_number * sizeof(Chunk *));

    for (int i = 0; i < chunks_number; i++)
    {
        Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));
        chunk__init(chunk);
        chunk->chunk_id = i;
        chunk->path = (char *)malloc(MAX_FILENAME_LENGTH * sizeof(char)); // TODO: modify (we shouldnt be using constant length)
        strcpy(chunk->path, path);
        chunk->n_replicas = REPLICATION_FACTOR;
        chunk->replicas = (Replica **)malloc(REPLICATION_FACTOR * sizeof(Replica *));
        int rand_ind = *replica_robin_index;

        print_logs(1, "\nChunk %d replicas:\n", i);

        for (int j = 0; j < REPLICATION_FACTOR; j++)
        {     
            while (!is_alive[rand_ind])
            {
                print_logs(3, "Replica %d is dead, incrementing rand_ind\n", rand_ind);
                rand_ind = (rand_ind + 1) % replicas_count;
            }
            
            Replica *replica = all_replicas[rand_ind];
            chunk->replicas[j] = replica;
            print_logs(1, "%d:   IP: %s, port: %d\n",
                j+1, all_replicas[rand_ind]->ip, all_replicas[rand_ind]->port);
            
            rand_ind = (rand_ind + 1) % replicas_count;
        }
        *replica_robin_index = (*replica_robin_index + 1) % replicas_count;
        
        chunk_list->chunks[i] = chunk;
    }

    g_hash_table_insert(hash_table, strdup(path), chunk_list);
}

void process_request(int epoll_fd, event_data_t *event_data, replicas_data_t *replicas_data, GHashTable *hash_table)
{
    int *replicas_count = &(replicas_data->replicas_count);
    int *total_alive_replicas = &(replicas_data->total_alive_replicas);
    int *replica_robin_index = &(replicas_data->replica_robin_index);
    Replica **all_replicas = replicas_data->all_replicas;
    bool *is_alive = replicas_data->is_alive;
    bool *already_used = replicas_data->already_used;

    char op_type = event_data->peer_data->buffer[0];

    print_logs(MAS_DEF_LVL, "request type: %c\n", op_type);

    if (op_type == 'w' || op_type == 'x')
    {
        print_logs(MAS_DEF_LVL, "write request detected \n");
        print_logs(MAS_DEF_LVL, "event_data->peer_data->payload_size - 1: %d\n", event_data->peer_data->payload_size - 1);
        
        FileRequestWrite *fileRequestWrite = file_request_write__unpack(
            NULL,
            event_data->peer_data->payload_size - 1,
            event_data->peer_data->buffer + 1
        );
        if (!fileRequestWrite)
            err_n_die("protobuf unpack error");
        
        print_logs(1, "Received write request for file %s\n", fileRequestWrite->path);

        print_logs(MAS_DEF_LVL, "fileRequestWrite->path: %s\n", fileRequestWrite->path);
        print_logs(MAS_DEF_LVL, "fileRequestWrite->size: %ld\n", fileRequestWrite->size);

        ChunkList* chunk_list = g_hash_table_lookup(hash_table, fileRequestWrite->path);
        if (chunk_list)
        {
            print_logs(MAS_DEF_LVL, "File %s already exists\n", fileRequestWrite->path);
            setup_outbound(epoll_fd, event_data, NULL);
            return;
        }

        add_file(fileRequestWrite->path, fileRequestWrite->size, replicas_data, hash_table, op_type == 'w' ? true : false);

        chunk_list = g_hash_table_lookup(hash_table, fileRequestWrite->path);
        if (chunk_list)
        {
            print_logs(MAS_DEF_LVL, "Sending ChunkList of file %s to client\n", 
                                        fileRequestWrite->path);

            setup_outbound(epoll_fd, event_data, chunk_list);
        }
        else
        {
            print_logs(MAS_DEF_LVL, "File %s not found, ", fileRequestWrite->path);
            err_n_die("undefined\n");
        }
    }
    else if (op_type == 'r')
    {
        print_logs(MAS_DEF_LVL, "read request detected \n"); 
        FileRequestRead *FileRequestRead = file_request_read__unpack(NULL, event_data->peer_data->payload_size - 1, event_data->peer_data->buffer + 1);
        print_logs(MAS_DEF_LVL, "fileRequest->path: %s\n", FileRequestRead->path);
        ChunkList* chunk_list = g_hash_table_lookup(hash_table, FileRequestRead->path);
        
        if (!chunk_list || !chunk_list->committed)
        {
            print_logs(1, "File %s was null or uncommitted\n", FileRequestRead->path);
            setup_outbound(epoll_fd, event_data, chunk_list);
        }
        else
        {
            setup_outbound(epoll_fd, event_data, chunk_list);
            print_logs(MAS_DEF_LVL, "File %s not found \n", FileRequestRead->path);
        }
    }
    else if(op_type == 'n')
    {
        print_logs(MAS_DEF_LVL, "new replica request detected \n");
        NewReplica *replica = new_replica__unpack(NULL, event_data->peer_data->payload_size - 1, event_data->peer_data->buffer + 1);

        for (int i = 0; i < *replicas_count; i++)
        {
            if (is_alive[i] && all_replicas[i]->port == replica->port 
                    && strcmp(all_replicas[i]->ip, replica->ip) == 0)
            {
                print_logs(1, "Replica - IP: %s, port: %d is already registered\n",
                        all_replicas[i]->ip, all_replicas[i]->port);

                print_logs(1, "Replicas count: %d\n", *replicas_count);
                print_logs(1, "Total alive replicas: %d\n", replicas_data->total_alive_replicas);
                return;
            }
            if (!is_alive[i] && all_replicas[i]->port == replica->port
                && strcmp(all_replicas[i]->ip, replica->ip) == 0)
            {
                print_logs(1, "Replica - IP: %s, port: %d will be registered back\n", 
                        all_replicas[i]->ip, all_replicas[i]->port);
                
                is_alive[i] = true;
                (*total_alive_replicas)++;
                print_logs(1, "Replicas count: %d\n", *replicas_count);
                print_logs(1, "Total alive replicas: %d\n", *total_alive_replicas);
                return;
            }
        }

        all_replicas[*replicas_count] = (Replica *)malloc(sizeof(Replica));
        replica__init(all_replicas[*replicas_count]);
        all_replicas[*replicas_count]->ip = (char *)malloc(IP_LENGTH * sizeof(char)); // Allocating memory for IP
        is_alive[*replicas_count] = true;
        all_replicas[*replicas_count]->id = *replicas_count;
        strcpy(all_replicas[*replicas_count]->ip, replica->ip);
        all_replicas[*replicas_count]->port = replica->port;
        
        print_logs(1, "New replica - IP: %s, port: %d registered\n", 
                all_replicas[*replicas_count]->ip, all_replicas[*replicas_count]->port);

        (*replicas_count)++;
        (*total_alive_replicas)++;
        print_logs(2, "Replicas count: %d\n", *replicas_count);
        print_logs(2, "Total alive replicas: %d\n", *total_alive_replicas);

        // TODO free NewReplica probably !!!
        new_replica__free_unpacked(replica, NULL);
    }
    else if (op_type == 'c')
    {
        print_logs(1, "Master received commit request\n");

        ChunkList *commit_chunk_list = chunk_list__unpack(
            NULL, 
            event_data->peer_data->payload_size - 1, 
            event_data->peer_data->buffer + 1
        );

        if (!commit_chunk_list)
            err_n_die("commit_chunk_list is NULL");

         // 
        ChunkList* chunk_list = g_hash_table_lookup(hash_table, commit_chunk_list->path);
        if(commit_chunk_list->success)
        {
            print_logs(1, "No uncommited chunks\nFile %s fully committed\n", commit_chunk_list->path);
            chunk_list->committed = true;
            chunk_list__free_unpacked(commit_chunk_list, NULL);
            return;
        }

        for (int i = 0; i < commit_chunk_list->n_chunks; i++)
        {
            for (int j = 0; j < commit_chunk_list->chunks[i]->n_replicas; j++)
            {
                if (is_alive[commit_chunk_list->chunks[i]->replicas[j]->id] == true)
                {
                   (*total_alive_replicas)--;
                   print_logs(1, "From chunk %d replica %d detected as dead, decrementing total_alive_replicas to %d\n",
                                     commit_chunk_list->chunks[i]->chunk_id, commit_chunk_list->chunks[i]->replicas[j]->port, (*total_alive_replicas));
                }
                is_alive[commit_chunk_list->chunks[i]->replicas[j]->id] = false;
            }
        }

        int index_stack[REPLICATION_FACTOR];
        int stack_size = 0;

        for (int i = 0; i < commit_chunk_list->n_chunks; i++)
        {
            int32_t cur_chunk_id = commit_chunk_list->chunks[i]->chunk_id;
            
            for (int j = 0; j < chunk_list->chunks[cur_chunk_id]->n_replicas; j++)
            {
                int32_t cur_replica_id = chunk_list->chunks[cur_chunk_id]->replicas[j]->id;

                if (is_alive[cur_replica_id])
                {
                    already_used[cur_replica_id] = true;
                    index_stack[stack_size++] = cur_replica_id; // TODO usuwamy to
                }
            }

            print_logs(1, "Chunk %d wasn't fully committed; faulty replicas:\n", cur_chunk_id);
            for (int j = 0; j < commit_chunk_list->chunks[i]->n_replicas; j++)
            {
                print_logs(1, "IP: %s, port %d\n", 
                            commit_chunk_list->chunks[i]->replicas[j]->ip,
                            commit_chunk_list->chunks[i]->replicas[j]->port);
            }

            print_logs(1, "\nChunk %d new replicas:\n", i);

            int replica_ind_ccl = 0; // ccl - commit_chunk_list
            for (int j = 0; j < chunk_list->chunks[cur_chunk_id]->n_replicas; j++)
            {
                int32_t cur_replica_id = chunk_list->chunks[cur_chunk_id]->replicas[j]->id;

                if (replica_ind_ccl < commit_chunk_list->chunks[i]->n_replicas &&
                        are_replicas_same(chunk_list->chunks[cur_chunk_id]->replicas[j], commit_chunk_list->chunks[i]->replicas[replica_ind_ccl])) 
                {
                    print_logs(1, "Faulty replica %d:   IP: %s, port: %d\n",
                                j+1, chunk_list->chunks[cur_chunk_id]->replicas[j]->ip, chunk_list->chunks[cur_chunk_id]->replicas[j]->port);
                    print_logs(3, "\nround_robin for chunk %d, replica %d\n", cur_chunk_id, cur_replica_id);
                    int new_replica_ind = round_robin(replicas_data);
                    if (new_replica_ind == -1)
                    {
                        disconnect_client(epoll_fd, event_data, event_data->peer_data->client_socket);
                        for (int i = 0; i < commit_chunk_list->n_chunks; i++)
                        {
                            free(commit_chunk_list->chunks[i]->path);
                            free(commit_chunk_list->chunks[i]->replicas);
                            free(commit_chunk_list->chunks[i]);
                        }
                        free(commit_chunk_list);
                        return;
                    }

                    print_logs(1, "New replica %d:   IP: %s, port: %d\n",
                                j+1, all_replicas[new_replica_ind]->ip, all_replicas[new_replica_ind]->port);

                    already_used[new_replica_ind] = true;
                    index_stack[stack_size++] = new_replica_ind;

                    chunk_list->chunks[cur_chunk_id]->replicas[j] = all_replicas[new_replica_ind];
                    free(commit_chunk_list->chunks[i]->replicas[replica_ind_ccl]);
                    commit_chunk_list->chunks[i]->replicas[replica_ind_ccl] = all_replicas[new_replica_ind];
                    replica_ind_ccl++;
                }
            }
            
            while(stack_size > 0)
            {
                already_used[index_stack[--stack_size]] = false;
            }
        }

        commit_chunk_list->success = true; // i.e. no problem like file already exists or master kernel buffer full

        uint32_t len_CommitChunkList = chunk_list__get_packed_size(commit_chunk_list);
        uint8_t *buffer = (uint8_t *)malloc(len_CommitChunkList * sizeof(uint8_t));
        chunk_list__pack(commit_chunk_list, buffer);

        set_fd_blocking(event_data->peer_data->client_socket);
        write_len_and_data(event_data->peer_data->client_socket, len_CommitChunkList, buffer);

        print_logs(3, "sent %d payload size to client\n", len_CommitChunkList);

        disconnect_client(epoll_fd, event_data, event_data->peer_data->client_socket);

        free(buffer);

        for (int i = 0; i < commit_chunk_list->n_chunks; i++)
        {
            free(commit_chunk_list->chunks[i]->path);
            free(commit_chunk_list->chunks[i]->replicas);
            free(commit_chunk_list->chunks[i]);
        }
        free(commit_chunk_list);
    }
    else
    {
        print_logs(MAS_DEF_LVL, "request rejected \n");
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_data->peer_data->client_socket, NULL))
            err_n_die("epoll_ctl error");
        close(event_data->peer_data->client_socket);
        free(event_data->peer_data->buffer);
        free(event_data->peer_data);
        free(event_data);
        return;
    }
}

void write_to_client(int epoll_fd, event_data_t *client_event_data)
{
    int bytes_written = bulk_write_nonblock(client_event_data->peer_data->client_socket,
        client_event_data->peer_data->out_buffer,
        &(client_event_data->peer_data->bytes_sent),
        &(client_event_data->peer_data->left_to_send)
    );

    if (bytes_written == -1)
        return;
    if (bytes_written == client_event_data->peer_data->out_payload_size)
        disconnect_client(epoll_fd, client_event_data, client_event_data->peer_data->client_socket);
    else
        err_n_die("undefined");
}

void handle_new_client_payload_declaration(int epoll_fd, event_data_t *event_data)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;
    int32_t network_payload_size;
    
    bytes_read = read(client_socket, &network_payload_size, sizeof(network_payload_size));
    print_logs(MAS_DEF_LVL, "handle_new_client_payload_declaration, bytes_read: %d\n", bytes_read);
    if (bytes_read < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            print_logs(MAS_DEF_LVL, "EAGAIN/EWOULDBLOK\n");
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
        print_logs(MAS_DEF_LVL, "Client disconnected or sent incomplete payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    /* At this point we know we have a new client who declared their payload */
    event_data->peer_data->reading_started = true;
    event_data->peer_data->payload_size = ntohl(network_payload_size);
    
    if (event_data->peer_data->payload_size <= 0 ||
        event_data->peer_data->payload_size > SINGLE_CLIENT_BUFFER_SIZE)
    {
        print_logs(MAS_DEF_LVL, "Client declared invalid payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    print_logs(MAS_DEF_LVL, "Client configured, declared payload size: %d bytes\n", event_data->peer_data->payload_size);
}

void handle_client(int epoll_fd, event_data_t *event_data, replicas_data_t *replicas_data, GHashTable *hash_table)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;

    if (event_data->peer_data->reading_started == false)
    {
        handle_new_client_payload_declaration(epoll_fd, event_data);
        return;
    }

    print_logs(MAS_DEF_LVL, "handle_client, client_socket: %d, payload: %d\n", client_socket, event_data->peer_data->payload_size);

    bytes_read = read(client_socket, event_data->peer_data->buffer + event_data->peer_data->bytes_stored,
        event_data->peer_data->space_left);

    if (bytes_read == 0)
    {
        print_logs(MAS_DEF_LVL, "Client %d disconnected \n", client_socket);
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    print_logs(MAS_DEF_LVL, "handle_client, bytes_read: %d\n", bytes_read);

    event_data->peer_data->space_left -= bytes_read;
    event_data->peer_data->bytes_stored += bytes_read;

    if (event_data->peer_data->bytes_stored == event_data->peer_data->payload_size)
        process_request(epoll_fd, event_data, replicas_data, hash_table);
    else if (event_data->peer_data->bytes_stored > event_data->peer_data->payload_size)    /*multi-queries clients - TODO*/
        err_n_die("undefined");
}

// void print_help()
// {
//     print_logs(0, "Usage:\n./server   (ip will be 127.0.0.1 and port 9001)\nOR\n/server port   (ip will be 127.0.0.1)\nOR\n./server ip port\n");
// }

int main(int argc, char **argv)
{   
    srand(time(NULL));
    GHashTable          *hash_table = g_hash_table_new(g_str_hash, g_str_equal);
    int                 server_socket;
    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];
    Replica             *all_replicas[1000]; // these are all replicas master knows
    bool                is_alive[1000] = {false}, already_used[1000] = {false};

    replicas_data_t replicas_data = {0, 0, 0, all_replicas, is_alive, already_used};

#ifdef RELEASE
    if (argc != 3)
        err_n_die("Error: Invalid parameters. Please provide the Master IP, Master port.");
    strcpy(master_ip, argv[1]);
    master_port = atoi(argv[2]);
#else
    if (argc == 3) // ./server master_ip master_port
    {
        strcpy(master_ip, argv[1]);
        master_port = atoi(argv[2]);
    }
    else if (argc == 2) // ./server master_port
    {
        master_port = atoi(argv[1]);
    }
#endif 

    server_setup(&server_socket, &epoll_fd, &event);

    print_logs(1, "Master, IP: %s, port: %d started running\n\n", master_ip, master_port);

    while (running) 
    {
        print_logs(MAS_DEF_LVL, "\n Master IP: %s, port: %d polling for events \n",
                master_ip, master_port);
        
        // MAX_EVENTS: 1000, przyjdzie na raz 30 
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); //connect
        print_logs(MAS_DEF_LVL, "\nReady events: %d \n", event_count);

        for (int i = 0; i < event_count; i++) 
        {
            event_data_t *event_data = (event_data_t *)events[i].data.ptr;

            if (event_data->is_server)
            {   
                print_logs(MAS_DEF_LVL, "server event\n");
                handle_new_connection(epoll_fd, server_socket);
            }
            else
            {
                if (events[i].events & EPOLLIN)
                {
                    print_logs(MAS_DEF_LVL, "client event EPOLLIN triggered\n");
                    handle_client(epoll_fd, event_data, &replicas_data, hash_table);
                }
                else if (events[i].events & EPOLLOUT)
                {
                    print_logs(MAS_DEF_LVL, "client event EPOLLOUT triggered\n");
                    write_to_client(epoll_fd, event_data);
                } 
                else
                {
                    err_n_die("undefined (client event was other than EPOLLIN or EPOLLOUT)");
                }
            }
        }
    }
    close(epoll_fd);
    close(server_socket);
}

// TODO common this, add clean exit
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
    // servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (inet_pton(AF_INET, master_ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error - invalid address");

    servaddr.sin_port = htons(master_port);

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
