#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "protocol.h"

#define BUFFER_SIZE (PROTOCOL_MAX_MESSAGE + 1U)

static int read_line(const char *prompt,
                     char *buffer,
                     size_t size)
{
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, size, stdin) == NULL)
        return -1;

    size_t length = strlen(buffer);

    if (length > 0 &&
        buffer[length - 1] == '\n') {

        buffer[length - 1] = '\0';

    } else {

        /*
         * Input was longer than our buffer.
         * Discard the remainder.
         */
        int ch;

        while ((ch = getchar()) != '\n' &&
               ch != EOF) {
        }
    }

    return 0;
}

static int read_password(char *password,
                         size_t size)
{
    struct termios old_settings;
    struct termios new_settings;

    if (tcgetattr(STDIN_FILENO,
                  &old_settings) == -1) {

        perror("tcgetattr");
        return -1;
    }

    new_settings = old_settings;

    new_settings.c_lflag &= ~ECHO;

    if (tcsetattr(STDIN_FILENO,
                  TCSAFLUSH,
                  &new_settings) == -1) {

        perror("tcsetattr");
        return -1;
    }

    printf("Password: ");
    fflush(stdout);

    int result = 0;

    if (fgets(password,
              size,
              stdin) == NULL) {

        result = -1;
    }

    /*
     * Always restore terminal echo.
     */
    if (tcsetattr(STDIN_FILENO,
                  TCSAFLUSH,
                  &old_settings) == -1) {

        perror("tcsetattr");
        return -1;
    }

    printf("\n");

    if (result == -1)
        return -1;

    password[strcspn(password, "\n")] =
        '\0';

    return 0;
}

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    char command[BUFFER_SIZE];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    sock_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port =
        htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) != 1) {

        fprintf(stderr,
                "Invalid server IP address.\n");

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

    printf("Connected to banking server.\n\n");

    printf("Available test commands:\n");
    printf("  LOGIN\n");
    printf("  LOGOUT\n");
    printf("  WHOAMI\n");
    printf("  EXIT\n\n");

    while (1) {

        if (read_line("bank> ",
                      command,
                      sizeof(command)) == -1) {

            printf("\nInput ended.\n");
            break;
        }

        if (command[0] == '\0')
            continue;

        /*
         * LOGIN needs username and hidden password.
         */
        if (strcmp(command, "LOGIN") == 0) {

            char username[MAX_USERNAME_LEN + 1];
            char password[MAX_PASSWORD_LEN + 1];

            if (read_line("Username: ",
                          username,
                          sizeof(username)) == -1) {
                break;
            }

            if (read_password(password,
                              sizeof(password)) == -1) {
                break;
            }

            int written =
                snprintf(request,
                         sizeof(request),
                         "LOGIN|%s|%s",
                         username,
                         password);

            memset(password,
                   0,
                   sizeof(password));

            if (written < 0 ||
                (size_t)written >= sizeof(request)) {

                printf("Login request is too large.\n");
                continue;
            }

        } else {

            snprintf(request,
                     sizeof(request),
                     "%s",
                     command);
        }

        if (send_message(sock_fd,
                         request) == -1) {

            perror("send_message");
            break;
        }

        int result =
            receive_message(sock_fd,
                            response,
                            sizeof(response));

        if (result == 0) {

            printf("Server disconnected.\n");
            break;
        }

        if (result == -1) {

            fprintf(stderr,
                    "Invalid server response.\n");

            break;
        }

        printf("%s\n\n", response);

        if (strcmp(command, "EXIT") == 0)
            break;
    }

    close(sock_fd);

    printf("Connection closed.\n");

    return EXIT_SUCCESS;
}
