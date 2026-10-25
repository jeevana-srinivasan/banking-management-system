#include "feedback.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#define FEEDBACK_FILE STORAGE_DIR "/feedback.txt"
#define FEEDBACK_LINE_SIZE 2048

static pthread_mutex_t feedback_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static int current_timestamp(
    char *buffer,
    size_t size)
{
    time_t now =
        time(NULL);

    if (now == (time_t)-1)
        return -1;

    struct tm local_time;

    if (localtime_r(
            &now,
            &local_time) == NULL) {

        return -1;
    }

    if (strftime(
            buffer,
            size,
            "%Y-%m-%d %H:%M:%S",
            &local_time) == 0) {

        return -1;
    }

    return 0;
}

static int valid_feedback_message(
    const char *message)
{
    if (message == NULL ||
        message[0] == '\0') {

        return 0;
    }

    size_t length =
        strlen(message);

    if (length >
        MAX_FEEDBACK_LEN) {

        return 0;
    }

    /*
     * '|' is used as our storage delimiter.
     * Newlines would break one-record-per-line.
     */
    if (strchr(message, '|') != NULL ||
        strchr(message, '\n') != NULL ||
        strchr(message, '\r') != NULL) {

        return 0;
    }

    return 1;
}

static int lock_feedback_file(
    int fd)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type =
        F_WRLCK;

    lock.l_whence =
        SEEK_SET;

    lock.l_start = 0;
    lock.l_len = 0;

    while (fcntl(
               fd,
               F_SETLKW,
               &lock) == -1) {

        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

static int unlock_feedback_file(
    int fd)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type =
        F_UNLCK;

    lock.l_whence =
        SEEK_SET;

    lock.l_start = 0;
    lock.l_len = 0;

    return fcntl(
        fd,
        F_SETLK,
        &lock
    );
}

static FeedbackId find_next_feedback_id(
    int fd)
{
    FILE *file =
        fdopen(
            dup(fd),
            "r"
        );

    if (file == NULL)
        return -1;

    FeedbackId maximum =
        80000;

    char line[
        FEEDBACK_LINE_SIZE
    ];

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        char *separator =
            strchr(line, '|');

        if (separator == NULL)
            continue;

        *separator =
            '\0';

        errno = 0;

        char *end =
            NULL;

        long long id =
            strtoll(
                line,
                &end,
                10
            );

        if (errno == 0 &&
            end != line &&
            *end == '\0' &&
            id > maximum) {

            maximum =
                (FeedbackId)id;
        }
    }

    fclose(file);

    if (maximum ==
        INT64_MAX) {

        return -1;
    }

    return maximum + 1;
}

OperationStatus submit_feedback(
    UserId user_id,
    const char *message,
    FeedbackRecord *created_feedback)
{
    if (user_id <= 0 ||
        !valid_feedback_message(message) ||
        created_feedback == NULL) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &feedback_mutex
    );

    int fd =
        open(
            FEEDBACK_FILE,
            O_RDWR |
            O_CREAT,
            0660
        );

    if (fd == -1) {

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_feedback_file(
            fd) == -1) {

        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FeedbackId feedback_id =
        find_next_feedback_id(
            fd
        );

    if (feedback_id <= 0) {

        unlock_feedback_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FeedbackRecord feedback;

    memset(
        &feedback,
        0,
        sizeof(feedback)
    );

    feedback.feedback_id =
        feedback_id;

    feedback.user_id =
        user_id;

    strcpy(
        feedback.message,
        message
    );

    if (current_timestamp(
            feedback.created_at,
            sizeof(
                feedback.created_at)) == -1) {

        unlock_feedback_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_INTERNAL_ERROR;
    }

    char record[
        FEEDBACK_LINE_SIZE
    ];

    int length =
        snprintf(
            record,
            sizeof(record),
            "%lld|%lld|%s|%s\n",

            (long long)
                feedback.feedback_id,

            (long long)
                feedback.user_id,

            feedback.message,

            feedback.created_at
        );

    if (length < 0 ||
        (size_t)length >=
            sizeof(record)) {

        unlock_feedback_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_INTERNAL_ERROR;
    }

    if (lseek(
            fd,
            0,
            SEEK_END) == -1) {

        unlock_feedback_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    ssize_t total = 0;

    while (total < length) {

        ssize_t written =
            write(
                fd,
                record + total,
                (size_t)
                    (length - total)
            );

        if (written < 0) {

            if (errno == EINTR)
                continue;

            unlock_feedback_file(fd);
            close(fd);

            pthread_mutex_unlock(
                &feedback_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        total +=
            written;
    }

    if (fsync(fd) == -1) {

        unlock_feedback_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &feedback_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    *created_feedback =
        feedback;

    unlock_feedback_file(fd);

    close(fd);

    pthread_mutex_unlock(
        &feedback_mutex
    );

    return STATUS_OK;
}

OperationStatus get_all_feedback(
    char *output,
    size_t output_size)
{
    if (output == NULL ||
        output_size == 0) {

        return STATUS_INVALID_INPUT;
    }

    FILE *file =
        fopen(
            FEEDBACK_FILE,
            "r"
        );

    if (file == NULL)
        return STATUS_STORAGE_ERROR;

    output[0] = '\0';

    char line[2048];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        long long feedback_id;
        long long user_id;

        char message[
            MAX_FEEDBACK_LEN + 1
        ];

        char created_at[32];

        int fields =
            sscanf(
                line,
                "%lld|%lld|%1000[^|]|%31[^\n]",
                &feedback_id,
                &user_id,
                message,
                created_at
            );

        if (fields != 4)
            continue;

        char entry[
            MAX_FEEDBACK_LEN + 256
        ];

        int written =
            snprintf(
                entry,
                sizeof(entry),

                "Feedback ID : %lld\n"
                "Customer ID : %lld\n"
                "Message     : %s\n"
                "Submitted   : %s\n"
                "----------------------------------------\n",

                feedback_id,
                user_id,
                message,
                created_at
            );

        if (written < 0 ||
            (size_t)written >=
                sizeof(entry)) {

            fclose(file);

            return STATUS_INTERNAL_ERROR;
        }

        if (strlen(output) +
                strlen(entry) + 1 >
            output_size) {

            fclose(file);

            return STATUS_STORAGE_ERROR;
        }

        strcat(
            output,
            entry
        );

        found = 1;
    }

    fclose(file);

    if (!found)
        return STATUS_NOT_FOUND;

    return STATUS_OK;
}
