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

    if (session.role !=
        ROLE_CUSTOMER) {

        send_message(
            client_fd,
            "ERROR|PERMISSION_DENIED"
        );

        return 0;
    }

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
