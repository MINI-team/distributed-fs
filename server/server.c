#include <stdio.h>
#include <glib.h>
#include <malloc.h>
#include <sys/epoll.h>

#include "common.h"

#define MAX_EVENTS 5
#define READ_SIZE 1024

int main()
{   
    int                 server_socket, client_socket, n;
    struct sockaddr_in  servaddr;
    size_t              bytes_read

    int                 epoll_fd, running = 1;
    struct epoll_event  event, events[MAX_EVENTS];

    
}