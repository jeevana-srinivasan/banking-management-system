#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "protocol.h"
#include "session.h"
#include "auth.h"

#define BUFFER_SIZE (PROTOCOL_MAX_MESSAGE + 1U)

static const char *role_name(UserRole role)
{
    switch (role) {
        case ROLE_CUSTOMER:
            return "CUSTOMER";

        case ROLE_EMPLOYEE:
            return "EMPLOYEE";

        case ROLE_MANAGER:
            return "MANAGER";

        case ROLE_ADMIN:
            return "ADMIN";

        default:
            return "UNKNOWN";
    }
}

/*
 * Parse:
 *
 * LOGIN|username|password
 *
 * Returns 0 on success and -1 on malformed input.
 */
static int parse_login_request(const char *request,
                               char *username,
                               size_t username_size,
                               char *password,
                               size_t password_size)
{
    const char prefix[] = "LOGIN|";

    if (strncmp(request, prefix, strlen(prefix)) != 0)
        return -1;

    const char *start = request + strlen(prefix);

    const char *separator = strchr(start, '|');

    if (separator == NULL)
        return -1;

    /*
     * We expect exactly:
     *
     * LOGIN|username|password
     *
     * Therefore another '|' is not allowed.
     */
    if (strchr(separator + 1, '|') != NULL)
        return -1;

    size_t username_length =
        (size_t)(separator - start);

    size_t password_length =
        strlen(separator + 1);

    if (username_length == 0 ||
        username_length >= username_size ||
        password_length == 0 ||
        password_length >= password_size) {
        return -1;
    }

    memcpy(username, start, username_length);
    username[username_length] = '\0';

    memcpy(password,
           separator + 1,
           password_length + 1);

    return 0;
}

/*
 * Returns:
 *
 * 0 -> continue connection
 * 1 -> close connection
 */
static int process_request(int client_fd,
                           const char *request)
{
    /*
     * ---------------- LOGIN ----------------
     */
    if (strncmp(request, "LOGIN|", 6) == 0) {

        Session existing_session;

        /*
         * One connection cannot login as another user
         * until the current user logs out.
         */
        if (session_get_by_socket(client_fd,
                                  &existing_session)) {

            send_message(
                client_fd,
                "LOGIN_FAILED|CONNECTION_ALREADY_AUTHENTICATED"
            );

            return 0;
        }

        char username[MAX_USERNAME_LEN + 1];
        char password[MAX_PASSWORD_LEN + 1];

        if (parse_login_request(request,
                                username,
                                sizeof(username),
                                password,
                                sizeof(password)) == -1) {

            send_message(
                client_fd,
                "LOGIN_FAILED|INVALID_REQUEST"
            );

            return 0;
        }

        UserRecord user;

        OperationStatus status =
            authenticate_user(username,
                              password,
                              &user);

        /*
         * Remove plaintext password from our local buffer
         * as soon as authentication is complete.
         */
        memset(password, 0, sizeof(password));

        if (status == STATUS_NOT_FOUND ||
            status == STATUS_PERMISSION_DENIED) {

            /*
             * Deliberately use the same response for an
             * unknown username and an incorrect password.
             */
            send_message(
                client_fd,
                "LOGIN_FAILED|INVALID_CREDENTIALS"
            );

            return 0;
        }

        if (status == STATUS_INACTIVE) {

            send_message(
                client_fd,
                "LOGIN_FAILED|ACCOUNT_INACTIVE"
            );

            return 0;
        }

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "LOGIN_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        /*
         * session_create() atomically checks whether
         * this user is already logged in.
         */
        status = session_create(client_fd,
                                user.user_id,
                                user.role);

        if (status == STATUS_DUPLICATE) {

            send_message(
                client_fd,
                "LOGIN_FAILED|USER_ALREADY_LOGGED_IN"
            );

            return 0;
        }

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "LOGIN_FAILED|SESSION_ERROR"
            );

            return 0;
        }

        char response[MAX_RESPONSE_SIZE];

        int written = snprintf(
            response,
            sizeof(response),
            "LOGIN_OK|%lld|%s|%s",
            (long long)user.user_id,
            role_name(user.role),
            user.full_name
        );

        if (written < 0 ||
            (size_t)written >= sizeof(response)) {

            session_remove_by_socket(client_fd);

            send_message(
                client_fd,
                "LOGIN_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        printf("User %s logged in on fd %d\n",
               username,
               client_fd);

        send_message(client_fd, response);

        return 0;
    }

    /*
     * ---------------- LOGOUT ----------------
     */
    if (strcmp(request, "LOGOUT") == 0) {

        Session session;

        if (!session_get_by_socket(client_fd,
                                   &session)) {

            send_message(
                client_fd,
                "LOGOUT_FAILED|NOT_LOGGED_IN"
            );

            return 0;
        }

        printf("User %lld logged out from fd %d\n",
               (long long)session.user_id,
               client_fd);

        session_remove_by_socket(client_fd);

        send_message(client_fd, "LOGOUT_OK");

        /*
         * Important:
         *
         * We DO NOT close the TCP connection.
         *
         * Therefore the same terminal can now login
         * as another user.
         */
        return 0;
    }

    /*
     * ---------------- WHOAMI ----------------
     *
     * Temporary helper for testing sessions.
     */
    if (strcmp(request, "WHOAMI") == 0) {

        Session session;

        if (!session_get_by_socket(client_fd,
                                   &session)) {

            send_message(
                client_fd,
                "WHOAMI_FAILED|NOT_LOGGED_IN"
            );

            return 0;
        }

        char response[256];

        snprintf(
            response,
            sizeof(response),
            "WHOAMI_OK|%lld|%s",
            (long long)session.user_id,
            role_name(session.role)
        );

        send_message(client_fd, response);

        return 0;
    }

    /*
     * ---------------- EXIT ----------------
     */
    if (strcmp(request, "EXIT") == 0) {

        /*
         * If the client exits while logged in,
         * remove the session automatically.
         */
        session_remove_by_socket(client_fd);

        send_message(client_fd, "GOODBYE");

        return 1;
    }

    /*
     * For future banking commands, authentication
     * will be mandatory.
     */
    Session session;

    if (!session_get_by_socket(client_fd, &session)) {

        send_message(
            client_fd,
            "ERROR|AUTHENTICATION_REQUIRED"
        );

        return 0;
    }

    /*
     * Banking operations are implemented later.
     */
    send_message(
        client_fd,
        "ERROR|COMMAND_NOT_IMPLEMENTED"
    );

    return 0;
}

static void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];

    printf("Client connected on fd %d\n",
           client_fd);

    while (1) {

        int result =
            receive_message(client_fd,
                            buffer,
                            sizeof(buffer));

        if (result == 0) {

            printf("Client fd %d disconnected.\n",
                   client_fd);

            break;
        }

        if (result == -1) {

            fprintf(stderr,
                    "Invalid/incomplete message from fd %d\n",
                    client_fd);

            break;
        }

        /*
         * DO NOT print the complete request here.
         *
         * LOGIN requests contain plaintext passwords.
         */

        int should_close =
            process_request(client_fd, buffer);

        if (should_close)
            break;
    }

    /*
     * This is essential.
     *
     * It also handles abnormal disconnects:
     * terminal closed, client killed, network lost, etc.
     */
    session_remove_by_socket(client_fd);

    printf("Closing connection for fd %d\n",
           client_fd);

    close(client_fd);

    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    /*
     * Initialize our active-session table before
     * accepting clients.
     */
    session_manager_init();

    server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

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

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) != 1) {

        fprintf(stderr,
                "Invalid SERVER_IP.\n");

        close(server_fd);
        return EXIT_FAILURE;
    }

    server_addr.sin_port =
        htons(SERVER_PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {

        perror("bind");

        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd,
               MAX_CLIENTS) == -1) {

        perror("listen");

        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Banking server listening on %s:%d\n",
           SERVER_IP,
           SERVER_PORT);

    while (1) {

        int client_fd =
            accept(server_fd, NULL, NULL);

        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        int *client_arg =
            malloc(sizeof(*client_arg));

        if (client_arg == NULL) {

            perror("malloc");

            close(client_fd);
            continue;
        }

        *client_arg = client_fd;

        pthread_t thread;

        int err =
            pthread_create(&thread,
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
