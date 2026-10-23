#ifndef USERS_H
#define USERS_H

#include "common.h"
#include "config.h"

#define MAX_PASSWORD_HASH_LEN 256

typedef struct {
    UserId user_id;
    char username[MAX_USERNAME_LEN + 1];
    char password_hash[MAX_PASSWORD_HASH_LEN];
    UserRole role;
    int active;
    char full_name[MAX_NAME_LEN + 1];
} UserRecord;

/*
 * Find a user by username.
 *
 * Returns:
 *   STATUS_OK        -> user found
 *   STATUS_NOT_FOUND -> username not found
 *   STATUS_STORAGE_ERROR -> file/storage problem
 */
OperationStatus find_user_by_username(const char *username,
                                      UserRecord *user);

/*
 * Find a user by user ID.
 */
OperationStatus find_user_by_id(UserId user_id,
                                UserRecord *user);

#endif
