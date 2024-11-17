#include <stdio.h>
#include <malloc.h>
#include <sys/epoll.h>

#include "common.h"

#define MAX_EVENTS 5
#define READ_SIZE 1024

#define MAX_CLIENTS 10

#define SINGLE_CLIENT_BUFFER_SIZE 20

int serverSetup(int *server_socket, int *epoll_fd, struct epoll_event *event)
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
    servaddr.sin_port = htons(SERVER_PORT);

    if (bind(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("bind error");
    
    if (listen(*server_socket, 10) < 0)
        err_n_die("listen error");
    
    if ((*epoll_fd = epoll_create1(0)) < 0)
        err_n_die("epoll_create1 error");

    event->events = EPOLLIN;
    event->data.fd = *server_socket;

    if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, *server_socket, event) < 0)
        err_n_die("epoll_ctl error");

    event->data.fd = 0;

    if (epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, 0, event) < 0)
        err_n_die("epoll_ctl error");

}

int main()
{   
    int                 server_socket, n;
    size_t              bytes_read;
    uint8_t             read_buffer[READ_SIZE + 1];        

    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];

    char                *clients_data[MAX_CLIENTS];
    int                 client_payload_size[MAX_CLIENTS];
    int                 current_client = -1;

    serverSetup(&server_socket, &epoll_fd, &event);
   
    while (running) {
        printf("Server polling for events \n");
        
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        printf("Ready events: %d \n", event_count);

        for (int i = 0; i < event_count; i++) {
            printf("Reading file descriptor: %d\n", events[i].data.fd);

            if (events[i].data.fd == server_socket) 
            {
                /* New client */
                int client_socket = accept(server_socket, (SA *)NULL, NULL);

                clients_data[++current_client] = (char *)malloc((SINGLE_CLIENT_BUFFER_SIZE+1) * sizeof(char));
                set_fd_nonblocking(client_socket);
                event.data.fd = client_socket;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event))
                    err_n_die("epol_ctl error");
                 
                // close(client_socket);
            }
            else if (events[i].data.fd > 0)
            {
                int client_socket = events[i].data.fd;    
                bytes_read = read(client_socket, clients_data[current_client], SINGLE_CLIENT_BUFFER_SIZE);

                if (bytes_read == 0) {
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event))
                        err_n_die("epoll_ctl error");
                    close(client_socket);
                    continue;
                }


                // client_payload_size[current_client] = (int)clients_data[0];

                printf("bytes read: %zu\n", bytes_read);
                // printf("Received: %s\n", read_buffer);

                printf("printiing the whole read buffer\n");
                for (int j = 0; j < SINGLE_CLIENT_BUFFER_SIZE; j++)
                    printf("%d: %c\n", j, clients_data[current_client][j]);
                printf("\n");

            }
            else if (events[i].data.fd == 0)
            {
                running = 0;
                break;
            }
            else
            {
                err_n_die("file descriptor error");
            }
        }
    }
    close(epoll_fd);
    close(server_socket);
}