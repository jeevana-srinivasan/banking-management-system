#ifndef AUTH_H
#define AUTH_H

#include "common.h"
#include "users.h"

OperationStatus authenticate_user(const char *username,
                                  const char *password,
                                  UserRecord *authenticated_user);

OperationStatus change_user_password(
    UserId user_id,
    const char *current_password,
    const char *new_password
);

#endif
