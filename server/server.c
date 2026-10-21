#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"

#define SERVER_PORT 8080
#define BUFFER_SIZE (PROTOCOL_MAX_MESSAGE + 1U)

static void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];

    printf("Client connected on fd %d\n", client_fd);

    while (1) {

        int result = receive_message(client_fd,
                                     buffer,
                                     sizeof(buffer));

        if (result == 0) {
            printf("Client fd %d disconnected.\n", client_fd);
            break;
        }

        if (result == -1) {
            fprintf(stderr,
                    "Invalid/incomplete message from client fd %d\n",
                    client_fd);
            break;
        }

        printf("Client fd %d says: %s\n",
               client_fd, buffer);

        /*
         * EXIT is only a temporary command for testing.
         * Later this will become LOGOUT/EXIT in our banking protocol.
         */
        if (strcmp(buffer, "EXIT") == 0) {

            if (send_message(client_fd,
                             "GOODBYE") == -1) {
                perror("send_message");
            }

            break;
        }

        char response[BUFFER_SIZE];

        int written = snprintf(response,
                               sizeof(response),
                               "SERVER_RECEIVED|%s",
                               buffer);

        if (written < 0 ||
            (size_t)written >= sizeof(response)) {

            if (send_message(client_fd,
                             "ERROR|Response too large") == -1) {
                perror("send_message");
                break;
            }

            continue;
        }

        if (send_message(client_fd, response) == -1) {
            perror("send_message");
            break;
        }
    }

    printf("Closing connection for fd %d\n", client_fd);

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

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) == -1) {

        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
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

    printf("Banking server listening on 127.0.0.1:%d\n",
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

        int err = pthread_create(&thread,
                                 NULL,
                                 handle_client,
                                 client_arg);

        if (err != 0) {

            fprintf(stderr,
                    "pthread_create: %s\n",
                    strerror(err));

            free(client_arg);
            close(client_fd);

            continue;
        }

        err = pthread_detach(thread);

        if (err != 0) {
            fprintf(stderr,
                    "pthread_detach: %s\n",
                    strerror(err));
        }
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
