#ifndef SESSION_H
#define SESSION_H

#include "common.h"
#include "config.h"

typedef struct {
    int active;
    int socket_fd;
    UserId user_id;
    UserRole role;
} Session;

/*
 * Initialize the global session table.
 */
void session_manager_init(void);

/*
 * Try to create a session for a user.
 *
 * Returns:
 *   STATUS_OK        -> session created
 *   STATUS_DUPLICATE -> user already logged in
 *   other status     -> error
 */
OperationStatus session_create(int socket_fd,
                               UserId user_id,
                               UserRole role);

/*
 * Remove the session associated with a socket.
 */
void session_remove_by_socket(int socket_fd);

/*
 * Check whether a user already has an active session.
 */
int session_is_user_active(UserId user_id);

/*
 * Retrieve session information for a socket.
 *
 * Returns 1 if found, 0 otherwise.
 */
int session_get_by_socket(int socket_fd,
                          Session *session);

#endif
