#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "protocol.h"
#include "client_ui.h"

#define BUFFER_SIZE \
    (PROTOCOL_MAX_MESSAGE + 1U)



static int read_line(
    const char *prompt,
    char *buffer,
    size_t size)
{
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(
            buffer,
            size,
            stdin) == NULL) {

        return -1;
    }

    size_t length =
        strlen(buffer);

    if (length > 0 &&
        buffer[length - 1] == '\n') {

        buffer[length - 1] =
            '\0';

    } else {

        int ch;

        while ((ch = getchar()) != '\n' &&
               ch != EOF) {
        }
    }

    return 0;
}

static int read_password(
    char *password,
    size_t size)
{
    struct termios old_settings;
    struct termios new_settings;

    if (tcgetattr(
            STDIN_FILENO,
            &old_settings) == -1) {

        perror("tcgetattr");

        return -1;
    }

    new_settings =
        old_settings;

    new_settings.c_lflag &=
        ~ECHO;

    if (tcsetattr(
            STDIN_FILENO,
            TCSAFLUSH,
            &new_settings) == -1) {

        perror("tcsetattr");

        return -1;
    }

    printf("Password: ");
    fflush(stdout);

    int result = 0;

    if (fgets(
            password,
            size,
            stdin) == NULL) {

        result = -1;
    }

    if (tcsetattr(
            STDIN_FILENO,
            TCSAFLUSH,
            &old_settings) == -1) {

        perror("tcsetattr");

        return -1;
    }

    printf("\n");

    if (result == -1)
        return -1;

    password[
        strcspn(password, "\n")
    ] = '\0';

    return 0;
}

static int parse_rupees_to_paise(
    const char *text,
    Money *result)
{
    if (text == NULL ||
        result == NULL ||
        text[0] == '\0') {

        return -1;
    }

    Money rupees = 0;
    Money paise = 0;

    size_t index = 0;

    int digit_seen = 0;

    while (text[index] != '\0' &&
           text[index] != '.') {

        if (text[index] < '0' ||
            text[index] > '9') {

            return -1;
        }

        int digit =
            text[index] - '0';

        if (rupees >
            (MAX_TRANSACTION_PAISE / 100 -
             digit) / 10) {

            return -1;
        }

        rupees =
            rupees * 10 + digit;

        digit_seen = 1;

        index++;
    }

    if (!digit_seen)
        return -1;

    if (text[index] == '.') {

        index++;

        if (text[index] == '\0')
            return -1;

        if (text[index] < '0' ||
            text[index] > '9') {

            return -1;
        }

        paise =
            (text[index] - '0') *
            10;

        index++;

        if (text[index] != '\0') {

            if (text[index] < '0' ||
                text[index] > '9') {

                return -1;
            }

            paise +=
                text[index] - '0';

            index++;

            if (text[index] != '\0')
                return -1;
        }
    }

    Money total =
        rupees * 100 + paise;

    if (total <= 0 ||
        total >
            MAX_TRANSACTION_PAISE) {

        return -1;
    }

    *result = total;

    return 0;
}

static int parse_login_success(
    const char *response,
    ClientUser *user)
{
    if (response == NULL ||
        user == NULL) {

        return -1;
    }

    if (strncmp(
            response,
            "LOGIN_OK|",
            9) != 0) {

        return -1;
    }

    char copy[BUFFER_SIZE];

    if (strlen(response) >=
        sizeof(copy)) {

        return -1;
    }

    strcpy(copy, response);

    char *saveptr = NULL;

    char *token =
        strtok_r(
            copy,
            "|",
            &saveptr
        );

    if (token == NULL ||
        strcmp(
            token,
            "LOGIN_OK") != 0) {

        return -1;
    }

    char *id_text =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    char *role_text =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    char *name =
        strtok_r(
            NULL,
            "|",
            &saveptr
        );

    if (id_text == NULL ||
        role_text == NULL ||
        name == NULL) {

        return -1;
    }

    if (strtok_r(
            NULL,
            "|",
            &saveptr) != NULL) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        user_id <= 0) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long role =
        strtol(
            role_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == role_text ||
        *end != '\0' ||
        role < ROLE_CUSTOMER ||
        role > ROLE_ADMIN) {

        return -1;
    }

    if (strlen(name) >
        MAX_NAME_LEN) {

        return -1;
    }

    user->logged_in = 1;

    user->user_id =
        (UserId)user_id;

    user->role =
        (UserRole)role;

    strcpy(
        user->full_name,
        name
    );

    return 0;
}

static void display_balance_response(
    const char *response)
{
    if (strncmp(
            response,
            "BALANCE_OK|",
            11) != 0) {

        printf("%s\n", response);

        return;
    }

    long long account_id;
    long long paise;

    if (sscanf(
            response,
            "BALANCE_OK|%lld|%lld",
            &account_id,
            &paise) != 2) {

        printf(
            "Invalid balance response.\n"
        );

        return;
    }

    printf("\n");
    printf("----------------------------------------\n");

    printf(
        "Account ID : %lld\n",
        account_id
    );

    printf(
        "Balance    : %s%lld.%02lld\n",
        CURRENCY_SYMBOL,
        paise / 100,
        paise % 100
    );

    printf("----------------------------------------\n");
}

static void display_deposit_response(
    const char *response)
{
    if (strncmp(
            response,
            "DEPOSIT_OK|",
            11) != 0) {

        printf("%s\n",
               response);

        return;
    }

    long long transaction_id;
    long long account_id;
    long long amount;
    long long balance;

    if (sscanf(
            response,
            "DEPOSIT_OK|%lld|%lld|%lld|%lld",
            &transaction_id,
            &account_id,
            &amount,
            &balance) != 4) {

        printf(
            "Invalid deposit response.\n"
        );

        return;
    }

    printf("\n");
    printf("----------------------------------------\n");
    printf("          DEPOSIT SUCCESSFUL\n");
    printf("----------------------------------------\n");

    printf(
        "Transaction ID : %lld\n",
        transaction_id
    );

    printf(
        "Account ID     : %lld\n",
        account_id
    );

    printf(
        "Amount         : %s%lld.%02lld\n",
        CURRENCY_SYMBOL,
        amount / 100,
        amount % 100
    );

    printf(
        "New Balance    : %s%lld.%02lld\n",
        CURRENCY_SYMBOL,
        balance / 100,
        balance % 100
    );

    printf("----------------------------------------\n");
}

static void display_withdraw_response(
    const char *response)
{
    if (strcmp(
            response,
            "WITHDRAW_FAILED|INSUFFICIENT_FUNDS") == 0) {

        printf(
            "\nWithdrawal failed: insufficient funds.\n"
        );

        return;
    }

    if (strncmp(
            response,
            "WITHDRAW_OK|",
            12) != 0) {

        printf("%s\n",
               response);

        return;
    }

    long long transaction_id;
    long long account_id;
    long long amount;
    long long balance;

    if (sscanf(
            response,
            "WITHDRAW_OK|%lld|%lld|%lld|%lld",
            &transaction_id,
            &account_id,
            &amount,
            &balance) != 4) {

        printf(
            "Invalid withdrawal response.\n"
        );

        return;
    }

    printf("\n");
    printf("----------------------------------------\n");
    printf("         WITHDRAWAL SUCCESSFUL\n");
    printf("----------------------------------------\n");

    printf(
        "Transaction ID : %lld\n",
        transaction_id
    );

    printf(
        "Account ID     : %lld\n",
        account_id
    );

    printf(
        "Amount         : %s%lld.%02lld\n",
        CURRENCY_SYMBOL,
        amount / 100,
        amount % 100
    );

    printf(
        "New Balance    : %s%lld.%02lld\n",
        CURRENCY_SYMBOL,
        balance / 100,
        balance % 100
    );

    printf("----------------------------------------\n");
}

static int send_and_receive(
    int sock_fd,
    const char *request,
    char *response,
    size_t response_size)
{
    if (send_message(
            sock_fd,
            request) == -1) {

        perror("send_message");

        return -1;
    }

    int result =
        receive_message(
            sock_fd,
            response,
            response_size
        );

    if (result == 0) {

        printf(
            "Server disconnected.\n"
        );

        return -1;
    }

    if (result == -1) {

        fprintf(
            stderr,
            "Invalid server response.\n"
        );

        return -1;
    }

    return 0;
}

static int perform_login(
    int sock_fd,
    ClientUser *user)
{
    char username[
        MAX_USERNAME_LEN + 1
    ];

    char password[
        MAX_PASSWORD_LEN + 1
    ];

    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if (read_line(
            "Username: ",
            username,
            sizeof(username)) == -1) {

        return -1;
    }

    if (read_password(
            password,
            sizeof(password)) == -1) {

        return -1;
    }

    int written =
        snprintf(
            request,
            sizeof(request),
            "LOGIN|%s|%s",
            username,
            password
        );

    memset(
        password,
        0,
        sizeof(password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "Login request is too large.\n"
        );

        return 0;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strncmp(
            response,
            "LOGIN_OK|",
            9) == 0) {

        if (parse_login_success(
                response,
                user) == -1) {

            printf(
                "Invalid login response.\n"
            );

            return 0;
        }

        printf(
            "\nLogin successful. Welcome, %s!\n",
            user->full_name
        );

    } else {

        printf("%s\n",
               response);
    }

    return 0;
}

static int customer_loop(
    int sock_fd,
    ClientUser *user)
{
    char choice[32];

    char response[
        BUFFER_SIZE
    ];

    while (user->logged_in) {

        display_customer_menu(
            user
        );

        if (read_line(
                "Enter choice: ",
                choice,
                sizeof(choice)) == -1) {

            return -1;
        }

        /*
         * ========== VIEW BALANCE ==========
         */
        if (strcmp(
                choice,
                "1") == 0) {

            if (send_and_receive(
                    sock_fd,
                    "VIEW_BALANCE",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            display_balance_response(
                response
            );

            continue;
        }

        /*
         * ============= DEPOSIT =============
         */
        if (strcmp(
                choice,
                "2") == 0) {

            char amount_text[64];

            if (read_line(
                    "Enter amount in rupees: ",
                    amount_text,
                    sizeof(amount_text)) == -1) {

                return -1;
            }

            Money amount;

            if (parse_rupees_to_paise(
                    amount_text,
                    &amount) == -1) {

                printf(
                    "\nInvalid amount. Enter a positive "
                    "amount with at most two decimal places.\n"
                );

                continue;
            }

            char request[128];

            snprintf(
                request,
                sizeof(request),
                "DEPOSIT|%lld",
                (long long)amount
            );

            if (send_and_receive(
                    sock_fd,
                    request,
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            display_deposit_response(
                response
            );

            continue;
        }

        /*
         * ============ WITHDRAWAL ============
         */
        if (strcmp(
                choice,
                "3") == 0) {

            char amount_text[64];

            if (read_line(
                    "Enter amount in rupees: ",
                    amount_text,
                    sizeof(amount_text)) == -1) {

                return -1;
            }

            Money amount;

            if (parse_rupees_to_paise(
                    amount_text,
                    &amount) == -1) {

                printf(
                    "\nInvalid amount. Enter a positive "
                    "amount with at most two decimal places.\n"
                );

                continue;
            }

            char request[128];

            snprintf(
                request,
                sizeof(request),
                "WITHDRAW|%lld",
                (long long)amount
            );

            if (send_and_receive(
                    sock_fd,
                    request,
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            display_withdraw_response(
                response
            );

            continue;
        }

        /*
	 *Transfer
	 */

	if (strcmp(choice, "4") == 0) {

   	 char account_text[64];
   	 char amount_text[64];

  	  if (read_line(
        	    "Enter destination account ID: ",
           	 account_text,
           	 sizeof(account_text)) == -1) {

    	    return -1;
  	  }

  	  errno = 0;

 	  char *end = NULL;

   	 long long destination =
       	 strtoll(
           	 account_text,
           	 &end,
           	 10
       	 );

   	 if (errno != 0 ||
       	 end == account_text ||
       	 *end != '\0' ||
       	 destination <= 0) {

        printf(
            "\nInvalid destination account ID.\n"
        );

        continue;
    }

    if (read_line(
            "Enter amount in rupees: ",
            amount_text,
            sizeof(amount_text)) == -1) {

        return -1;
    }

    Money amount;

    if (parse_rupees_to_paise(
            amount_text,
            &amount) == -1) {

        printf(
            "\nInvalid amount.\n"
        );

        continue;
    }

    char request[256];

    snprintf(
        request,
        sizeof(request),
        "TRANSFER|%lld|%lld",
        destination,
        (long long)amount
    );

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "TRANSFER_FAILED|INSUFFICIENT_FUNDS") == 0) {

        printf(
            "\nTransfer failed: insufficient funds.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "TRANSFER_FAILED|DESTINATION_NOT_FOUND") == 0) {

        printf(
            "\nTransfer failed: destination account does not exist.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "TRANSFER_FAILED|SAME_ACCOUNT") == 0) {

        printf(
            "\nTransfer failed: source and destination cannot be the same account.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "TRANSFER_OK|",
            12) == 0) {

        long long reference_id;
        long long source_id;
        long long destination_id;
        long long transferred;
        long long new_balance;

        if (sscanf(
                response,
                "TRANSFER_OK|%lld|%lld|%lld|%lld|%lld",
                &reference_id,
                &source_id,
                &destination_id,
                &transferred,
                &new_balance) == 5) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          TRANSFER SUCCESSFUL\n");
            printf("----------------------------------------\n");

            printf(
                "Reference ID        : %lld\n",
                reference_id
            );

            printf(
                "From Account        : %lld\n",
                source_id
            );

            printf(
                "To Account          : %lld\n",
                destination_id
            );

            printf(
                "Amount              : %s%lld.%02lld\n",
                CURRENCY_SYMBOL,
                transferred / 100,
                transferred % 100
            );

            printf(
                "Your New Balance    : %s%lld.%02lld\n",
                CURRENCY_SYMBOL,
                new_balance / 100,
                new_balance % 100
            );

            printf("----------------------------------------\n");

        } else {

            printf(
                "Invalid transfer response.\n"
            );
        }

        continue;
    }

    printf("%s\n",
           response);

    continue;
}

	/* Transaction history*/
if (strcmp(
        choice,
        "5") == 0) {

    if (send_and_receive(
            sock_fd,
            "TRANSACTION_HISTORY",
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "HISTORY_EMPTY") == 0) {

        printf(
            "\nNo transactions found.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "HISTORY_OK|",
            11) == 0) {

        printf("\n");
        printf("========================================\n");
        printf("          TRANSACTION HISTORY\n");
        printf("========================================\n");

        printf(
            "%s",
            response + 11
        );

        printf(
            "========================================\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

/*loan application*/

if (strcmp(
        choice,
        "6") == 0) {

    char amount_text[64];
    char tenure_text[32];

    if (read_line(
            "Enter loan amount in rupees: ",
            amount_text,
            sizeof(amount_text)) == -1) {

        return -1;
    }

    Money amount;

    if (parse_rupees_to_paise(
            amount_text,
            &amount) == -1) {

        printf(
            "\nInvalid loan amount.\n"
        );

        continue;
    }

    printf("\nAvailable tenure options:\n");
    printf("6 months\n");
    printf("12 months\n");
    printf("24 months\n");
    printf("36 months\n");
    printf("60 months\n");

    if (read_line(
            "Enter tenure in months: ",
            tenure_text,
            sizeof(tenure_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long tenure =
        strtol(
            tenure_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == tenure_text ||
        *end != '\0' ||
        (tenure != 6 &&
         tenure != 12 &&
         tenure != 24 &&
         tenure != 36 &&
         tenure != 60)) {

        printf(
            "\nInvalid tenure.\n"
        );

        continue;
    }

    char request[256];

    snprintf(
        request,
        sizeof(request),
        "APPLY_LOAN|%lld|%ld",
        (long long)amount,
        tenure
    );

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strncmp(
            response,
            "LOAN_OK|",
            8) == 0) {

        long long loan_id;
        long long loan_amount;
        int loan_tenure;
        int interest_bp;
        int status;

        if (sscanf(
                response,
                "LOAN_OK|%lld|%lld|%d|%d|%d",
                &loan_id,
                &loan_amount,
                &loan_tenure,
                &interest_bp,
                &status) == 5) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("        LOAN APPLICATION SUBMITTED\n");
            printf("----------------------------------------\n");

            printf(
                "Loan ID       : %lld\n",
                loan_id
            );

            printf(
                "Amount        : %s%lld.%02lld\n",
                CURRENCY_SYMBOL,
                loan_amount / 100,
                loan_amount % 100
            );

            printf(
                "Tenure        : %d months\n",
                loan_tenure
            );

            printf(
                "Interest Rate : %d.%02d%% per annum\n",
                interest_bp / 100,
                interest_bp % 100
            );

            printf(
                "Status        : PENDING\n"
            );

            printf("----------------------------------------\n");

        } else {

            printf(
                "Invalid loan response.\n"
            );
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "7") == 0) {

    if (send_and_receive(
            sock_fd,
            "VIEW_LOANS",
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "LOANS_EMPTY") == 0) {

        printf(
            "\nYou have no loan applications.\n"
        );

        continue;
    }

    /*view loans*/

    if (strncmp(
            response,
            "LOANS_OK|",
            9) == 0) {

        printf("\n");
        printf("========================================\n");
        printf("             MY LOANS\n");
        printf("========================================\n");

        printf(
            "%s",
            response + 9
        );

        printf(
            "========================================\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "8") == 0) {

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    printf("\n");

    if (read_password(
            current_password,
            sizeof(current_password)) == -1) {

        return -1;
    }

    printf(
        "Enter new "
    );

    if (read_password(
            new_password,
            sizeof(new_password)) == -1) {

        memset(
            current_password,
            0,
            sizeof(current_password)
        );

        return -1;
    }

    printf(
        "Confirm new "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

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

        return -1;
    }

    if (strcmp(
            new_password,
            confirm_password) != 0) {

        printf(
            "\nNew passwords do not match.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    size_t new_length =
        strlen(new_password);

    if (new_length < 8) {

        printf(
            "\nNew password must contain at least "
            "8 characters.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    char request[
        2 * MAX_PASSWORD_LEN + 64
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "CHANGE_PASSWORD|%s|%s",
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

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nPassword request is too large.\n"
        );

        continue;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    /*
     * Request contains plaintext credentials,
     * so clear it after transmission.
     */
    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "PASSWORD_OK") == 0) {

        printf(
            "\nPassword changed successfully.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD") == 0) {

        printf(
            "\nCurrent password is incorrect.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "PASSWORD_FAILED|SAME_PASSWORD") == 0) {

        printf(
            "\nNew password must be different from "
            "your current password.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "PASSWORD_FAILED|INVALID_NEW_PASSWORD") == 0) {

        printf(
            "\nInvalid new password.\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "9") == 0) {

    char feedback[
        MAX_FEEDBACK_LEN + 1
    ];

    if (read_line(
            "Enter feedback: ",
            feedback,
            sizeof(feedback)) == -1) {

        return -1;
    }

    if (feedback[0] == '\0') {

        printf(
            "\nFeedback cannot be empty.\n"
        );

        continue;
    }

    if (strchr(
            feedback,
            '|') != NULL) {

        printf(
            "\nFeedback cannot contain the '|' character.\n"
        );

        continue;
    }

    char request[
        MAX_FEEDBACK_LEN + 64
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "SUBMIT_FEEDBACK|%s",
            feedback
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nFeedback is too long.\n"
        );

        continue;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strncmp(
            response,
            "FEEDBACK_OK|",
            12) == 0) {

        long long feedback_id;

        char timestamp[32];

        if (sscanf(
                response,
                "FEEDBACK_OK|%lld|%31[^\n]",
                &feedback_id,
                timestamp) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("        FEEDBACK SUBMITTED\n");
            printf("----------------------------------------\n");

            printf(
                "Feedback ID : %lld\n",
                feedback_id
            );

            printf(
                "Submitted   : %s\n",
                timestamp
            );

            printf("----------------------------------------\n");

        } else {

            printf(
                "\nFeedback submitted successfully.\n"
            );
        }

        continue;
    }

    if (strcmp(
            response,
            "FEEDBACK_FAILED|INVALID_MESSAGE") == 0) {

        printf(
            "\nInvalid feedback message.\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

        /*
         * ============== LOGOUT ==============
         */
        if (strcmp(choice,"10") == 0) {

            if (send_and_receive(
                    sock_fd,
                    "LOGOUT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            printf("%s\n",
                   response);

            if (strcmp(
                    response,
                    "LOGOUT_OK") == 0) {

                memset(
                    user,
                    0,
                    sizeof(*user)
                );
            }

            continue;
        }

        /*
         * =============== EXIT ===============
         */
        if (strcmp(
                choice,
                "0") == 0) {

            if (send_and_receive(
                    sock_fd,
                    "EXIT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            printf("%s\n",
                   response);

            return 1;
        }

        printf(
            "\nInvalid choice.\n"
        );
    }

    return 0;
}

/* 	
 *  	EMPLOYEE MENU
 */

static int employee_loop(
    int sock_fd,
    ClientUser *user)
{
    char choice[32];

    char response[
        BUFFER_SIZE
    ];

    while (user->logged_in) {

        display_employee_menu(
            user
        );

        if (read_line(
                "Enter choice: ",
                choice,
                sizeof(choice)) == -1) {

            return -1;
        }

        /*
         * ===== ADD CUSTOMER ACCOUNT =====
         */
        if (strcmp(
        choice,
        "1") == 0) {

    char full_name[
        MAX_NAME_LEN + 1
    ];

    char username[
        MAX_USERNAME_LEN + 1
    ];

    char password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    char amount_text[64];

    if (read_line(
            "Enter customer full name: ",
            full_name,
            sizeof(full_name)) == -1) {

        return -1;
    }

    if (full_name[0] == '\0' ||
        strchr(full_name, '|') != NULL) {

        printf(
            "\nInvalid customer name.\n"
        );

        continue;
    }

    if (read_line(
            "Enter username: ",
            username,
            sizeof(username)) == -1) {

        return -1;
    }

    if (username[0] == '\0' ||
        strchr(username, '|') != NULL) {

        printf(
            "\nInvalid username.\n"
        );

        continue;
    }

    printf(
        "Enter temporary "
    );

    if (read_password(
            password,
            sizeof(password)) == -1) {

        return -1;
    }

    printf(
        "Confirm temporary "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

        memset(
            password,
            0,
            sizeof(password)
        );

        return -1;
    }

    if (strcmp(
            password,
            confirm_password) != 0) {

        printf(
            "\nPasswords do not match.\n"
        );

        memset(
            password,
            0,
            sizeof(password)
        );

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    if (strlen(password) < 8) {

        printf(
            "\nTemporary password must contain "
            "at least 8 characters.\n"
        );

        memset(
            password,
            0,
            sizeof(password)
        );

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    if (read_line(
            "Enter initial deposit in rupees "
            "(0 allowed): ",
            amount_text,
            sizeof(amount_text)) == -1) {

        memset(
            password,
            0,
            sizeof(password)
        );

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return -1;
    }

    Money initial_balance = 0;

    if (strcmp(
            amount_text,
            "0") != 0 &&
        strcmp(
            amount_text,
            "0.00") != 0) {

        if (parse_rupees_to_paise(
                amount_text,
                &initial_balance) == -1) {

            printf(
                "\nInvalid initial deposit.\n"
            );

            memset(
                password,
                0,
                sizeof(password)
            );

            memset(
                confirm_password,
                0,
                sizeof(confirm_password)
            );

            continue;
        }
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),

            "ADD_CUSTOMER|%s|%s|%s|%lld",

            username,
            password,
            full_name,
            (long long)
                initial_balance
        );

    memset(
        password,
        0,
        sizeof(password)
    );

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nRequest is too large.\n"
        );

        memset(
            request,
            0,
            sizeof(request)
        );

        continue;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    /*
     * request contained temporary plaintext
     * credentials.
     */
    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "ADD_CUSTOMER_FAILED|USERNAME_EXISTS") == 0) {

        printf(
            "\nThat username already exists.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "ADD_CUSTOMER_OK|",
            16) == 0) {

        long long user_id;
        long long account_id;
        long long balance;

        char created_username[
            MAX_USERNAME_LEN + 1
        ];

        if (sscanf(
                response,
                "ADD_CUSTOMER_OK|%lld|%32[^|]|%lld|%lld",
                &user_id,
                created_username,
                &account_id,
                &balance) == 4) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          CUSTOMER CREATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID         : %lld\n",
                user_id
            );

            printf(
                "Username        : %s\n",
                created_username
            );

            printf(
                "Account ID      : %lld\n",
                account_id
            );

            printf(
                "Initial Balance : %s%lld.%02lld\n",
                CURRENCY_SYMBOL,
                balance / 100,
                balance % 100
            );

            printf(
                "Status          : ACTIVE\n"
            );

            printf("----------------------------------------\n");
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

	if (strcmp(
        choice,
        "2") == 0) {

    char lookup_choice[16];

    printf("\n");
    printf("1. Search by Account ID\n");
    printf("2. Search by Customer User ID\n");

    if (read_line(
            "Enter choice: ",
            lookup_choice,
            sizeof(lookup_choice)) == -1) {

        return -1;
    }

    char id_text[64];

    if (strcmp(
            lookup_choice,
            "1") == 0) {

        if (read_line(
                "Enter account ID: ",
                id_text,
                sizeof(id_text)) == -1) {

            return -1;
        }

    } else if (strcmp(
                   lookup_choice,
                   "2") == 0) {

        if (read_line(
                "Enter customer user ID: ",
                id_text,
                sizeof(id_text)) == -1) {

            return -1;
        }

    } else {

        printf(
            "\nInvalid search option.\n"
        );

        continue;
    }

    errno = 0;

    char *end = NULL;

    long long id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        id <= 0) {

        printf(
            "\nInvalid ID.\n"
        );

        continue;
    }

    char request[128];

    if (strcmp(
            lookup_choice,
            "1") == 0) {

        snprintf(
            request,
            sizeof(request),
            "VIEW_ACCOUNT|ACCOUNT|%lld",
            id
        );

    } else {

        snprintf(
            request,
            sizeof(request),
            "VIEW_ACCOUNT|USER|%lld",
            id
        );
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "VIEW_ACCOUNT_FAILED|NOT_FOUND") == 0) {

        printf(
            "\nAccount not found.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "VIEW_ACCOUNT_OK|",
            16) == 0) {

        long long account_id;
        long long user_id;
        long long balance;

        char full_name[
            MAX_NAME_LEN + 1
        ];

        char status[32];
        char created_at[32];

        if (sscanf(
                response,
                "VIEW_ACCOUNT_OK|%lld|%lld|%100[^|]|%lld|%31[^|]|%31[^\n]",
                &account_id,
                &user_id,
                full_name,
                &balance,
                status,
                created_at) == 6) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          CUSTOMER ACCOUNT\n");
            printf("----------------------------------------\n");

            printf(
                "Account ID : %lld\n",
                account_id
            );

            printf(
                "User ID    : %lld\n",
                user_id
            );

            printf(
                "Name       : %s\n",
                full_name
            );

            printf(
                "Balance    : %s%lld.%02lld\n",
                CURRENCY_SYMBOL,
                balance / 100,
                balance % 100
            );

            printf(
                "Status     : %s\n",
                status
            );

            printf(
                "Created    : %s\n",
                created_at
            );

            printf("----------------------------------------\n");

        } else {

            printf(
                "\nInvalid account response.\n"
            );
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "3") == 0) {

    char user_text[64];

    if (read_line(
            "Enter customer user ID: ",
            user_text,
            sizeof(user_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            user_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == user_text ||
        *end != '\0' ||
        user_id <= 0) {

        printf(
            "\nInvalid customer user ID.\n"
        );

        continue;
    }

    char new_name[
        MAX_NAME_LEN + 1
    ];

    if (read_line(
            "Enter new customer name: ",
            new_name,
            sizeof(new_name)) == -1) {

        return -1;
    }

    if (new_name[0] == '\0') {

        printf(
            "\nName cannot be empty.\n"
        );

        continue;
    }

    if (strchr(
            new_name,
            '|') != NULL) {

        printf(
            "\nName cannot contain '|'.\n"
        );

        continue;
    }

    char request[
        MAX_NAME_LEN + 128
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "MODIFY_CUSTOMER|%lld|%s",
            user_id,
            new_name
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nRequest is too large.\n"
        );

        continue;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "MODIFY_CUSTOMER_FAILED|USER_NOT_FOUND") == 0) {

        printf(
            "\nCustomer user not found.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "MODIFY_CUSTOMER_FAILED|NOT_CUSTOMER") == 0) {

        printf(
            "\nThe specified user is not a customer.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "MODIFY_CUSTOMER_OK|",
            19) == 0) {

        long long updated_id;

        char updated_name[
            MAX_NAME_LEN + 1
        ];

        if (sscanf(
                response,
                "MODIFY_CUSTOMER_OK|%lld|%100[^\n]",
                &updated_id,
                updated_name) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("       CUSTOMER DETAILS UPDATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID  : %lld\n",
                updated_id
            );

            printf(
                "New Name : %s\n",
                updated_name
            );

            printf("----------------------------------------\n");
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "4") == 0) {

    char account_text[64];

    if (read_line(
            "Enter account ID to close: ",
            account_text,
            sizeof(account_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long account_id =
        strtoll(
            account_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == account_text ||
        *end != '\0' ||
        account_id <= 0) {

        printf(
            "\nInvalid account ID.\n"
        );

        continue;
    }

    printf(
        "\nWARNING: Account closure is permanent.\n"
    );

    char confirmation[16];

    if (read_line(
            "Type YES to confirm: ",
            confirmation,
            sizeof(confirmation)) == -1) {

        return -1;
    }

    if (strcmp(
            confirmation,
            "YES") != 0) {

        printf(
            "\nAccount closure cancelled.\n"
        );

        continue;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "CLOSE_ACCOUNT|%lld",
        account_id
    );

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "CLOSE_ACCOUNT_FAILED|NOT_FOUND") == 0) {

        printf(
            "\nAccount not found.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "CLOSE_ACCOUNT_FAILED|NONZERO_BALANCE") == 0) {

        printf(
            "\nAccount cannot be closed because "
            "the balance is not zero.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "CLOSE_ACCOUNT_FAILED|ALREADY_CLOSED") == 0) {

        printf(
            "\nAccount is already closed.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "CLOSE_ACCOUNT_OK|",
            17) == 0) {

        long long closed_account_id;
        long long customer_user_id;

        if (sscanf(
                response,
                "CLOSE_ACCOUNT_OK|%lld|%lld",
                &closed_account_id,
                &customer_user_id) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          ACCOUNT CLOSED\n");
            printf("----------------------------------------\n");

            printf(
                "Account ID : %lld\n",
                closed_account_id
            );

            printf(
                "Customer ID: %lld\n",
                customer_user_id
            );

            printf(
                "Status     : CLOSED\n"
            );

            printf("----------------------------------------\n");
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "5") == 0) {

    char lookup_choice[16];

    printf("\n");
    printf("1. Search by Account ID\n");
    printf("2. Search by Customer User ID\n");

    if (read_line(
            "Enter choice: ",
            lookup_choice,
            sizeof(lookup_choice)) == -1) {

        return -1;
    }

    char id_text[64];

    if (strcmp(
            lookup_choice,
            "1") == 0) {

        if (read_line(
                "Enter account ID: ",
                id_text,
                sizeof(id_text)) == -1) {

            return -1;
        }

    } else if (strcmp(
                   lookup_choice,
                   "2") == 0) {

        if (read_line(
                "Enter customer user ID: ",
                id_text,
                sizeof(id_text)) == -1) {

            return -1;
        }

    } else {

        printf(
            "\nInvalid search option.\n"
        );

        continue;
    }

    errno = 0;

    char *end = NULL;

    long long id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        id <= 0) {

        printf(
            "\nInvalid ID.\n"
        );

        continue;
    }

    char request[128];

    if (strcmp(
            lookup_choice,
            "1") == 0) {

        snprintf(
            request,
            sizeof(request),
            "VIEW_CUSTOMER_TXNS|ACCOUNT|%lld",
            id
        );

    } else {

        snprintf(
            request,
            sizeof(request),
            "VIEW_CUSTOMER_TXNS|USER|%lld",
            id
        );
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "CUSTOMER_TXNS_FAILED|ACCOUNT_NOT_FOUND") == 0) {

        printf(
            "\nCustomer account not found.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "CUSTOMER_TXNS_EMPTY|",
            20) == 0) {

        long long account_id;
        long long user_id;

        char customer_name[
            MAX_NAME_LEN + 1
        ];

        if (sscanf(
                response,
                "CUSTOMER_TXNS_EMPTY|%lld|%lld|%100[^\n]",
                &account_id,
                &user_id,
                customer_name) == 3) {

            printf("\n");
            printf("========================================\n");
            printf("          CUSTOMER PASSBOOK\n");
            printf("========================================\n");

            printf(
                "Account ID : %lld\n",
                account_id
            );

            printf(
                "User ID    : %lld\n",
                user_id
            );

            printf(
                "Customer   : %s\n",
                customer_name
            );

            printf("----------------------------------------\n");

            printf(
                "No transactions found.\n"
            );

            printf(
                "========================================\n"
            );
        }

        continue;
    }

    if (strncmp(
            response,
            "CUSTOMER_TXNS_OK|",
            17) == 0) {

        char copy[
            BUFFER_SIZE
        ];

        if (strlen(response) >=
            sizeof(copy)) {

            printf(
                "\nResponse is too large.\n"
            );

            continue;
        }

        strcpy(
            copy,
            response + 17
        );

        char *saveptr = NULL;

        char *account_field =
            strtok_r(
                copy,
                "|",
                &saveptr
            );

        char *user_field =
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

        char *history =
            saveptr;

        if (account_field == NULL ||
            user_field == NULL ||
            name_field == NULL ||
            history == NULL ||
            history[0] == '\0') {

            printf(
                "\nInvalid transaction response.\n"
            );

            continue;
        }

        printf("\n");
        printf("========================================\n");
        printf("          CUSTOMER PASSBOOK\n");
        printf("========================================\n");

        printf(
            "Account ID : %s\n",
            account_field
        );

        printf(
            "User ID    : %s\n",
            user_field
        );

        printf(
            "Customer   : %s\n",
            name_field
        );

        printf("----------------------------------------\n");

        printf(
            "%s",
            history
        );

        printf(
            "========================================\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "6") == 0) {

    if (send_and_receive(
            sock_fd,
            "VIEW_ASSIGNED_LOANS",
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "ASSIGNED_LOANS_EMPTY") == 0) {

        printf(
            "\nNo loan applications are currently "
            "assigned to you.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "ASSIGNED_LOANS_OK|",
            18) == 0) {

        printf("\n");
        printf("========================================\n");
        printf("       ASSIGNED LOAN APPLICATIONS\n");
        printf("========================================\n");

        printf(
            "%s",
            response + 18
        );

        printf(
            "========================================\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "7") == 0) {

    char loan_text[64];

    if (read_line(
            "Enter loan ID: ",
            loan_text,
            sizeof(loan_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long loan_id =
        strtoll(
            loan_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == loan_text ||
        *end != '\0' ||
        loan_id <= 0) {

        printf(
            "\nInvalid loan ID.\n"
        );

        continue;
    }

    printf("\n");
    printf("1. Approve Loan\n");
    printf("2. Reject Loan\n");

    char decision[16];

    if (read_line(
            "Enter choice: ",
            decision,
            sizeof(decision)) == -1) {

        return -1;
    }

    const char *action;

    if (strcmp(
            decision,
            "1") == 0) {

        action =
            "APPROVE";

    } else if (strcmp(
                   decision,
                   "2") == 0) {

        action =
            "REJECT";

    } else {

        printf(
            "\nInvalid choice.\n"
        );

        continue;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "DECIDE_LOAN|%lld|%s",
        loan_id,
        action
    );

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "LOAN_DECISION_FAILED|NOT_FOUND") == 0) {

        printf(
            "\nLoan application not found.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "LOAN_DECISION_FAILED|NOT_ASSIGNED") == 0) {

        printf(
            "\nThis loan is not assigned to you "
            "or has already been processed.\n"
        );

        continue;
    }

    if (strncmp(
            response,
            "LOAN_DECISION_OK|",
            17) == 0) {

        long long processed_loan_id;

        char status[32];

        if (sscanf(
                response,
                "LOAN_DECISION_OK|%lld|%31s",
                &processed_loan_id,
                status) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          LOAN PROCESSED\n");
            printf("----------------------------------------\n");

            printf(
                "Loan ID : %lld\n",
                processed_loan_id
            );

            printf(
                "Decision: %s\n",
                status
            );

            printf("----------------------------------------\n");
        }

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}

if (strcmp(
        choice,
        "8") == 0) {

    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    printf(
        "Enter current "
    );

    if (read_password(
            current_password,
            sizeof(current_password)) == -1) {

        return -1;
    }

    printf(
        "Enter new "
    );

    if (read_password(
            new_password,
            sizeof(new_password)) == -1) {

        memset(
            current_password,
            0,
            sizeof(current_password)
        );

        return -1;
    }

    printf(
        "Confirm new "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

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

        return -1;
    }

    if (strcmp(
            new_password,
            confirm_password) != 0) {

        printf(
            "\nNew passwords do not match.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    if (strlen(
            new_password) < 8) {

        printf(
            "\nNew password must contain "
            "at least 8 characters.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        continue;
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),

            "EMP_CHANGE_PASSWORD|%s|%s",

            current_password,
            new_password
        );

    /*
     * Clear local plaintext copies.
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

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nUnable to create password request.\n"
        );

        memset(
            request,
            0,
            sizeof(request)
        );

        continue;
    }

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    /*
     * Request itself contained plaintext
     * password data.
     */
    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "EMP_PASSWORD_OK") == 0) {

        printf(
            "\nPassword changed successfully.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "EMP_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD") == 0) {

        printf(
            "\nCurrent password is incorrect.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "EMP_PASSWORD_FAILED|SAME_PASSWORD") == 0) {

        printf(
            "\nNew password cannot be the same "
            "as the current password.\n"
        );

        continue;
    }

    if (strcmp(
            response,
            "EMP_PASSWORD_FAILED|INVALID_PASSWORD") == 0) {

        printf(
            "\nInvalid new password.\n"
        );

        continue;
    }

    printf(
        "%s\n",
        response
    );

    continue;
}


        /*
         * ============== LOGOUT ==============
         */
        if (strcmp(
                choice,
                "9") == 0) {

            if (send_and_receive(
                    sock_fd,
                    "LOGOUT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            printf(
                "%s\n",
                response
            );

            if (strcmp(
                    response,
                    "LOGOUT_OK") == 0) {

                memset(
                    user,
                    0,
                    sizeof(*user)
                );
            }

            continue;
        }

        /*
         * =============== EXIT ===============
         */
        if (strcmp(
                choice,
                "0") == 0) {

            if (send_and_receive(
                    sock_fd,
                    "EXIT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            printf(
                "%s\n",
                response
            );

            return 1;
        }

        printf(
            "\nInvalid choice.\n"
        );
    }

    return 0;
}

static int manager_change_account_status(
    int sock_fd,
    int activate)
{
    char account_text[64];

    if (read_line(
            "Enter customer account ID: ",
            account_text,
            sizeof(account_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long account_id =
        strtoll(
            account_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == account_text ||
        *end != '\0' ||
        account_id <= 0) {

        printf(
            "\nInvalid account ID.\n"
        );

        return 0;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "%s%lld",
        activate
            ? "ACTIVATE_ACCOUNT|"
            : "DEACTIVATE_ACCOUNT|",
        account_id
    );

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "MANAGER_ACCOUNT_FAILED|NOT_FOUND") == 0) {

        printf(
            "\nCustomer account not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "MANAGER_ACCOUNT_FAILED|ACCOUNT_CLOSED") == 0) {

        printf(
            "\nThis account is permanently closed "
            "and cannot be activated or deactivated.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "MANAGER_ACCOUNT_OK|",
            19) == 0) {

        long long returned_account_id;
        long long user_id;
        char status[32];

        if (sscanf(
                response,
                "MANAGER_ACCOUNT_OK|%lld|%lld|%31s",
                &returned_account_id,
                &user_id,
                status) == 3) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          ACCOUNT UPDATED\n");
            printf("----------------------------------------\n");

            printf(
                "Account ID : %lld\n",
                returned_account_id
            );

            printf(
                "Customer ID: %lld\n",
                user_id
            );

            printf(
                "Status     : %s\n",
                status
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int manager_view_pending_loans(
    int sock_fd)
{
    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            "VIEW_PENDING_LOANS",
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "PENDING_LOANS_EMPTY") == 0) {

        printf(
            "\nNo pending loan applications.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "PENDING_LOANS_OK|",
            17) == 0) {

        printf("\n");
        printf("========================================\n");
        printf("       PENDING LOAN APPLICATIONS\n");
        printf("========================================\n");

        printf(
            "%s",
            response + 17
        );

        printf(
            "========================================\n"
        );

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int manager_assign_loan(
    int sock_fd)
{
    char loan_text[64];
    char employee_text[64];

    if (read_line(
            "Enter loan ID: ",
            loan_text,
            sizeof(loan_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long loan_id =
        strtoll(
            loan_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == loan_text ||
        *end != '\0' ||
        loan_id <= 0) {

        printf(
            "\nInvalid loan ID.\n"
        );

        return 0;
    }

    if (read_line(
            "Enter employee user ID: ",
            employee_text,
            sizeof(employee_text)) == -1) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long long employee_id =
        strtoll(
            employee_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == employee_text ||
        *end != '\0' ||
        employee_id <= 0) {

        printf(
            "\nInvalid employee user ID.\n"
        );

        return 0;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "ASSIGN_LOAN|%lld|%lld",
        loan_id,
        employee_id
    );

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "ASSIGN_LOAN_FAILED|LOAN_NOT_FOUND") == 0) {

        printf(
            "\nLoan application not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ASSIGN_LOAN_FAILED|EMPLOYEE_NOT_FOUND") == 0) {

        printf(
            "\nEmployee not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ASSIGN_LOAN_FAILED|NOT_EMPLOYEE") == 0) {

        printf(
            "\nThe specified user is not an employee.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ASSIGN_LOAN_FAILED|EMPLOYEE_INACTIVE") == 0) {

        printf(
            "\nThe employee account is inactive.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ASSIGN_LOAN_FAILED|NOT_PENDING") == 0) {

        printf(
            "\nThis loan is no longer pending "
            "or has already been assigned.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "ASSIGN_LOAN_OK|",
            15) == 0) {

        long long assigned_loan_id;
        long long assigned_employee_id;

        if (sscanf(
                response,
                "ASSIGN_LOAN_OK|%lld|%lld",
                &assigned_loan_id,
                &assigned_employee_id) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("            LOAN ASSIGNED\n");
            printf("----------------------------------------\n");

            printf(
                "Loan ID     : %lld\n",
                assigned_loan_id
            );

            printf(
                "Employee ID : %lld\n",
                assigned_employee_id
            );

            printf(
                "Status      : ASSIGNED\n"
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int manager_review_feedback(
    int sock_fd)
{
    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            "REVIEW_FEEDBACK",
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "FEEDBACK_REVIEW_EMPTY") == 0) {

        printf(
            "\nNo customer feedback available.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "FEEDBACK_REVIEW_OK|",
            19) == 0) {

        printf("\n");
        printf("========================================\n");
        printf("          CUSTOMER FEEDBACK\n");
        printf("========================================\n");

        printf(
            "%s",
            response + 19
        );

        printf(
            "========================================\n"
        );

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int manager_change_password(
    int sock_fd)
{
    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    printf(
        "Enter current "
    );

    if (read_password(
            current_password,
            sizeof(current_password)) == -1) {

        return -1;
    }

    printf(
        "Enter new "
    );

    if (read_password(
            new_password,
            sizeof(new_password)) == -1) {

        memset(
            current_password,
            0,
            sizeof(current_password)
        );

        return -1;
    }

    printf(
        "Confirm new "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

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

        return -1;
    }

    if (strcmp(
            new_password,
            confirm_password) != 0) {

        printf(
            "\nNew passwords do not match.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    if (strlen(
            new_password) < 8) {

        printf(
            "\nNew password must contain "
            "at least 8 characters.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),

            "MANAGER_CHANGE_PASSWORD|%s|%s",

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

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nUnable to create password request.\n"
        );

        memset(
            request,
            0,
            sizeof(request)
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "MANAGER_PASSWORD_OK") == 0) {

        printf(
            "\nPassword changed successfully.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "MANAGER_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD") == 0) {

        printf(
            "\nCurrent password is incorrect.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "MANAGER_PASSWORD_FAILED|SAME_PASSWORD") == 0) {

        printf(
            "\nNew password cannot be the same "
            "as the current password.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "MANAGER_PASSWORD_FAILED|INVALID_PASSWORD") == 0) {

        printf(
            "\nInvalid new password.\n"
        );

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}



/*========MANAGER LOOP========*/
static int manager_loop(
    int sock_fd,
    ClientUser *user)
{
    char choice[32];

    while (user->logged_in) {

        display_manager_menu(
            user
        );

        if (read_line(
                "Enter choice: ",
                choice,
                sizeof(choice)) == -1) {

            return -1;
        }

        if (strcmp(
                choice,
                "1") == 0) {

            if (manager_change_account_status(
                    sock_fd,
                    1) == -1) {

                return -1;
            }

            continue;
        }

        if (strcmp(
                choice,
                "2") == 0) {

            if (manager_change_account_status(
                    sock_fd,
                    0) == -1) {

                return -1;
            }

            continue;
        }

	if (strcmp(
        choice,
        "3") == 0) {

   	 if (manager_view_pending_loans(
            sock_fd) == -1) {

        return -1;
   	 }

   	 continue;
	}

	if (strcmp(
        choice,
        "4") == 0) {

   	 if (manager_assign_loan(
            sock_fd) == -1) {

        	return -1;
   	 }

   	 continue;
	}

	if (strcmp(
        choice,
        "5") == 0) {

   	 if (manager_review_feedback(
            sock_fd) == -1) {

        return -1;
   	 }

   	 continue;
	}

	if (strcmp(
        choice,
        "6") == 0) {

   	 if (manager_change_password(
            sock_fd) == -1) {

       	 return -1;
  	 }

   	 continue;
	}

        if (strcmp(
                choice,
                "7") == 0) {

            char response[
                MAX_RESPONSE_SIZE
            ];

            if (send_and_receive(
                    sock_fd,
                    "LOGOUT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            if (strcmp(
                    response,
                    "LOGOUT_OK") == 0) {

                user->logged_in = 0;

                printf(
                    "\nLogged out successfully.\n"
                );

                return 0;
            }

            printf(
                "%s\n",
                response
            );

            continue;
        }

        if (strcmp(
                choice,
                "0") == 0) {

            send_message(
                sock_fd,
                "EXIT"
            );

            return 1;
        }

        printf(
            "\nInvalid choice.\n"
        );
    }

    return 0;
}


//admin functions
static int admin_add_employee(
    int sock_fd)
{
    char full_name[
        MAX_NAME_LEN + 1
    ];

    char username[
        MAX_USERNAME_LEN + 1
    ];

    char password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    if (read_line(
            "Enter employee full name: ",
            full_name,
            sizeof(full_name)) == -1) {

        return -1;
    }

    if (full_name[0] == '\0' ||
        strchr(
            full_name,
            '|') != NULL) {

        printf(
            "\nInvalid employee name.\n"
        );

        return 0;
    }

    if (read_line(
            "Enter username: ",
            username,
            sizeof(username)) == -1) {

        return -1;
    }

    if (username[0] == '\0' ||
        strchr(
            username,
            '|') != NULL) {

        printf(
            "\nInvalid username.\n"
        );

        return 0;
    }

    printf(
        "Enter temporary "
    );

    if (read_password(
            password,
            sizeof(password)) == -1) {

        return -1;
    }

    printf(
        "Confirm temporary "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

        memset(
            password,
            0,
            sizeof(password)
        );

        return -1;
    }

    if (strcmp(
            password,
            confirm_password) != 0) {

        printf(
            "\nPasswords do not match.\n"
        );

        memset(
            password,
            0,
            sizeof(password)
        );

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    if (strlen(
            password) < 8) {

        printf(
            "\nTemporary password must contain "
            "at least 8 characters.\n"
        );

        memset(
            password,
            0,
            sizeof(password)
        );

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "ADD_EMPLOYEE|%s|%s|%s",
            username,
            password,
            full_name
        );

    memset(
        password,
        0,
        sizeof(password)
    );

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nRequest is too large.\n"
        );

        memset(
            request,
            0,
            sizeof(request)
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "ADD_EMPLOYEE_FAILED|USERNAME_EXISTS") == 0) {

        printf(
            "\nThat username already exists.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "ADD_EMPLOYEE_OK|",
            16) == 0) {

        long long user_id;

        char created_username[
            MAX_USERNAME_LEN + 1
        ];

        char created_name[
            MAX_NAME_LEN + 1
        ];

        if (sscanf(
                response,
                "ADD_EMPLOYEE_OK|%lld|%32[^|]|%100[^\n]",
                &user_id,
                created_username,
                created_name) == 3) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          EMPLOYEE CREATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID  : %lld\n",
                user_id
            );

            printf(
                "Username : %s\n",
                created_username
            );

            printf(
                "Name     : %s\n",
                created_name
            );

            printf(
                "Role     : EMPLOYEE\n"
            );

            printf(
                "Status   : ACTIVE\n"
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int admin_view_user(
    int sock_fd)
{
    char id_text[64];

    if (read_line(
            "Enter user ID: ",
            id_text,
            sizeof(id_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        user_id <= 0) {

        printf(
            "\nInvalid user ID.\n"
        );

        return 0;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "VIEW_USER|%lld",
        user_id
    );

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "VIEW_USER_FAILED|NOT_FOUND") == 0) {

        printf(
            "\nUser not found.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "VIEW_USER_OK|",
            13) == 0) {

        long long returned_id;
        int role;
        int active;

        char username[
            MAX_USERNAME_LEN + 1
        ];

        char full_name[
            MAX_NAME_LEN + 1
        ];

        if (sscanf(
                response,
                "VIEW_USER_OK|%lld|%32[^|]|%100[^|]|%d|%d",
                &returned_id,
                username,
                full_name,
                &role,
                &active) == 5) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("              USER DETAILS\n");
            printf("----------------------------------------\n");

            printf(
                "User ID  : %lld\n",
                returned_id
            );

            printf(
                "Username : %s\n",
                username
            );

            printf(
                "Name     : %s\n",
                full_name
            );

            printf(
                "Role     : %s\n",
                client_role_name(
                    (UserRole)role
                )
            );

            printf(
                "Status   : %s\n",
                active
                    ? "ACTIVE"
                    : "INACTIVE"
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int admin_modify_user(
    int sock_fd)
{
    char id_text[64];

    if (read_line(
            "Enter customer/employee user ID: ",
            id_text,
            sizeof(id_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        user_id <= 0) {

        printf(
            "\nInvalid user ID.\n"
        );

        return 0;
    }

    char new_name[
        MAX_NAME_LEN + 1
    ];

    if (read_line(
            "Enter new full name: ",
            new_name,
            sizeof(new_name)) == -1) {

        return -1;
    }

    if (new_name[0] == '\0' ||
        strchr(new_name, '|') != NULL) {

        printf(
            "\nInvalid name.\n"
        );

        return 0;
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "ADMIN_MODIFY_USER|%lld|%s",
            user_id,
            new_name
        );

    if (written < 0 ||
        (size_t)written >= sizeof(request)) {

        printf(
            "\nRequest is too large.\n"
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "ADMIN_MODIFY_USER_FAILED|USER_NOT_FOUND") == 0) {

        printf(
            "\nUser not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ADMIN_MODIFY_USER_FAILED|INVALID_ROLE") == 0) {

        printf(
            "\nOnly customer or employee details "
            "can be modified here.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ADMIN_MODIFY_USER_FAILED|INVALID_NAME") == 0) {

        printf(
            "\nInvalid name.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "ADMIN_MODIFY_USER_OK|",
            21) == 0) {

        long long returned_id;

        char returned_name[
            MAX_NAME_LEN + 1
        ];

        if (sscanf(
                response,
                "ADMIN_MODIFY_USER_OK|%lld|%100[^\n]",
                &returned_id,
                returned_name) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          USER DETAILS UPDATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID  : %lld\n",
                returned_id
            );

            printf(
                "New Name : %s\n",
                returned_name
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int admin_manage_user_role(
    int sock_fd)
{
    char id_text[64];

    if (read_line(
            "Enter user ID: ",
            id_text,
            sizeof(id_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        user_id <= 0) {

        printf(
            "\nInvalid user ID.\n"
        );

        return 0;
    }

    printf("\n");
    printf("1. CUSTOMER\n");
    printf("2. EMPLOYEE\n");

    char role_choice[16];

    if (read_line(
            "Select new role: ",
            role_choice,
            sizeof(role_choice)) == -1) {

        return -1;
    }

    const char *role_name;

    if (strcmp(
            role_choice,
            "1") == 0) {

        role_name =
            "CUSTOMER";

    } else if (strcmp(
                   role_choice,
                   "2") == 0) {

        role_name =
            "EMPLOYEE";

    } else {

        printf(
            "\nInvalid role choice.\n"
        );

        return 0;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "CHANGE_ROLE|%lld|%s",
        user_id,
        role_name
    );

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "CHANGE_ROLE_FAILED|USER_NOT_FOUND") == 0) {

        printf(
            "\nUser not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "CHANGE_ROLE_FAILED|PROTECTED_ROLE") == 0) {

        printf(
            "\nManager and Admin roles cannot "
            "be changed here.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "CHANGE_ROLE_FAILED|SELF") == 0) {

        printf(
            "\nYou cannot change your own role.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "CHANGE_ROLE_FAILED|USER_LOGGED_IN") == 0) {

        printf(
            "\nThe user is currently logged in. "
            "Ask them to logout before changing the role.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "CHANGE_ROLE_FAILED|SAME_ROLE") == 0) {

        printf(
            "\nThe user already has that role.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "CHANGE_ROLE_OK|",
            15) == 0) {

        long long returned_id;
        char returned_role[32];

        if (sscanf(
                response,
                "CHANGE_ROLE_OK|%lld|%31s",
                &returned_id,
                returned_role) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("            ROLE UPDATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID  : %lld\n",
                returned_id
            );

            printf(
                "New Role : %s\n",
                returned_role
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int admin_change_password(
    int sock_fd)
{
    char current_password[
        MAX_PASSWORD_LEN + 1
    ];

    char new_password[
        MAX_PASSWORD_LEN + 1
    ];

    char confirm_password[
        MAX_PASSWORD_LEN + 1
    ];

    printf(
        "Enter current "
    );

    if (read_password(
            current_password,
            sizeof(current_password)) == -1) {

        return -1;
    }

    printf(
        "Enter new "
    );

    if (read_password(
            new_password,
            sizeof(new_password)) == -1) {

        memset(
            current_password,
            0,
            sizeof(current_password)
        );

        return -1;
    }

    printf(
        "Confirm new "
    );

    if (read_password(
            confirm_password,
            sizeof(confirm_password)) == -1) {

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

        return -1;
    }

    if (strcmp(
            new_password,
            confirm_password) != 0) {

        printf(
            "\nNew passwords do not match.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    if (strlen(
            new_password) < 8) {

        printf(
            "\nNew password must contain "
            "at least 8 characters.\n"
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

        memset(
            confirm_password,
            0,
            sizeof(confirm_password)
        );

        return 0;
    }

    char request[
        MAX_REQUEST_SIZE
    ];

    int written =
        snprintf(
            request,
            sizeof(request),
            "ADMIN_CHANGE_PASSWORD|%s|%s",
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

    memset(
        confirm_password,
        0,
        sizeof(confirm_password)
    );

    if (written < 0 ||
        (size_t)written >=
            sizeof(request)) {

        printf(
            "\nUnable to create password request.\n"
        );

        memset(
            request,
            0,
            sizeof(request)
        );

        return 0;
    }

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        memset(
            request,
            0,
            sizeof(request)
        );

        return -1;
    }

    memset(
        request,
        0,
        sizeof(request)
    );

    if (strcmp(
            response,
            "ADMIN_PASSWORD_OK") == 0) {

        printf(
            "\nPassword changed successfully.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ADMIN_PASSWORD_FAILED|INCORRECT_CURRENT_PASSWORD") == 0) {

        printf(
            "\nCurrent password is incorrect.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ADMIN_PASSWORD_FAILED|SAME_PASSWORD") == 0) {

        printf(
            "\nNew password cannot be the same "
            "as the current password.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "ADMIN_PASSWORD_FAILED|INVALID_PASSWORD") == 0) {

        printf(
            "\nInvalid new password.\n"
        );

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}

static int admin_manage_user_access(
    int sock_fd)
{
    char id_text[64];

    if (read_line(
            "Enter user ID: ",
            id_text,
            sizeof(id_text)) == -1) {

        return -1;
    }

    errno = 0;

    char *end = NULL;

    long long user_id =
        strtoll(
            id_text,
            &end,
            10
        );

    if (errno != 0 ||
        end == id_text ||
        *end != '\0' ||
        user_id <= 0) {

        printf(
            "\nInvalid user ID.\n"
        );

        return 0;
    }

    printf("\n");
    printf("1. Enable User Login\n");
    printf("2. Disable User Login\n");

    char access_choice[16];

    if (read_line(
            "Enter choice: ",
            access_choice,
            sizeof(access_choice)) == -1) {

        return -1;
    }

    const char *action;

    if (strcmp(
            access_choice,
            "1") == 0) {

        action =
            "ENABLE";

    } else if (strcmp(
                   access_choice,
                   "2") == 0) {

        action =
            "DISABLE";

    } else {

        printf(
            "\nInvalid choice.\n"
        );

        return 0;
    }

    char request[128];

    snprintf(
        request,
        sizeof(request),
        "SET_USER_ACCESS|%lld|%s",
        user_id,
        action
    );

    char response[
        MAX_RESPONSE_SIZE
    ];

    if (send_and_receive(
            sock_fd,
            request,
            response,
            sizeof(response)) == -1) {

        return -1;
    }

    if (strcmp(
            response,
            "USER_ACCESS_FAILED|USER_NOT_FOUND") == 0) {

        printf(
            "\nUser not found.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "USER_ACCESS_FAILED|SELF") == 0) {

        printf(
            "\nYou cannot disable your own Admin account.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "USER_ACCESS_FAILED|PROTECTED_USER") == 0) {

        printf(
            "\nManager/Admin access cannot be "
            "changed from this option.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "USER_ACCESS_FAILED|ALREADY_ENABLED") == 0) {

        printf(
            "\nUser access is already enabled.\n"
        );

        return 0;
    }

    if (strcmp(
            response,
            "USER_ACCESS_FAILED|ALREADY_DISABLED") == 0) {

        printf(
            "\nUser access is already disabled.\n"
        );

        return 0;
    }

    if (strncmp(
            response,
            "USER_ACCESS_OK|",
            15) == 0) {

        long long returned_id;
        char status[32];

        if (sscanf(
                response,
                "USER_ACCESS_OK|%lld|%31s",
                &returned_id,
                status) == 2) {

            printf("\n");
            printf("----------------------------------------\n");
            printf("          USER ACCESS UPDATED\n");
            printf("----------------------------------------\n");

            printf(
                "User ID : %lld\n",
                returned_id
            );

            printf(
                "Access  : %s\n",
                status
            );

            printf("----------------------------------------\n");
        }

        return 0;
    }

    printf(
        "%s\n",
        response
    );

    return 0;
}



/*=======ADMIN LOOP=======*/
static int admin_loop(
    int sock_fd,
    ClientUser *user)
{
    char choice[32];

    while (user->logged_in) {

        display_admin_menu(
            user
        );

        if (read_line(
                "Enter choice: ",
                choice,
                sizeof(choice)) == -1) {

            return -1;
        }

        if (strcmp(
                choice,
                "1") == 0) {

            if (admin_add_employee(
                    sock_fd) == -1) {

                return -1;
            }

            continue;
        }

	if (strcmp(
        choice,
        "2") == 0) {

   	 if (admin_view_user(
            sock_fd) == -1) {

       	 return -1;
   	 }

   	 continue;
	}
	
	if (strcmp(
        choice,
        "3") == 0) {

   	 if (admin_modify_user(
            sock_fd) == -1) {

        return -1;
    	}

    	continue;
	}

	if (strcmp(
        choice,
        "4") == 0) {

   	 if (admin_manage_user_access(
            sock_fd) == -1) {

       	 return -1;
   	 }

   	 continue;
	}

	if (strcmp(
        choice,
        "5") == 0) {

    	if (admin_change_password(
            sock_fd) == -1) {

        return -1;
   	 }

    	continue;
	}


        if (strcmp(
                choice,
                "6") == 0) {

            char response[
                MAX_RESPONSE_SIZE
            ];

            if (send_and_receive(
                    sock_fd,
                    "LOGOUT",
                    response,
                    sizeof(response)) == -1) {

                return -1;
            }

            if (strcmp(
                    response,
                    "LOGOUT_OK") == 0) {

                user->logged_in = 0;

                printf(
                    "\nLogged out successfully.\n"
                );

                return 0;
            }

            printf(
                "%s\n",
                response
            );

            continue;
        }

        if (strcmp(
                choice,
                "0") == 0) {

            send_message(
                sock_fd,
                "EXIT"
            );

            return 1;
        }

        printf(
            "\nInvalid choice.\n"
        );
    }

    return 0;
}



int main(void)
{
    int sock_fd;

    struct sockaddr_in
        server_addr;

    ClientUser user;

    memset(
        &user,
        0,
        sizeof(user)
    );

    sock_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (sock_fd == -1) {

        perror("socket");

        return EXIT_FAILURE;
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(SERVER_PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr) != 1) {

        fprintf(
            stderr,
            "Invalid server IP address.\n"
        );

        close(sock_fd);

        return EXIT_FAILURE;
    }

    if (connect(
            sock_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1) {

        perror("connect");

        close(sock_fd);

        return EXIT_FAILURE;
    }

    printf(
        "Connected to banking server.\n"
    );

    int running = 1;

    while (running) {

        if (!user.logged_in) {

            char choice[32];

            printf("\n");
            printf("========================================\n");
            printf("          BANKING MANAGEMENT SYSTEM\n");
            printf("========================================\n");
            printf("1. Login\n");
            printf("0. Exit\n");
            printf("========================================\n");

            if (read_line(
                    "Enter choice: ",
                    choice,
                    sizeof(choice)) == -1) {

                break;
            }

            if (strcmp(
                    choice,
                    "1") == 0) {

                if (perform_login(
                        sock_fd,
                        &user) == -1) {

                    break;
                }

                continue;
            }

            if (strcmp(
                    choice,
                    "0") == 0) {

                char response[
                    BUFFER_SIZE
                ];

                if (send_and_receive(
                        sock_fd,
                        "EXIT",
                        response,
                        sizeof(response)) == -1) {

                    break;
                }

                printf("%s\n",
                       response);

                break;
            }

            printf(
                "\nInvalid choice.\n"
            );

            continue;
        }

        if (user.role ==
            ROLE_CUSTOMER) {

            int result =
                customer_loop(
                    sock_fd,
                    &user
                );

            if (result != 0)
                running = 0;

            continue;
        }

	if (user.role ==
   	 ROLE_EMPLOYEE) {

   	 int result =
       	 employee_loop(
            sock_fd,
            &user
        );
	
	if (result!=0)
		running = 0;
	continue;
	}

	if (user.role ==
         ROLE_MANAGER) {

   	int  result =
        manager_loop(
            sock_fd,
            &user
        );

   	 if (result != 0)
       		 running = 0;

   	 continue;
	}


	if (user.role ==
         ROLE_ADMIN) {

   	int  result =
        admin_loop(
            sock_fd,
            &user
        );

   	 if (result != 0)
       		 running = 0;

   	 continue;
	}


        printf(
            "\nRole menu not implemented yet.\n"
        );

        char response[
            BUFFER_SIZE
        ];

        send_and_receive(
            sock_fd,
            "LOGOUT",
            response,
            sizeof(response)
        );

        memset(
            &user,
            0,
            sizeof(user)
        );
    }

    close(sock_fd);

    printf(
        "Connection closed.\n"
    );

    return EXIT_SUCCESS;
}


