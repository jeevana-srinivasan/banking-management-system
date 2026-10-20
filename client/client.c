#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP address.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    const char *message = "Hello from client";
    if (send(sock_fd, message, strlen(message), 0) == -1) {
        perror("send");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    ssize_t bytes_received = recv(sock_fd, buffer,
                                  sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        if (bytes_received == -1)
            perror("recv");
        else
            fprintf(stderr, "Server closed the connection.\n");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    buffer[bytes_received] = '\0';
    printf("Server replied: %s\n", buffer);

    close(sock_fd);
    return EXIT_SUCCESS;
}

