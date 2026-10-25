#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include "common.h"
#include "config.h"

typedef struct {
    int logged_in;

    UserId user_id;
    UserRole role;

    char full_name[MAX_NAME_LEN + 1];
} ClientUser;

void display_customer_menu(const ClientUser *user);

const char *client_role_name(UserRole role);

void display_employee_menu(const ClientUser *user);

void display_manager_menu( const ClientUser *user);

void display_admin_menu(const ClientUser *user);

#endif
