#ifndef TRANSACTIONS_H
#define TRANSACTIONS_H

#include "common.h"
#include "config.h"
#include <stddef.h>

typedef struct {
    TransactionId transaction_id;
    TransactionId reference_id;

    AccountId account_id;

    TransactionType type;

    Money amount;
    Money balance_after;

    char details[MAX_DETAILS_LEN + 1];
    char timestamp[32];

    UserId performed_by;
} TransactionRecord;

OperationStatus append_transaction(
    AccountId account_id,
    TransactionType type,
    Money amount,
    Money balance_after,
    const char *details,
    UserId performed_by,
    TransactionId reference_id,
    TransactionId *created_transaction_id
);

/*
 * Append the matching TRANSFER_OUT and TRANSFER_IN
 * records as one protected transaction-log operation.
 */
OperationStatus append_transfer_transactions(
    AccountId source_account_id,
    AccountId destination_account_id,
    Money amount,
    Money source_balance_after,
    Money destination_balance_after,
    UserId performed_by,
    TransactionId *reference_id,
    TransactionId *out_transaction_id,
    TransactionId *in_transaction_id
);

OperationStatus get_transaction_history(
    AccountId account_id,
    char *output,
    size_t output_size
);

OperationStatus get_account_transaction_history(
    AccountId account_id,
    char *output,
    size_t output_size
);

#endif
