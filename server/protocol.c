#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

/* Send exactly length bytes unless an error occurs. */
static int send_all(int fd, const void *data, size_t length)
{
    const char *ptr = data;

    while (length > 0) {
        ssize_t n = send(fd, ptr, length, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        ptr += n;
        length -= (size_t)n;
    }

    return 0;
}

/*
 * Receive exactly length bytes.
 * Returns 1 if complete, 0 if EOF before any bytes,
 * and -1 on error or premature EOF.
 */
static int receive_all(int fd, void *data, size_t length)
{
    char *ptr = data;
    size_t received = 0;

    while (received < length) {
        ssize_t n = recv(fd, ptr + received,
                         length - received, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0) {
            return received == 0 ? 0 : -1;
        }

        received += (size_t)n;
    }

    return 1;
}

int send_message(int socket_fd, const char *message)
{
    if (message == NULL)
        return -1;

    size_t length = strlen(message);

    if (length == 0 || length > PROTOCOL_MAX_MESSAGE)
        return -1;

    uint32_t header = htonl((uint32_t)length);

    if (send_all(socket_fd, &header, sizeof(header)) == -1)
        return -1;

    return send_all(socket_fd, message, length);
}

int receive_message(int socket_fd, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < 2)
        return -1;

    uint32_t header;

    int result = receive_all(socket_fd, &header, sizeof(header));

    if (result != 1)
        return result;

    uint32_t length = ntohl(header);

    if (length == 0 ||
        length > PROTOCOL_MAX_MESSAGE ||
        (size_t)length >= capacity) {
        return -1;
    }

    result = receive_all(socket_fd, buffer, length);

    if (result != 1)
        return -1;

    buffer[length] = '\0';
    return 1;
}
