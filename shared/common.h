#ifndef _COMMON_H_
#define _COMMON_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
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
#include "dfs.pb-c.h"

// #define SERVER_PORT 18000

#define MAX_FILENAME_LENGTH 201 // to be deleted
//#define MASTER_ADDRESS "127.0.0.1"
// #define MASTER_ADDRESS "server_container"
// #define REPLICA_ADDRESS "replica_container"
// #define MASTER_PORT 9001

// #define MASTER_SERVER_IP "127.0.0.1"
// #define MASTER_SERVER_PORT 9001

// #define DOCKER

// #define RELEASE

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

#define LOG_LEVEL 1

// #define COM_DEF_LVL 7
// #define MAS_DEF_LVL 6
// #define REP_DEF_LVL 7
// #define CLI_DEF_LVL 7

#define COM_DEF_LVL 7
#define MAS_DEF_LVL 1
#define REP_DEF_LVL 1
#define CLI_DEF_LVL 7

// #define CHUNK_SIZE 4096
// #define CHUNK_SIZE 4096
// #define CHUNK_SIZE 3554432
// #define CHUNK_SIZE 355443
// #define CHUNK_SIZE 100000 // imp
// #define CHUNK_SIZE 300000 // tested
// #define CHUNK_SIZE 3000000
// #define CHUNK_SIZE 32760

// #define CHUNK_SIZE 1000

// #define CHUNK_SIZE 1

#define CHUNK_SIZE 32000000 // 32MB zabije

#define MAX_THREADS_COUNT 32
// #define MAX_THREADS_COUNT 16 // uwaga na slabych komputerach to zabije
#define TIMEOUT_SEC 3
#define TIMEOUT_MSEC 0

#define REPLICATION_FACTOR 3

#define IP_LENGTH 16 // 15 + 1 for a null terminator
#define SA struct sockaddr

#define MAX_CONNECTIONS 1000000

typedef enum
{
    CLIENT_READ,
    CLIENT_WRITE,
    MASTER,
    REPLICA_PRIMO,
    REPLICA_SECUNDO,
    CLIENT_ACK,
    EL_PRIMO
} peer_type_t;

typedef struct event_data_t event_data_t;
typedef struct {
    int client_socket;
    int connection_id;
    
    event_data_t *true_client_event_data; // TODO: INITIALIZE AS NULL????????????????????
    int true_client_connection_id;

    // INBOUND
    uint8_t *buffer; // in buffer
    int payload_size;
    int bytes_stored;
    int space_left;
    bool reading_started;
    
    // OUTBOUND
    uint8_t *out_buffer;
    int out_payload_size;
    int bytes_sent;
    int left_to_send;
} peer_data_t;

typedef struct {
    uint8_t *out_buffer;
    int out_payload_size;
    int bytes_sent;
    int left_to_send;
} duplication_data_t;

/* This struct we keep for every descriptor that will be multiplexed with epoll */
struct event_data_t {
    int is_server;
    peer_type_t peer_type;
    union {
        int server_socket;          // server event
        peer_data_t *peer_data;  // client connection
        duplication_data_t *duplication_data;
    };
};

// typedef struct {
//     int id;
//     char *ip;
//     int32_t port;
//     int stored_chunks; // not needed?
//     bool isAlive;
// } replica_info_t;

void err_n_die(const char *fmt, ...);
char *bin2hex(const unsigned char *input, size_t len);
int set_fd_nonblocking(int fd);
char *resolve_host(char *host_name);
// void debug_log(int debugfd, const char *fmt, ...);
void debug_log(FILE *debugfd, const char *fmt, ...);

int bulk_read(int fd, void *buf, int count);
int read_payload_size(int fd, bool *timeout);
int bulk_write(int fd, const void *buf, int count);
// int bulk_write_nonblock(peer_data_t *peer_data);
int bulk_write_nonblock(int fd, void *buf, int *bytes_sent, int *left_to_send);

void abort_with_cleanup(char *msg, int serverfd);
bool read_payload_and_data(int serverfd, uint8_t **buffer, uint32_t *payload);
int write_len_and_data(int fd, uint32_t len, uint8_t *data);

void setup_connection(int *server_socket, char *ip, uint16_t port);
int setup_connection_retry(int *server_socket, char *ip, uint16_t port);

int64_t file_size(int filefd);

const char *peer_type_to_string(peer_type_t peer_type);

void print_logs(int level, const char *fmt, ...);

bool are_replicas_same(Replica *r1, Replica *r2);


int set_fd_blocking(int fd);    
#endif
