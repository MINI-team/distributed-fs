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

int bulk_read(int fd, char *buf, int count)
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

int bulk_write(int fd, char *buf, int count)
{
    int c;
    int len = 0;
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

void abort_with_cleanup(char *msg, int serverfd)
{
    printf("%s\n",msg);
    close(serverfd);
    exit(1);
}

int32_t read_payload_size(int fd)
{
    /* We expect that first four bytes coming should be an integer declaring payload */
    int32_t net_payload;
    int bytes_read;
    // printf("wejdzie\n");
    if ((bytes_read = read(fd, &net_payload, sizeof(net_payload))) < 0)
        err_n_die("read error");
    // printf("a to nie wejdzie\n");
    if (bytes_read != sizeof(net_payload))
        abort_with_cleanup("Sever sent incomplete payload size\n Try again\n", fd);

    return ntohl(net_payload);
}

void read_paylaod_and_data(int fd, uint8_t **buffer, int32_t *payload)
{
    int32_t bytes_read = 0;
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

    printf("chuj, czy to sie wypisze3?\n");
    if((sent = bulk_write(fd, &net_len, sizeof(net_len))) != (int)sizeof(net_len))
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, (int)sizeof(net_len));

    printf("chuj, czy to sie wypisze4?, len = %d\n", len);
    // printf("writing to replica OK\nwrote %d (/%d) bytes\n", sent, (int)sizeof(net_len));
    if ((sent = bulk_write(fd, data, len)) != len)
    {
        printf("dupa\n");
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, len);
    }


    printf("bufffer\n");
    

    // printf("writing to replica OK\nwrote %d(/%d) bytes\n", sent, len);
    // err = write(fd, data, len);
}