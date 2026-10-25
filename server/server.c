#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "protocol.h"
#include "session.h"
#include "auth.h"
#include "accounts.h"
#include "transactions.h"
#include "loans.h"
#include "feedback.h"
#include "users.h"

#define BUFFER_SIZE (PROTOCOL_MAX_MESSAGE + 1U)

static int parse_login_request(
    const char *request,
    char *username,
    size_t username_size,
    char *password,
    size_t password_size)
{
    const char *start =
        request + 6;

    const char *separator =
        strchr(start, '|');

    if (separator == NULL ||
        strchr(separator + 1, '|') != NULL) {

        return -1;
    }

    size_t ulen =
        (size_t)(separator - start);

    size_t plen =
        strlen(separator + 1);

    if (ulen == 0 ||
        ulen >= username_size ||
        plen == 0 ||
        plen >= password_size) {

        return -1;
    }

    memcpy(username,
           start,
           ulen);

    username[ulen] = '\0';

    strcpy(password,
           separator + 1);

    return 0;
}

static int parse_money_request(
    const char *request,
    const char *command,
    Money *amount)
{
    size_t length =
        strlen(command);

    if (strncmp(request,
                command,
                length) != 0 ||
        request[length] != '|') {

        return -1;
    }

    const char *value =
        request + length + 1;

    errno = 0;

    char *end = NULL;

    long long parsed =
        strtoll(value,
                &end,
                10);

    if (errno != 0 ||
        end == value ||
        *end != '\0' ||
        parsed <= 0 ||
        parsed >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    *amount =
        (Money)parsed;

    return 0;
}

static int parse_transfer_request(
    const char *request,
    AccountId *destination,
    Money *amount)
{
    if (strncmp(request,
                "TRANSFER|",
                9) != 0) {

        return -1;
    }

    char copy[256];

    if (strlen(request) >= sizeof(copy))
        return -1;

    strcpy(copy,
           request + 9);

    char *separator =
        strchr(copy, '|');

    if (separator == NULL)
        return -1;

    *separator = '\0';

    if (strchr(separator + 1, '|') != NULL)
        return -1;

    errno = 0;

    char *end = NULL;

    long long account =
        strtoll(copy,
                &end,
                10);

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        account <= 0) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long long value =
        strtoll(separator + 1,
                &end,
                10);

    if (errno != 0 ||
        end == separator + 1 ||
        *end != '\0' ||
        value <= 0 ||
        value >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    *destination =
        (AccountId)account;

    *amount =
        (Money)value;

    return 0;
}

static int parse_loan_request(
    const char *request,
    Money *amount,
    int *tenure)
{
    if (strncmp(
            request,
            "APPLY_LOAN|",
            11) != 0) {

        return -1;
    }

    char copy[256];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request + 11
    );

    char *separator =
        strchr(copy, '|');

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end =
        NULL;

    long long parsed_amount =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        parsed_amount <= 0 ||
        parsed_amount >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long parsed_tenure =
        strtol(
            separator + 1,
            &end,
            10
        );

    if (errno != 0 ||
        end == separator + 1 ||
        *end != '\0') {

        return -1;
    }

    if (parsed_tenure != 6 &&
        parsed_tenure != 12 &&
        parsed_tenure != 24 &&
        parsed_tenure != 36 &&
        parsed_tenure != 60) {

        return -1;
    }

    *amount =
        (Money)parsed_amount;

    *tenure =
        (int)parsed_tenure;

    return 0;
}

static int parse_change_password_request(
    const char *request,
    char *current_password,
    size_t current_size,
    char *new_password,
    size_t new_size)
{
    const char prefix[] =
        "CHANGE_PASSWORD|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    const char *start =
        request +
        strlen(prefix);

    const char *separator =
        strchr(
            start,
            '|'
        );

    if (separator == NULL)
        return -1;

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    size_t current_length =
        (size_t)
            (separator - start);

    size_t new_length =
        strlen(
            separator + 1
        );

    if (current_length == 0 ||
        current_length >=
            current_size ||
        new_length < 8 ||
        new_length >=
            new_size) {

        return -1;
    }

    memcpy(
        current_password,
        start,
        current_length
    );

    current_password[
        current_length
    ] = '\0';

    strcpy(
        new_password,
        separator + 1
    );

    return 0;
}

static int parse_open_account_request(
    const char *request,
    UserId *customer_user_id,
    Money *initial_balance)
{
    if (strncmp(
            request,
            "OPEN_ACCOUNT|",
            13) != 0) {

        return -1;
    }

    char copy[256];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request + 13
    );

    char *separator =
        strchr(copy, '|');

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        user_id <= 0 ||
        user_id >
            9999999999LL) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long long balance =
        strtoll(
            separator + 1,
            &end,
            10
        );

    if (errno != 0 ||
        end == separator + 1 ||
        *end != '\0' ||
        balance < 0 ||
        balance >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    *customer_user_id =
        (UserId)user_id;

    *initial_balance =
        (Money)balance;

    return 0;
}

static const char *account_status_name(
    AccountStatus status)
{
    switch (status) {

        case ACCOUNT_ACTIVE:
            return "ACTIVE";

        case ACCOUNT_INACTIVE:
            return "INACTIVE";

        case ACCOUNT_CLOSED:
            return "CLOSED";

        default:
            return "UNKNOWN";
    }
}

static int parse_view_account_request(
    const char *request,
    int *lookup_type,
    long long *lookup_value)
{
    if (strncmp(
            request,
            "VIEW_ACCOUNT|",
            13) != 0) {

        return -1;
    }

    char copy[256];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request + 13
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    if (strcmp(
            copy,
            "ACCOUNT") == 0) {

        *lookup_type = 1;

    } else if (strcmp(
                   copy,
                   "USER") == 0) {

        *lookup_type = 2;

    } else {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long value =
        strtoll(
            separator + 1,
            &end,
            10
        );

    if (errno != 0 ||
        end == separator + 1 ||
        *end != '\0' ||
        value <= 0) {

        return -1;
    }

    *lookup_value =
        value;

    return 0;
}

static int parse_modify_customer_request(
    const char *request,
    UserId *user_id,
    char *new_name,
    size_t name_size)
{
    const char prefix[] =
        "MODIFY_CUSTOMER|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    const char *start =
        request +
        strlen(prefix);

    const char *separator =
        strchr(
            start,
            '|'
        );

    if (separator == NULL)
        return -1;

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    size_t id_length =
        (size_t)
            (separator - start);

    size_t name_length =
        strlen(
            separator + 1
        );

    if (id_length == 0 ||
        id_length >= 32 ||
        name_length == 0 ||
        name_length >=
            name_size) {

        return -1;
    }

    char id_text[32];

    memcpy(
        id_text,
        start,
        id_length
    );

    id_text[
        id_length
    ] = '\0';

    errno = 0;

    char *end = NULL;

    long long parsed_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        parsed_id <= 0) {

        return -1;
    }

    strcpy(
        new_name,
        separator + 1
    );

    *user_id =
        (UserId)parsed_id;

    return 0;
}

static int parse_add_customer_request(
    const char *request,
    char *username,
    size_t username_size,
    char *password,
    size_t password_size,
    char *full_name,
    size_t name_size,
    Money *initial_balance)
{
    const char prefix[] =
        "ADD_CUSTOMER|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[
        MAX_REQUEST_SIZE
    ];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *saveptr = NULL;

    char *username_field =
        strtok_r(
            copy,
            "|",
            &saveptr
        );

    char *password_field =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    char *name_field =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    char *balance_field =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    if (username_field == NULL ||
        password_field == NULL ||
        name_field == NULL ||
        balance_field == NULL ||
        strtok_r(
            NULL,
            "|",
            &saveptr) != NULL) {

        return -1;
    }

    if (strlen(username_field) == 0 ||
        strlen(username_field) >=
            username_size ||
        strlen(password_field) < 8 ||
        strlen(password_field) >=
            password_size ||
        strlen(name_field) == 0 ||
        strlen(name_field) >=
            name_size) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long balance =
        strtoll(
            balance_field,
            &end,
            10
        );

    if (errno != 0 ||
        end == balance_field ||
        *end != '\0' ||
        balance < 0 ||
        balance >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    strcpy(
        username,
        username_field
    );

    strcpy(
        password,
        password_field
    );

    strcpy(
        full_name,
        name_field
    );

    *initial_balance =
        (Money)balance;

    return 0;
}

static int parse_close_account_request(
    const char *request,
    AccountId *account_id)
{
    const char prefix[] =
        "CLOSE_ACCOUNT|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    const char *value =
        request +
        strlen(prefix);

    if (value[0] == '\0' ||
        strchr(value, '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long parsed =
        strtoll(
            value,
            &end,
            10
        );

    if (errno != 0 ||
        end == value ||
        *end != '\0' ||
        parsed <= 0) {

        return -1;
    }

    *account_id =
        (AccountId)parsed;

    return 0;
}

static int parse_customer_transactions_request(
    const char *request,
    int *lookup_type,
    long long *lookup_value)
{
    const char prefix[] =
        "VIEW_CUSTOMER_TXNS|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[256];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    if (strcmp(
            copy,
            "ACCOUNT") == 0) {

        *lookup_type = 1;

    } else if (strcmp(
                   copy,
                   "USER") == 0) {

        *lookup_type = 2;

    } else {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long value =
        strtoll(
            separator + 1,
            &end,
            10
        );

    if (errno != 0 ||
        end == separator + 1 ||
        *end != '\0' ||
        value <= 0) {

        return -1;
    }

    *lookup_value =
        value;

    return 0;
}

static int parse_loan_decision_request(
    const char *request,
    LoanId *loan_id,
    int *approve)
{
    const char prefix[] =
        "DECIDE_LOAN|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[128];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long id =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        id <= 0) {

        return -1;
    }

    if (strcmp(
            separator + 1,
            "APPROVE") == 0) {

        *approve = 1;

    } else if (strcmp(
                   separator + 1,
                   "REJECT") == 0) {

        *approve = 0;

    } else {

        return -1;
    }

    *loan_id =
        (LoanId)id;

    return 0;
}

static int parse_employee_change_password_request(
    const char *request,
    char *current_password,
    size_t current_size,
    char *new_password,
    size_t new_size)
{
    const char prefix[] =
        "EMP_CHANGE_PASSWORD|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[
        MAX_REQUEST_SIZE
    ];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    if (copy[0] == '\0' ||
        separator[1] == '\0' ||
        strlen(copy) >=
            current_size ||
        strlen(separator + 1) >=
            new_size) {

        return -1;
    }

    strcpy(
        current_password,
        copy
    );

    strcpy(
        new_password,
        separator + 1
    );

    return 0;
}

static int parse_manager_account_status_request(
    const char *request,
    const char *command,
    AccountId *account_id)
{
    size_t command_length =
        strlen(command);

    if (strncmp(
            request,
            command,
            command_length) != 0) {

        return -1;
    }

    const char *value =
        request +
        command_length;

    if (value[0] == '\0' ||
        strchr(
            value,
            '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long parsed =
        strtoll(
            value,
            &end,
            10
        );

    if (errno != 0 ||
        end == value ||
        *end != '\0' ||
        parsed <= 0) {

        return -1;
    }

    *account_id =
        (AccountId)parsed;

    return 0;
}

//Manager handler function
static int handle_manager_account_status(
    int client_fd,
    const Session *session,
    const char *request,
    AccountStatus new_status,
    const char *command)
{
    if (session->role !=
        ROLE_MANAGER) {

        send_message(
            client_fd,
            "MANAGER_ACCOUNT_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    AccountId account_id;

    if (parse_manager_account_status_request(
            request,
            command,
            &account_id) == -1) {

        send_message(
            client_fd,
            "MANAGER_ACCOUNT_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    AccountRecord account;

    OperationStatus status =
        set_account_status(
            account_id,
            new_status,
            &account
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "MANAGER_ACCOUNT_FAILED|NOT_FOUND"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "MANAGER_ACCOUNT_FAILED|ACCOUNT_CLOSED"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "MANAGER_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    const char *status_name =
        account.status ==
                ACCOUNT_ACTIVE
            ? "ACTIVE"
            : "INACTIVE";

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "MANAGER_ACCOUNT_OK|%lld|%lld|%s",
        (long long)
            account.account_id,
        (long long)
            account.user_id,
        status_name
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int handle_view_pending_loans(
    int client_fd,
    const Session *session)
{
    if (session->role !=
        ROLE_MANAGER) {

        send_message(
            client_fd,
            "PENDING_LOANS_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char loans[
        MAX_RESPONSE_SIZE
    ];

    OperationStatus status =
        get_pending_loans(
            loans,
            sizeof(loans)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "PENDING_LOANS_EMPTY"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "PENDING_LOANS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),
            "PENDING_LOANS_OK|%s",
            loans
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "PENDING_LOANS_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int parse_assign_loan_request(
    const char *request,
    LoanId *loan_id,
    UserId *employee_id)
{
    const char prefix[] =
        "ASSIGN_LOAN|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[128];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    errno = 0;

    char *end1 = NULL;
    char *end2 = NULL;

    long long parsed_loan =
        strtoll(
            copy,
            &end1,
            10
        );

    if (errno != 0 ||
        end1 == copy ||
        *end1 != '\0' ||
        parsed_loan <= 0) {

        return -1;
    }

    errno = 0;

    long long parsed_employee =
        strtoll(
            separator + 1,
            &end2,
            10
        );

    if (errno != 0 ||
        end2 == separator + 1 ||
        *end2 != '\0' ||
        parsed_employee <= 0) {

        return -1;
    }

    *loan_id =
        (LoanId)parsed_loan;

    *employee_id =
        (UserId)parsed_employee;

    return 0;
}

static int handle_assign_loan(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_MANAGER) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    LoanId loan_id;
    UserId employee_id;

    if (parse_assign_loan_request(
            request,
            &loan_id,
            &employee_id) == -1) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    /*
     * Verify that the supplied user really
     * is an active employee.
     */
    UserRecord employee;

    OperationStatus status =
        find_user_by_id(
            employee_id,
            &employee
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|EMPLOYEE_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    if (employee.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|NOT_EMPLOYEE"
        );

        return 0;
    }

    if (!employee.active) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|EMPLOYEE_INACTIVE"
        );

        return 0;
    }

    LoanRecord loan;

    status =
        assign_loan_to_employee(
            loan_id,
            employee_id,
            &loan
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|LOAN_NOT_FOUND"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|NOT_PENDING"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ASSIGN_LOAN_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "ASSIGN_LOAN_OK|%lld|%lld",
        (long long)
            loan.loan_id,
        (long long)
            employee_id
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int handle_review_feedback(
    int client_fd,
    const Session *session)
{
    if (session->role !=
        ROLE_MANAGER) {

        send_message(
            client_fd,
            "FEEDBACK_REVIEW_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char feedback[
        MAX_RESPONSE_SIZE
    ];

    OperationStatus status =
        get_all_feedback(
            feedback,
            sizeof(feedback)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "FEEDBACK_REVIEW_EMPTY"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "FEEDBACK_REVIEW_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),
            "FEEDBACK_REVIEW_OK|%s",
            feedback
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "FEEDBACK_REVIEW_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int parse_manager_change_password_request(
    const char *request,
    char *current_password,
    size_t current_size,
    char *new_password,
    size_t new_size)
{
    const char prefix[] =
        "MANAGER_CHANGE_PASSWORD|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[
        MAX_REQUEST_SIZE
    ];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    if (copy[0] == '\0' ||
        separator[1] == '\0' ||
        strlen(copy) >= current_size ||
        strlen(separator + 1) >= new_size) {

        return -1;
    }

    strcpy(
        current_password,
        copy
    );

    strcpy(
        new_password,
        separator + 1
    );

    return 0;
}


static int handle_manager_change_password(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_MANAGER) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    if (parse_manager_change_password_request(
            request,
            current_password,
            sizeof(current_password),
            new_password,
            sizeof(new_password)) == -1) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    OperationStatus status =
        change_user_password(
            session->user_id,
            current_password,
            new_password
        );

    memset(
        current_password,
        0,
        sizeof(current_password)
    );

    memset(
        new_password,
        0,
        sizeof(new_password)
    );

    if (status ==
        STATUS_PERMISSION_DENIED) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|SAME_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|INVALID_PASSWORD"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "MANAGER_PASSWORD_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        "MANAGER_PASSWORD_OK"
    );

    return 0;
}

//ADMIN FUNCTIONS
static int parse_add_employee_request(
    const char *request,
    char *username,
    size_t username_size,
    char *password,
    size_t password_size,
    char *full_name,
    size_t name_size)
{
    const char prefix[] =
        "ADD_EMPLOYEE|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[
        MAX_REQUEST_SIZE
    ];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *saveptr = NULL;

    char *username_field =
        strtok_r(
            copy,
            "|",
            &saveptr
        );

    char *password_field =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    char *name_field =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    if (username_field == NULL ||
        password_field == NULL ||
        name_field == NULL ||
        strtok_r(
            NULL,
            "|",
            &saveptr) != NULL) {

        return -1;
    }

    if (strlen(username_field) == 0 ||
        strlen(username_field) >=
            username_size ||
        strlen(password_field) < 8 ||
        strlen(password_field) >=
            password_size ||
        strlen(name_field) == 0 ||
        strlen(name_field) >=
            name_size) {

        return -1;
    }

    strcpy(
        username,
        username_field
    );

    strcpy(
        password,
        password_field
    );

    strcpy(
        full_name,
        name_field
    );

    return 0;
}

static int handle_add_employee(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_ADMIN) {

        send_message(
            client_fd,
            "ADD_EMPLOYEE_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char username[
        MAX_USERNAME_LEN + 1
    ];

    char password[
        MAX_PASSWORD_LEN + 1
    ];

    char full_name[
        MAX_NAME_LEN + 1
    ];

    if (parse_add_employee_request(
            request,
            username,
            sizeof(username),
            password,
            sizeof(password),
            full_name,
            sizeof(full_name)) == -1) {

        send_message(
            client_fd,
            "ADD_EMPLOYEE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    char password_hash[
        MAX_PASSWORD_HASH_LEN
    ];

    OperationStatus status =
        generate_password_hash(
            password,
            password_hash,
            sizeof(password_hash)
        );

    memset(
        password,
        0,
        sizeof(password)
    );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADD_EMPLOYEE_FAILED|PASSWORD_ERROR"
        );

        return 0;
    }

    UserRecord employee;

    status =
        create_user_record(
            username,
            password_hash,
            ROLE_EMPLOYEE,
            1,
            full_name,
            &employee
        );

    memset(
        password_hash,
        0,
        sizeof(password_hash)
    );

    if (status ==
        STATUS_DUPLICATE) {

        send_message(
            client_fd,
            "ADD_EMPLOYEE_FAILED|USERNAME_EXISTS"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADD_EMPLOYEE_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[512];

    snprintf(
        response,
        sizeof(response),
        "ADD_EMPLOYEE_OK|%lld|%s|%s",
        (long long)
            employee.user_id,
        employee.username,
        employee.full_name
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int handle_admin_view_user(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role != ROLE_ADMIN) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    const char prefix[] =
        "VIEW_USER|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    const char *value =
        request + strlen(prefix);

    if (value[0] == '\0' ||
        strchr(value, '|') != NULL) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    errno = 0;

    char *end = NULL;

    long long parsed_id =
        strtoll(
            value,
            &end,
            10
        );

    if (errno != 0 ||
        end == value ||
        *end != '\0' ||
        parsed_id <= 0) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            (UserId)parsed_id,
            &user
        );

    if (status == STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "VIEW_USER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[512];

    snprintf(
        response,
        sizeof(response),
        "VIEW_USER_OK|%lld|%s|%s|%d|%d",
        (long long)user.user_id,
        user.username,
        user.full_name,
        (int)user.role,
        user.active
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int handle_admin_modify_user(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role != ROLE_ADMIN) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    const char prefix[] =
        "ADMIN_MODIFY_USER|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    char copy[MAX_REQUEST_SIZE];

    if (strlen(request) >= sizeof(copy)) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    strcpy(
        copy,
        request + strlen(prefix)
    );

    char *separator =
        strchr(copy, '|');

    if (separator == NULL) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    *separator = '\0';

    const char *name =
        separator + 1;

    if (name[0] == '\0' ||
        strchr(name, '|') != NULL) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    errno = 0;

    char *end = NULL;

    long long parsed_id =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        parsed_id <= 0) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            (UserId)parsed_id,
            &user
        );

    if (status == STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|USER_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    if (user.role != ROLE_CUSTOMER &&
        user.role != ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_ROLE"
        );

        return 0;
    }

    status =
        update_user_full_name(
            user.user_id,
            name
        );

    if (status == STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|INVALID_NAME"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADMIN_MODIFY_USER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[512];

    snprintf(
        response,
        sizeof(response),
        "ADMIN_MODIFY_USER_OK|%lld|%s",
        (long long)user.user_id,
        name
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

static int handle_admin_change_role(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_ADMIN) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    const char prefix[] =
        "CHANGE_ROLE|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    char copy[128];

    if (strlen(request) >=
        sizeof(copy)) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    errno = 0;

    char *end = NULL;

    long long parsed_id =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        parsed_id <= 0) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    UserId user_id =
        (UserId)parsed_id;

    /*
     * Admin cannot change their own role.
     */
    if (user_id ==
        session->user_id) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|SELF"
        );

        return 0;
    }

    UserRole new_role;

    if (strcmp(
            separator + 1,
            "CUSTOMER") == 0) {

        new_role =
            ROLE_CUSTOMER;

    } else if (strcmp(
                   separator + 1,
                   "EMPLOYEE") == 0) {

        new_role =
            ROLE_EMPLOYEE;

    } else {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|INVALID_ROLE"
        );

        return 0;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|USER_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    /*
     * Only CUSTOMER and EMPLOYEE accounts
     * may be changed.
     */
    if (user.role != ROLE_CUSTOMER &&
        user.role != ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|PROTECTED_ROLE"
        );

        return 0;
    }

    /*
     * Avoid stale role stored inside an
     * already-active session.
     */
    if (session_is_user_active(
            user_id)) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|USER_LOGGED_IN"
        );

        return 0;
    }

    status =
        update_user_role(
            user_id,
            new_role
        );

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|SAME_ROLE"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CHANGE_ROLE_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "CHANGE_ROLE_OK|%lld|%s",
        (long long)user_id,
        new_role == ROLE_CUSTOMER
            ? "CUSTOMER"
            : "EMPLOYEE"
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}


static int parse_admin_change_password_request(
    const char *request,
    char *current_password,
    size_t current_size,
    char *new_password,
    size_t new_size)
{
    const char prefix[] =
        "ADMIN_CHANGE_PASSWORD|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        return -1;
    }

    char copy[
        MAX_REQUEST_SIZE
    ];

    if (strlen(request) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL)
        return -1;

    *separator =
        '\0';

    if (strchr(
            separator + 1,
            '|') != NULL) {

        return -1;
    }

    if (copy[0] == '\0' ||
        separator[1] == '\0' ||
        strlen(copy) >=
            current_size ||
        strlen(separator + 1) >=
            new_size) {

        return -1;
    }

    strcpy(
        current_password,
        copy
    );

    strcpy(
        new_password,
        separator + 1
    );

    return 0;
}


static int handle_admin_change_password(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_ADMIN) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    if (parse_admin_change_password_request(
            request,
            current_password,
            sizeof(current_password),
            new_password,
            sizeof(new_password)) == -1) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    OperationStatus status =
        change_user_password(
            session->user_id,
            current_password,
            new_password
        );

    memset(
        current_password,
        0,
        sizeof(current_password)
    );

    memset(
        new_password,
        0,
        sizeof(new_password)
    );

    if (status ==
        STATUS_PERMISSION_DENIED) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|SAME_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|INVALID_PASSWORD"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADMIN_PASSWORD_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        "ADMIN_PASSWORD_OK"
    );

    return 0;
}

static int handle_admin_user_access(
    int client_fd,
    const Session *session,
    const char *request)
{
    if (session->role !=
        ROLE_ADMIN) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    const char prefix[] =
        "SET_USER_ACCESS|";

    if (strncmp(
            request,
            prefix,
            strlen(prefix)) != 0) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    char copy[128];

    if (strlen(request) >=
        sizeof(copy)) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    strcpy(
        copy,
        request +
            strlen(prefix)
    );

    char *separator =
        strchr(
            copy,
            '|'
        );

    if (separator == NULL) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    *separator =
        '\0';

    errno = 0;

    char *end = NULL;

    long long parsed_id =
        strtoll(
            copy,
            &end,
            10
        );

    if (errno != 0 ||
        end == copy ||
        *end != '\0' ||
        parsed_id <= 0) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    UserId user_id =
        (UserId)parsed_id;

    /*
     * Admin cannot disable themselves.
     */
    if (user_id ==
        session->user_id) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|SELF"
        );

        return 0;
    }

    int active;

    if (strcmp(
            separator + 1,
            "ENABLE") == 0) {

        active = 1;

    } else if (strcmp(
                   separator + 1,
                   "DISABLE") == 0) {

        active = 0;

    } else {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    UserRecord user;

    OperationStatus status =
        find_user_by_id(
            user_id,
            &user
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|USER_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    /*
     * Admin controls Customer/Employee access.
     * Manager/Admin accounts remain protected.
     */
    if (user.role != ROLE_CUSTOMER &&
        user.role != ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|PROTECTED_USER"
        );

        return 0;
    }

    status =
        update_user_active_status(
            user_id,
            active
        );

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            active
                ? "USER_ACCESS_FAILED|ALREADY_ENABLED"
                : "USER_ACCESS_FAILED|ALREADY_DISABLED"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "USER_ACCESS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "USER_ACCESS_OK|%lld|%s",
        (long long)user_id,
        active
            ? "ENABLED"
            : "DISABLED"
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

//=======================================Process request============================================//
static int process_request(
    int client_fd,
    const char *request)
{
    if (strncmp(request,
                "LOGIN|",
                6) == 0) {

        Session existing;

        if (session_get_by_socket(
                client_fd,
                &existing)) {

            send_message(
                client_fd,
                "LOGIN_FAILED|CONNECTION_ALREADY_AUTHENTICATED"
            );

            return 0;
        }

        char username[
            MAX_USERNAME_LEN + 1
        ];

        char password[
            MAX_PASSWORD_LEN + 1
        ];

        if (parse_login_request(
                request,
                username,
                sizeof(username),
                password,
                sizeof(password)) == -1) {

            send_message(
                client_fd,
                "LOGIN_FAILED|INVALID_REQUEST"
            );

            return 0;
        }

        UserRecord user;

        OperationStatus status =
            authenticate_user(
                username,
                password,
                &user
            );

        memset(password,
               0,
               sizeof(password));

        if (status ==
                STATUS_NOT_FOUND ||
            status ==
                STATUS_PERMISSION_DENIED) {

            send_message(
                client_fd,
                "LOGIN_FAILED|INVALID_CREDENTIALS"
            );

            return 0;
        }

        if (status ==
            STATUS_INACTIVE) {

            send_message(
                client_fd,
                "LOGIN_FAILED|ACCOUNT_INACTIVE"
            );

            return 0;
        }

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "LOGIN_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        status =
            session_create(
                client_fd,
                user.user_id,
                user.role
            );

        if (status ==
            STATUS_DUPLICATE) {

            send_message(
                client_fd,
                "LOGIN_FAILED|USER_ALREADY_LOGGED_IN"
            );

            return 0;
        }

        char response[
            MAX_RESPONSE_SIZE
        ];

        snprintf(
            response,
            sizeof(response),
            "LOGIN_OK|%lld|%d|%s",
            (long long)user.user_id,
            (int)user.role,
            user.full_name
        );

        send_message(client_fd,
                     response);

        return 0;
    }

    if (strcmp(request,
               "LOGOUT") == 0) {

        session_remove_by_socket(
            client_fd
        );

        send_message(
            client_fd,
            "LOGOUT_OK"
        );

        return 0;
    }

    if (strcmp(request,
               "EXIT") == 0) {

        session_remove_by_socket(
            client_fd
        );

        send_message(
            client_fd,
            "GOODBYE"
        );

        return 1;
    }

    Session session;

    if (!session_get_by_socket(
            client_fd,
            &session)) {

        send_message(
            client_fd,
            "ERROR|AUTHENTICATION_REQUIRED"
        );

        return 0;
    }

    UserRecord current_user;

OperationStatus user_status =
    find_user_by_id(
        session.user_id,
        &current_user
    );

if (user_status != STATUS_OK) {

    send_message(
        client_fd,
        "ERROR|USER_NOT_FOUND"
    );

    return 0;
}

if (!current_user.active) {

    send_message(
        client_fd,
        "ERROR|USER_DISABLED"
    );

    return 0;
}


/*ADMIN COMMANDS*/

if (strncmp(
        request,
        "ADD_EMPLOYEE|",
        13) == 0) {

    return handle_add_employee(
        client_fd,
        &session,
        request
    );
}

if (strncmp(
        request,
        "VIEW_USER|",
        10) == 0) {

    return handle_admin_view_user(
        client_fd,
        &session,
        request
    );
}

if (strncmp(
        request,
        "ADMIN_MODIFY_USER|",
        18) == 0) {

    return handle_admin_modify_user(
        client_fd,
        &session,
        request
    );
}

if (strncmp(
        request,
        "CHANGE_ROLE|",
        12) == 0) {

    return handle_admin_change_role(
        client_fd,
        &session,
        request
    );
}

if (strncmp(
        request,
        "ADMIN_CHANGE_PASSWORD|",
        22) == 0) {

    return handle_admin_change_password(
        client_fd,
        &session,
        request
    );
}

if (strncmp(
        request,
        "SET_USER_ACCESS|",
        16) == 0) {

    return handle_admin_user_access(
        client_fd,
        &session,
        request
    );
}



/*MANAGER COMMANDS*/
if (strncmp(
        request,
        "ACTIVATE_ACCOUNT|",
        17) == 0) {

    return handle_manager_account_status(
        client_fd,
        &session,
        request,
        ACCOUNT_ACTIVE,
        "ACTIVATE_ACCOUNT|"
    );
}

if (strncmp(
        request,
        "DEACTIVATE_ACCOUNT|",
        19) == 0) {

    return handle_manager_account_status(
        client_fd,
        &session,
        request,
        ACCOUNT_INACTIVE,
        "DEACTIVATE_ACCOUNT|"
    );
}

if (strcmp(
        request,
        "VIEW_PENDING_LOANS") == 0) {

    return handle_view_pending_loans(
        client_fd,
        &session
    );
}

if (strncmp(
        request,
        "ASSIGN_LOAN|",
        12) == 0) {

    return handle_assign_loan(
        client_fd,
        &session,
        request
    );
}

if (strcmp(
        request,
        "REVIEW_FEEDBACK") == 0) {

    return handle_review_feedback(
        client_fd,
        &session
    );
}

if (strncmp(
        request,
        "MANAGER_CHANGE_PASSWORD|",
        24) == 0) {

    return handle_manager_change_password(
        client_fd,
        &session,
        request
    );
}


/* Employee commands */

    //open account

    if (strncmp(
        request,
        "OPEN_ACCOUNT|",
        13) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    UserId customer_user_id;
    Money initial_balance;

    if (parse_open_account_request(
            request,
            &customer_user_id,
            &initial_balance) == -1) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    /*
     * Verify that this is an existing,
     * active CUSTOMER user.
     */
    UserRecord customer;

    OperationStatus status =
        find_user_by_id(
            customer_user_id,
            &customer
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|USER_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    if (!customer.active) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|USER_INACTIVE"
        );

        return 0;
    }

    if (customer.role !=
        ROLE_CUSTOMER) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|NOT_CUSTOMER"
        );

        return 0;
    }

    AccountRecord account;

    status =
        open_customer_account(
            customer_user_id,
            initial_balance,
            session.user_id,
            &account
        );

    if (status ==
        STATUS_DUPLICATE) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|ACTIVE_ACCOUNT_EXISTS"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[512];

    int written =
        snprintf(
            response,
            sizeof(response),

            "OPEN_ACCOUNT_OK|%lld|%lld|%lld|%s",

            (long long)
                account.account_id,

            (long long)
                account.user_id,

            (long long)
                account.balance,

            account.created_at
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "OPEN_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }
	send_message(
       		 client_fd,
       		 response
   	 );

    return 0;
}

	//add customer
if (strncmp(
        request,
        "ADD_CUSTOMER|",
        13) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char username[
        MAX_USERNAME_LEN + 1
    ];

    char password[
        MAX_PASSWORD_LEN + 1
    ];

    char full_name[
        MAX_NAME_LEN + 1
    ];

    Money initial_balance;

    if (parse_add_customer_request(
            request,
            username,
            sizeof(username),
            password,
            sizeof(password),
            full_name,
            sizeof(full_name),
            &initial_balance) == -1) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    char password_hash[
        MAX_PASSWORD_HASH_LEN
    ];

    OperationStatus status =
        generate_password_hash(
            password,
            password_hash,
            sizeof(password_hash)
        );

    /*
     * Plaintext password is no longer needed.
     */
    memset(
        password,
        0,
        sizeof(password)
    );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|PASSWORD_ERROR"
        );

        return 0;
    }

    UserRecord customer;

    status =
        create_user_record(
            username,
            password_hash,
            ROLE_CUSTOMER,
            1,
            full_name,
            &customer
        );

    memset(
        password_hash,
        0,
        sizeof(password_hash)
    );

    if (status ==
        STATUS_DUPLICATE) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|USERNAME_EXISTS"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    AccountRecord account;

    status =
        open_customer_account(
            customer.user_id,
            initial_balance,
            session.user_id,
            &account
        );

    if (status != STATUS_OK) {

        /*
         * Customer login was created but account
         * creation failed. Remove the login again.
         */
        delete_user_by_id(
            customer.user_id
        );

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|ACCOUNT_CREATION_FAILED"
        );

        return 0;
    }

    char response[512];

    int written =
        snprintf(
            response,
            sizeof(response),

            "ADD_CUSTOMER_OK|%lld|%s|%lld|%lld",

            (long long)
                customer.user_id,

            customer.username,

            (long long)
                account.account_id,

            (long long)
                account.balance
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "ADD_CUSTOMER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

    //view account

    if (strncmp(
        request,
        "VIEW_ACCOUNT|",
        13) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    int lookup_type;
    long long lookup_value;

    if (parse_view_account_request(
            request,
            &lookup_type,
            &lookup_value) == -1) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    AccountRecord account;

    OperationStatus status;

    if (lookup_type == 1) {

        status =
            find_account_by_id(
                (AccountId)
                    lookup_value,
                &account
            );

    } else {

        status =
            find_account_by_user_any_status(
                (UserId)
                    lookup_value,
                &account
            );
    }

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    UserRecord customer;

    status =
        find_user_by_id(
            account.user_id,
            &customer
        );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[1024];

    int written =
        snprintf(
            response,
            sizeof(response),

            "VIEW_ACCOUNT_OK|%lld|%lld|%s|%lld|%s|%s",

            (long long)
                account.account_id,

            (long long)
                account.user_id,

            customer.full_name,

            (long long)
                account.balance,

            account_status_name(
                account.status
            ),

            account.created_at
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "VIEW_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

	//modify account details
if (strncmp(
        request,
        "MODIFY_CUSTOMER|",
        16) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    UserId customer_user_id;

    char new_name[
        MAX_NAME_LEN + 1
    ];

    if (parse_modify_customer_request(
            request,
            &customer_user_id,
            new_name,
            sizeof(new_name)) == -1) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    /*
     * First verify that the target really
     * is a CUSTOMER.
     */
    UserRecord customer;

    OperationStatus status =
        find_user_by_id(
            customer_user_id,
            &customer
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|USER_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    if (customer.role !=
        ROLE_CUSTOMER) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|NOT_CUSTOMER"
        );

        return 0;
    }

    status =
        update_user_full_name(
            customer_user_id,
            new_name
        );

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|INVALID_NAME"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "MODIFY_CUSTOMER_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_NAME_LEN + 128
    ];

    snprintf(
        response,
        sizeof(response),
        "MODIFY_CUSTOMER_OK|%lld|%s",
        (long long)
            customer_user_id,
        new_name
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

//close account
if (strncmp(
        request,
        "CLOSE_ACCOUNT|",
        14) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    AccountId account_id;

    if (parse_close_account_request(
            request,
            &account_id) == -1) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    AccountRecord account;

    OperationStatus status =
        close_customer_account(
            account_id,
            &account
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|NOT_FOUND"
        );

        return 0;
    }

    if (status ==
        STATUS_INSUFFICIENT_FUNDS) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|NONZERO_BALANCE"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|ALREADY_CLOSED"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CLOSE_ACCOUNT_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "CLOSE_ACCOUNT_OK|%lld|%lld",
        (long long)
            account.account_id,
        (long long)
            account.user_id
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

//view customer history

if (strncmp(
        request,
        "VIEW_CUSTOMER_TXNS|",
        19) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    int lookup_type;
    long long lookup_value;

    if (parse_customer_transactions_request(
            request,
            &lookup_type,
            &lookup_value) == -1) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    AccountRecord account;

    OperationStatus status;

    if (lookup_type == 1) {

        status =
            find_account_by_id(
                (AccountId)
                    lookup_value,
                &account
            );

    } else {

        status =
            find_account_by_user_any_status(
                (UserId)
                    lookup_value,
                &account
            );
    }

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|ACCOUNT_NOT_FOUND"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    UserRecord customer;

    status =
        find_user_by_id(
            account.user_id,
            &customer
        );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char history[
        MAX_RESPONSE_SIZE
    ];

    status =
        get_account_transaction_history(
            account.account_id,
            history,
            sizeof(history)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        char response[512];

        snprintf(
            response,
            sizeof(response),
            "CUSTOMER_TXNS_EMPTY|%lld|%lld|%s",
            (long long)
                account.account_id,
            (long long)
                account.user_id,
            customer.full_name
        );

        send_message(
            client_fd,
            response
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),

            "CUSTOMER_TXNS_OK|%lld|%lld|%s|%s",

            (long long)
                account.account_id,

            (long long)
                account.user_id,

            customer.full_name,

            history
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "CUSTOMER_TXNS_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

//view assigned loans
if (strcmp(
        request,
        "VIEW_ASSIGNED_LOANS") == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "ASSIGNED_LOANS_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char loans[
        MAX_RESPONSE_SIZE
    ];

    OperationStatus status =
        get_assigned_loans(
            session.user_id,
            loans,
            sizeof(loans)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "ASSIGNED_LOANS_EMPTY"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "ASSIGNED_LOANS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),
            "ASSIGNED_LOANS_OK|%s",
            loans
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "ASSIGNED_LOANS_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

//approve or reject loans
if (strncmp(
        request,
        "DECIDE_LOAN|",
        12) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    LoanId loan_id;
    int approve;

    if (parse_loan_decision_request(
            request,
            &loan_id,
            &approve) == -1) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    LoanRecord loan;

    OperationStatus status =
        decide_assigned_loan(
            loan_id,
            session.user_id,
            approve,
            &loan
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|NOT_FOUND"
        );

        return 0;
    }

    if (status ==
        STATUS_PERMISSION_DENIED) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|NOT_ASSIGNED"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    if (approve) {

    Money new_balance;

    status =
        disburse_loan_amount(
            loan.account_id,
            loan.amount,
            session.user_id,
            loan.loan_id,
            &new_balance
        );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "LOAN_DECISION_FAILED|DISBURSEMENT_FAILED"
        );

        return 0;
    }
}

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "LOAN_DECISION_OK|%lld|%s",
        (long long)
            loan.loan_id,
        approve
            ? "APPROVED"
            : "REJECTED"
    );

    send_message(
        client_fd,
        response
    );

    return 0;
}

//change password
if (strncmp(
        request,
        "EMP_CHANGE_PASSWORD|",
        20) == 0) {

    if (session.role !=
        ROLE_EMPLOYEE) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|PERMISSION_DENIED"
        );

        return 0;
    }

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    if (parse_employee_change_password_request(
            request,
            current_password,
            sizeof(current_password),
            new_password,
            sizeof(new_password)) == -1) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    OperationStatus status =
        change_user_password(
            session.user_id,
            current_password,
            new_password
        );

    /*
     * Plaintext passwords no longer needed.
     */
    memset(
        current_password,
        0,
        sizeof(current_password)
    );

    memset(
        new_password,
        0,
        sizeof(new_password)
    );

    if (status ==
        STATUS_PERMISSION_DENIED) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|SAME_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|INVALID_PASSWORD"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "EMP_PASSWORD_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        "EMP_PASSWORD_OK"
    );

    return 0;
}

/* Customer commands */

    if (strcmp(request,
               "VIEW_BALANCE") == 0) {

        AccountRecord account;

        OperationStatus status =
            find_account_by_user(
                session.user_id,
                &account
            );

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "BALANCE_FAILED|ACCOUNT_NOT_FOUND"
            );

            return 0;
        }

        char response[256];

        snprintf(
            response,
            sizeof(response),
            "BALANCE_OK|%lld|%lld",
            (long long)account.account_id,
            (long long)account.balance
        );

        send_message(client_fd,
                     response);

        return 0;
    }

    if (strncmp(request,
                "DEPOSIT|",
                8) == 0) {

        Money amount;

        if (parse_money_request(
                request,
                "DEPOSIT",
                &amount) == -1) {

            send_message(
                client_fd,
                "DEPOSIT_FAILED|INVALID_AMOUNT"
            );

            return 0;
        }

        AccountRecord account;
        TransactionId transaction_id;

        OperationStatus status =
            deposit_to_user_account(
                session.user_id,
                amount,
                &account,
                &transaction_id
            );

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "DEPOSIT_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        char response[256];

        snprintf(
            response,
            sizeof(response),
            "DEPOSIT_OK|%lld|%lld|%lld|%lld",
            (long long)transaction_id,
            (long long)account.account_id,
            (long long)amount,
            (long long)account.balance
        );

        send_message(client_fd,
                     response);

        return 0;
    }

    if (strncmp(request,
                "WITHDRAW|",
                9) == 0) {

        Money amount;

        if (parse_money_request(
                request,
                "WITHDRAW",
                &amount) == -1) {

            send_message(
                client_fd,
                "WITHDRAW_FAILED|INVALID_AMOUNT"
            );

            return 0;
        }

        AccountRecord account;
        TransactionId transaction_id;

        OperationStatus status =
            withdraw_from_user_account(
                session.user_id,
                amount,
                &account,
                &transaction_id
            );

        if (status ==
            STATUS_INSUFFICIENT_FUNDS) {

            send_message(
                client_fd,
                "WITHDRAW_FAILED|INSUFFICIENT_FUNDS"
            );

            return 0;
        }

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "WITHDRAW_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        char response[256];

        snprintf(
            response,
            sizeof(response),
            "WITHDRAW_OK|%lld|%lld|%lld|%lld",
            (long long)transaction_id,
            (long long)account.account_id,
            (long long)amount,
            (long long)account.balance
        );

        send_message(client_fd,
                     response);

        return 0;
    }

    if (strncmp(request,
                "TRANSFER|",
                9) == 0) {

        AccountId destination;
        Money amount;

        if (parse_transfer_request(
                request,
                &destination,
                &amount) == -1) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|INVALID_REQUEST"
            );

            return 0;
        }

        AccountRecord source;
        AccountRecord destination_account;

        TransactionId reference_id;

        OperationStatus status =
            transfer_from_user_account(
                session.user_id,
                destination,
                amount,
                &source,
                &destination_account,
                &reference_id
            );

        if (status ==
            STATUS_INSUFFICIENT_FUNDS) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|INSUFFICIENT_FUNDS"
            );

            return 0;
        }

        if (status ==
            STATUS_NOT_FOUND) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|DESTINATION_NOT_FOUND"
            );

            return 0;
        }

        if (status ==
            STATUS_CONFLICT) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|SAME_ACCOUNT"
            );

            return 0;
        }

        if (status ==
            STATUS_INACTIVE) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|ACCOUNT_INACTIVE"
            );

            return 0;
        }

        if (status != STATUS_OK) {

            send_message(
                client_fd,
                "TRANSFER_FAILED|SERVER_ERROR"
            );

            return 0;
        }

        char response[256];

        snprintf(
            response,
            sizeof(response),
            "TRANSFER_OK|%lld|%lld|%lld|%lld|%lld",
            (long long)reference_id,
            (long long)source.account_id,
            (long long)destination_account.account_id,
            (long long)amount,
            (long long)source.balance
        );

        send_message(client_fd,
                     response);

        return 0;
    }

    if (strcmp(
        request,
        "TRANSACTION_HISTORY") == 0) {

    AccountRecord account;

    OperationStatus status =
        find_account_by_user(
            session.user_id,
            &account
        );

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "HISTORY_FAILED|ACCOUNT_NOT_FOUND"
        );

        return 0;
    }

    char history[
        MAX_RESPONSE_SIZE
    ];

    status =
        get_transaction_history(
            account.account_id,
            history,
            sizeof(history)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "HISTORY_EMPTY"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "HISTORY_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),
            "HISTORY_OK|%s",
            history
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "HISTORY_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

if (strncmp(
        request,
        "APPLY_LOAN|",
        11) == 0) {

    Money amount;
    int tenure;

    if (parse_loan_request(
            request,
            &amount,
            &tenure) == -1) {

        send_message(
            client_fd,
            "LOAN_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    LoanRecord loan;

    OperationStatus status =
        apply_for_loan(
            session.user_id,
            amount,
            tenure,
            &loan
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "LOAN_FAILED|ACCOUNT_NOT_FOUND"
        );

        return 0;
    }

    if (status ==
        STATUS_INACTIVE) {

        send_message(
            client_fd,
            "LOAN_FAILED|ACCOUNT_INACTIVE"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "LOAN_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[512];

    int written =
        snprintf(
            response,
            sizeof(response),

            "LOAN_OK|%lld|%lld|%d|%d|%d",

            (long long)
                loan.loan_id,

            (long long)
                loan.amount,

            loan.tenure_months,

            loan.interest_basis_points,

            (int)
                loan.status
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "LOAN_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

if (strcmp(
        request,
        "VIEW_LOANS") == 0) {

    char loans[
        MAX_RESPONSE_SIZE
    ];

    OperationStatus status =
        get_customer_loans(
            session.user_id,
            loans,
            sizeof(loans)
        );

    if (status ==
        STATUS_NOT_FOUND) {

        send_message(
            client_fd,
            "LOANS_EMPTY"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "LOANS_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    int written =
        snprintf(
            response,
            sizeof(response),
            "LOANS_OK|%s",
            loans
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "LOANS_FAILED|TOO_LARGE"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

if (strncmp(
        request,
        "CHANGE_PASSWORD|",
        16) == 0) {

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    if (parse_change_password_request(
            request,
            current_password,
            sizeof(current_password),
            new_password,
            sizeof(new_password)) == -1) {

        send_message(
            client_fd,
            "PASSWORD_FAILED|INVALID_REQUEST"
        );

        return 0;
    }

    OperationStatus status =
        change_user_password(
            session.user_id,
            current_password,
            new_password
        );

    /*
     * Remove plaintext passwords from memory
     * as soon as they are no longer needed.
     */
    memset(
        current_password,
        0,
        sizeof(current_password)
    );

    memset(
        new_password,
        0,
        sizeof(new_password)
    );

    if (status ==
        STATUS_PERMISSION_DENIED) {

        send_message(
            client_fd,
            "PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_CONFLICT) {

        send_message(
            client_fd,
            "PASSWORD_FAILED|SAME_PASSWORD"
        );

        return 0;
    }

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "PASSWORD_FAILED|INVALID_NEW_PASSWORD"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "PASSWORD_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        "PASSWORD_OK"
    );

    return 0;
}

if (strncmp(
        request,
        "SUBMIT_FEEDBACK|",
        16) == 0) {

    const char *message =
        request + 16;

    if (message[0] == '\0' ||
        strlen(message) >
            MAX_FEEDBACK_LEN ||
        strchr(message, '|') != NULL ||
        strchr(message, '\n') != NULL ||
        strchr(message, '\r') != NULL) {

        send_message(
            client_fd,
            "FEEDBACK_FAILED|INVALID_MESSAGE"
        );

        return 0;
    }

    FeedbackRecord feedback;

    OperationStatus status =
        submit_feedback(
            session.user_id,
            message,
            &feedback
        );

    if (status ==
        STATUS_INVALID_INPUT) {

        send_message(
            client_fd,
            "FEEDBACK_FAILED|INVALID_MESSAGE"
        );

        return 0;
    }

    if (status != STATUS_OK) {

        send_message(
            client_fd,
            "FEEDBACK_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    char response[256];

    int written =
        snprintf(
            response,
            sizeof(response),
            "FEEDBACK_OK|%lld|%s",

            (long long)
                feedback.feedback_id,

            feedback.created_at
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(response)) {

        send_message(
            client_fd,
            "FEEDBACK_FAILED|SERVER_ERROR"
        );

        return 0;
    }

    send_message(
        client_fd,
        response
    );

    return 0;
}

    send_message(
        client_fd,
        "ERROR|COMMAND_NOT_IMPLEMENTED"
    );

    return 0;
}

static void *handle_client(void *arg)
{
    int client_fd =
        *(int *)arg;

    free(arg);
    
    printf("Client connected on fd %d\n", client_fd);

    char buffer[
        BUFFER_SIZE
    ];

    while (1) {

        int result =
            receive_message(
                client_fd,
                buffer,
                sizeof(buffer)
            );

        if (result == 0) {
   	 printf("Client fd %d disconnected.\n", client_fd);
   	 break;
	}

	if (result == -1) {
   		 fprintf(stderr,
           	 "Invalid/incomplete message from fd %d\n",
           	 client_fd);
   		 break;
	}

        if (process_request(
                client_fd,
                buffer)) {

            break;
        }
    }

    session_remove_by_socket(
        client_fd
    );

    printf("Closing connection for fd %d\n", client_fd);

    close(client_fd);

    return NULL;
}

int main(void)
{
    session_manager_init();

    int server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_fd == -1) {

        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    struct sockaddr_in server_addr;

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(SERVER_PORT);

    inet_pton(
        AF_INET,
        SERVER_IP,
        &server_addr.sin_addr
    );

    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1) {

        perror("bind");

        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(
            server_fd,
            MAX_CLIENTS) == -1) {

        perror("listen");

        close(server_fd);
        return EXIT_FAILURE;
    }

    printf(
        "Banking server listening on %s:%d\n",
        SERVER_IP,
        SERVER_PORT
    );

    while (1) {

        int client_fd =
            accept(
                server_fd,
                NULL,
                NULL
            );

        if (client_fd == -1)
            continue;

        int *argument =
            malloc(sizeof(*argument));

        if (argument == NULL) {

            close(client_fd);
            continue;
        }

        *argument =
            client_fd;

        pthread_t thread;

        if (pthread_create(
                &thread,
                NULL,
                handle_client,
                argument) != 0) {

            free(argument);
            close(client_fd);
            continue;
        }

        pthread_detach(thread);
    }
}

