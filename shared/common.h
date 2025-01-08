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
#include <stdbool.h>

// #define SERVER_PORT 18000

#define MAXLINE 400096 // ADDITIONAL ZERO?
#define MAX_FILENAME_LENGTH 200 // to be deleted
//#define MASTER_ADDRESS "127.0.0.1"
// #define MASTER_ADDRESS "server_container"
// #define REPLICA_ADDRESS "replica_container"
// #define MASTER_PORT 9001

// #define MASTER_SERVER_IP "127.0.0.1"
// #define MASTER_SERVER_PORT 9001

// #define DOCKER

#ifdef DOCKER
#define MASTER_SERVER_IP "server_container"
#define REPLICA_SERVER_IP_0 "replica_container_0"
#define REPLICA_SERVER_IP_1 "replica_container_1"
#define REPLICA_SERVER_IP_2 "replica_container_2"
#define REPLICA_SERVER_IP_3 "replica_container_3"
#define REPLICA_SERVER_IP_4 "replica_container_4"
#define CLIENT_IP "client_container"

#define MASTER_SERVER_PORT 9001
#define REPLICA_SERVER_PORT_0 8080
#define REPLICA_SERVER_PORT_1 8081
#define REPLICA_SERVER_PORT_2 8082
#define REPLICA_SERVER_PORT_3 8083
#define REPLICA_SERVER_PORT_4 8084
#else 
/* local development */
#define MASTER_SERVER_IP "127.0.0.1"
#define REPLICA_SERVER_IP_0 "127.0.0.1"
#define REPLICA_SERVER_IP_1 "127.0.0.1"
#define REPLICA_SERVER_IP_2 "127.0.0.1"
#define REPLICA_SERVER_IP_3 "127.0.0.1"
#define REPLICA_SERVER_IP_4 "127.0.0.1"

#define MASTER_SERVER_PORT 9001
#define REPLICA_SERVER_PORT_0 8080
#define REPLICA_SERVER_PORT_1 8081
#define REPLICA_SERVER_PORT_2 8082
#define REPLICA_SERVER_PORT_3 8083
#define REPLICA_SERVER_PORT_4 8084
#endif

#define DEBUG

// #define CHUNK_SIZE 4096
// #define CHUNK_SIZE 4096
// #define CHUNK_SIZE 3554432
// #define CHUNK_SIZE 355443
// #define CHUNK_SIZE 100000 // imp
#define CHUNK_SIZE 300000
// #define CHUNK_SIZE 32760

// #define CHUNK_SIZE 1000

#define MAX_THREADS_COUNT 16

#define REPLICAS_COUNT 2

#define IP_LENGTH 16 // 15 + 1 for a null terminator
#define SA struct sockaddr

void err_n_die(const char *fmt, ...);
char *bin2hex(const unsigned char *input, size_t len);
int set_fd_nonblocking(int fd);
void write_len_and_data(int fd, uint32_t len, uint8_t *data);
char *resolve_host(char *host_name);
// void debug_log(int debugfd, const char *fmt, ...);
void debug_log(FILE *debugfd, const char *fmt, ...);

int bulk_read(int fd, char *buf, int count);
int bulk_write(int fd, char *buf, int count);
#endif
