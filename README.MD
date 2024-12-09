
# files for writing need to placed in the build/client, not build/client/data_client !
   ```bash
##############################
# WRITE && READ DEMO
##############################
bash setup.bash
mkdir build build/data_replica1/8080/chunks build/data_replica1/8081/chunks
cd build
cmake ..
make
cp client/data_client/test client/
cd replica
./replica 8080
cd replica
./replica 8081
cd server
./server
cd client
./client write test
./client read test

sudo apt install protobuf-c-compiler libprotobuf-c-dev libglib2.0-dev
bash setup.bash
mkdir build
cd build
cmake ..
make
./replica/replica
./server/server
./client/client read ala


##############################
# WRITE
##############################

cd .. # start in home (distributed-fs)
# do previous setup
rm data_replica1/8080/chunks/* && rm data_replica1/8081/chunks/* # to see that (many) replicas write the chunks
cd build
./replica/replica 8080
./replica/replica 8081
./server/server
./client/client write komar.txt
   ```

Chunks data for replica1 is stored in data_replica1/chunks


------------------------------------------------------------------------
Important issues:

malicious client sends too big request

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
