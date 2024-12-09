#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main()
{
    int fd = open("file.txt", O_RDONLY);

    char buffer[10];

    ssize_t bytes_read = read(fd, buffer, 5);

    printf("Bytes read: %zu\n", bytes_read);
    printf("Len: %d\n", strlen(buffer));
    for(int i = 0; i < bytes_read; i++)
    {
        printf("%d: %c\n", i, buffer[i]);
    }

    printf("----------------\n");

    bytes_read = read(fd, buffer, 5);
    printf("Bytes read: %zu\n", bytes_read);
    printf("Len: %d\n", strlen(buffer));
    for(int i = 0; i < bytes_read; i++)
    {
        printf("%d: %c\n", i, buffer[i]);
    }

    bytes_read = read(fd, buffer, 5);


    // printf("last char: %d\n", buffer[6]);
    // printf("slash zero: %d\n", '\0');
    // printf("slash n: %d\n", '\n');
}