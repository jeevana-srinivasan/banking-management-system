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

OperationStatus update_user_password_hash(
    UserId user_id,
    const char *new_password_hash
);

OperationStatus update_user_full_name(
    UserId user_id,
    const char *new_full_name
);

OperationStatus create_user_record(
    const char *username,
    const char *password_hash,
    UserRole role,
    int active,
    const char *full_name,
    UserRecord *created_user
);

OperationStatus delete_user_by_id(
    UserId user_id
);

OperationStatus update_user_role(
    UserId user_id,
    UserRole new_role
);

#endif
