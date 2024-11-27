
# Server Data Structures

```c
typedef struct {
    int is_server; 
    union {
        int server_socket;
        client_data_t *client_data;
    };
} event_data_t;
```

For each descriptor that triggers `epoll`, the server requires a properly configured `event_data_t` object. There are two distinct scenarios:

1. **`event_data_t` for a server socket**  
   In this case, `is_server` is set to `true`, and the union stores the `server_socket`.

2. **`event_data_t` for a client socket**  
   In this case, `is_server` is set to `false`, and the union stores a pointer to a `client_data_t` object.

---

```c
typedef struct {
    int client_socket;
    char *buffer;
    int payload_size;
    int bytes_stored;
    int space_left;
} client_data_t;
```