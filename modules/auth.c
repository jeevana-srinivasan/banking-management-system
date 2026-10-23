#include "auth.h"

#include <crypt.h>
#include <string.h>

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
