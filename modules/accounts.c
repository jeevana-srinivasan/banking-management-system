#include "accounts.h"

#include "config.h"
#include "transactions.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ACCOUNTS_FILE STORAGE_DIR "/accounts.txt"
#define ACCOUNT_LINE_SIZE 128

#define ACCOUNT_MUTEX_COUNT 64

static pthread_mutex_t
    account_mutexes[ACCOUNT_MUTEX_COUNT];

static pthread_once_t account_mutex_once =
    PTHREAD_ONCE_INIT;

static void initialize_account_mutexes(void)
{
    for (int i = 0;
         i < ACCOUNT_MUTEX_COUNT;
         i++) {

        pthread_mutex_init(
            &account_mutexes[i],
            NULL
        );
    }
}

static pthread_mutex_t *mutex_for_account(
    AccountId account_id)
{
    pthread_once(
        &account_mutex_once,
        initialize_account_mutexes
    );

    unsigned long long value =
        (unsigned long long)account_id;

    return &account_mutexes[
        value % ACCOUNT_MUTEX_COUNT
    ];
}

static int parse_account_line(
    const char *line,
    AccountRecord *account)
{
    if (line == NULL ||
        account == NULL) {

        return -1;
    }

    char copy[ACCOUNT_LINE_SIZE];

    if (strlen(line) >= sizeof(copy))
        return -1;

    strcpy(copy, line);

    copy[
        strcspn(copy, "\r\n")
    ] = '\0';

    char *fields[5];
    size_t count = 0;

    char *saveptr = NULL;

    char *token =
        strtok_r(copy,
                 "|",
                 &saveptr);

    while (token != NULL &&
           count < 5) {

        fields[count++] = token;

        token =
            strtok_r(NULL,
                     "|",
                     &saveptr);
    }

    if (count != 5 ||
        token != NULL) {

        return -1;
    }

    errno = 0;
    char *end = NULL;

    long long account_id =
        strtoll(fields[0],
                &end,
                10);

    if (errno != 0 ||
        end == fields[0] ||
        *end != '\0' ||
        account_id <= 0) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long long user_id =
        strtoll(fields[1],
                &end,
                10);

    if (errno != 0 ||
        end == fields[1] ||
        *end != '\0' ||
        user_id <= 0) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long long balance =
        strtoll(fields[2],
                &end,
                10);

    if (errno != 0 ||
        end == fields[2] ||
        *end != '\0' ||
        balance < 0) {

        return -1;
    }

    errno = 0;
    end = NULL;

    long status =
        strtol(fields[3],
               &end,
               10);

    if (errno != 0 ||
        end == fields[3] ||
        *end != '\0' ||
        status < ACCOUNT_ACTIVE ||
        status > ACCOUNT_CLOSED) {

        return -1;
    }

    if (strlen(fields[4]) >=
        sizeof(account->created_at)) {

        return -1;
    }

    account->account_id =
        (AccountId)account_id;

    account->user_id =
        (UserId)user_id;

    account->balance =
        (Money)balance;

    account->status =
        (AccountStatus)status;

    strcpy(account->created_at,
           fields[4]);

    return 0;
}

static int format_account_record(
    const AccountRecord *account,
    char *record,
    size_t size)
{
    if (account == NULL ||
        record == NULL ||
        size < ACCOUNT_RECORD_SIZE + 1) {

        return -1;
    }

    int written =
        snprintf(
            record,
            size,
            "%010lld|%010lld|%020lld|%d|%s\n",
            (long long)account->account_id,
            (long long)account->user_id,
            (long long)account->balance,
            (int)account->status,
            account->created_at
        );

    return written == ACCOUNT_RECORD_SIZE
        ? 0
        : -1;
}

static int lock_account_record(
    int fd,
    off_t offset)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = offset;
    lock.l_len = ACCOUNT_RECORD_SIZE;

    while (fcntl(fd,
                 F_SETLKW,
                 &lock) == -1) {

        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

static int unlock_account_record(
    int fd,
    off_t offset)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = offset;
    lock.l_len = ACCOUNT_RECORD_SIZE;

    return fcntl(fd,
                 F_SETLK,
                 &lock);
}

static int find_record_by_account_id(
    int fd,
    AccountId account_id,
    off_t *offset_out,
    AccountRecord *account_out)
{
    struct stat info;

    if (fstat(fd, &info) == -1 ||
        info.st_size %
            ACCOUNT_RECORD_SIZE != 0) {

        return -1;
    }

    for (off_t offset = 0;
         offset < info.st_size;
         offset += ACCOUNT_RECORD_SIZE) {

        char record[
            ACCOUNT_RECORD_SIZE + 1
        ];

        ssize_t n =
            pread(fd,
                  record,
                  ACCOUNT_RECORD_SIZE,
                  offset);

        if (n != ACCOUNT_RECORD_SIZE)
            continue;

        record[
            ACCOUNT_RECORD_SIZE
        ] = '\0';

        AccountRecord current;

        if (parse_account_line(
                record,
                &current) != 0) {

            continue;
        }

        if (current.account_id ==
            account_id) {

            if (offset_out != NULL)
                *offset_out = offset;

            if (account_out != NULL)
                *account_out = current;

            return 0;
        }
    }

    return 1;
}

OperationStatus find_account_by_id(
    AccountId account_id,
    AccountRecord *account)
{
    if (account_id <= 0 ||
        account == NULL) {

        return STATUS_INVALID_INPUT;
    }

    FILE *file =
        fopen(ACCOUNTS_FILE,
              "r");

    if (file == NULL)
        return STATUS_STORAGE_ERROR;

    char line[ACCOUNT_LINE_SIZE];

    while (fgets(line,
                 sizeof(line),
                 file) != NULL) {

        AccountRecord current;

        if (parse_account_line(
                line,
                &current) != 0) {

            continue;
        }

        if (current.account_id ==
            account_id) {

            *account = current;

            fclose(file);
            return STATUS_OK;
        }
    }

    fclose(file);

    return STATUS_NOT_FOUND;
}

OperationStatus find_account_by_user(
    UserId user_id,
    AccountRecord *account)
{
    if (user_id <= 0 ||
        account == NULL) {

        return STATUS_INVALID_INPUT;
    }

    FILE *file =
        fopen(ACCOUNTS_FILE,
              "r");

    if (file == NULL)
        return STATUS_STORAGE_ERROR;

    char line[ACCOUNT_LINE_SIZE];

    while (fgets(line,
                 sizeof(line),
                 file) != NULL) {

        AccountRecord current;

        if (parse_account_line(
                line,
                &current) != 0) {

            continue;
        }

        if (current.user_id == user_id &&
            current.status == ACCOUNT_ACTIVE) {

            *account = current;

            fclose(file);
            return STATUS_OK;
        }
    }

    fclose(file);

    return STATUS_NOT_FOUND;
}

static OperationStatus modify_balance(
    UserId user_id,
    Money amount,
    TransactionType transaction_type,
    const char *details,
    AccountRecord *updated_account,
    TransactionId *transaction_id)
{
    if (user_id <= 0 ||
        amount <= 0 ||
        amount > MAX_TRANSACTION_PAISE ||
        updated_account == NULL ||
        transaction_id == NULL) {

        return STATUS_INVALID_INPUT;
    }

    AccountRecord initial;

    OperationStatus status =
        find_account_by_user(
            user_id,
            &initial
        );

    if (status != STATUS_OK)
        return status;

    pthread_mutex_t *mutex =
        mutex_for_account(
            initial.account_id
        );

    pthread_mutex_lock(mutex);

    int fd =
        open(ACCOUNTS_FILE,
             O_RDWR);

    if (fd == -1) {

        pthread_mutex_unlock(mutex);
        return STATUS_STORAGE_ERROR;
    }

    off_t offset;

    if (find_record_by_account_id(
            fd,
            initial.account_id,
            &offset,
            NULL) != 0) {

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_NOT_FOUND;
    }

    if (lock_account_record(fd,
                            offset) == -1) {

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_STORAGE_ERROR;
    }

    char old_record[
        ACCOUNT_RECORD_SIZE + 1
    ];

    if (pread(fd,
              old_record,
              ACCOUNT_RECORD_SIZE,
              offset) != ACCOUNT_RECORD_SIZE) {

        unlock_account_record(fd,
                              offset);

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_STORAGE_ERROR;
    }

    old_record[
        ACCOUNT_RECORD_SIZE
    ] = '\0';

    AccountRecord account;

    if (parse_account_line(
            old_record,
            &account) != 0) {

        unlock_account_record(fd,
                              offset);

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_STORAGE_ERROR;
    }

    Money old_balance =
        account.balance;

    if (transaction_type ==
        TXN_DEPOSIT) {

        if (amount >
            INT64_MAX -
                account.balance) {

            unlock_account_record(fd,
                                  offset);

            close(fd);
            pthread_mutex_unlock(mutex);

            return STATUS_INVALID_INPUT;
        }

        account.balance += amount;

    } else {

        if (amount >
            account.balance) {

            unlock_account_record(fd,
                                  offset);

            close(fd);
            pthread_mutex_unlock(mutex);

            return STATUS_INSUFFICIENT_FUNDS;
        }

        account.balance -= amount;
    }

    char new_record[
        ACCOUNT_RECORD_SIZE + 1
    ];

    if (format_account_record(
            &account,
            new_record,
            sizeof(new_record)) == -1) {

        unlock_account_record(fd,
                              offset);

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_INTERNAL_ERROR;
    }

    if (pwrite(fd,
               new_record,
               ACCOUNT_RECORD_SIZE,
               offset) != ACCOUNT_RECORD_SIZE ||
        fsync(fd) == -1) {

        pwrite(fd,
               old_record,
               ACCOUNT_RECORD_SIZE,
               offset);

        fsync(fd);

        unlock_account_record(fd,
                              offset);

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_STORAGE_ERROR;
    }

    TransactionId created_id;

    status =
        append_transaction(
            account.account_id,
            transaction_type,
            amount,
            account.balance,
            details,
            user_id,
            0,
            &created_id
        );

    if (status != STATUS_OK) {

        account.balance =
            old_balance;

        pwrite(fd,
               old_record,
               ACCOUNT_RECORD_SIZE,
               offset);

        fsync(fd);

        unlock_account_record(fd,
                              offset);

        close(fd);
        pthread_mutex_unlock(mutex);

        return STATUS_STORAGE_ERROR;
    }

    *updated_account =
        account;

    *transaction_id =
        created_id;

    unlock_account_record(fd,
                          offset);

    close(fd);
    pthread_mutex_unlock(mutex);

    return STATUS_OK;
}

OperationStatus deposit_to_user_account(
    UserId user_id,
    Money amount,
    AccountRecord *updated_account,
    TransactionId *transaction_id)
{
    return modify_balance(
        user_id,
        amount,
        TXN_DEPOSIT,
        "Cash deposit",
        updated_account,
        transaction_id
    );
}

OperationStatus withdraw_from_user_account(
    UserId user_id,
    Money amount,
    AccountRecord *updated_account,
    TransactionId *transaction_id)
{
    return modify_balance(
        user_id,
        amount,
        TXN_WITHDRAWAL,
        "Cash withdrawal",
        updated_account,
        transaction_id
    );
}

OperationStatus transfer_from_user_account(
    UserId source_user_id,
    AccountId destination_account_id,
    Money amount,
    AccountRecord *updated_source,
    AccountRecord *updated_destination,
    TransactionId *reference_id)
{
    if (source_user_id <= 0 ||
        destination_account_id <= 0 ||
        amount <= 0 ||
        amount > MAX_TRANSACTION_PAISE ||
        updated_source == NULL ||
        updated_destination == NULL ||
        reference_id == NULL) {

        return STATUS_INVALID_INPUT;
    }

    AccountRecord source_initial;

    OperationStatus status =
        find_account_by_user(
            source_user_id,
            &source_initial
        );

    if (status != STATUS_OK)
        return status;

    if (source_initial.account_id ==
        destination_account_id) {

        return STATUS_CONFLICT;
    }

    AccountRecord destination_initial;

    status =
        find_account_by_id(
            destination_account_id,
            &destination_initial
        );

    if (status != STATUS_OK)
        return status;

    if (destination_initial.status !=
        ACCOUNT_ACTIVE) {

        return STATUS_INACTIVE;
    }

    AccountId first_id =
        source_initial.account_id <
        destination_account_id
            ? source_initial.account_id
            : destination_account_id;

    AccountId second_id =
        source_initial.account_id <
        destination_account_id
            ? destination_account_id
            : source_initial.account_id;

    pthread_mutex_t *first_mutex =
        mutex_for_account(first_id);

    pthread_mutex_t *second_mutex =
        mutex_for_account(second_id);

    pthread_mutex_lock(first_mutex);

    if (second_mutex != first_mutex)
        pthread_mutex_lock(second_mutex);

    int fd =
        open(ACCOUNTS_FILE,
             O_RDWR);

    if (fd == -1) {

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_STORAGE_ERROR;
    }

    off_t source_offset;
    off_t destination_offset;

    if (find_record_by_account_id(
            fd,
            source_initial.account_id,
            &source_offset,
            NULL) != 0 ||
        find_record_by_account_id(
            fd,
            destination_account_id,
            &destination_offset,
            NULL) != 0) {

        close(fd);

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_NOT_FOUND;
    }

    off_t first_offset =
        source_initial.account_id <
        destination_account_id
            ? source_offset
            : destination_offset;

    off_t second_offset =
        source_initial.account_id <
        destination_account_id
            ? destination_offset
            : source_offset;

    if (lock_account_record(
            fd,
            first_offset) == -1) {

        close(fd);

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_STORAGE_ERROR;
    }

    if (lock_account_record(
            fd,
            second_offset) == -1) {

        unlock_account_record(
            fd,
            first_offset
        );

        close(fd);

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_STORAGE_ERROR;
    }

    char source_old[
        ACCOUNT_RECORD_SIZE + 1
    ];

    char destination_old[
        ACCOUNT_RECORD_SIZE + 1
    ];

    if (pread(fd,
              source_old,
              ACCOUNT_RECORD_SIZE,
              source_offset) != ACCOUNT_RECORD_SIZE ||
        pread(fd,
              destination_old,
              ACCOUNT_RECORD_SIZE,
              destination_offset) != ACCOUNT_RECORD_SIZE) {

        unlock_account_record(fd,
                              second_offset);

        unlock_account_record(fd,
                              first_offset);

        close(fd);

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_STORAGE_ERROR;
    }

    source_old[
        ACCOUNT_RECORD_SIZE
    ] = '\0';

    destination_old[
        ACCOUNT_RECORD_SIZE
    ] = '\0';

    AccountRecord source;
    AccountRecord destination;

    if (parse_account_line(
            source_old,
            &source) != 0 ||
        parse_account_line(
            destination_old,
            &destination) != 0) {

        unlock_account_record(fd,
                              second_offset);

        unlock_account_record(fd,
                              first_offset);

        close(fd);

        if (second_mutex != first_mutex)
            pthread_mutex_unlock(second_mutex);

        pthread_mutex_unlock(first_mutex);

        return STATUS_STORAGE_ERROR;
    }

    if (source.user_id !=
        source_user_id) {

        status =
            STATUS_PERMISSION_DENIED;

        goto transfer_cleanup;
    }

    if (source.status != ACCOUNT_ACTIVE ||
        destination.status != ACCOUNT_ACTIVE) {

        status =
            STATUS_INACTIVE;

        goto transfer_cleanup;
    }

    if (amount >
        source.balance) {

        status =
            STATUS_INSUFFICIENT_FUNDS;

        goto transfer_cleanup;
    }

    if (amount >
        INT64_MAX -
            destination.balance) {

        status =
            STATUS_INVALID_INPUT;

        goto transfer_cleanup;
    }

    source.balance -= amount;
    destination.balance += amount;

    char source_new[
        ACCOUNT_RECORD_SIZE + 1
    ];

    char destination_new[
        ACCOUNT_RECORD_SIZE + 1
    ];

    if (format_account_record(
            &source,
            source_new,
            sizeof(source_new)) == -1 ||
        format_account_record(
            &destination,
            destination_new,
            sizeof(destination_new)) == -1) {

        status =
            STATUS_INTERNAL_ERROR;

        goto transfer_cleanup;
    }

    if (pwrite(fd,
               source_new,
               ACCOUNT_RECORD_SIZE,
               source_offset) != ACCOUNT_RECORD_SIZE ||
        pwrite(fd,
               destination_new,
               ACCOUNT_RECORD_SIZE,
               destination_offset) != ACCOUNT_RECORD_SIZE ||
        fsync(fd) == -1) {

        pwrite(fd,
               source_old,
               ACCOUNT_RECORD_SIZE,
               source_offset);

        pwrite(fd,
               destination_old,
               ACCOUNT_RECORD_SIZE,
               destination_offset);

        fsync(fd);

        status =
            STATUS_STORAGE_ERROR;

        goto transfer_cleanup;
    }

    TransactionId out_id;
    TransactionId in_id;
    TransactionId ref;

    status =
        append_transfer_transactions(
            source.account_id,
            destination.account_id,
            amount,
            source.balance,
            destination.balance,
            source_user_id,
            &ref,
            &out_id,
            &in_id
        );

    if (status != STATUS_OK) {

        pwrite(fd,
               source_old,
               ACCOUNT_RECORD_SIZE,
               source_offset);

        pwrite(fd,
               destination_old,
               ACCOUNT_RECORD_SIZE,
               destination_offset);

        fsync(fd);

        status =
            STATUS_STORAGE_ERROR;

        goto transfer_cleanup;
    }

    *updated_source =
        source;

    *updated_destination =
        destination;

    *reference_id =
        ref;

    status =
        STATUS_OK;

transfer_cleanup:

    unlock_account_record(
        fd,
        second_offset
    );

    unlock_account_record(
        fd,
        first_offset
    );

    close(fd);

    if (second_mutex != first_mutex)
        pthread_mutex_unlock(second_mutex);

    pthread_mutex_unlock(first_mutex);

    return status;
}
