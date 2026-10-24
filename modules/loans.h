#ifndef LOANS_H
#define LOANS_H

#include "common.h"
#include "config.h"

typedef struct {
    LoanId loan_id;

    UserId user_id;
    AccountId account_id;

    Money amount;

    int tenure_months;

    /*
     * Interest rate stored in basis points.
     *
     * Example:
     * 9.00% = 900
     * 9.50% = 950
     */
    int interest_basis_points;

    LoanStatus status;

    UserId assigned_employee_id;

    /*
     * 0  = no recommendation yet
     * 1  = recommend approval
     * 2  = recommend rejection
     */
    int employee_recommendation;

    char employee_notes[MAX_REASON_LEN + 1];

    UserId decision_by;

    char decision_reason[MAX_REASON_LEN + 1];

    char created_at[32];
    char updated_at[32];

} LoanRecord;

/*
 * Create a new pending loan application
 * for the authenticated customer.
 */
OperationStatus apply_for_loan(
    UserId user_id,
    Money amount,
    int tenure_months,
    LoanRecord *created_loan
);

OperationStatus get_customer_loans(
    UserId user_id,
    char *output,
    size_t output_size
);

const char *loan_status_name(
    LoanStatus status
);

OperationStatus get_assigned_loans(
    UserId employee_id,
    char *output,
    size_t output_size
);

OperationStatus decide_assigned_loan(
    LoanId loan_id,
    UserId employee_id,
    int approve,
    LoanRecord *updated_loan
);

#endif
