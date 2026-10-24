#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include "common.h"

#define ACCOUNT_RECORD_SIZE 65

typedef struct {
    AccountId account_id;
    UserId user_id;

    Money balance;

    AccountStatus status;

    char created_at[32];
} AccountRecord;

OperationStatus find_account_by_id(
    AccountId account_id,
    AccountRecord *account
);

OperationStatus find_account_by_user(
    UserId user_id,
    AccountRecord *account
);

OperationStatus deposit_to_user_account(
    UserId user_id,
    Money amount,
    AccountRecord *updated_account,
    TransactionId *transaction_id
);

OperationStatus withdraw_from_user_account(
    UserId user_id,
    Money amount,
    AccountRecord *updated_account,
    TransactionId *transaction_id
);

OperationStatus transfer_from_user_account(
    UserId source_user_id,
    AccountId destination_account_id,
    Money amount,
    AccountRecord *updated_source,
    AccountRecord *updated_destination,
    TransactionId *reference_id
);

#endif
