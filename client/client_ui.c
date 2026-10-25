#include "client_ui.h"

#include <stdio.h>

const char *client_role_name(UserRole role)
{
    switch (role) {

        case ROLE_CUSTOMER:
            return "CUSTOMER";

        case ROLE_EMPLOYEE:
            return "EMPLOYEE";

        case ROLE_MANAGER:
            return "MANAGER";

        case ROLE_ADMIN:
            return "ADMIN";

        default:
            return "UNKNOWN";
    }
}

void display_customer_menu(const ClientUser *user)
{
    if (user == NULL)
        return;

    printf("\n");

    printf("========================================\n");
    printf("             CUSTOMER MENU\n");
    printf("========================================\n");

    printf("User ID : %lld\n",
           (long long)user->user_id);

    printf("Name    : %s\n",
           user->full_name);

    printf("Role    : %s\n",
           client_role_name(user->role));

    printf("----------------------------------------\n");

    printf("1. View Balance\n");
    printf("2. Deposit\n");
    printf("3. Withdraw\n");
    printf("4. Transfer\n");
    printf("5. Transaction History\n");
    printf("6. Apply for Loan\n");
    printf("7. View Loan Status\n");
    printf("8. Change Password\n");
    printf("9. Submit Feedback\n");
    printf("10. Logout\n");
    printf("0. Exit\n");

    printf("========================================\n");
}

void display_employee_menu(
    const ClientUser *user)
{
    printf("\n");
    printf("========================================\n");
    printf("             EMPLOYEE MENU\n");
    printf("========================================\n");

    printf(
        "User ID : %lld\n",
        (long long)user->user_id
    );

    printf(
        "Name    : %s\n",
        user->full_name
    );

    printf(
        "Role    : %s\n",
        client_role_name(user->role)
    );

    printf("----------------------------------------\n");

    printf("1. Add New Customer Account\n");
    printf("2. View Customer Account Details\n");
    printf("3. Modify Customer Details\n");
    printf("4. Close Customer Account\n");
    printf("5. View Customer Transactions\n");
    printf("6. View Assigned Loan Applications\n");
    printf("7. Process Loan Application\n");
    printf("8. Change Password\n");
    printf("9. Logout\n");
    printf("0. Exit\n");

    printf("========================================\n");
}

void display_manager_menu(
    const ClientUser *user)
{
    printf("\n");
    printf("========================================\n");
    printf("              MANAGER MENU\n");
    printf("========================================\n");

    printf(
        "User ID : %lld\n",
        (long long)user->user_id
    );

    printf(
        "Name    : %s\n",
        user->full_name
    );

    printf(
        "Role    : MANAGER\n"
    );

    printf("----------------------------------------\n");

    printf("1. Activate Customer Account\n");
    printf("2. Deactivate Customer Account\n");
    printf("3. View Pending Loan Applications\n");
    printf("4. Assign Loan to Employee\n");
    printf("5. Review Customer Feedback\n");
    printf("6. Change Password\n");
    printf("7. Logout\n");
    printf("0. Exit\n");

    printf("========================================\n");
}

void display_admin_menu(
    const ClientUser *user)
{
    printf("\n");
    printf("========================================\n");
    printf("               ADMIN MENU\n");
    printf("========================================\n");

    printf(
        "User ID : %lld\n",
        (long long)user->user_id
    );

    printf(
        "Name    : %s\n",
        user->full_name
    );

    printf(
        "Role    : ADMIN\n"
    );

    printf("----------------------------------------\n");

    printf("1. Add New Bank Employee\n");
    printf("2. View User Details\n");
    printf("3. Modify Customer/Employee Details\n");
    printf("4. Manage User Roles\n");
    printf("5. Change Password\n");
    printf("6. Logout\n");
    printf("0. Exit\n");

    printf("========================================\n");
}
