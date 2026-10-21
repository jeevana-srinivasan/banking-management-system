#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define PROTOCOL_MAX_MESSAGE 4096U

/*
 * Returns 0 on success, -1 on error.
 */
int send_message(int socket_fd, const char *message);

/*
 * Returns:
 *   1  = complete message received
 *   0  = peer disconnected before sending a new message
 *  -1  = error or invalid/truncated message
 */
int receive_message(int socket_fd, char *buffer, size_t capacity);

#endif
