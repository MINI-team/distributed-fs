#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <limits.h>
#include <string.h>
#include "common.h"
#include "dfs.pb-c.h"

int running = 1;

int master_port = 9001;
char master_ip[IP_LENGTH] = "127.0.0.1";
int replica_port = 8080;
char replica_ip[IP_LENGTH] = "127.0.0.1";

// int current_connection_cnt = 0;
int current_connection_id = -1;
bool current_connections[MAX_CONNECTIONS];

#define MAX_EVENTS 4096
#define SINGLE_CLIENT_BUFFER_SIZE CHUNK_SIZE + 2000

void server_setup(event_data_t **server_event_data, int *server_socket, char *server_ip,
        int server_port, int *epoll_fd)
{
    struct sockaddr_in servaddr;
    struct epoll_event event;

    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    int option = 1;
    if (setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        err_n_die("setsockopt error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error - invalid address");

    servaddr.sin_port = htons(server_port);

    if (bind(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("bind error");

    if (listen(*server_socket, 4096) < 0)
        err_n_die("listen error");

    set_fd_nonblocking(*server_socket);

    if ((*epoll_fd = epoll_create1(0)) < 0)
        err_n_die("epoll_create1 error");

    *server_event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!server_event_data)
        err_n_die("malloc error");

    (*server_event_data)->is_server = 1;
    (*server_event_data)->server_socket = *server_socket;

    event.events = EPOLLIN;
    event.data.ptr = *server_event_data;

    if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, *server_socket, &event) < 0)
        err_n_die("epoll_ctl error");
}

void mark_new_connection(peer_data_t *peer_data)
{
    if (++current_connection_id >= MAX_CONNECTIONS)
        err_n_die("current_connection_id exceeded MAX_CONNECTIONS");

    peer_data->connection_id = current_connection_id;
    current_connections[peer_data->connection_id] = true;

    // current_connection_id = (current_connection_id + 1) % MAX_CONNECTIONS;
    // current_connection_cnt++;
}

void handle_new_connection(int epoll_fd, int server_socket)
{
    int client_socket;

    if ((client_socket = accept(server_socket, (SA *)NULL, NULL)) < 0)
    {
        print_logs(REP_DEF_LVL, "Server couldnt accept client\n");
        return;
    }

    event_data_t *peer_event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!peer_event_data)
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
    peer_data->out_buffer = NULL;

    // marking the connection in current_connection; another case is that we initiate the connection
    mark_new_connection(peer_data);

    peer_event_data->is_server = 0;
    peer_event_data->peer_data = peer_data;
    peer_event_data->peer_type = EL_PRIMO;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = peer_event_data;

    print_logs(3, "New client connected, id %d\n", peer_data->connection_id);

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event))
        err_n_die("epoll_ctl error");

    set_fd_nonblocking(client_socket);
}

void disconnect_client(int epoll_fd, event_data_t *event_data, int client_socket)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL))
        err_n_die("epoll_ctl error");

    // set connection as not active and decrement current connection count
    // current_connection_cnt--;
    current_connections[event_data->peer_data->connection_id] = false;

    print_logs(3, "disconnect_client, setting connection %d with peer as false\n",
        event_data->peer_data->connection_id);

    if (event_data->peer_data->buffer)
    {
        free(event_data->peer_data->buffer);
        event_data->peer_data->buffer = NULL;
    }

    if (event_data->peer_data->out_buffer)
    {
        free(event_data->peer_data->out_buffer);
        event_data->peer_data->out_buffer = NULL;
    }

    free(event_data->peer_data);
    event_data->peer_data = NULL;
    free(event_data);
    event_data = NULL;

    print_logs(3, "freeing every structure (event_data and members) of client (socket %d) and setting them as NULL\n", client_socket);

    close(client_socket);
}

void handle_new_client_payload_declaration(int epoll_fd, event_data_t *event_data)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;
    int32_t network_payload_size;

    bytes_read = read(client_socket, &network_payload_size, sizeof(network_payload_size));
    print_logs(REP_DEF_LVL, "handle_new_client_payload_declaration, bytes_read: %d\n", bytes_read);
    if (bytes_read < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            print_logs(1, "EPIPE/ECONNRESET in read, broken pipe\n");
            disconnect_client(epoll_fd, event_data, event_data->peer_data->client_socket);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            print_logs(REP_DEF_LVL, "EAGAIN/EWOULDBLOK\n");
            return;
        }
        err_n_die("read error 183");
    }
    if (bytes_read < sizeof(network_payload_size))
    {
        /*
            Disconnect the client if the payload size cannot be fully read
            Typically, a zero-byte read indicates the client has disconnected
        */
        print_logs(REP_DEF_LVL, "Client disconnected or sent incomplete payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    /* At this point we know we have a new client who declared their payload */
    event_data->peer_data->reading_started = true;
    event_data->peer_data->payload_size = ntohl(network_payload_size);

    if (event_data->peer_data->payload_size <= 0 ||
        event_data->peer_data->payload_size > SINGLE_CLIENT_BUFFER_SIZE)
    {
        print_logs(REP_DEF_LVL, "Client declared invalid payload size\n");
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }

    print_logs(REP_DEF_LVL, "Client configured, declared payload size: %d bytes\n", event_data->peer_data->payload_size);
}

void prepare_chunk_to_send(int epoll_fd, event_data_t *event_data, int64_t chunk_size, uint8_t* file_buf)
{
    /*
        musimy ustawic dwa pola
        event_data->peer_data->out_payload_size
        event_data->peer_data->out_buffer

        eout_buffer:
        chunk_size: 4 bytes
        file_buf: chunk_size bytes
    */

    uint32_t chunk_net_size = htonl(chunk_size);
    uint32_t out_payload_size = sizeof(uint32_t) + chunk_size;
    event_data->peer_data->out_payload_size = out_payload_size;
    event_data->peer_data->out_buffer = (uint8_t *)malloc(out_payload_size * sizeof(uint8_t));
    memcpy(event_data->peer_data->out_buffer, &chunk_net_size, sizeof(uint32_t));
    if(file_buf != NULL)
        memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t), file_buf, chunk_size);
    event_data->peer_data->bytes_sent = 0;
    event_data->peer_data->left_to_send = out_payload_size;
    event_data->peer_type = CLIENT_READ;

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLIN;
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
        err_n_die("unable to add EPOLLOUT");

    free(file_buf);
}

void readChunkFile(int epoll_fd, event_data_t *event_data, const char *chunkname)
{
    int fd, bytes_read;
    int64_t chunk_size;

    if ((fd = open(chunkname, O_RDONLY)) == -1)
    {
        chunk_size = 0;
        print_logs(3, "\nCHUNK NOT FOUND, I was probably dead when I was supposed to get it\n================\n");
        // err_n_die("open error");
        prepare_chunk_to_send(epoll_fd, event_data, chunk_size, NULL);
    }
    else
    {
        chunk_size = file_size(fd);

        uint8_t* file_buf = (uint8_t *)malloc(chunk_size * sizeof(uint8_t));

        if ((bytes_read = bulk_read(fd, file_buf, chunk_size)) !=  chunk_size)
            err_n_die("putChunk read error");

        print_logs(REP_DEF_LVL, "to ostatnie, bytes_read: %d\n", bytes_read);
        // set_fd_blocking(event_data->peer_data->client_socket); // tego bardzo nie chcemy !!!!!!!!!!!!!!!!!!!!
        prepare_chunk_to_send(epoll_fd, event_data, chunk_size, file_buf);
    }

    close(fd);
}

// prepare to listen for acknowledgement from secondary (and tertiary) replicas
void prepare_to_listen_for_ack(int epoll_fd, event_data_t *event_data)
{
    peer_data_t *peer_data = event_data->peer_data;

    if (peer_data->out_buffer)
        free(peer_data->out_buffer);
    else
        err_n_die("out_buffer was NULL");

    peer_data->buffer = (uint8_t *)malloc((SINGLE_CLIENT_BUFFER_SIZE + 1) * sizeof(uint8_t));
    peer_data->payload_size = 0;
    peer_data->bytes_stored = 0;
    peer_data->space_left = SINGLE_CLIENT_BUFFER_SIZE;
    peer_data->reading_started = false;
    peer_data->out_buffer = NULL;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, peer_data->client_socket, &event))
        err_n_die("epoll_ctl error");
}

void write_to_peer(int epoll_fd, event_data_t *event_data)
{
    peer_type_t peer_type = event_data->peer_type;

    if (peer_type == CLIENT_WRITE)
    {
        print_logs(3, "Before bulk_write_nonblock left_to_send is %d\n", event_data->peer_data->left_to_send);
    }

    int bytes_written = bulk_write_nonblock(event_data->peer_data->client_socket,
        event_data->peer_data->out_buffer,
        &(event_data->peer_data->bytes_sent),
        &(event_data->peer_data->left_to_send)
    );


    if(peer_type == CLIENT_WRITE)
    {
        print_logs(3, "After bulk_write_nonblock left_to_send is %d\n", event_data->peer_data->left_to_send);
    }
    print_logs(REP_DEF_LVL, "poszlo bytes_written: %d, w sumie wyslano: %d; out_payload size to: %d\n",
     bytes_written, event_data->peer_data->bytes_sent, event_data->peer_data->out_payload_size);

    if (bytes_written == -2)
    {
        disconnect_client(epoll_fd, event_data, event_data->peer_data->client_socket);
        return;
    }

    if (bytes_written == -1)
        return;
    if (bytes_written == event_data->peer_data->out_payload_size)
    {
        print_logs(3, "WRITTEN to %s\n", peer_type_to_string(peer_type));
        switch (peer_type)
        {
        case CLIENT_READ:
        case MASTER:
        case REPLICA_PRIMO:
            disconnect_client(epoll_fd, event_data, event_data->peer_data->client_socket);
            break;
        case CLIENT_WRITE:
        {
            // wysylamy
            //
            // na razie nie zamykamy (powinnismy zamknac, jak trzy repliki zapisza)
            struct epoll_event event;
            event.events = EPOLLIN;
            // we will overwrite the out buffer, so we prepare for that here
            event_data->peer_data->out_payload_size = 0;
            event_data->peer_data->bytes_sent = 0;
            event_data->peer_data->left_to_send = 0;

            event.data.ptr = event_data;

            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
                err_n_die("unable to modify epoll for CLIENT_WRITE");

            // if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_data->peer_data->client_socket, NULL) < 0)
            //     err_n_die("unable to del");
            break;
        }
        case REPLICA_SECUNDO:
            // nie zamykamy socketu, zamieniamy na EPOLLIN
            // EPOLLOUT -> EPOLLIN
            // struct epoll_event event;
            // event.events = EPOLLIN;
            // event.data.ptr = event_data;
            // if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
            //     err_n_die("unable to add EPOLLOUT");

            prepare_to_listen_for_ack(epoll_fd, event_data);
            break;

        default:
            err_n_die("peer_type is probably EL_PRIMO");
            break;
        }
    }
    
    else
        err_n_die("NIGGA WHAAT THE FUUUUUUUUUUUUUUUUUUCK");
    }
    

void processReadRequest(int epoll_fd, event_data_t *event_data, char *path, int id)
{
    char chunkname[MAX_FILENAME_LENGTH];
    snprintf(chunkname, sizeof(chunkname), "data_replica/%s%d.chunk", path, id);
    print_logs(REP_DEF_LVL, "chunkname: %s\n", chunkname);
    readChunkFile(epoll_fd, event_data, chunkname);
}

void write_to_disk(const char *filepat, uint8_t *data, int length)
{
    // print_logs(REP_DEF_LVL, "PRINTING\n\n\n");
    // print_logs(REP_DEF_LVL, "%s", data);
    // exit(1);
    int fd, n;

    if ((fd = open(filepat, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    print_logs(REP_DEF_LVL, "imo tu sie wyjebie\n");
    if ((n = bulk_write(fd, data, length)) == -1)
        err_n_die("read error 383");

    close(fd);
}

void prepare_epollout(int epoll_fd, int peer_fd, uint32_t proto_len, uint8_t *proto_buf, 
                        peer_type_t peer_type, int connection_id)
{
    event_data_t *event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!event_data)
        err_n_die("malloc error");

    peer_data_t *peer_data = (peer_data_t *)malloc(sizeof(peer_data_t));
    if (!peer_data)
        err_n_die("malloc error");

    event_data->peer_data = peer_data;
    event_data->peer_data->buffer = NULL;
    event_data->peer_data->client_socket = peer_fd;
    event_data->peer_data->connection_id = connection_id;
    event_data->is_server = false;

    uint32_t proto_net_len = htonl(proto_len);
    uint32_t out_payload_size = sizeof(uint32_t) + proto_len;
    event_data->peer_data->out_payload_size = out_payload_size;
    event_data->peer_data->out_buffer = (uint8_t *) malloc(out_payload_size * sizeof(uint8_t));
    memcpy(event_data->peer_data->out_buffer, &proto_net_len, sizeof(uint32_t));
    memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t), proto_buf, proto_len);
    event_data->peer_data->bytes_sent = 0;
    event_data->peer_data->left_to_send = out_payload_size;

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLIN; // 
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
        err_n_die("unable to add EPOLLOUT");
}

void prepare_local_write_ack(int epoll_fd, event_data_t *event_data, peer_type_t peer_type)
{
    // prepare protobuf message, pack it to proto_buf, 
    // prepare old epoll event for writing with payload and packed message

    uint32_t proto_len, proto_net_len;

    ChunkCommitReport chunk_commit_report = CHUNK_COMMIT_REPORT__INIT;
    chunk_commit_report.ip = replica_ip;
    chunk_commit_report.port = replica_port;
    chunk_commit_report.is_success = 1;
    proto_len = chunk_commit_report__get_packed_size(&chunk_commit_report);
    uint8_t *proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    chunk_commit_report__pack(&chunk_commit_report, proto_buf);

    // prepare_epollout(epoll_fd, client_event_data->peer_data->client_socket, proto_len,
    //     proto_buf, CLIENT_ACK, client_event_data->peer_data->connection_id);

    proto_net_len = htonl(proto_len);
    int32_t out_payload_size = sizeof(uint32_t) + proto_len;
    event_data->peer_data->out_payload_size = out_payload_size;
    event_data->peer_data->out_buffer = (uint8_t *) malloc(out_payload_size * sizeof(uint8_t));


    memcpy(event_data->peer_data->out_buffer, &proto_net_len, sizeof(uint32_t));
    memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t), proto_buf, proto_len);
    event_data->peer_data->bytes_sent = 0;
    event_data->peer_data->left_to_send = out_payload_size;
    event_data->peer_type = peer_type;

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLIN; // 
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_data->peer_data->client_socket, &event) < 0)
        err_n_die("unable to add EPOLLOUT");
}

void processWriteRequest(int epoll_fd, char *path, int id, uint8_t *data, int length, Chunk *chunk, 
                         peer_type_t peer_type, event_data_t *event_data)
{
    char chunkname[MAX_FILENAME_LENGTH];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/%d/chunks/%s%d.chunk",
             replica_port, path, id);
    print_logs(REP_DEF_LVL, "chunkname: %s\n", chunkname);
    write_to_disk(chunkname, data, length);

    prepare_local_write_ack(epoll_fd, event_data, peer_type);
}

int forwardChunk(int epoll_fd, Chunk *chunk, uint32_t chunk_size, uint8_t *buffer, event_data_t *true_client_event_data)
{
    // if (replica_port == 8081)
    //     err_n_die("test exit");
    /*
        uint8_t *buffer

        Replication 'd'
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
        chunk content length - 4 bytes
        chunk content - (chunk content length) bytes
    */ 

    int success = 1, replicafd, ret;
    char recvchar;
    
    buffer[0] = 'd';

    for (int i = 0; i < chunk->n_replicas; i++)
    {
        if (chunk->replicas[i]->port == replica_port) // SIMPLE HEURISTIC FOR NOW,
                                                      // should add ip as well
            continue;
        
        ret = setup_connection_retry(&replicafd, chunk->replicas[i]->ip, chunk->replicas[i]->port);
        if (ret < 0)
        {
            print_logs(REP_DEF_LVL, "skipping this replica\n");
            close(replicafd);
            continue;
        }
        // setup_connection(&replicafd, chunk->replicas[i]->ip, chunk->replicas[i]->port);
        set_fd_nonblocking(replicafd); // to jest do zrobienia !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

        event_data_t *event_data = (event_data_t *)malloc(sizeof(event_data_t));
        if (!event_data)
            err_n_die("malloc error");

        peer_data_t *peer_data = (peer_data_t *)malloc(sizeof(peer_data_t));
        if (!peer_data)
            err_n_die("malloc error");

        // marking the connection in current_connection; another case is when someone (client) tries to connect to us
        mark_new_connection(peer_data);

        event_data->peer_data = peer_data;
        event_data->peer_data->buffer = NULL;
        event_data->peer_data->client_socket = replicafd;
        event_data->is_server = false;
        event_data->peer_type = REPLICA_SECUNDO;

        // event_data->peer_data->true_client_socket = true_client_socket;
        event_data->peer_data->true_client_connection_id = true_client_event_data->peer_data->connection_id;
        event_data->peer_data->true_client_event_data = true_client_event_data;

        uint32_t chunk_net_size = htonl(chunk_size);
        int32_t out_payload_size = sizeof(uint32_t) + chunk_size;
        event_data->peer_data->out_payload_size = out_payload_size;
        event_data->peer_data->out_buffer = (uint8_t *)malloc(out_payload_size * sizeof(uint8_t));
        memcpy(event_data->peer_data->out_buffer, &chunk_net_size, sizeof(uint32_t));
        memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t), buffer, chunk_size);
        event_data->peer_data->bytes_sent = 0;
        event_data->peer_data->left_to_send = out_payload_size;

        print_logs(REP_DEF_LVL, "payload_size=%d\n", chunk_size);

        struct epoll_event event;
        event.events = EPOLLOUT | EPOLLIN;
        event.data.ptr = event_data;

        // print_logs(3, "Primary replica goes to sleep!!!\n");
        // sleep(5);
        // print_logs(3, "Primary replica wakes up!!!\n\n");

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, replicafd, &event) < 0)
            err_n_die("unable to add EPOLLOUT");

    }
    return success;
}

void process_request(int epoll_fd, event_data_t *event_data)
{
    /*
    event_data->peer_data->buffer:
        
        Write 'w'
        
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
        chunk content length - 4 bytes
        chunk content - (chunk content length) bytes

        Replication 'd'
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
        chunk content length - 4 bytes
        chunk content - (chunk content length) bytes


        Read 'r'
        operation type - 1 byte
        proto_buf length - 4 bytes
        proto_buf - (proto_buf length) bytes
    */
    uint8_t     op_type;
    uint32_t    proto_len, chunk_content_len;
    uint8_t     *buffer = event_data->peer_data->buffer;
    uint8_t     *proto_buf, *chunk_content_buf;
    int         current_offset = 0;

    if (event_data->peer_type == REPLICA_SECUNDO)
    {
        print_logs(3, "\n===================\nRECEIVED ACKNOWLEDGEMENT FROM SECONDARY\n\n");

                // handle this


        if (!current_connections[event_data->peer_data->true_client_connection_id])
        {
            // TODO disconnect replica
            printf("CLIENT disconnected, ACK aborted\n");
            return;
        }

        // ponizszy kod zostanie PRZEORANY

        event_data_t *event_data_with_client = event_data->peer_data->true_client_event_data;


        if (!event_data_with_client)
        {
            print_logs(3, "event_data_with_client is NULL !!!!\n");
            return;
        }
        if (!(event_data_with_client->peer_data))
        {
            print_logs(3, "event_data_with_client->peer_data is NULL !!!!\n");
            return;
        }

        if (!current_connections[event_data_with_client->peer_data->connection_id])
        {
            print_logs(3, "lost connection with peer(client), id %d\ntherefore can't send ack\n\n\n",
                       event_data_with_client->peer_data->connection_id);

            return;
        }


        if (!(event_data_with_client->peer_data->out_buffer))
        {
            print_logs(3, "event_data_with_client out_buffer is NULL !!!!\n");
            return;
        }
        if (!(event_data->peer_data))
        {
            print_logs(3, "peer_data with replica is NULL !!!!\n");
            return;
        }

        int32_t old_payload_size = event_data_with_client->peer_data->out_payload_size;
        int32_t additional_payload_size = sizeof(uint32_t) + event_data->peer_data->payload_size;
        int32_t proto_net_len = htonl(event_data->peer_data->payload_size);

        event_data_with_client->peer_data->out_payload_size += additional_payload_size;
        // event_data_with_client->peer_data->bytes_sent = 0;
        // don't zero this; it was zeroed when we wrote all that was to send to this client
        // if it's not zero, then we are in the middle of writing and it's true that we sent some
        event_data_with_client->peer_data->left_to_send += additional_payload_size;

        printf("event_data_with_client->peer_data->left_to_send: %d\n", event_data_with_client->peer_data->left_to_send);
        printf("event_data_with_client->peer_data->out_payload_size: %d\n", event_data_with_client->peer_data->out_payload_size);

        event_data_with_client->peer_data->out_buffer =
            (uint8_t *)realloc(event_data_with_client->peer_data->out_buffer, 
            event_data_with_client->peer_data->out_payload_size * sizeof(uint8_t));

        memcpy(event_data_with_client->peer_data->out_buffer + old_payload_size,
                 &proto_net_len, sizeof(uint32_t));
        memcpy(event_data_with_client->peer_data->out_buffer + old_payload_size + sizeof(uint32_t),
               event_data->peer_data->buffer, event_data->peer_data->payload_size);

        // event_data_with_client->peer_data->out_buffer = event_data->peer_data->buffer;

        struct epoll_event event;
        event.events = EPOLLOUT | EPOLLIN;

        if (!event_data_with_client)
            err_n_die("event_data->peer_data->true_client_event_data was NULL");

        event.data.ptr = event_data_with_client;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD,
                      event_data_with_client->peer_data->client_socket, &event) < 0)
            err_n_die("unable to add EPOLLOUT 512");
            
        print_logs(3, "ACK FROM replica ready to be written\n");
        return;
    }

    op_type = buffer[0];
    current_offset += 1;

    memcpy(&proto_len, buffer + current_offset, sizeof(uint32_t));
    proto_len = ntohl(proto_len);
    current_offset += sizeof(uint32_t);

    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    memcpy(proto_buf, buffer + current_offset, proto_len);
    current_offset += proto_len;

    if (op_type == 'r')
    {
        print_logs(REP_DEF_LVL, "received read request\n");
     
        ChunkRequest *chunkRequest = chunk_request__unpack(NULL, proto_len, proto_buf);
        if (!chunkRequest)
            err_n_die("process_request, chunkRequest is null");

        print_logs(REP_DEF_LVL, "chunkRequest->path: %s\n", chunkRequest->path);
        print_logs(REP_DEF_LVL, "chunkRequest->chunk_id: %d\n", chunkRequest->chunk_id);

        processReadRequest(epoll_fd, event_data, chunkRequest->path, chunkRequest->chunk_id);
        // REPLACE THIS <-----------------------------------------------------------
        // REPLACE THIS <-----------------------------------------------------------
        // REPLACE THIS <-----------------------------------------------------------
        chunk_request__free_unpacked(chunkRequest, NULL);
        return;
    }
    else if (op_type == 'w' || op_type == 'd')
    {
        print_logs(REP_DEF_LVL, "received %c request\n", op_type);

        memcpy(&chunk_content_len, buffer + current_offset, sizeof(uint32_t));
        chunk_content_len = ntohl(chunk_content_len);
        current_offset += sizeof(uint32_t);

        chunk_content_buf = (uint8_t *)malloc(chunk_content_len * sizeof(uint8_t));
        memcpy(chunk_content_buf, buffer + current_offset, chunk_content_len);

        Chunk *chunk = chunk__unpack(NULL, proto_len, proto_buf);

        if (!chunk)
            err_n_die("process_request, chunk is null");

        // print_logs(REP_DEF_LVL, "chunk->path: %s\n", chunk->path);
        print_logs(REP_DEF_LVL, "chunk->chunk_id: %d\n", chunk->chunk_id);
        print_logs(REP_DEF_LVL, "chunk->n_replicas: %ld\n", chunk->n_replicas);

        for (int i = 0; i < chunk->n_replicas; i++)
        {
            print_logs(REP_DEF_LVL, "Id: %d IP: %s Port: %d \n",
                   chunk->replicas[i]->id, chunk->replicas[i]->ip,
                   chunk->replicas[i]->port);
        }

        peer_type_t peer_type = op_type == 'w' ? CLIENT_WRITE : REPLICA_PRIMO;

        processWriteRequest(epoll_fd, chunk->path, chunk->chunk_id, chunk_content_buf, chunk_content_len, chunk, 
                            peer_type, event_data);

        if (op_type == 'w')
        {
            int res = forwardChunk(epoll_fd, chunk, event_data->peer_data->payload_size, buffer, 
                                    event_data);
        }
        // else if (op_type == 'd')
        // {

        // }
        chunk__free_unpacked(chunk, NULL);
    }
    else
        err_n_die("wrong operation type");

    // else if (strcmp(operation_type_buff, "write_primary") == 0 || strcmp(operation_type_buff, "write") == 0)
    free(proto_buf);
    free(chunk_content_buf);
}

void handle_client(int epoll_fd, event_data_t *event_data)
{
    int client_socket = event_data->peer_data->client_socket;
    int bytes_read;

    if (event_data->peer_data->reading_started == false)
    {
        handle_new_client_payload_declaration(epoll_fd, event_data);
        return;
    }

    // print_logs(REP_DEF_LVL, "handle_client, client_socket: %d, payload: %d\n", client_socket, event_data->peer_data->payload_size);
    
    bytes_read = read(client_socket, event_data->peer_data->buffer + event_data->peer_data->bytes_stored,
                      event_data->peer_data->space_left);

    if (bytes_read == 0)
    {
        print_logs(3, "Client %d (%s) disconnected \n", client_socket, peer_type_to_string(event_data->peer_type));
        disconnect_client(epoll_fd, event_data, client_socket);
        return;
    }
    if (bytes_read < 0)
    {
        if (errno == ECONNRESET)
        {
            print_logs(3, "Client %d (%s) disconnected ABRUPTLY <--------------\n", client_socket, peer_type_to_string(event_data->peer_type));
            disconnect_client(epoll_fd, event_data, client_socket);
            return;
        }
        else
        {
            print_logs(0, "ERRNO code: %d and msg: %s\n\n\n", errno, strerror(errno));
            print_logs(3, "Client %d (%s) disconnected ABRUPTLY <--------------\n", client_socket, peer_type_to_string(event_data->peer_type));
            disconnect_client(epoll_fd, event_data, client_socket);
            return;
            // TODO read error inspection needed!!!
            // err_n_die("read error 798");
        }
    }

    // print_logs(REP_DEF_LVL, "handle_client, bytes_read: %d\n", bytes_read);

    event_data->peer_data->space_left -= bytes_read;
    event_data->peer_data->bytes_stored += bytes_read;

    if (event_data->peer_data->bytes_stored == event_data->peer_data->payload_size)
        process_request(epoll_fd, event_data);
    else if (event_data->peer_data->bytes_stored > event_data->peer_data->payload_size) /*multi-queries clients - TODO*/
        err_n_die("undefined 816");
}

void register_to_master(int epoll_fd)
{
    int success = 1, masterfd, ret;
    uint32_t proto_len, proto_net_len;
    uint8_t op_type = 'n';

    NewReplica replica = NEW_REPLICA__INIT;
    replica.ip = replica_ip;
    replica.port = replica_port;

    proto_len = new_replica__get_packed_size(&replica);
    uint8_t *proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    new_replica__pack(&replica, proto_buf);

    // buffer[0] = 'n';

    ret = setup_connection_retry(&masterfd, master_ip, master_port);
    if (ret < 0)
    {
        print_logs(REP_DEF_LVL, "skipping this replica\n");
        err_n_die("couldn't connect to master");
    }
    // setup_connection(&masterfd, chunk->replicas[i]->ip, chunk->replicas[i]->port);
    set_fd_nonblocking(masterfd); // to jest do zrobienia !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


    event_data_t *event_data = (event_data_t *)malloc(sizeof(event_data_t));
    if (!event_data)
        err_n_die("malloc error");

    peer_data_t *peer_data = (peer_data_t *)malloc(sizeof(peer_data_t));
    if (!peer_data)
        err_n_die("malloc error");

    mark_new_connection(peer_data);

    event_data->peer_data = peer_data;
    event_data->peer_data->buffer = NULL;
    event_data->peer_data->client_socket = masterfd;
    event_data->is_server = false;

    proto_net_len = htonl(proto_len);
    int32_t master_payload_size = sizeof(uint8_t) + proto_len;
    int32_t out_payload_size = sizeof(uint32_t) + master_payload_size;
    int32_t net_master_payload_size = htonl(master_payload_size);
    event_data->peer_data->out_payload_size = out_payload_size;
    event_data->peer_data->out_buffer = (uint8_t *)malloc(out_payload_size * sizeof(uint8_t));
    
    memcpy(event_data->peer_data->out_buffer, &net_master_payload_size, sizeof(uint32_t));
    event_data->peer_data->out_buffer[sizeof(uint32_t)] = op_type;
    // memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t) + sizeof(uint8_t), &proto_net_len, sizeof(uint32_t));
    memcpy(event_data->peer_data->out_buffer + sizeof(uint32_t) + sizeof(uint8_t), proto_buf, proto_len);

    event_data->peer_data->bytes_sent = 0;
    event_data->peer_data->left_to_send = out_payload_size;
    event_data->peer_type = MASTER;
    print_logs(REP_DEF_LVL, "payload_size=%d\n", out_payload_size);

    struct epoll_event event;
    event.events = EPOLLOUT;
    event.data.ptr = event_data;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, masterfd, &event) < 0)
        err_n_die("unable to add EPOLLOUT");

    free(proto_buf);
}


void handle_sigint(int sig) 
{
    printf("\nCaught SIGINT, graceful shutdown...\n");
    running = 0;
}

int main(int argc, char **argv)
{
    int             server_socket, epoll_fd;
    event_data_t    *server_event_data;
    struct          epoll_event events[MAX_EVENTS];

#ifdef RELEASE
    if (argc != 5)
        err_n_die("Error: Invalid parameters. Please provide the Master IP, Master port, Replica IP, and Replica port.");
    strcpy(master_ip, argv[1]);
    master_port = atoi(argv[2]);
    strcpy(replica_ip, argv[3]);
    replica_port = atoi(argv[4]);
#else
    if (argc == 5) // /.replica master_ip master_port replica_ip replica_port
    {
        strcpy(master_ip, argv[1]);
        master_port = atoi(argv[2]);
        strcpy(replica_ip, argv[3]);
        replica_port = atoi(argv[4]);
    }
    if (argc == 3) // ./replica ip port
    {
        // replica_ip = argv[1];
        strcpy(replica_ip, argv[1]);
        replica_port = atoi(argv[2]);
    }
    else if (argc == 2) // ./replica port
    {
        replica_port = atoi(argv[1]);
    }
#endif    

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);    
    server_setup(&server_event_data, &server_socket, replica_ip, replica_port, &epoll_fd);
    register_to_master(epoll_fd);

    while (running)
    {
        print_logs(3, "\n Replica IP: %s, port: %d polling for events \n",
                replica_ip, replica_port);

        // MAX_EVENTS: 1000, przyjdzie na raz 30
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // connect

        for (int i = 0; i < event_count; i++)
        {
            event_data_t *event_data = (event_data_t *)events[i].data.ptr;

            if (event_data->is_server)
            {
                print_logs(3, "server event - new peer wants to connect\n");
                handle_new_connection(epoll_fd, server_socket);
            }
            else
            {
                // print_logs(REP_DEF_LVL, "client event\n");
                // handle_client(epoll_fd, event_data);

                if (events[i].events & EPOLLIN)
                {
                    // print_logs(REP_DEF_LVL, "client event EPOLLIN triggered\n");
                    handle_client(epoll_fd, event_data);
                }
                else if (events[i].events & EPOLLOUT)
                {
                    // print_logs(REP_DEF_LVL, "client event EPOLLOUT triggered\n");
                    write_to_peer(epoll_fd, event_data);
                } 
                else
                {
                    err_n_die("SHOULDNT HAPPEN!!!");
                }
            }
        }
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_socket, NULL))
        err_n_die("epoll_ctl error");

    free(server_event_data);

    close(server_socket);
    close(epoll_fd);

    return 0;
}