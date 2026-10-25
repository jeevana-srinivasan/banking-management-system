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

OperationStatus update_user_full_name(
    UserId user_id,
    const char *new_full_name)
{
    if (user_id <= 0 ||
        new_full_name == NULL ||
        new_full_name[0] == '\0' ||
        strlen(new_full_name) > MAX_NAME_LEN ||
        strchr(new_full_name, '|') != NULL ||
        strchr(new_full_name, '\n') != NULL ||
        strchr(new_full_name, '\r') != NULL) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &users_write_mutex
    );

    /*
     * Get the current user record.
     *
     * Since all users.txt writers use the same
     * mutex, another server thread cannot modify
     * this file simultaneously.
     */
    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status != STATUS_OK) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return status;
    }

    int source_fd =
        open(
            USERS_FILE,
            O_RDWR
        );

    if (source_fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    /*
     * Since changing a name can change the
     * length of a textual record, lock the
     * entire file instead of one byte range.
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

    lock.l_start = 0;
    lock.l_len = 0;

    while (fcntl(
               source_fd,
               F_SETLKW,
               &lock) == -1) {

        if (errno == EINTR)
            continue;

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *source =
        fdopen(
            dup(source_fd),
            "r"
        );

    if (source == NULL) {

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    const char *temp_file =
        STORAGE_DIR "/users.tmp";

    int temp_fd =
        open(
            temp_file,
            O_WRONLY |
            O_CREAT |
            O_TRUNC,
            0660
        );

    if (temp_fd == -1) {

        fclose(source);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *temp =
        fdopen(
            temp_fd,
            "w"
        );

    if (temp == NULL) {

        close(temp_fd);
        unlink(temp_file);

        fclose(source);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    int replaced = 0;

    while (fgets(
               line,
               sizeof(line),
               source) != NULL) {

        char copy[2048];

        if (strlen(line) >=
            sizeof(copy)) {

            fclose(temp);
            unlink(temp_file);

            fclose(source);

            lock.l_type =
                F_UNLCK;

            fcntl(
                source_fd,
                F_SETLK,
                &lock
            );

            close(source_fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        strcpy(
            copy,
            line
        );

        char *separator =
            strchr(copy, '|');

        if (separator == NULL) {

            fputs(
                line,
                temp
            );

            continue;
        }

        *separator =
            '\0';

        errno = 0;

        char *end = NULL;

        long long current_id =
            strtoll(
                copy,
                &end,
                10
            );

        if (errno == 0 &&
            end != copy &&
            *end == '\0' &&
            (UserId)current_id ==
                user_id) {

            if (fprintf(
                    temp,
                    "%lld|%s|%s|%d|%d|%s\n",

                    (long long)
                        user.user_id,

                    user.username,

                    user.password_hash,

                    (int)
                        user.role,

                    user.active,

                    new_full_name
                ) < 0) {

                fclose(temp);
                unlink(temp_file);

                fclose(source);

                lock.l_type =
                    F_UNLCK;

                fcntl(
                    source_fd,
                    F_SETLK,
                    &lock
                );

                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }

            replaced = 1;

        } else {

            if (fputs(
                    line,
                    temp) == EOF) {

                fclose(temp);
                unlink(temp_file);

                fclose(source);

                lock.l_type =
                    F_UNLCK;

                fcntl(
                    source_fd,
                    F_SETLK,
                    &lock
                );

                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }
        }
    }

    fclose(source);

    if (!replaced) {

        fclose(temp);
        unlink(temp_file);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_NOT_FOUND;
    }

    /*
     * fflush moves stdio data into the kernel.
     */
    if (fflush(temp) == EOF ||
        fsync(fileno(temp)) == -1) {

        fclose(temp);
        unlink(temp_file);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (fclose(temp) == EOF) {

        unlink(temp_file);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    /*
     * Atomically replace users.txt with the
     * completely rewritten file.
     */
    if (rename(
            temp_file,
            USERS_FILE) == -1) {

        unlink(temp_file);

        lock.l_type =
            F_UNLCK;

        fcntl(
            source_fd,
            F_SETLK,
            &lock
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    lock.l_type =
        F_UNLCK;

    fcntl(
        source_fd,
        F_SETLK,
        &lock
    );

    close(source_fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}

static int user_id_range(
    UserRole role,
    UserId *start,
    UserId *end)
{
    switch (role) {

        case ROLE_CUSTOMER:
            *start = 1001;
            *end = 1999;
            return 0;

        case ROLE_EMPLOYEE:
            *start = 2001;
            *end = 2999;
            return 0;

        case ROLE_MANAGER:
            *start = 3001;
            *end = 3999;
            return 0;

        case ROLE_ADMIN:
            *start = 4001;
            *end = 4999;
            return 0;

        default:
            return -1;
    }
}


static int lock_users_file(
    int fd)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
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

static int unlock_users_file(
    int fd)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    return fcntl(
        fd,
        F_SETLK,
        &lock
    );
}

OperationStatus create_user_record(
    const char *username,
    const char *password_hash,
    UserRole role,
    int active,
    const char *full_name,
    UserRecord *created_user)
{
    if (username == NULL ||
        password_hash == NULL ||
        full_name == NULL ||
        created_user == NULL ||
        username[0] == '\0' ||
        full_name[0] == '\0' ||
        strlen(username) > MAX_USERNAME_LEN ||
        strlen(password_hash) >= MAX_PASSWORD_HASH_LEN ||
        strlen(full_name) > MAX_NAME_LEN ||
        strchr(username, '|') != NULL ||
        strchr(full_name, '|') != NULL ||
        (active != 0 && active != 1)) {

        return STATUS_INVALID_INPUT;
    }

    UserId range_start;
    UserId range_end;

    if (user_id_range(
            role,
            &range_start,
            &range_end) == -1) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &users_write_mutex
    );

    int fd =
        open(
            USERS_FILE,
            O_RDWR | O_CREAT,
            0660
        );

    if (fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_users_file(fd) == -1) {

        close(fd);

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

        unlock_users_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    UserId maximum =
        range_start - 1;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        char copy[2048];

        if (strlen(line) >=
            sizeof(copy)) {

            continue;
        }

        strcpy(copy, line);

        copy[
            strcspn(copy, "\r\n")
        ] = '\0';

        char *fields[6];
        size_t count = 0;

        char *saveptr = NULL;

        char *token =
            strtok_r(
                copy,
                "|",
                &saveptr
            );

        while (token != NULL &&
               count < 6) {

            fields[count++] =
                token;

            token =
                strtok_r(
                    NULL,
                    "|",
                    &saveptr
                );
        }

        if (count != 6)
            continue;

        /*
         * Username must be unique.
         */
        if (strcmp(
                fields[1],
                username) == 0) {

            fclose(file);

            unlock_users_file(fd);
            close(fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_DUPLICATE;
        }

        char *end = NULL;

        errno = 0;

        long long id =
            strtoll(
                fields[0],
                &end,
                10
            );

        if (errno != 0 ||
            end == fields[0] ||
            *end != '\0') {

            continue;
        }

        if (id >= range_start &&
            id <= range_end &&
            id > maximum) {

            maximum =
                (UserId)id;
        }
    }

    fclose(file);

    UserId new_user_id =
        maximum + 1;

    if (new_user_id >
        range_end) {

        unlock_users_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char record[2048];

    int length =
        snprintf(
            record,
            sizeof(record),
            "%lld|%s|%s|%d|%d|%s\n",
            (long long)new_user_id,
            username,
            password_hash,
            (int)role,
            active,
            full_name
        );

    if (length < 0 ||
        (size_t)length >=
            sizeof(record)) {

        unlock_users_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_INTERNAL_ERROR;
    }

    if (lseek(
            fd,
            0,
            SEEK_END) == -1) {

        unlock_users_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    ssize_t total = 0;

    while (total < length) {

        ssize_t written =
            write(
                fd,
                record + total,
                (size_t)(length - total)
            );

        if (written < 0) {

            if (errno == EINTR)
                continue;

            unlock_users_file(fd);
            close(fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        total += written;
    }

    if (fsync(fd) == -1) {

        unlock_users_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    memset(
        created_user,
        0,
        sizeof(*created_user)
    );

    created_user->user_id =
        new_user_id;

    strcpy(
        created_user->username,
        username
    );

    strcpy(
        created_user->password_hash,
        password_hash
    );

    created_user->role =
        role;

    created_user->active =
        active;

    strcpy(
        created_user->full_name,
        full_name
    );

    unlock_users_file(fd);
    close(fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}

OperationStatus delete_user_by_id(
    UserId user_id)
{
    if (user_id <= 0)
        return STATUS_INVALID_INPUT;

    pthread_mutex_lock(
        &users_write_mutex
    );

    int source_fd =
        open(
            USERS_FILE,
            O_RDWR
        );

    if (source_fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_users_file(
            source_fd) == -1) {

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *source =
        fdopen(
            dup(source_fd),
            "r"
        );

    if (source == NULL) {

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    const char *temp_path =
        STORAGE_DIR "/users_delete.tmp";

    FILE *temp =
        fopen(
            temp_path,
            "w"
        );

    if (temp == NULL) {

        fclose(source);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               source) != NULL) {

        char copy[2048];

        if (strlen(line) >=
            sizeof(copy)) {

            continue;
        }

        strcpy(copy, line);

        char *separator =
            strchr(copy, '|');

        if (separator == NULL) {

            fputs(line, temp);
            continue;
        }

        *separator =
            '\0';

        char *end = NULL;

        long long id =
            strtoll(
                copy,
                &end,
                10
            );

        if (end != copy &&
            *end == '\0' &&
            (UserId)id ==
                user_id) {

            found = 1;
            continue;
        }

        fputs(
            line,
            temp
        );
    }

    fclose(source);

    if (fflush(temp) == EOF ||
        fsync(fileno(temp)) == -1) {

        fclose(temp);
        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    fclose(temp);

    if (!found) {

        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_NOT_FOUND;
    }

    if (rename(
            temp_path,
            USERS_FILE) == -1) {

        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    unlock_users_file(source_fd);
    close(source_fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}

OperationStatus update_user_role(
    UserId user_id,
    UserRole new_role)
{
    if (user_id <= 0 ||
        (new_role != ROLE_CUSTOMER &&
         new_role != ROLE_EMPLOYEE)) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &users_write_mutex
    );

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status != STATUS_OK) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return status;
    }

    if (user.role != ROLE_CUSTOMER &&
        user.role != ROLE_EMPLOYEE) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_PERMISSION_DENIED;
    }

    if (user.role == new_role) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_CONFLICT;
    }

    int source_fd =
        open(
            USERS_FILE,
            O_RDWR
        );

    if (source_fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_users_file(
            source_fd) == -1) {

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *source =
        fdopen(
            dup(source_fd),
            "r"
        );

    if (source == NULL) {

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    const char *temp_path =
        STORAGE_DIR "/users_role.tmp";

    FILE *temp =
        fopen(
            temp_path,
            "w"
        );

    if (temp == NULL) {

        fclose(source);

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               source) != NULL) {

        char copy[2048];

        if (strlen(line) >=
            sizeof(copy)) {

            fclose(source);
            fclose(temp);

            unlink(temp_path);

            unlock_users_file(
                source_fd
            );

            close(source_fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        strcpy(
            copy,
            line
        );

        char *separator =
            strchr(
                copy,
                '|'
            );

        if (separator == NULL) {

            fputs(
                line,
                temp
            );

            continue;
        }

        *separator =
            '\0';

        errno = 0;

        char *end = NULL;

        long long current_id =
            strtoll(
                copy,
                &end,
                10
            );

        if (errno == 0 &&
            end != copy &&
            *end == '\0' &&
            (UserId)current_id ==
                user_id) {

            if (fprintf(
                    temp,
                    "%lld|%s|%s|%d|%d|%s\n",

                    (long long)
                        user.user_id,

                    user.username,

                    user.password_hash,

                    (int)new_role,

                    user.active,

                    user.full_name
                ) < 0) {

                fclose(source);
                fclose(temp);

                unlink(temp_path);

                unlock_users_file(
                    source_fd
                );

                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }

            found = 1;

        } else {

            if (fputs(
                    line,
                    temp) == EOF) {

                fclose(source);
                fclose(temp);

                unlink(temp_path);

                unlock_users_file(
                    source_fd
                );

                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }
        }
    }

    fclose(source);

    if (!found) {

        fclose(temp);

        unlink(temp_path);

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_NOT_FOUND;
    }

    if (fflush(temp) == EOF ||
        fsync(
            fileno(temp)) == -1) {

        fclose(temp);

        unlink(temp_path);

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (fclose(temp) == EOF) {

        unlink(temp_path);

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (rename(
            temp_path,
            USERS_FILE) == -1) {

        unlink(temp_path);

        unlock_users_file(
            source_fd
        );

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    unlock_users_file(
        source_fd
    );

    close(source_fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}

OperationStatus update_user_active_status(
    UserId user_id,
    int active)
{
    if (user_id <= 0 ||
        (active != 0 && active != 1)) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(
        &users_write_mutex
    );

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status != STATUS_OK) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return status;
    }

    if (user.active == active) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_CONFLICT;
    }

    int source_fd =
        open(
            USERS_FILE,
            O_RDWR
        );

    if (source_fd == -1) {

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_users_file(
            source_fd) == -1) {

        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    FILE *source =
        fdopen(
            dup(source_fd),
            "r"
        );

    if (source == NULL) {

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    const char *temp_path =
        STORAGE_DIR "/users_active.tmp";

    FILE *temp =
        fopen(
            temp_path,
            "w"
        );

    if (temp == NULL) {

        fclose(source);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    char line[2048];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               source) != NULL) {

        char copy[2048];

        if (strlen(line) >=
            sizeof(copy)) {

            fclose(source);
            fclose(temp);
            unlink(temp_path);

            unlock_users_file(source_fd);
            close(source_fd);

            pthread_mutex_unlock(
                &users_write_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        strcpy(
            copy,
            line
        );

        char *separator =
            strchr(
                copy,
                '|'
            );

        if (separator == NULL) {

            fputs(
                line,
                temp
            );

            continue;
        }

        *separator =
            '\0';

        errno = 0;

        char *end = NULL;

        long long current_id =
            strtoll(
                copy,
                &end,
                10
            );

        if (errno == 0 &&
            end != copy &&
            *end == '\0' &&
            (UserId)current_id ==
                user_id) {

            if (fprintf(
                    temp,
                    "%lld|%s|%s|%d|%d|%s\n",

                    (long long)
                        user.user_id,

                    user.username,

                    user.password_hash,

                    (int)
                        user.role,

                    active,

                    user.full_name
                ) < 0) {

                fclose(source);
                fclose(temp);
                unlink(temp_path);

                unlock_users_file(source_fd);
                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }

            found = 1;

        } else {

            if (fputs(
                    line,
                    temp) == EOF) {

                fclose(source);
                fclose(temp);
                unlink(temp_path);

                unlock_users_file(source_fd);
                close(source_fd);

                pthread_mutex_unlock(
                    &users_write_mutex
                );

                return STATUS_STORAGE_ERROR;
            }
        }
    }

    fclose(source);

    if (!found) {

        fclose(temp);
        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_NOT_FOUND;
    }

    if (fflush(temp) == EOF ||
        fsync(
            fileno(temp)) == -1) {

        fclose(temp);
        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (fclose(temp) == EOF) {

        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (rename(
            temp_path,
            USERS_FILE) == -1) {

        unlink(temp_path);

        unlock_users_file(source_fd);
        close(source_fd);

        pthread_mutex_unlock(
            &users_write_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    unlock_users_file(source_fd);
    close(source_fd);

    pthread_mutex_unlock(
        &users_write_mutex
    );

    return STATUS_OK;
}
