#ifndef _COMMON_H_
#define _COMMON_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netdb.h>

// #define SERVER_PORT 18000

#define MAXLINE 40096 // ADDITIONAL ZERO?
#define SERVER_PORT 9001
#define SA struct sockaddr

#define CHUNK_SIZE 42

void err_n_die(const char *fmt, ...);
char* bin2hex(const unsigned char *input, size_t len);
int set_fd_nonblocking(int fd);

#endif
