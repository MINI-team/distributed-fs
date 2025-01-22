#ifndef SERVER_H
#define SERVER_H

#include "common.h"

#define SINGLE_CLIENT_BUFFER_SIZE 2000000 // TODO nieoptymalne

// typedef struct {
//     int client_socket;
//     uint8_t *buffer;
//     int payload_size;
//     int bytes_stored;
//     int space_left;
//     bool reading_started;
// } client_data_t;

// /* This struct we keep for every descriptor that will be multiplexed with epoll */
// typedef struct {
//     int is_server; 
//     union {
//         int server_socket;          // server event
//         client_data_t *client_data;  // client connection
//     };
// } event_data_t;

// typedef struct {
//     int id;
//     char *ip;
//     int32_t port;
//     int stored_chunks;
// } replica_info_t;


typedef struct {
    int replicas_count;
    int replica_robin_index;
    int total_alive_replicas;
    Replica **all_replicas;
    bool *is_alive;
    bool *already_used;
} replicas_data_t;

// Function prototypes

/* Server setup */
void server_setup(int *server_socket, int *epoll_fd, struct epoll_event *event);

/* New clients */
void handle_new_connection(int epoll_fd, int server_socket);

/* Existing clients */
void add_file(char* path, int64_t size, replicas_data_t *replicas_data, GHashTable *hash_table, bool committed);
void process_request(int epoll_fd, event_data_t *event_data, replicas_data_t *replicas_data, GHashTable *hash_table);
void handle_client(int epoll_fd, event_data_t *event_data, replicas_data_t *replicas_data, GHashTable *hash_table);

#endif