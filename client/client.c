#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"

#define SERVER_PORT 8080
#define BUFFER_SIZE (PROTOCOL_MAX_MESSAGE + 1U)

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    char message[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &server_addr.sin_addr) != 1) {

        fprintf(stderr, "Invalid server IP address.\n");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1) {

        perror("connect");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to banking server.\n");
    printf("Type EXIT to disconnect.\n\n");

    while (1) {

        printf("Enter request: ");

        if (fgets(message,
                  sizeof(message),
                  stdin) == NULL) {

            printf("\nInput ended.\n");
            break;
        }

        /*
         * Remove trailing newline added by fgets().
         */
        message[strcspn(message, "\n")] = '\0';

        if (message[0] == '\0') {
            printf("Request cannot be empty.\n");
            continue;
        }

        if (send_message(sock_fd, message) == -1) {
            perror("send_message");
            break;
        }

        int result = receive_message(sock_fd,
                                     response,
                                     sizeof(response));

        if (result == 0) {

            printf("Server disconnected.\n");
            break;

        } else if (result == -1) {

            fprintf(stderr,
                    "Invalid response from server.\n");

            break;
        }

        printf("Server: %s\n\n", response);

        if (strcmp(message, "EXIT") == 0)
            break;
    }

    close(sock_fd);

    printf("Connection closed.\n");

    return EXIT_SUCCESS;
}
