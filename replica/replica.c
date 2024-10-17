#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8000
#define BUFFER_SIZE 1024

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Create a TCP socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Define the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("192.168.0.38");;  // Bind to any available interface
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 3) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d...\n", PORT);

    // Accept an incoming connection
    if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len)) < 0) {
        perror("Accept failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Print a message when a new client connects
    printf("New client connected!\n");

    // Close the client connection
    close(client_socket);

    // Close the server socket
    close(server_socket);

    return 0;
}
