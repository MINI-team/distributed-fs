#include "common.h"

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

void err_n_die(const char *fmt, ...)
{
    int errno_save;
    va_list     ap;

    errno_save = errno;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);

    if (errno_save != 0)
    {
        fprintf(stdout, "(errno = %d) : %s\n", errno_save, 
        strerror(errno_save));
        fprintf(stdout, "\n");
        fflush(stdout);
    }

    va_end(ap);

    exit(1);
}

char *bin2hex(const unsigned char *input, size_t len)
{
    char *result;
    char *hexits = "0123456789ABCDEF";

    if (input == NULL || len <= 0)
    {
        return NULL;
    }

    int resultlength = (len*3)+1;

    result = malloc(resultlength);
    bzero(result, resultlength);

    for (int i = 0; i < len; i++)
    {
        result[i*3]   = hexits[input[i] >> 4];
        result[i*3+1] = hexits[input[i] & 0x0F];
        result[i*3+2] = ' ';
    }

    return result;
}

int set_fd_nonblocking(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, flags)) < 0)
        err_n_die("fcntl error");
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

char *resolve_host(char *host_name) {
    struct hostent *host_entry;
    static char IPbuffer[INET_ADDRSTRLEN];
    host_entry = gethostbyname(host_name);
    if (host_entry == NULL) {
        fprintf(stderr, "Error: Unable to resolve host %s\n", host_name);
        return NULL;
    }
    strcpy(IPbuffer, inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0])));
    return IPbuffer;
}

void debug_log(FILE *debugfd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(debugfd, fmt, ap);
    va_end(ap);
}

int bulk_read(int fd, void *buf, int count)
{
    int c;
    int len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

// int bulk_write(int fd, void *buf, int count)
// {
//     printf("count: %d\n", count); //32000000
//     int c;
//     int len = 0;
//     do
//     {
//         // c = TEMP_FAILURE_RETRY(write(fd, buf, count));
//         c = write(fd, buf, count); // this return -1
//         printf("tutaj c: %d\n", c);
//         if (c < 0)
//             return c;
//         buf += c;
//         len += c;
//         count -= c;
//     } while (count > 0);
//     return len;
// }

ssize_t bulk_write(int fd, const void *buf, size_t count)
{
    printf("count: %zu\n", count);

    const unsigned char *p = buf;  // safer for pointer arithmetic
    ssize_t total_written  = 0;

    while (count > 0)
    {
        ssize_t c = write(fd, p, count);
        if (c < 0) {
            // If interrupted by a signal, you might want to continue
            if (errno == EINTR) {
                continue;  // just retry
            }
            // Otherwise, report error and return
            fprintf(stderr, "write() failed: %s\n", strerror(errno));
            return -1;
        }
        // c == 0 would mean no more can be written (e.g. broken pipe)
        if (c == 0) {
            fprintf(stderr, "No more data could be written.\n");
            return total_written;
        }
        p             += c;
        total_written += c;
        count         -= c;
    }
    return total_written;
}

// int bulk_write_nonblock(client_data_t *client_data)
// {
//     int c;
//     do
//     {
//         c = TEMP_FAILURE_RETRY(write(client_data->client_socket, 
//             client_data->out_buffer + client_data->bytes_sent, 
//             client_data->left_to_send
//         ));
        
//         if (c < 0)
//         {
//             if (errno == EAGAIN || errno == EWOULDBLOCK)
//             {
//                 printf("EAGAIN/EWOULDBLOCK, returning from bulk_write_nonblock to go towards epoll_wait\n");
//                 return -1;
//             }
//             err_n_die("read error"); // maybe replace THIS <--------------------------------
//             // -----------------------------------------------------------------------------
//         }
        
//         client_data->bytes_sent += c;
//         client_data->left_to_send -= c;
//     } while (client_data->left_to_send > 0);
//     return client_data->bytes_sent;
// }

int bulk_write_nonblock(int fd, void *buf, int *bytes_sent, int *left_to_send)
{
    int c;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf + *bytes_sent, *left_to_send));
        if (c < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("EAGAIN/EWOULDBLOCK, returning from bulk_write_nonblock to go towards epoll_wait\n");
                return -1;
            }
            err_n_die("read error");
        }

        *bytes_sent += c;
        *left_to_send -= c;
    } while (*left_to_send > 0);
    return *bytes_sent;
}

void abort_with_cleanup(char *msg, int serverfd)
{
    printf("%s\n",msg);
    close(serverfd);
    exit(1);
}

uint32_t read_payload_size(int fd)
{
    /* We expect that first four bytes coming should be an integer declaring payload */
    uint32_t net_payload;
    int bytes_read;
    // printf("wejdzie\n");
    if ((bytes_read = read(fd, &net_payload, sizeof(net_payload))) < 0)
        err_n_die("read error");

    printf("bytes_read: %d\n", bytes_read);
    // printf("a to nie wejdzie\n");
    if (bytes_read != sizeof(net_payload))
        abort_with_cleanup("Sever sent incomplete payload size\n Try again\n", fd);

    return ntohl(net_payload);
}

void read_paylaod_and_data(int fd, uint8_t **buffer, uint32_t *payload)
{
    uint32_t bytes_read = 0;
    printf("reading payload\n");
    (*payload) = read_payload_size(fd);
    printf("payload received: %d \n", *payload);
    *buffer = (uint8_t *)malloc((*payload) * sizeof(uint8_t));

    if ((bytes_read = bulk_read(fd, *buffer, *payload)) != *payload)
        err_n_die("read_paylaod_and_data bytes_read: %d, payload: %d\n", bytes_read, *payload);
}

void write_len_and_data(int fd, uint32_t len, uint8_t *data)
{
    int net_len = htonl(len), sent;

    if((sent = bulk_write(fd, &net_len, sizeof(net_len))) != (int)sizeof(net_len))
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, (int)sizeof(net_len));

    // printf("writing to replica OK\nwrote %d (/%d) bytes\n", sent, (int)sizeof(net_len));
    if ((sent = bulk_write(fd, data, len)) != len)
    {
        printf("no i sie nie udalo\n");
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, len);
    }
    else
    {
        printf("write_len_and_data_succeeded\n");
    }
}

void setup_connection(int *server_socket, char *ip, uint16_t port)
{
    struct sockaddr_in servaddr;
#ifdef DOCKER
    ip = resolve_host(ip);
#endif
    
    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("IP: %s, Port: %d\n", ip, port);
        err_n_die("connect error");
    }
}

int setup_connection_retry(int *server_socket, char *ip, uint16_t port)
{
    struct sockaddr_in servaddr;
#ifdef DOCKER
    ip = resolve_host(ip);
#endif
    
    if ((*server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error");
    
    if (connect(*server_socket, (SA *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("IP: %s, Port: %d unavailable!! Retrying with different one\n", ip, port);
        return -1;
    }
    
    return 0;
}

int64_t file_size(int filefd)
{
    struct stat file_stat;

    if (fstat(filefd, &file_stat))
    {
        close(filefd);
        err_n_die("fstat error");
    }

    return (int64_t)file_stat.st_size;
}
