#include <stdio.h>
#include "dfs.pb-c.h"
#include "common.h"

const char* SERVER_ADDRESS = "127.0.0.1";
const uint16_t SERVER_PORT = 8000;

void setFileRequest(int arc, char **arv, FileRequest *request) 
{
    request->path = "test.txt";
    request->offset = 0;
    request->size = 0;
}

int main(int argc, char **argv) 
{
    FileRequest request = FILE_REQUEST__INIT;
    
    setFileRequest(argc, argv, &request);

    size_t len = file_request__get_packed_size(&request);
    uint8_t *buffer = (uint8_t *)malloc(len * sizeof(uint8_t));
    file_request__pack(&request, buffer);

///
    int serverfd;
    struct sockaddr_in servaddr;

    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_n_die("socket error");

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_ADDRESS, &servaddr.sin_addr) <= 0)
        err_n_die("inet_pton error for %s", argv[1]);

    if (connect(serverfd, (SA *)&servaddr, sizeof(servaddr)) < 0)
        err_n_die("connect error");

    // char* message = "client sending data\n";
    // write(serverfd, message, strlen(message));

    write(serverfd, buffer, len);

    char recvline[MAXLINE];
    memset(recvline, 0, sizeof(recvline));
    int n = read(serverfd, recvline, MAXLINE);

    ChunkList *chunks = chunk_list__unpack(NULL, n, recvline);
    printf("Succcess: %d\n", chunks->success);
    printf("n_chunks: %d\n", chunks->n_chunks);

    // printf("Received from server: %s\n", recvline);
}