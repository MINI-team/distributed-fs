#include "common.h"

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

void write_len_and_data(int fd, int len, uint8_t *data)
{
    int net_len = htonl(len), sent;

    if((sent = write(fd, &net_len, sizeof(net_len))) != (int)sizeof(net_len))
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, (int)sizeof(net_len));

    // printf("writing to replica OK\nwrote %d (/%d) bytes\n", sent, (int)sizeof(net_len));
    if ((sent = write(fd, data, len)) != len)
        err_n_die("writing length didn't succeed\nwrote %d bytes, but should've written %d\n",
                  sent, len);

    // printf("writing to replica OK\nwrote %d(/%d) bytes\n", sent, len);
    // err = write(fd, data, len);
}

char* resolve_host(char* host_name) {
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