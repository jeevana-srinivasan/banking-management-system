#include "session.h"

#include <pthread.h>
#include <string.h>

static Session sessions[MAX_CLIENTS];

static pthread_mutex_t session_mutex =
    PTHREAD_MUTEX_INITIALIZER;

void session_manager_init(void)
{
    pthread_mutex_lock(&session_mutex);

    memset(sessions, 0, sizeof(sessions));

    pthread_mutex_unlock(&session_mutex);
}

OperationStatus session_create(int socket_fd,
                               UserId user_id,
                               UserRole role)
{
    pthread_mutex_lock(&session_mutex);

    /*
     * Enforce one active session per user.
     */
    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (sessions[i].active &&
            sessions[i].user_id == user_id) {

            pthread_mutex_unlock(&session_mutex);
            return STATUS_DUPLICATE;
        }
    }

    /*
     * Find an empty slot.
     */
    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (!sessions[i].active) {

            sessions[i].active = 1;
            sessions[i].socket_fd = socket_fd;
            sessions[i].user_id = user_id;
            sessions[i].role = role;

            pthread_mutex_unlock(&session_mutex);
            return STATUS_OK;
        }
    }

    pthread_mutex_unlock(&session_mutex);

    return STATUS_INTERNAL_ERROR;
}

void session_remove_by_socket(int socket_fd)
{
    pthread_mutex_lock(&session_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (sessions[i].active &&
            sessions[i].socket_fd == socket_fd) {

            memset(&sessions[i], 0,
                   sizeof(sessions[i]));

            break;
        }
    }

    pthread_mutex_unlock(&session_mutex);
}

int session_is_user_active(UserId user_id)
{
    int found = 0;

    pthread_mutex_lock(&session_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (sessions[i].active &&
            sessions[i].user_id == user_id) {

            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&session_mutex);

    return found;
}

int session_get_by_socket(int socket_fd,
                          Session *session)
{
    if (session == NULL)
        return 0;

    int found = 0;

    pthread_mutex_lock(&session_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (sessions[i].active &&
            sessions[i].socket_fd == socket_fd) {

            *session = sessions[i];
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&session_mutex);

    return found;
}
