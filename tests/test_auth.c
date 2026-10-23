#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "users.h"

static const char *status_name(OperationStatus status)
{
    switch (status) {
        case STATUS_OK:
            return "STATUS_OK";

        case STATUS_INVALID_INPUT:
            return "STATUS_INVALID_INPUT";

        case STATUS_NOT_FOUND:
            return "STATUS_NOT_FOUND";

        case STATUS_PERMISSION_DENIED:
            return "STATUS_PERMISSION_DENIED";

        case STATUS_INACTIVE:
            return "STATUS_INACTIVE";

        case STATUS_STORAGE_ERROR:
            return "STATUS_STORAGE_ERROR";

        case STATUS_INTERNAL_ERROR:
            return "STATUS_INTERNAL_ERROR";

        default:
            return "OTHER_STATUS";
    }
}

static void test_login(const char *description,
                       const char *username,
                       const char *password)
{
    UserRecord user;

    OperationStatus status =
        authenticate_user(username, password, &user);

    printf("\n--- %s ---\n", description);
    printf("Username: %s\n", username);
    printf("Result: %s\n", status_name(status));

    if (status == STATUS_OK) {
        printf("User ID: %lld\n",
               (long long)user.user_id);

        printf("Full name: %s\n",
               user.full_name);

        printf("Role: %d\n",
               user.role);
    }
}

int main(void)
{
    /*
     * Change these passwords only if you chose
     * different test passwords when generating hashes.
     */

    test_login(
        "Correct login",
        "alice",
        "alice123"
    );

    test_login(
        "Wrong password",
        "alice",
        "wrongpassword"
    );

    test_login(
        "Unknown user",
        "unknown_user",
        "anything"
    );

    test_login(
        "Disabled user",
        "disabled1",
        "disabled123"
    );

    return 0;
}
