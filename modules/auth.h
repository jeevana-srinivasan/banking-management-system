#ifndef AUTH_H
#define AUTH_H

#include "common.h"
#include "users.h"

OperationStatus authenticate_user(const char *username,
                                  const char *password,
                                  UserRecord *authenticated_user);

#endif
