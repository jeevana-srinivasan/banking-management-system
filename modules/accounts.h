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

OperationStatus open_customer_account(
    UserId customer_user_id,
    Money initial_balance,
    UserId performed_by,
    AccountRecord *created_account
);

OperationStatus find_account_by_user_any_status(
    UserId user_id,
    AccountRecord *account
);

OperationStatus close_customer_account(
    AccountId account_id,
    AccountRecord *closed_account
);

OperationStatus set_account_status(
    AccountId account_id,
    AccountStatus new_status,
    AccountRecord *updated_account
);

OperationStatus disburse_loan_amount(
    AccountId account_id,
    Money amount,
    UserId performed_by,
    LoanId loan_id,
    Money *new_balance
);

#endif
