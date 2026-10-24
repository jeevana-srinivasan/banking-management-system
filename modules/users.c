#include "users.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#define USERS_FILE STORAGE_DIR "/users.txt"
#define USER_LINE_SIZE 1024

static pthread_mutex_t users_write_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static int parse_user_line(const char *line, UserRecord *user)
{
    if (line == NULL || user == NULL)
        return -1;

    char copy[USER_LINE_SIZE];

    if (strlen(line) >= sizeof(copy))
        return -1;

    strcpy(copy, line);

    /*
     * Remove newline written by fgets().
     */
    copy[strcspn(copy, "\r\n")] = '\0';

    char *fields[6];
    size_t count = 0;

    char *saveptr = NULL;
    char *token = strtok_r(copy, "|", &saveptr);

    while (token != NULL && count < 6) {
        fields[count++] = token;
        token = strtok_r(NULL, "|", &saveptr);
    }

    /*
     * Exactly six fields are required.
     */
    if (count != 6 || token != NULL)
        return -1;

    /*
     * Parse user ID.
     */
    errno = 0;
    char *end = NULL;

    long long parsed_id = strtoll(fields[0], &end, 10);

    if (errno != 0 ||
        end == fields[0] ||
        *end != '\0' ||
        parsed_id <= 0) {
        return -1;
    }

    /*
     * Parse role.
     */
    errno = 0;
    end = NULL;

    long parsed_role = strtol(fields[3], &end, 10);

    if (errno != 0 ||
        end == fields[3] ||
        *end != '\0' ||
        parsed_role < ROLE_CUSTOMER ||
        parsed_role > ROLE_ADMIN) {
        return -1;
    }

    /*
     * Parse active flag.
     */
    errno = 0;
    end = NULL;

    long parsed_active = strtol(fields[4], &end, 10);

    if (errno != 0 ||
        end == fields[4] ||
        *end != '\0' ||
        (parsed_active != 0 && parsed_active != 1)) {
        return -1;
    }

    /*
     * Validate field lengths before copying.
     */
    if (strlen(fields[1]) > MAX_USERNAME_LEN ||
        strlen(fields[2]) >= MAX_PASSWORD_HASH_LEN ||
        strlen(fields[5]) > MAX_NAME_LEN) {
        return -1;
    }

    user->user_id = (UserId)parsed_id;

    strcpy(user->username, fields[1]);
    strcpy(user->password_hash, fields[2]);

    user->role = (UserRole)parsed_role;
    user->active = (int)parsed_active;

    strcpy(user->full_name, fields[5]);

    return 0;
}

OperationStatus find_user_by_username(const char *username,
                                      UserRecord *user)
{
    if (username == NULL ||
        user == NULL ||
        username[0] == '\0') {
        return STATUS_INVALID_INPUT;
    }

    FILE *file = fopen(USERS_FILE, "r");

    if (file == NULL) {
        perror("fopen users.txt");
        return STATUS_STORAGE_ERROR;
    }

    char line[USER_LINE_SIZE];

    while (fgets(line, sizeof(line), file) != NULL) {

        UserRecord current;

        if (parse_user_line(line, &current) != 0) {
            fprintf(stderr,
                    "Warning: malformed user record ignored.\n");
            continue;
        }

        if (strcmp(current.username, username) == 0) {
            *user = current;
            fclose(file);
            return STATUS_OK;
        }
    }

    if (ferror(file)) {
        perror("fgets users.txt");
        fclose(file);
        return STATUS_STORAGE_ERROR;
    }

    fclose(file);

    return STATUS_NOT_FOUND;
}

OperationStatus find_user_by_id(UserId user_id,
                                UserRecord *user)
{
    if (user_id <= 0 || user == NULL)
        return STATUS_INVALID_INPUT;

    FILE *file = fopen(USERS_FILE, "r");

    if (file == NULL) {
        perror("fopen users.txt");
        return STATUS_STORAGE_ERROR;
    }

    char line[USER_LINE_SIZE];

    while (fgets(line, sizeof(line), file) != NULL) {

        UserRecord current;

        if (parse_user_line(line, &current) != 0) {
            fprintf(stderr,
                    "Warning: malformed user record ignored.\n");
            continue;
        }

        if (current.user_id == user_id) {
            *user = current;
            fclose(file);
            return STATUS_OK;
        }
    }

    if (ferror(file)) {
        perror("fgets users.txt");
        fclose(file);
        return STATUS_STORAGE_ERROR;
    }

    fclose(file);

    return STATUS_NOT_FOUND;
}

OperationStatus update_user_password_hash(
    UserId user_id,
    const char *new_password_hash)
{
    if (user_id <= 0 ||
        new_password_hash == NULL ||
        new_password_hash[0] == '\0' ||
        strlen(new_password_hash) >=
            MAX_PASSWORD_HASH_LEN) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &users_write_mutex
    );

    int fd =
        open(
            USERS_FILE,
            O_RDWR
        );

    if (fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *file =
        fdopen(
            dup(fd),
            "r"
        );

    if (file == NULL) {

        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    off_t record_offset = 0;
    off_t target_offset = -1;
    size_t target_length = 0;
    off_t hash_offset = -1;
    size_t old_hash_length = 0;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        size_t line_length =
            strlen(line);

        char copy[2048];

        if (line_length >=
            sizeof(copy)) {

            record_offset +=
                (off_t)line_length;

            continue;
        }

        strcpy(
            copy,
            line
        );

        char *first =
            strchr(copy, '|');

        if (first == NULL) {

            record_offset +=
                (off_t)line_length;

            continue;
        }

        *first = '\0';

        errno = 0;

        char *end = NULL;

        long long id =
            strtoll(
                copy,
                &end,
                10
            );

        if (errno != 0 ||
            end == copy ||
            *end != '\0') {

            record_offset +=
                (off_t)line_length;

            continue;
        }

        if ((UserId)id !=
            user_id) {

            record_offset +=
                (off_t)line_length;

            continue;
        }

        /*
         * Format:
         * user_id|username|password_hash|...
         */

        char *username_start =
            first + 1;

        char *second =
            strchr(
                username_start,
                '|'
            );

        if (second == NULL)
            break;

        char *hash_start =
            second + 1;

        char *third =
            strchr(
                hash_start,
                '|'
            );

        if (third == NULL)
            break;

        old_hash_length =
            (size_t)
                (third - hash_start);

        /*
         * To update safely in place, the new
         * crypt hash must have the same length.
         */
        if (strlen(
                new_password_hash) !=
            old_hash_length) {

            fclose(file);
            close(fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_CONFLICT;
        }

        target_offset =
            record_offset;

        target_length =
            line_length;

        hash_offset =
            record_offset +
            (off_t)
                (hash_start - copy);

        break;
    }

    fclose(file);

    if (target_offset < 0 ||
        hash_offset < 0) {

        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_NOT_FOUND;
    }

    /*
     * Lock this user's complete textual record.
     */
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

    lock.l_start =
        target_offset;

    lock.l_len =
        (off_t)target_length;

    while (fcntl(
               fd,
               F_SETLKW,
               &lock) == -1) {

        if (errno == EINTR)
            continue;

        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    ssize_t written =
        pwrite(
            fd,
            new_password_hash,
            old_hash_length,
            hash_offset
        );

    if (written !=
        (ssize_t)old_hash_length ||
        fsync(fd) == -1) {

        lock.l_type =
            F_UNLCK;

        fcntl(
            fd,
            F_SETLK,
            &lock
        );

        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    lock.l_type =
        F_UNLCK;

    fcntl(
        fd,
        F_SETLK,
        &lock
    );

    close(fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}
