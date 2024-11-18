sudo apt install protobuf-c-compiler libprotobuf-c-dev libglib2.0-dev


bash setup.bash
mkdir build
cd build
cmake ..
make

./replica
./server

./client read ala

Chunks data for replica1 is stored in data_replica1/chunks


------------------------------------------------------------------------
Important issues:

malicious client sends to big request

uint32_t len = ntohl(net_len);
if (len > MAX_ALLOWED_SIZE) {
    fprintf(stderr, "Requested length too large, rejecting request.\n");
    close(client_socket);
    continue;
}

graceful shutdown

Handling Invalid/Corrupted Protocol Buffers:
FileRequest *fileRequest = file_request__unpack(NULL, len, client_read_buffer);
if (fileRequest == NULL) {
    fprintf(stderr, "Failed to unpack protobuf message.\n");
    free(client_read_buffer);
    close(client_socket);
    continue;
}

client_socket = accept(server_socket, (SA *)NULL, NULL);
if (client_socket < 0) {
    perror("accept failed");
    continue;
}


Currently, client_socket is closed after processing a request, which is good, but make sure to properly handle all edge cases where the client connection could fail before closing.
