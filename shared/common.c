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

int bulk_write(int fd, const void *buf, int count)
{
    print_logs(COM_DEF_LVL, "count: %zu\n", count);

    const unsigned char *p = buf;  // safer for pointer arithmetic
    int total_written  = 0;

    while (count > 0)
    {
        int c = write(fd, p, count); // is this write going to actually white until server reads the whole thing, using read()?
        print_logs(COM_DEF_LVL, "bulk_write: after write, c = %d\n", c);
        if (c < 0) {
            
            if (errno == EPIPE || errno == ECONNRESET)
            {
                return -2;
            }
            err_n_die("bulk_write error");
        }
        if (c == 0) {
            fprintf(stderr, "No more data could be written.\n");
            return -2; // TODO: why -2 here?
            // return total_written;
        }
        p             += c;
        total_written += c;
        count         -= c;
    }
    return total_written;
}

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
                print_logs(COM_DEF_LVL, "EAGAIN/EWOULDBLOCK, returning from bulk_write_nonblock to go towards epoll_wait\n");
                return -1;
            }
            if (errno == EPIPE || errno == ECONNRESET)
            {
                print_logs(3, "EPIPE/ECONNRESET in bulk_write_nonblock, broken pipe\n");
                return -2;
            }
            err_n_die("write error");
        }

        *bytes_sent += c;
        *left_to_send -= c;
    } while (*left_to_send > 0);
    return *bytes_sent;
}

void abort_with_cleanup(char *msg, int serverfd)
{
    print_logs(COM_DEF_LVL, "%s\n",msg);
    close(serverfd);
    exit(1);
}

int read_payload_size(int fd, bool *timeout)
{
    /* We expect that first four bytes coming should be an integer declaring payload */
    int net_payload;
    int bytes_read;

    if ((bytes_read = read(fd, &net_payload, sizeof(net_payload))) < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            print_logs(COM_DEF_LVL, "\n\nread_payload_size, TIMEOUT\n\n");
            *timeout = true;
            return 0;
        }
        else if (errno == EPIPE || errno == ECONNRESET) // TODO read shouldn't result in EPIPE
        {
            print_logs(4, "EPIPE/ECONNRESET in bulk_read, broken pipe\n");
            return -2;
        }
        err_n_die("read error 230");
    }
    print_logs(COM_DEF_LVL, "bytes_read: %d\n", bytes_read);

    if (bytes_read != sizeof(net_payload))
        // abort_with_cleanup("Server sent incomplete payload size\n Try again\n", fd);
        return -2;

    return ntohl(net_payload);
}

bool read_payload_and_data(int fd, uint8_t **buffer, uint32_t *payload)
{
    bool timeout = false;
    uint32_t bytes_read = 0;
    print_logs(COM_DEF_LVL, "reading payload\n");
    (*payload) = read_payload_size(fd, &timeout);
    if (timeout)
        return timeout;
    print_logs(COM_DEF_LVL, "payload received: %d \n", *payload);
    *buffer = (uint8_t *)malloc((*payload) * sizeof(uint8_t));

    if ((bytes_read = bulk_read(fd, *buffer, *payload)) != *payload)
    {
        print_logs(COM_DEF_LVL, "\n\nread_payload_and_data: bulk_read TIMEOUT\n\n");
        return true;
    }

    return false;
}

int write_len_and_data(int fd, uint32_t len, uint8_t *data)
{
    int net_len = htonl(len), sent;

    if((sent = bulk_write(fd, &net_len, sizeof(net_len))) != (int)sizeof(net_len))
    {
        if (sent == -2)
            return -2;
        err_n_die("writing payload size didn't succeed\nwrote %d bytes, but should've written %d\n",
            sent, (int)sizeof(net_len));
    }

    if ((sent = bulk_write(fd, data, len)) != len)
    {
        if (sent == -2)
            return -2;
        err_n_die("writing data didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, len);
    }
    return sent;
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
        print_logs(COM_DEF_LVL, "IP: %s, Port: %d\n", ip, port);
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
        print_logs(COM_DEF_LVL, "IP: %s, Port: %d unavailable!! Retrying with different one\n", ip, port);
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


const char *peer_type_to_string(peer_type_t peer_type)
{
    switch (peer_type)
    {
            case CLIENT_READ:       return "CLIENT_READ";
            case CLIENT_WRITE:      return "CLIENT_WRITE";
            case MASTER:            return "MASTER";
            case REPLICA_PRIMO:     return "REPLICA_PRIMO";
            case REPLICA_SECUNDO:   return "REPLICA_SECUNDO";
            case EL_PRIMO:          return "EL_PRIMO";
            default:                return "UNKNOWN";
    }
}

void print_logs(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    if (level <= LOG_LEVEL)
        vfprintf(stdout, fmt, ap);
    
    va_end(ap);
}

bool are_replicas_same(Replica *r1,  Replica *r2)
{
    return strcmp(r1->ip, r2->ip) == 0 && r1->port == r2->port;
}

int set_fd_blocking(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, flags)) < 0)
        err_n_die("fcntl error");
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}