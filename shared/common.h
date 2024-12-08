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
#define MAX_FILENAME_LENGTH 20 // to be deleted
//#define MASTER_ADDRESS "127.0.0.1"
#define MASTER_ADDRESS "server_container"
#define REPLICA_ADDRESS "replica_container"
// #define MASTER_PORT 9001

#define MASTER_SERVER_IP "127.0.0.1"
#define MASTER_SERVER_PORT 9001

#define CHUNK_SIZE 5

#define REPLICAS_COUNT 2

#define IP_LENGTH 16 // 15 + 1 for a null terminator
#define SA struct sockaddr

void err_n_die(const char *fmt, ...);
char* bin2hex(const unsigned char *input, size_t len);
int set_fd_nonblocking(int fd);
void write_len_and_data(int fd, int len, uint8_t *data);
char* resolve_host(char* host_name);

#endif      