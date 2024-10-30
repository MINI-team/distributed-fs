#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <arpa/inet.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
    (__extension__({                               \
        long int __result;                         \
        do                                         \
            __result = (long int)(expression);     \
        while (__result == -1L && errno == EINTR); \
        __result;                                  \
    }))
#endif

#define ERR(source) (perror(source), fprintf(stdout, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))
#define BACKLOG 3
#define MAX_EVENTS 16
#define PORT 8080
#define ADDRESS "192.168.0.38"
#define BINARY_NUMBER_LENGTH 32
#define CHUNK_SIZE 10
#define NUMBER_OF_CHUNKS 48
#define NUMBER_OF_MB 32
#define BYTES_IN_MB 1048576
#define FILENAME_MAX_LEN 20

volatile sig_atomic_t do_work = 1;

ssize_t bulk_read(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (0 == c)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

int file_exists(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return 1;  // File exists
    }
    return 0;      // File does not exist
}

size_t getFileSize(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Could not open file");
        return 0;
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fclose(file);

    return size;
}

char* read_chunk_file(const char* filename, size_t size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) ERR("open file");

    char* buffer = (char*)malloc(size);
    if (!buffer) ERR("malloc");

    ssize_t bytes_read = bulk_read(fd, buffer, size);
    if (bytes_read < 0) {
        free(buffer);
        ERR("read file");
    }

    close(fd);
    return buffer;
}

void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) { fprintf(stderr, "USAGE: %s socket port\n", name); }

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

int make_tcp_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

struct sockaddr_in make_address(char *address, char *port)
{
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if ((ret = getaddrinfo(address, port, &hints, &result)))
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}

int bind_tcp_socket(int backlog_size)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_tcp_socket();
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(ADDRESS);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, backlog_size) < 0)
        ERR("listen");
    return socketfd;
}

int add_new_client(int sfd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}

int binary_to_int(const char *binary_number) {
    int result = 0;
    for (int i = 0; i < BINARY_NUMBER_LENGTH; i++) {
        result = (result << 1) | (binary_number[i] - '0');
    }

    return result;
}
void int_to_binary_string(int number, char *binary_str) {
    for (int i = 0; i < BINARY_NUMBER_LENGTH; i++) {
        binary_str[BINARY_NUMBER_LENGTH - i - 1] = (number & 1) ? '1' : '0';
        number >>= 1; 
    }
}

void print_char_array(const char array[], int length) {
    for (int i = 0; i < length; i++) {
        printf("%c", array[i]);
    }
    printf("\n"); 
}

void doServer(int tcp_listen_socket)
{
    int epoll_descriptor;
    if ((epoll_descriptor = epoll_create1(0)) < 0)
    {
        ERR("epoll_create:");
    }
    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;

    event.data.fd = tcp_listen_socket;
    if (epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, tcp_listen_socket, &event) == -1)
    {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    int nfds;
    char message_length_binary[BINARY_NUMBER_LENGTH];
    int message_length;
    char chunk_number_binary[BINARY_NUMBER_LENGTH];
    int chunk_number_int;
    char *ok_response = "ok";
    char *not_ok_response = "not ok";
    int ok_response_length_int = strlen(ok_response);
    int not_ok_response_length_int = strlen(not_ok_response);
    char ok_response_length_binary[BINARY_NUMBER_LENGTH];
    char not_ok_response_length_binary[BINARY_NUMBER_LENGTH];
    int_to_binary_string(ok_response_length_int, ok_response_length_binary);
    int_to_binary_string(not_ok_response_length_int, not_ok_response_length_binary);
    print_char_array(ok_response_length_binary, BINARY_NUMBER_LENGTH);
    print_char_array(not_ok_response_length_binary, BINARY_NUMBER_LENGTH);
    ssize_t size;
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    char filename[FILENAME_MAX_LEN];

    while (do_work)
    {
        if ((nfds = epoll_pwait(epoll_descriptor, events, MAX_EVENTS, -1, &oldmask)) > 0)
        {
            for (int n = 0; n < nfds; n++)
            {
                int client_socket = add_new_client(events[n].data.fd);
                printf("New client was connected\n");

                if ((size = bulk_read(client_socket, chunk_number_binary, BINARY_NUMBER_LENGTH)) < 0)
                    ERR("read:");
                
                printf("Received string from client: \n");
                print_char_array(chunk_number_binary, BINARY_NUMBER_LENGTH);
                
                chunk_number_int = binary_to_int(chunk_number_binary);
                printf("Received chunk number is %d\n", chunk_number_int);
                snprintf(filename, FILENAME_MAX_LEN, "%d.chunk", chunk_number_int);

                if(chunk_number_int >= NUMBER_OF_CHUNKS || 0 == file_exists(filename)){
                    printf("Incorrect chunk number\n");
                    printf("No ok response will be sent with length: \n");
                    print_char_array(not_ok_response_length_binary, BINARY_NUMBER_LENGTH);
                    printf("Sending length of no ok response...\n");
                    if (bulk_write(client_socket, not_ok_response_length_binary, BINARY_NUMBER_LENGTH) < 0 && errno != EPIPE){
                        printf("Errors while sending no ok response length");
                        ERR("write:");
                    }
                    printf("No ok response will be sent\n");
                    if (bulk_write(client_socket, not_ok_response, not_ok_response_length_int) < 0 && errno != EPIPE){   
                        printf("Errors while sending not ok response");
                        ERR("write:");
                    }
                    printf("No ok response was sent\n");
                } else {
                    printf("Chunk number is correct\n");
                    printf("Ok response will be sent with length: \n");
                    print_char_array(ok_response_length_binary, BINARY_NUMBER_LENGTH);
                    printf("Sending length of ok response...\n");
                    if (bulk_write(client_socket, ok_response_length_binary, BINARY_NUMBER_LENGTH) < 0 && errno != EPIPE){
                        printf("Errors while sending ok response length");
                        ERR("write:");
                    }
                    printf("Ok response will be send sent\n");
                    if (bulk_write(client_socket, ok_response, ok_response_length_int) < 0 && errno != EPIPE){
                        printf("Errors while sending ok response");
                        ERR("write:");
                    }
                    printf("ok response was sent\n");
                    // TO DO: send length of data
                    size_t fileSize = getFileSize(filename);
                    int_to_binary_string(fileSize, message_length_binary);
                    printf("Data length (%zu) will be send sent\n", fileSize);
                    if (bulk_write(client_socket, message_length_binary, BINARY_NUMBER_LENGTH) < 0 && errno != EPIPE){
                        printf("Errors while sending data");
                        ERR("write:");
                    }
                    // send data:
                    char* chunk_data = read_chunk_file(filename, fileSize);
                    if (chunk_data == NULL) {
                        fprintf(stderr, "Failed to read chunk file: %s\n", filename);
                        return EXIT_FAILURE;
                    }

                    printf("Data will be send sent\n");
                    if (bulk_write(client_socket, chunk_data, fileSize) < 0 && errno != EPIPE){
                        printf("Errors while sending data");
                        ERR("write:");
                    }
                    printf("Data was sent");
                }


                if (TEMP_FAILURE_RETRY(close(client_socket)) < 0)
                    ERR("close");
            }
        }
        else
        {
            if (errno == EINTR)
                continue;
            ERR("epoll_pwait");
        }
    }
    if (TEMP_FAILURE_RETRY(close(epoll_descriptor)) < 0)
        ERR("close");
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main() {
    int tcp_listen_socket;
    int new_flags;
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    tcp_listen_socket = bind_tcp_socket(BACKLOG);
    new_flags = fcntl(tcp_listen_socket, F_GETFL) | O_NONBLOCK;
    fcntl(tcp_listen_socket, F_SETFL, new_flags);
    doServer(tcp_listen_socket);
    if (TEMP_FAILURE_RETRY(close(tcp_listen_socket)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
