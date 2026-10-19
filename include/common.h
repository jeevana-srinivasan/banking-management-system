#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* User roles */
typedef enum {
    ROLE_CUSTOMER = 1,
    ROLE_EMPLOYEE = 2,
    ROLE_MANAGER = 3,
    ROLE_ADMIN = 4
} UserRole;

/* Account lifecycle */
typedef enum {
    ACCOUNT_ACTIVE = 1,
    ACCOUNT_INACTIVE = 2,
    ACCOUNT_CLOSED = 3
} AccountStatus;

/* Loan lifecycle */
typedef enum {
    LOAN_PENDING = 1,
    LOAN_ASSIGNED = 2,
    LOAN_UNDER_REVIEW = 3,
    LOAN_APPROVED = 4,
    LOAN_REJECTED = 5,
    LOAN_DISBURSED = 6,
    LOAN_CANCELLED = 7
} LoanStatus;

/* Financial transaction types */
typedef enum {
    TXN_DEPOSIT = 1,
    TXN_WITHDRAWAL = 2,
    TXN_TRANSFER_OUT = 3,
    TXN_TRANSFER_IN = 4,
    TXN_LOAN_DISBURSEMENT = 5
} TransactionType;

/* Common operation results */
typedef enum {
    STATUS_OK = 0,
    STATUS_INVALID_INPUT,
    STATUS_NOT_FOUND,
    STATUS_PERMISSION_DENIED,
    STATUS_INACTIVE,
    STATUS_INSUFFICIENT_FUNDS,
    STATUS_DUPLICATE,
    STATUS_CONFLICT,
    STATUS_STORAGE_ERROR,
    STATUS_INTERNAL_ERROR
} OperationStatus;

/* Money is always represented in integer paise. */
typedef int64_t Money;

/* Identifiers */
typedef int64_t UserId;
typedef int64_t AccountId;
typedef int64_t TransactionId;
typedef int64_t LoanId;
typedef int64_t FeedbackId;

#endif

