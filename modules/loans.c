#include "loans.h"

#include "accounts.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#define LOANS_FILE STORAGE_DIR "/loans.txt"
#define LOAN_LINE_SIZE 4096

static pthread_mutex_t loan_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static int current_timestamp(
    char *buffer,
    size_t size)
{
    time_t now =
        time(NULL);

    if (now == (time_t)-1)
        return -1;

    struct tm local_time;

    if (localtime_r(
            &now,
            &local_time) == NULL) {

        return -1;
    }

    if (strftime(
            buffer,
            size,
            "%Y-%m-%d %H:%M:%S",
            &local_time) == 0) {

        return -1;
    }

    return 0;
}

static int lock_file(
    int fd,
    short lock_type)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type =
        lock_type;

    lock.l_whence =
        SEEK_SET;

    lock.l_start = 0;

    /*
     * 0 means from start to EOF,
     * including future file growth.
     */
    lock.l_len = 0;

    while (fcntl(
               fd,
               F_SETLKW,
               &lock) == -1) {

        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

static int unlock_file(int fd)
{
    struct flock lock;

    memset(
        &lock,
        0,
        sizeof(lock)
    );

    lock.l_type =
        F_UNLCK;

    lock.l_whence =
        SEEK_SET;

    lock.l_start = 0;
    lock.l_len = 0;

    return fcntl(
        fd,
        F_SETLK,
        &lock
    );
}

static int interest_for_tenure(
    int tenure_months)
{
    switch (tenure_months) {

        case 6:
            return 800;

        case 12:
            return 850;

        case 24:
            return 900;

        case 36:
            return 950;

        case 60:
            return 1000;

        default:
            return -1;
    }
}

static LoanId find_next_loan_id(
    int fd)
{
    if (lseek(
            fd,
            0,
            SEEK_SET) == -1) {

        return -1;
    }

    FILE *file =
        fdopen(dup(fd), "r");

    if (file == NULL)
        return -1;

    char line[
        LOAN_LINE_SIZE
    ];

    LoanId maximum =
        70000;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        char *separator =
            strchr(line, '|');

        if (separator == NULL)
            continue;

        *separator =
            '\0';

        errno = 0;

        char *end =
            NULL;

        long long id =
            strtoll(
                line,
                &end,
                10
            );

        if (errno == 0 &&
            end != line &&
            *end == '\0' &&
            id > maximum) {

            maximum =
                (LoanId)id;
        }
    }

    fclose(file);

    if (maximum ==
        INT64_MAX) {

        return -1;
    }

    return maximum + 1;
}

OperationStatus apply_for_loan(
    UserId user_id,
    Money amount,
    int tenure_months,
    LoanRecord *created_loan)
{
    if (user_id <= 0 ||
        amount <= 0 ||
        amount >
            MAX_TRANSACTION_PAISE ||
        created_loan == NULL) {

        return STATUS_INVALID_INPUT;
    }

    int interest =
        interest_for_tenure(
            tenure_months
        );

    if (interest == -1)
        return STATUS_INVALID_INPUT;

    /*
     * The server determines which bank
     * account belongs to this customer.
     */
    AccountRecord account;

    OperationStatus status =
        find_account_by_user(
            user_id,
            &account
        );

    if (status != STATUS_OK)
        return status;

    if (account.status !=
        ACCOUNT_ACTIVE) {

        return STATUS_INACTIVE;
    }

    pthread_mutex_lock(
        &loan_mutex
    );

    int fd =
        open(
            LOANS_FILE,
            O_RDWR |
            O_CREAT,
            0660
        );

    if (fd == -1) {

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    if (lock_file(
            fd,
            F_WRLCK) == -1) {

        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    LoanId loan_id =
        find_next_loan_id(fd);

    if (loan_id <= 0) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    LoanRecord loan;

    memset(
        &loan,
        0,
        sizeof(loan)
    );

    loan.loan_id =
        loan_id;

    loan.user_id =
        user_id;

    loan.account_id =
        account.account_id;

    loan.amount =
        amount;

    loan.tenure_months =
        tenure_months;

    loan.interest_basis_points =
        interest;

    loan.status =
        LOAN_PENDING;

    loan.assigned_employee_id =
        0;

    loan.employee_recommendation =
        0;

    strcpy(
        loan.employee_notes,
        "-"
    );

    loan.decision_by =
        0;

    strcpy(
        loan.decision_reason,
        "-"
    );

    if (current_timestamp(
            loan.created_at,
            sizeof(
                loan.created_at)) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_INTERNAL_ERROR;
    }

    strcpy(
        loan.updated_at,
        loan.created_at
    );

    char record[
        LOAN_LINE_SIZE
    ];

    int length =
        snprintf(
            record,
            sizeof(record),

            "%lld|%lld|%lld|%lld|%d|%d|%d|%lld|%d|%s|%lld|%s|%s|%s\n",

            (long long)
                loan.loan_id,

            (long long)
                loan.user_id,

            (long long)
                loan.account_id,

            (long long)
                loan.amount,

            loan.tenure_months,

            loan.interest_basis_points,

            (int)
                loan.status,

            (long long)
                loan.assigned_employee_id,

            loan.employee_recommendation,

            loan.employee_notes,

            (long long)
                loan.decision_by,

            loan.decision_reason,

            loan.created_at,

            loan.updated_at
        );

    if (length < 0 ||
        (size_t)length >=
            sizeof(record)) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_INTERNAL_ERROR;
    }

    if (lseek(
            fd,
            0,
            SEEK_END) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    ssize_t total =
        0;

    while (total < length) {

        ssize_t written =
            write(
                fd,
                record + total,
                (size_t)
                    (length - total)
            );

        if (written < 0) {

            if (errno == EINTR)
                continue;

            unlock_file(fd);
            close(fd);

            pthread_mutex_unlock(
                &loan_mutex
            );

            return STATUS_STORAGE_ERROR;
        }

        total +=
            written;
    }

    if (fsync(fd) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(
            &loan_mutex
        );

        return STATUS_STORAGE_ERROR;
    }

    *created_loan =
        loan;

    unlock_file(fd);

    close(fd);

    pthread_mutex_unlock(
        &loan_mutex
    );

    return STATUS_OK;
}

const char *loan_status_name(
    LoanStatus status)
{
    switch (status) {

        case LOAN_PENDING:
            return "PENDING";

        case LOAN_ASSIGNED:
            return "ASSIGNED";

        case LOAN_UNDER_REVIEW:
            return "UNDER REVIEW";

        case LOAN_APPROVED:
            return "APPROVED";

        case LOAN_REJECTED:
            return "REJECTED";

        case LOAN_DISBURSED:
            return "DISBURSED";

        case LOAN_CANCELLED:
            return "CANCELLED";

        default:
            return "UNKNOWN";
    }
}

OperationStatus get_customer_loans(
    UserId user_id,
    char *output,
    size_t output_size)
{
    if (user_id <= 0 ||
        output == NULL ||
        output_size == 0) {

        return STATUS_INVALID_INPUT;
    }

    FILE *file =
        fopen(LOANS_FILE, "r");

    if (file == NULL)
        return STATUS_STORAGE_ERROR;

    output[0] = '\0';

    char line[LOAN_LINE_SIZE];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        char copy[
            LOAN_LINE_SIZE
        ];

        if (strlen(line) >=
            sizeof(copy)) {

            continue;
        }

        strcpy(copy, line);

        copy[
            strcspn(copy, "\r\n")
        ] = '\0';

        char *fields[14];

        size_t count = 0;

        char *saveptr = NULL;

        char *token =
            strtok_r(
                copy,
                "|",
                &saveptr
            );

        while (token != NULL &&
               count < 14) {

            fields[count++] =
                token;

            token =
                strtok_r(
                    NULL,
                    "|",
                    &saveptr
                );
        }

        if (count != 14)
            continue;

        char *end = NULL;

        errno = 0;

        long long record_user =
            strtoll(
                fields[1],
                &end,
                10
            );

        if (errno != 0 ||
            end == fields[1] ||
            *end != '\0') {

            continue;
        }

        /*
         * Only return loans belonging
         * to this authenticated customer.
         */
        if ((UserId)record_user !=
            user_id) {

            continue;
        }

        long long loan_id =
            strtoll(
                fields[0],
                NULL,
                10
            );

        long long amount =
            strtoll(
                fields[3],
                NULL,
                10
            );

        int tenure =
            (int)strtol(
                fields[4],
                NULL,
                10
            );

        int interest =
            (int)strtol(
                fields[5],
                NULL,
                10
            );

        int status =
            (int)strtol(
                fields[6],
                NULL,
                10
            );

        long long employee_id =
            strtoll(
                fields[7],
                NULL,
                10
            );

        long long decision_by =
            strtoll(
                fields[10],
                NULL,
                10
            );

        char formatted[2048];

        int written =
            snprintf(
                formatted,
                sizeof(formatted),

                "Loan ID       : %lld\n"
                "Amount        : %s%lld.%02lld\n"
                "Tenure        : %d months\n"
                "Interest Rate : %d.%02d%% p.a.\n"
                "Status        : %s\n"
                "Assigned To   : %s\n"
                "Decision By   : %s\n"
                "Decision      : %s\n"
                "Applied On    : %s\n"
                "Last Updated  : %s\n"
                "----------------------------------------\n",

                loan_id,

                CURRENCY_SYMBOL,
                amount / 100,
                amount % 100,

                tenure,

                interest / 100,
                interest % 100,

                loan_status_name(
                    (LoanStatus)status
                ),

                employee_id == 0
                    ? "Not assigned"
                    : fields[7],

                decision_by == 0
                    ? "Not decided"
                    : fields[10],

                strcmp(fields[11], "-") == 0
                    ? "Not decided"
                    : fields[11],

                fields[12],
                fields[13]
            );

        if (written < 0)
            continue;

        size_t used =
            strlen(output);

        if (used +
            (size_t)written + 1 >
            output_size) {

            fclose(file);

            return STATUS_STORAGE_ERROR;
        }

        strcat(
            output,
            formatted
        );

        found = 1;
    }

    fclose(file);

    return found
        ? STATUS_OK
        : STATUS_NOT_FOUND;
}
