/*******************************************************
 * epoll_server.c
 * To compile:
 *     gcc -o epoll_server epoll_server.c
 * To run:
 *     ./epoll_server
 *******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define PORT 8080
#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

/* Set a file descriptor to non-blocking mode */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL) failed");
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL) failed");
        return -1;
    }

    return 0;
}

int main(void)
{
    int server_fd, epoll_fd;
    struct sockaddr_in server_addr;
    struct epoll_event event, events[MAX_EVENTS];

    /* 1. Create the server socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Allow reuse of port */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 2. Bind to the specified port */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 3. Listen on the server socket */
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    /* 4. Set the server socket to non-blocking */
    if (set_nonblocking(server_fd) < 0) {
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 5. Create the epoll instance */
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 6. Add the server socket to the epoll set to monitor incoming connections (EPOLLIN) */
    event.data.fd = server_fd;
    event.events  = EPOLLIN;  // We only care about read events for the listening socket
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl(ADD) server_fd failed");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    /* Event loop */
    while (1) {
        int n_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_ready < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, just continue
                continue;
            }
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < n_ready; i++) {
            int event_fd  = events[i].data.fd;
            uint32_t evts = events[i].events;

            /* 7A. Check if we have an incoming connection on the server socket */
            if (event_fd == server_fd) {
                // Accept all pending connections
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // No more pending connections
                            break;
                        } else {
                            perror("accept failed");
                            break;
                        }
                    }

                    // Make the client socket non-blocking
                    if (set_nonblocking(client_fd) < 0) {
                        close(client_fd);
                        continue;
                    }

                    // Add the new client socket to epoll, interested in reading (EPOLLIN)
                    event.data.fd = client_fd;
                    event.events  = EPOLLIN;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        perror("epoll_ctl(ADD) client_fd failed");
                        close(client_fd);
                        continue;
                    }

                    printf("Accepted new client (fd = %d)\n", client_fd);
                }
            }
            /* 7B. Otherwise, it's an event on one of the connected client sockets */
            else {
                /* Handle EPOLLIN: client is ready for reading */
                if (evts & EPOLLIN) {
                    char buffer[BUFFER_SIZE];
                    ssize_t bytes_read = read(event_fd, buffer, sizeof(buffer));

                    if (bytes_read > 0) {
                        printf("Received %zd bytes from client %d\n", bytes_read, event_fd);

                        // Store data somewhere or process it. 
                        // For simplicity, we immediately try to write it back.
                        // We will change interest to EPOLLOUT for this socket to write next time around.
                        
                        // Here, we re-arm the socket for EPOLLOUT so we can write in next epoll_wait
                        event.data.fd = event_fd;
                        event.events  = EPOLLOUT; 
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_fd, &event) == -1) {
                            perror("epoll_ctl(MOD) -> EPOLLOUT failed");
                            close(event_fd);
                            continue;
                        }

                        // Optionally store the data to be written later if partial writes are a concern.
                        // For demonstration, we'll just keep the data in a temporary buffer on the stack.
                        // In a real server, you'd keep a per-connection buffer.

                    } else if (bytes_read == 0) {
                        // Client disconnected
                        printf("Client %d disconnected\n", event_fd);
                        close(event_fd);
                    } else {
                        // Error or EAGAIN/EWOULDBLOCK
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("read failed");
                            close(event_fd);
                        }
                    }
                }

                /* Handle EPOLLOUT: client is ready for writing */
                if (evts & EPOLLOUT) {
                    // In a real-world scenario, you'd have a buffer of data to write. 
                    // Here we just send a simple message or echo what was just read.

                    const char *msg = "Server echo: Hello from epoll server!\n";
                    ssize_t bytes_to_write = strlen(msg);
                    ssize_t bytes_written   = write(event_fd, msg, bytes_to_write);

                    if (bytes_written == -1) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("write failed");
                            close(event_fd);
                            continue;
                        }
                    } else {
                        printf("Wrote %zd bytes to client %d\n", bytes_written, event_fd);
                    }

                    // After writing, switch back to EPOLLIN to handle new incoming data
                    event.data.fd = event_fd;
                    event.events  = EPOLLIN; 
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_fd, &event) == -1) {
                        perror("epoll_ctl(MOD) -> EPOLLIN failed");
                        close(event_fd);
                    }
                }

                /* Handle EPOLLERR or EPOLLHUP or other error conditions */
                if (evts & (EPOLLERR | EPOLLHUP)) {
                    perror("EPOLLERR or EPOLLHUP occurred");
                    close(event_fd);
                }
            }
        }
    }

    /* Clean up (unreachable in this example unless error occurs) */
    close(server_fd);
    close(epoll_fd);
    return 0;
}
