#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"

const char* SERVER_ADDRESS = "127.0.0.1";
const uint16_t SERVER_PORT = 8001;

char* DEFAULT_PATH = "/home/vlada/Documents/thesis/distributed-fs/server/gfs.png";

char* OUTPUT_PATH = "output.txt";

void setFileRequest(int arc, char **arv, FileRequest *request) 
{
    // request->path = "test.txt";
    request->path = DEFAULT_PATH;
    request->offset = 0;
    request->size = 0;
}

int main(int argc, char **argv) 
{
    int                 serverfd, outputfd, n;
    struct sockaddr_in  servaddr;
    char                recvline[MAXLINE];

    if ((outputfd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        err_n_die("outputfd error");

    FileRequest request = FILE_REQUEST__INIT;
    
    setFileRequest(argc, argv, &request);

    size_t len = file_request__get_packed_size(&request);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    file_request__pack(&request, buffer);


    /* Connecting with the server */
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_ADDRESS, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", argv[1]);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");


    /* Sending file request */
    uint32_t net_len = htonl(len);
    write(serverfd, &net_len, sizeof(net_len));

    if (write(serverfd, buffer, len) != len)
        err_n_die("write error");

    free(buffer);


    /* Receiving ChunkList from the Master*/
    memset(recvline, 0, sizeof(recvline));
    n = read(serverfd, recvline, MAXLINE);

    ChunkList *chunkList = chunk_list__unpack(NULL, n, recvline);
    printf("n_chunks: %d\n", chunkList->n_chunks);
    printf("\n");

    for(int i = 0; i < chunkList->n_chunks; i++){
        printf("chunk_id: %d \n", chunkList->chunks[i]->chunk_id);
        for(int j = 0; j < chunkList->chunks[i]->n_replicas; j++){
            printf("replica_name: %s \n", chunkList->chunks[i]->replicas[j]->name);
            printf("ip: %s \n", chunkList->chunks[i]->replicas[j]->ip);
            printf("port: %d \n", chunkList->chunks[i]->replicas[j]->port);
        }
        printf("\n");
    }

    close(serverfd);

    int offset = 0;

    for(int i = 0; i < chunkList->n_chunks; i++){
        printf("chunk_id: %d \n", chunkList->chunks[i]->chunk_id);

        ChunkRequest chunkRequest = CHUNK_REQUEST__INIT;
        chunkRequest.chunk_id = chunkList->chunks[i]->chunk_id;
        chunkRequest.path = DEFAULT_PATH;

        int len = chunk_request__get_packed_size(&chunkRequest);
        uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
        chunk_request__pack(&chunkRequest, buffer);

        if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            err_n_die("socket error");

        char* ip = chunkList->chunks[i]->replicas[0]->ip;
        uint16_t port = chunkList->chunks[i]->replicas[0]->port;

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);

        printf("ip: %s \n", ip);
        printf("port: %d \n", port);

        if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
            err_n_die("inet_pton error for %s", argv[1]);

        if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
            err_n_die("connect error");


        int network_length = htonl(len);
        write(serverfd, &network_length, sizeof(network_length));

        write(serverfd, buffer, len);
        free(buffer);

        memset(recvline, 0, MAXLINE);
        n = read(serverfd, recvline, MAXLINE);
        
        if ((pwrite(outputfd, recvline, n, offset)) < 0)
            err_n_die("pwrite error");

        offset += n;
        printf("received: %s \n", recvline);

        close(serverfd);

        printf("\n");
    }
}