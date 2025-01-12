#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <limits.h>
#include <string.h>
#include "common.h"
#include "dfs.pb-c.h"

int replica_port = 8080;

#define MAX_EVENTS 4096
#define SINGLE_CLIENT_BUFFER_SIZE CHUNK_SIZE + 2000

void server_setup(int *server_socket, int server_port, int *epoll_fd)
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
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

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

    event.events = EPOLLIN;
    event.data.ptr = server_event_data;

    if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, *server_socket, &event) < 0)
        err_n_die("epoll_ctl error");
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

void readChunkFile(const char *chunkname, int connfd)
{
    printf("chuj, czy to sie wypisze?\n");
    int fd;
    int32_t bytes_read;
    char buffer[MAXLINE + 1];

    if ((fd = open(chunkname, O_RDONLY)) == -1)
        err_n_die("open error");

    if ((bytes_read = bulk_read(fd, buffer, MAXLINE)) == -1)
        err_n_die("read error");

    buffer[bytes_read] = '\0';

    close(fd);

    printf("chuj, czy to sie wypisze2?\n");

    write_len_and_data(connfd, bytes_read, buffer);

    printf("chuj, czy to sie wypisze5?\n");

    // int bytes_written;
    // if ((bytes_written = write(connfd, buffer, bytes_read)) == -1)
    //     err_n_die("write error");
    // printf("should have sent bytes_read: %d, bytes_written: %d\n", bytes_read, bytes_written);
}

void processReadRequest(char *path, int id, int connfd)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/%d/chunks/%s%d.chunk", replica_port, path, id);
    printf("chunkname: %s\n", chunkname);
    readChunkFile(chunkname, connfd);
}

void writeChunkFile(const char *filepat, uint8_t *data, int length)
{
    // printf("PRINTING\n\n\n");
    // printf("%s", data);
    // exit(1);
    int fd, n;

    if ((fd = open(filepat, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("filefd error");

    printf("imo tu sie wyjebie\n");
    if ((n = bulk_write(fd, data, length)) == -1)
        err_n_die("read error");

    close(fd);
}

void processWriteRequest(char *path, int id, uint8_t *data, int length, Chunk *chunk)
{
    char chunkname[MAXLINE + 1];
    snprintf(chunkname, sizeof(chunkname), "data_replica1/%d/chunks/%s%d.chunk",
             replica_port, path, id);
    printf("chunkname: %s\n", chunkname);
    writeChunkFile(chunkname, data, length);
}

// int forwardChunk(Chunk *chunk, uint32_t proto_len, uint8_t *proto_buf,
//                  uint32_t chunk_content_len, uint8_t *chunk_content_buf)
int forwardChunk(Chunk *chunk, uint32_t payload_size, uint8_t *buffer)
{
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
            printf("skipping this replica\n");
            continue;
        }
        // setup_connection(&replicafd, chunk->replicas[i]->ip, chunk->replicas[i]->port);
        // set_fd_nonblocking(replicafd); // to jest do zrobienia !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        
        printf("payload_size=%d\n", payload_size);
        printf("w tym miejscu bedzie zle\n");
        write_len_and_data(replicafd, payload_size, buffer); // to jest do wyjebania

        printf("before reading ack char\n");

        read(replicafd, &recvchar, 1); // to jest  do wyjebania

        printf("after reading ack char\n");

        if (recvchar == 'y')
            printf("received acknowledgement of receiving chunk, %c\n", recvchar);
        else
            success = 0;

        close(replicafd);
    }
    return success;
}

void process_request(int epoll_fd, event_data_t *event_data)
{
    /*
    event_data->client_data->buffer:
        
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
    uint8_t     *buffer = event_data->client_data->buffer;
    uint8_t     *proto_buf, *chunk_content_buf;
    int         current_offset = 0;

    op_type = buffer[0];
    current_offset += 1;

    memcpy(&proto_len, buffer + current_offset, sizeof(uint32_t));
    proto_len = ntohl(proto_len);
    current_offset += sizeof(uint32_t);

    proto_buf = (uint8_t *)malloc(proto_len * sizeof(uint8_t));
    memcpy(proto_buf, buffer + current_offset, proto_len);                   // here it's 18890640
    current_offset += proto_len;

    if (op_type == 'r')
    {
        printf("received read request\n");
     
        ChunkRequest *chunkRequest = chunk_request__unpack(NULL, proto_len, proto_buf);
        if (!chunkRequest)
            err_n_die("process_request, chunkRequest is null");

        printf("chunkRequest->path: %s\n", chunkRequest->path);
        printf("chunkRequest->chunk_id: %d\n", chunkRequest->chunk_id);

        processReadRequest(chunkRequest->path, chunkRequest->chunk_id, event_data->client_data->client_socket);
        // REPLACE THIS <-----------------------------------------------------------
        // REPLACE THIS <-----------------------------------------------------------
        // REPLACE THIS <-----------------------------------------------------------
        return;
    }
    else if (op_type == 'w' || op_type == 'd')
    {
        printf("received %c request\n", op_type);

        memcpy(&chunk_content_len, buffer + current_offset, sizeof(uint32_t));
        chunk_content_len = ntohl(chunk_content_len);
        current_offset += sizeof(uint32_t);

        chunk_content_buf = (uint8_t *)malloc(chunk_content_len * sizeof(uint8_t));
        memcpy(chunk_content_buf, buffer + current_offset, chunk_content_len);

        // n = bulk_read(connfd, &proto_len, sizeof(proto_len));
        // proto_len = ntohl(proto_len);
        // printf("proto_len: %d\n", proto_len);

        // memset(recvline, 0, MAXLINE);
        // n = bulk_read(connfd, recvline, proto_len);

        Chunk *chunk = chunk__unpack(NULL, proto_len, proto_buf);

        if (!chunk)
            err_n_die("process_request, chunk is null");

        // printf("chunk->path: %s\n", chunk->path);
        printf("chunk->chunk_id: %d\n", chunk->chunk_id);
        printf("chunk->n_replicas: %ld\n", chunk->n_replicas);

        for (int i = 0; i < chunk->n_replicas; i++)
        {
            printf("Name: %s IP: %s Port: %d Is_primary: %d\n",
                   chunk->replicas[i]->name, chunk->replicas[i]->ip,
                   chunk->replicas[i]->port, chunk->replicas[i]->is_primary);
        }

        // n = bulk_read(connfd, &buf_len, sizeof(buf_len));
        // buf_len = ntohl(buf_len);
        // printf("buf_len: %d\n", buf_len);

        // memset(recvline, 0, MAXLINE);
        // n = bulk_read(connfd, recvline, buf_len);

        // printf("received chunk:\n%s\n", recvline);

        // processWriteRequest("dummypath", chunk->chunk_id, recvline, buf_len, chunk);
        processWriteRequest(chunk->path, chunk->chunk_id, chunk_content_buf, chunk_content_len, chunk);

        // chuj z replikacją (duplikacją)
        if (op_type == 'w')
        {
            // int res = forwardChunk(chunk, proto_len, proto_buf, chunk_content_len, chunk_content_buf);
            int res = forwardChunk(chunk, event_data->client_data->payload_size, buffer);
            
            // uncomment this !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
            // CommitChunk commit = COMMIT_CHUNK__INIT;
            // commit.success = res;
            // commit.chunk_id = chunk->chunk_id;

            // if (res)
            //     commit.replicas_success = chunk->replicas;
            // else
            //     commit.replicas_fail = chunk->replicas;

            // msg_len = commit_chunk__get_packed_size(&commit);
            // proto_buf = (uint8_t *)malloc(msg_len * sizeof(uint8_t));
            // commit_chunk__pack(&commit, proto_buf);


            // REPLACE THIS
            // masterfd = connect_with_master();
            // write_len_and_data(masterfd, msg_len, proto_buf);
            // close(masterfd);
        }
        else
        {
            char send_char = 'y';
            int n = bulk_write(event_data->client_data->client_socket, &send_char, 1);
            printf("sent acknowledgement of receiving chunk\n");
        }

        // DUPA DUPA DUPA
        // if (strcmp(operation_type_buff, "write") == 0)
        // {
        //     char send_char = 'y';
        //     n = bulk_write(connfd, &send_char, 1);
        //     printf("sent acknowledgement of receiving chunk\n");
        // }
    }
    else
        err_n_die("wrong operation type");

    // else if (strcmp(operation_type_buff, "write_primary") == 0 || strcmp(operation_type_buff, "write") == 0)
}

void handle_client(int epoll_fd, event_data_t *event_data)
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
        process_request(epoll_fd, event_data);
    else if (event_data->client_data->bytes_stored > event_data->client_data->payload_size) /*multi-queries clients - TODO*/
        err_n_die("undefined");
}

int main(int argc, char **argv)
{
    int server_socket, epoll_fd, running = 1;
    struct epoll_event events[MAX_EVENTS];

    if (argc >= 2)
        replica_port = atoi(argv[1]);

    server_setup(&server_socket, replica_port, &epoll_fd);

    while (running)
    {
        printf("\n Replica %d polling for events \n", replica_port);

        // MAX_EVENTS: 1000, przyjdzie na raz 30
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // connect
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
                handle_client(epoll_fd, event_data);
            }
        }
    }
}