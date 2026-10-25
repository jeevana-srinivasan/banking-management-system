#include "auth.h"
#include "users.h"

#include <crypt.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

OperationStatus authenticate_user(const char *username,
                                  const char *password,
                                  UserRecord *authenticated_user)
{
    if (username == NULL ||
        password == NULL ||
        authenticated_user == NULL ||
        username[0] == '\0' ||
        password[0] == '\0') {
        return STATUS_INVALID_INPUT;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_username(username, &user);

    if (status != STATUS_OK)
        return status;

    if (!user.active)
        return STATUS_INACTIVE;

    char *calculated_hash =
        crypt(password, user.password_hash);

    if (calculated_hash == NULL)
        return STATUS_INTERNAL_ERROR;

    if (strcmp(calculated_hash,
               user.password_hash) != 0) {
        return STATUS_PERMISSION_DENIED;
    }

    *authenticated_user = user;

    return STATUS_OK;
}

static int generate_password_salt(
    char *salt,
    size_t salt_size)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789./";

    unsigned char random_bytes[16];

    int fd =
        open(
            "/dev/urandom",
            O_RDONLY
        );

    if (fd == -1)
        return -1;

    ssize_t total = 0;

    while (total <
           (ssize_t)
               sizeof(random_bytes)) {

        ssize_t n =
            read(
                fd,
                random_bytes + total,
                sizeof(random_bytes) -
                    (size_t)total
            );

        if (n < 0) {

            if (errno == EINTR)
                continue;

            close(fd);
            return -1;
        }

        if (n == 0) {

            close(fd);
            return -1;
        }

        total += n;
    }

    close(fd);

    char random_salt[17];

    for (size_t i = 0;
         i < 16;
         i++) {

        random_salt[i] =
            charset[
                random_bytes[i] %
                (sizeof(charset) - 1)
            ];
    }

    random_salt[16] =
        '\0';

    int written =
        snprintf(
            salt,
            salt_size,
            "$6$%s$",
            random_salt
        );

    if (written < 0 ||
        (size_t)written >=
            salt_size) {

        return -1;
    }

    return 0;
}

OperationStatus generate_password_hash(
    const char *password,
    char *hash_output,
    size_t output_size)
{
    if (password == NULL ||
        hash_output == NULL ||
        output_size == 0) {

        return STATUS_INVALID_INPUT;
    }

    size_t length =
        strlen(password);

    if (length < 8 ||
        length > MAX_PASSWORD_LEN) {

        return STATUS_INVALID_INPUT;
    }

    if (strchr(
        password,
        '|') != NULL) {

   	 return STATUS_INVALID_INPUT;
    }	

    char salt[64];

    if (generate_password_salt(
            salt,
            sizeof(salt)) == -1) {

        return STATUS_INTERNAL_ERROR;
    }

    char *generated =
        crypt(
            password,
            salt
        );

    if (generated == NULL)
        return STATUS_INTERNAL_ERROR;

    if (strlen(generated) >=
        output_size) {

        return STATUS_INTERNAL_ERROR;
    }

    strcpy(
        hash_output,
        generated
    );

    return STATUS_OK;
}

OperationStatus change_user_password(
    UserId user_id,
    const char *current_password,
    const char *new_password)
{
    if (user_id <= 0 ||
        current_password == NULL ||
        new_password == NULL) {

        return STATUS_INVALID_INPUT;
    }

    size_t current_length =
        strlen(current_password);

    size_t new_length =
        strlen(new_password);

    if (current_length == 0 ||
        new_length < 8 ||
        new_length >
            MAX_PASSWORD_LEN) {

        return STATUS_INVALID_INPUT;
    }

    if (strchr(
        current_password,
        '|') != NULL ||
    strchr(
        new_password,
        '|') != NULL) {

    return STATUS_INVALID_INPUT;
	}

    /*
     * Do not allow "changing" to exactly
     * the same password.
     */
    if (strcmp(
            current_password,
            new_password) == 0) {

        return STATUS_CONFLICT;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status != STATUS_OK)
        return status;

    /*
     * Verify CURRENT password against
     * the customer's existing hash.
     */
    char *verified =
        crypt(
            current_password,
            user.password_hash
        );

    if (verified == NULL ||
        strcmp(
            verified,
            user.password_hash) != 0) {

        return STATUS_PERMISSION_DENIED;
    }

    char salt[64];

    if (generate_password_salt(
            salt,
            sizeof(salt)) == -1) {

        return STATUS_INTERNAL_ERROR;
    }

    /*
     * Generate completely new SHA-512 hash.
     */
    char *new_hash =
        crypt(
            new_password,
            salt
        );

    if (new_hash == NULL)
        return STATUS_INTERNAL_ERROR;

    char hash_copy[
        MAX_PASSWORD_HASH_LEN
    ];

    if (strlen(new_hash) >=
        sizeof(hash_copy)) {

        return STATUS_INTERNAL_ERROR;
    }

    strcpy(
        hash_copy,
        new_hash
    );

    status =
        update_user_password_hash(
            user_id,
            hash_copy
        );

    /*
     * Remove password-hash copy from
     * this stack frame when possible.
     */
    memset(
        hash_copy,
        0,
        sizeof(hash_copy)
    );

    return status;
}
