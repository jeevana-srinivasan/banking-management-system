#include "transactions.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TRANSACTIONS_FILE STORAGE_DIR "/transactions.txt"

static pthread_mutex_t transaction_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static int lock_file(int fd, short lock_type)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));

    lock.l_type = lock_type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    while (fcntl(fd, F_SETLKW, &lock) == -1) {

        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

static int unlock_file(int fd)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    return fcntl(fd, F_SETLK, &lock);
}

static int valid_details(const char *details)
{
    if (details == NULL)
        return 0;

    if (strlen(details) > MAX_DETAILS_LEN)
        return 0;

    if (strchr(details, '|') != NULL ||
        strchr(details, '\n') != NULL ||
        strchr(details, '\r') != NULL) {

        return 0;
    }

    return 1;
}

static int current_timestamp(char *buffer,
                             size_t size)
{
    time_t now = time(NULL);

    if (now == (time_t)-1)
        return -1;

    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL)
        return -1;

    if (strftime(buffer,
                 size,
                 "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0) {

        return -1;
    }

    return 0;
}

static TransactionId find_next_transaction_id(
    const char *contents)
{
    TransactionId max_id = 0;

    if (contents == NULL ||
        contents[0] == '\0') {

        return 1;
    }

    char *copy = strdup(contents);

    if (copy == NULL)
        return -1;

    char *saveptr = NULL;

    char *line =
        strtok_r(copy, "\n", &saveptr);

    while (line != NULL) {

        char *separator =
            strchr(line, '|');

        if (separator != NULL) {

            *separator = '\0';

            errno = 0;

            char *end = NULL;

            long long value =
                strtoll(line, &end, 10);

            if (errno == 0 &&
                end != line &&
                *end == '\0' &&
                value > max_id) {

                max_id = value;
            }
        }

        line =
            strtok_r(NULL, "\n", &saveptr);
    }

    free(copy);

    if (max_id >= INT64_MAX - 1)
        return -1;

    return max_id + 1;
}

static TransactionId get_next_id_locked(int fd)
{
    struct stat info;

    if (fstat(fd, &info) == -1)
        return -1;

    char *contents =
        malloc((size_t)info.st_size + 1);

    if (contents == NULL)
        return -1;

    ssize_t total = 0;

    while (total < info.st_size) {

        ssize_t n =
            pread(fd,
                  contents + total,
                  (size_t)(info.st_size - total),
                  total);

        if (n < 0) {

            if (errno == EINTR)
                continue;

            free(contents);
            return -1;
        }

        if (n == 0)
            break;

        total += n;
    }

    contents[total] = '\0';

    TransactionId next =
        find_next_transaction_id(contents);

    free(contents);

    return next;
}

static int write_all(int fd,
                     const char *buffer,
                     size_t length)
{
    size_t total = 0;

    while (total < length) {

        ssize_t n =
            write(fd,
                  buffer + total,
                  length - total);

        if (n < 0) {

            if (errno == EINTR)
                continue;

            return -1;
        }

        total += (size_t)n;
    }

    return 0;
}

static const char *transaction_type_name(
    TransactionType type)
{
    switch (type) {

        case TXN_DEPOSIT:
            return "Deposit";

        case TXN_WITHDRAWAL:
            return "Withdrawal";

        case TXN_TRANSFER_OUT:
            return "Transfer Out";

        case TXN_TRANSFER_IN:
            return "Transfer In";

        case TXN_LOAN_DISBURSEMENT:
            return "Loan Disbursement";

        default:
            return "Unknown";
    }
}

OperationStatus append_transaction(
    AccountId account_id,
    TransactionType type,
    Money amount,
    Money balance_after,
    const char *details,
    UserId performed_by,
    TransactionId reference_id,
    TransactionId *created_transaction_id)
{
    if (account_id <= 0 ||
        amount <= 0 ||
        balance_after < 0 ||
        performed_by <= 0 ||
        created_transaction_id == NULL ||
        !valid_details(details)) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(&transaction_mutex);

    int fd =
        open(TRANSACTIONS_FILE,
             O_RDWR | O_CREAT,
             0660);

    if (fd == -1) {

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    if (lock_file(fd, F_WRLCK) == -1) {

        close(fd);
        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    TransactionId transaction_id =
        get_next_id_locked(fd);

    if (transaction_id <= 0) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_STORAGE_ERROR;
    }

    if (reference_id == 0)
        reference_id = transaction_id;

    char timestamp[32];

    if (current_timestamp(timestamp,
                          sizeof(timestamp)) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_INTERNAL_ERROR;
    }

    char record[2048];

    int length =
        snprintf(
            record,
            sizeof(record),
            "%lld|%lld|%lld|%d|%lld|%lld|%s|%s|%lld\n",
            (long long)transaction_id,
            (long long)reference_id,
            (long long)account_id,
            (int)type,
            (long long)amount,
            (long long)balance_after,
            details,
            timestamp,
            (long long)performed_by
        );

    if (length < 0 ||
        (size_t)length >= sizeof(record)) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_INTERNAL_ERROR;
    }

    if (lseek(fd, 0, SEEK_END) == -1 ||
        write_all(fd,
                  record,
                  (size_t)length) == -1 ||
        fsync(fd) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_STORAGE_ERROR;
    }

    *created_transaction_id =
        transaction_id;

    unlock_file(fd);
    close(fd);

    pthread_mutex_unlock(&transaction_mutex);

    return STATUS_OK;
}

OperationStatus append_transfer_transactions(
    AccountId source_account_id,
    AccountId destination_account_id,
    Money amount,
    Money source_balance_after,
    Money destination_balance_after,
    UserId performed_by,
    TransactionId *reference_id,
    TransactionId *out_transaction_id,
    TransactionId *in_transaction_id)
{
    if (source_account_id <= 0 ||
        destination_account_id <= 0 ||
        source_account_id == destination_account_id ||
        amount <= 0 ||
        source_balance_after < 0 ||
        destination_balance_after < 0 ||
        performed_by <= 0 ||
        reference_id == NULL ||
        out_transaction_id == NULL ||
        in_transaction_id == NULL) {

        return STATUS_INVALID_INPUT;
    }

    pthread_mutex_lock(&transaction_mutex);

    int fd =
        open(TRANSACTIONS_FILE,
             O_RDWR | O_CREAT,
             0660);

    if (fd == -1) {

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    if (lock_file(fd, F_WRLCK) == -1) {

        close(fd);

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    struct stat info;

    if (fstat(fd, &info) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    off_t original_size =
        info.st_size;

    TransactionId first_id =
        get_next_id_locked(fd);

    if (first_id <= 0 ||
        first_id == INT64_MAX) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_STORAGE_ERROR;
    }

    TransactionId second_id =
        first_id + 1;

    TransactionId ref =
        first_id;

    char timestamp[32];

    if (current_timestamp(timestamp,
                          sizeof(timestamp)) == -1) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);
        return STATUS_INTERNAL_ERROR;
    }

    char output[4096];

    int length =
        snprintf(
            output,
            sizeof(output),

            "%lld|%lld|%lld|%d|%lld|%lld|Transfer to account %lld|%s|%lld\n"
            "%lld|%lld|%lld|%d|%lld|%lld|Transfer from account %lld|%s|%lld\n",

            (long long)first_id,
            (long long)ref,
            (long long)source_account_id,
            (int)TXN_TRANSFER_OUT,
            (long long)amount,
            (long long)source_balance_after,
            (long long)destination_account_id,
            timestamp,
            (long long)performed_by,

            (long long)second_id,
            (long long)ref,
            (long long)destination_account_id,
            (int)TXN_TRANSFER_IN,
            (long long)amount,
            (long long)destination_balance_after,
            (long long)source_account_id,
            timestamp,
            (long long)performed_by
        );

    if (length < 0 ||
        (size_t)length >= sizeof(output)) {

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_INTERNAL_ERROR;
    }

    if (lseek(fd, 0, SEEK_END) == -1 ||
        write_all(fd,
                  output,
                  (size_t)length) == -1 ||
        fsync(fd) == -1) {

        /*
         * Restore transaction file to its
         * pre-transfer size.
         */
        ftruncate(fd,
                  original_size);

        fsync(fd);

        unlock_file(fd);
        close(fd);

        pthread_mutex_unlock(&transaction_mutex);

        return STATUS_STORAGE_ERROR;
    }

    *reference_id = ref;
    *out_transaction_id = first_id;
    *in_transaction_id = second_id;

    unlock_file(fd);
    close(fd);

    pthread_mutex_unlock(&transaction_mutex);

    return STATUS_OK;
}


OperationStatus get_transaction_history(
    AccountId account_id,
    char *output,
    size_t output_size)
{
    if (account_id <= 0 ||
        output == NULL ||
        output_size == 0) {

        return STATUS_INVALID_INPUT;
    }

    FILE *file =
        fopen(TRANSACTIONS_FILE, "r");

    if (file == NULL)
        return STATUS_STORAGE_ERROR;

    output[0] = '\0';

    char line[2048];

    int found = 0;

    while (fgets(
               line,
               sizeof(line),
               file) != NULL) {

        char copy[2048];

        if (strlen(line) >= sizeof(copy))
            continue;

        strcpy(copy, line);

        copy[
            strcspn(copy, "\r\n")
        ] = '\0';

        char *fields[9];

        size_t count = 0;

        char *saveptr = NULL;

        char *token =
            strtok_r(
                copy,
                "|",
                &saveptr
            );

        while (token != NULL &&
               count < 9) {

            fields[count++] = token;

            token =
                strtok_r(
                    NULL,
                    "|",
                    &saveptr
                );
        }

        if (count != 9)
            continue;

        char *end = NULL;

        errno = 0;

        long long record_account =
            strtoll(
                fields[2],
                &end,
                10
            );

        if (errno != 0 ||
            end == fields[2] ||
            *end != '\0') {

            continue;
        }

        if ((AccountId)record_account !=
            account_id) {

            continue;
        }

        long long transaction_id =
            strtoll(fields[0], NULL, 10);

        long transaction_type =
            strtol(fields[3], NULL, 10);

        long long amount =
            strtoll(fields[4], NULL, 10);

        long long balance_after =
            strtoll(fields[5], NULL, 10);

        char formatted[1024];

        int written =
            snprintf(
                formatted,
                sizeof(formatted),

                "ID: %lld | %s | Amount: %s%lld.%02lld | "
                "Balance: %s%lld.%02lld | %s | %s\n",

                transaction_id,

                transaction_type_name(
                    (TransactionType)
                    transaction_type
                ),

                CURRENCY_SYMBOL,
                amount / 100,
                amount % 100,

                CURRENCY_SYMBOL,
                balance_after / 100,
                balance_after % 100,

                fields[7],
                fields[6]
            );

        if (written < 0)
            continue;

        size_t current =
            strlen(output);

        size_t required =
            (size_t)written;

        if (current + required + 1 >
            output_size) {

            fclose(file);

            return STATUS_STORAGE_ERROR;
        }

        strcat(output,
               formatted);

        found = 1;
    }

    fclose(file);

    if (!found)
        return STATUS_NOT_FOUND;

    return STATUS_OK;
}

OperationStatus get_account_transaction_history(
    AccountId account_id,
    char *output,
    size_t output_size)
{
    return get_transaction_history(
        account_id,
        output,
        output_size
    );
}
