#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

static void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];

    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (n > 0) {
        buffer[n] = '\0';
        printf("Client %d says: %s\n", client_fd, buffer);

        const char *reply = "Hello from concurrent server";

        if (send(client_fd, reply, strlen(reply), MSG_NOSIGNAL) == -1) {
            perror("send");
        }
    } else if (n == 0) {
        printf("Client %d disconnected.\n", client_fd);
    } else {
        perror("recv");
    }

    close(client_fd);
    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Concurrent server listening on 127.0.0.1:%d\n",
           SERVER_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        int *client_arg = malloc(sizeof(*client_arg));
        if (client_arg == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *client_arg = client_fd;

        pthread_t thread;
        int err = pthread_create(&thread, NULL,
                                 handle_client, client_arg);

        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            free(client_arg);
            close(client_fd);
            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}

