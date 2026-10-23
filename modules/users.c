#include "users.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USERS_FILE STORAGE_DIR "/users.txt"
#define USER_LINE_SIZE 1024

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
