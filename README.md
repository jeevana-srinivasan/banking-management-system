# Banking Management System

A multi-user **Banking Management System implemented in C on Linux** using TCP sockets, POSIX threads, file-based persistent storage, record locking, authentication, and role-based access control.

The project simulates core banking operations for Customers, Bank Employees, Managers, and Administrators while focusing on important System Software concepts such as:

- Client-server programming
- TCP sockets
- Multithreading
- Concurrent client handling
- File handling
- Record locking using `fcntl()`
- Thread synchronization using `pthread_mutex_t`
- Persistent storage
- Role-based access control
- Session management
- Password hashing
- Race-condition prevention
- Deadlock avoidance
- Transaction consistency

---

# 1. Project Overview

The system follows a client-server architecture.

```text
                    TCP / IPv4
              127.0.0.1 : 8080
                       │
        ┌──────────────┴──────────────┐
        │                             │
  Banking Client                Banking Server
        │                             │
        │                    Thread per client
        │                             │
        │                 ┌───────────┼───────────┐
        │                 │           │           │
        │              Users       Accounts    Loans
        │                 │           │           │
        │              Auth      Transactions  Feedback
        │                 │           │           │
        └─────────────────────────────┴───────────┘
                              │
                       Persistent files
```

The client provides the user interface and sends requests to the server.

All authentication, authorization, account modification, transaction processing, loan processing, session handling, storage access, and consistency checks are performed by the server.

The client never directly accesses or modifies storage files.

---

# 2. Technologies Used

- C
- Linux / Ubuntu
- POSIX APIs
- TCP sockets
- IPv4
- POSIX Threads (`pthread`)
- `fcntl()` file and record locking
- Linux `crypt()` password hashing
- File-based persistent storage
- GNU Make

---

# 3. Dependencies

The project is intended to run on Linux.

Install the required packages using:

```bash
sudo apt update
sudo apt install build-essential libcrypt-dev
```

The project requires:

```text
gcc
make
pthread
crypt
POSIX/Linux system calls
```

Check the compiler:

```bash
gcc --version
```

Check Make:

```bash
make --version
```

---

# 4. Project Structure

```text
banking-management-system/
│
├── client/
│   ├── client.c
│   ├── client_ui.c
│   └── client_ui.h
│
├── server/
│   ├── server.c
│   ├── server.h
│   ├── protocol.c
│   ├── protocol.h
│   ├── session.c
│   └── session.h
│
├── modules/
│   ├── auth.c
│   ├── auth.h
│   ├── users.c
│   ├── users.h
│   ├── accounts.c
│   ├── accounts.h
│   ├── transactions.c
│   ├── transactions.h
│   ├── loans.c
│   ├── loans.h
│   ├── feedback.c
│   ├── feedback.h
│   ├── storage.c
│   └── storage.h
│
├── include/
│   ├── common.h
│   └── config.h
│
├── storage/
│   ├── users.txt
│   ├── accounts.txt
│   ├── transactions.txt
│   ├── loans.txt
│   ├── feedback.txt
│   └── audit.log
│
├── docs/
│   ├── design.md
│   └── test_cases.md
│
├── Makefile
├── README.md
└── .gitignore
```

---

# 5. Compilation

From the project root:

```bash
make
```

This creates:

```text
banking_server
banking_client
```

To perform a clean rebuild:

```bash
make clean
make
```

The project links against:

```text
pthread
crypt
```

---

# 6. Running the Project

The current configuration uses the loopback address:

```text
127.0.0.1
```

and port:

```text
8080
```

Therefore, the server and clients are currently intended to run on the same machine or virtual machine.

## Start the server

Open Terminal 1:

```bash
./banking_server
```

## Start a client

Open Terminal 2:

```bash
./banking_client
```

Additional clients can be started in separate terminals:

```bash
./banking_client
```

Each connected client is handled by a separate server thread.

---

# 7. Client-Server Protocol

Communication uses persistent TCP connections.

Each message is prefixed with a 4-byte unsigned integer containing the payload length in network byte order.

```text
┌──────────────────┬────────────────────────────┐
│ 4-byte length    │ Message payload            │
└──────────────────┴────────────────────────────┘
```

Example application-level requests include:

```text
LOGIN|username|password
VIEW_BALANCE
DEPOSIT|50000
TRANSFER|50002|10000
APPLY_LOAN|10000000|12
ADD_EMPLOYEE|username|password|name
SET_USER_ACCESS|1001|DISABLE
```

The `|` character is used as the protocol field separator.

Therefore passwords are not allowed to contain:

```text
|
```

---

# 8. Server Architecture

The server follows a thread-per-client model.

```text
Main Server
    │
    ├── accept client 1
    │       └── worker thread 1
    │
    ├── accept client 2
    │       └── worker thread 2
    │
    ├── accept client 3
    │       └── worker thread 3
    │
    └── ...
```

Each worker thread maintains a persistent connection with one client until the client exits or disconnects.

---

# 9. Session Management

The system maintains an in-memory session table.

A session stores information such as:

```text
socket descriptor
user ID
role
active session status
```

The system enforces:

```text
One active session per user
```

Different users can be logged in concurrently.

However, the same user cannot maintain two simultaneous sessions.

If a client disconnects unexpectedly, the server removes the corresponding session so the user can log in again.

---

# 10. Authentication

Authentication data is stored in:

```text
storage/users.txt
```

Passwords are never stored in plaintext.

Passwords are hashed using Linux:

```text
crypt()
```

with SHA-512 style salts.

A user record contains:

```text
user_id
username
password_hash
role
active
full_name
```

Conceptual format:

```text
1001|alice|<password-hash>|1|1|Alice
```

The `active` field determines whether the user is allowed to access the system.

```text
active = 1 → login/system access allowed
active = 0 → login/system access denied
```

---

# 11. Password Rules

Passwords must:

- contain at least 8 characters
- not exceed `MAX_PASSWORD_LEN`
- not contain `|`
- may contain numbers
- may contain special characters
- may contain spaces

Examples:

```text
Bank@2026!      valid
hello123        valid
abc|12345       invalid
abc123          invalid
```

Password changes require the current password to be verified first.

---

# 12. Roles

The system supports four roles:

```text
1 → Customer
2 → Bank Employee
3 → Manager
4 → Administrator
```

Authorization is enforced on the server.

The client menu is only a user-interface layer and is not treated as the security boundary.

---

# 13. Customer Functionalities

The Customer menu provides:

```text
1. View Balance
2. Deposit
3. Withdraw
4. Transfer
5. Transaction History
6. Apply for Loan
7. View Loan Status
8. Change Password
9. Submit Feedback
10. Logout
0. Exit
```

## View Balance

Displays the current balance of the customer's active account.

## Deposit

Allows the customer to deposit money into their account.

Money entered in rupees is stored internally as integer paise.

Example:

```text
₹100.50
```

is stored as:

```text
10050 paise
```

Floating-point values are not used for account balances.

## Withdraw

Allows withdrawal if sufficient balance exists.

The balance check is performed while the account record is locked, preventing concurrent operations from producing an invalid negative balance.

## Transfer

Allows transfer from the customer's account to another active account.

A successful transfer creates:

```text
TRANSFER_OUT
TRANSFER_IN
```

transaction records with the same reference ID.

Both accounts are locked in deterministic Account ID order to avoid deadlocks.

## Transaction History

Displays persistent transaction history including:

- deposit
- withdrawal
- transfer-out
- transfer-in
- loan disbursement

Rejected loans do not create financial transaction records.

## Apply for Loan

The customer specifies:

```text
loan amount
loan tenure
```

Supported project-defined rates are:

```text
6 months   → 8.00% p.a.
12 months  → 8.50% p.a.
24 months  → 9.00% p.a.
36 months  → 9.50% p.a.
60 months  → 10.00% p.a.
```

The server determines the interest rate.

A newly-created loan begins with:

```text
PENDING
```

## View Loan Status

Possible statuses include:

```text
PENDING
ASSIGNED
APPROVED
REJECTED
```

## Change Password

The customer supplies:

```text
current password
new password
confirmation
```

The current password is verified before the password hash is replaced.

## Submit Feedback

Feedback is stored persistently in:

```text
storage/feedback.txt
```

---

# 14. Bank Employee Functionalities

The Employee menu provides:

```text
1. Add New Customer
2. View Customer Details
3. Modify Customer Details
4. Close Customer Account
5. View Customer Transactions
6. View Assigned Loan Applications
7. Approve / Reject Loan Application
8. Change Password
9. Logout
0. Exit
```

## Add New Customer

The Employee provides:

```text
full name
username
temporary password
initial deposit
```

The server:

1. allocates a Customer User ID
2. hashes the temporary password
3. creates the user
4. creates a bank account
5. records the initial deposit if non-zero

Duplicate usernames are rejected globally.

## View Customer Details

The Employee may search using:

- Account ID
- Customer User ID

Inactive and closed account details remain viewable.

## Modify Customer Details

The Employee can modify the customer's full name.

The Employee cannot directly change:

- User ID
- Username
- Role
- Account ID
- Balance
- Password
- Account activation status

## Close Customer Account

An Employee may permanently close a bank account only when:

```text
balance == ₹0
```

A closed account cannot be reopened.

## View Customer Transactions

Provides a passbook-style view of the customer's transaction history.

## View Assigned Loan Applications

Displays loans assigned to the currently logged-in Employee.

Employees cannot process loans assigned to another Employee.

## Approve / Reject Loan

Loan workflow:

```text
PENDING
   ↓ Manager assigns
ASSIGNED
   ↓ Employee decides
APPROVED / REJECTED
```

### Approval

On approval:

1. loan status becomes `APPROVED`
2. loan amount is credited to the customer's account
3. a `LOAN_DISBURSEMENT` transaction is created
4. balance increases exactly once

### Rejection

On rejection:

1. loan status becomes `REJECTED`
2. balance remains unchanged
3. no financial transaction is created

---

# 15. Manager Functionalities

The Manager menu provides:

```text
1. Activate Customer Account
2. Deactivate Customer Account
3. View Pending Loan Applications
4. Assign Loan to Employee
5. Review Customer Feedback
6. Change Password
7. Logout
0. Exit
```

## Activate / Deactivate Customer Account

The Manager controls temporary bank-account status:

```text
ACTIVE ⇄ INACTIVE
```

A permanently closed account cannot be activated or deactivated.

## View Pending Loan Applications

Displays loans whose status is:

```text
PENDING
```

After assignment, the loan is no longer shown in the pending list.

## Assign Loan to Employee

The Manager supplies:

```text
Loan ID
Employee User ID
```

The server verifies that:

- loan exists
- loan is still pending
- employee exists
- target user is an Employee
- employee login is active

Successful assignment changes:

```text
PENDING → ASSIGNED
```

## Review Customer Feedback

Displays feedback stored in:

```text
storage/feedback.txt
```

## Change Password

Manager can securely update their own password.

---

# 16. Administrator Functionalities

The Admin menu provides:

```text
1. Add New Bank Employee
2. View User Details
3. Modify Customer/Employee Details
4. Enable / Disable User Login
5. Change Password
6. Logout
0. Exit
```

## Add New Bank Employee

The Admin provides:

```text
employee name
username
temporary password
```

The server:

1. allocates an Employee User ID
2. hashes the password
3. creates an active Employee account

Duplicate usernames are rejected.

## View User Details

The Admin can view:

```text
User ID
Username
Full name
Role
Login status
```

## Modify Customer / Employee Details

The Admin can modify the full name of:

```text
CUSTOMER
EMPLOYEE
```

Manager and Admin records are protected from this operation.

## Enable / Disable User Login

The Admin controls whether a Customer or Employee may access the system.

```text
active = 1 → system access enabled
active = 0 → system access disabled
```

The user's role is not changed.

The user's historical records are not deleted.

This preserves references from:

```text
accounts.txt
transactions.txt
loans.txt
feedback.txt
```

For example:

```text
Customer 1001
active = 0
```

means the customer cannot log in, but their account, transaction history, loans, and feedback remain intact.

Similarly:

```text
Employee 2001
active = 0
```

means the Employee cannot log in or process banking operations, but historical loan-processing information remains valid.

### Immediate disable behavior

If a user is already logged in and the Admin disables them, the server checks their current `active` status before processing subsequent requests.

Therefore:

```text
Customer logs in
        ↓
Admin disables customer
        ↓
Customer sends next request
        ↓
Request denied
```

The user must be enabled again before accessing the system.

Manager and Admin accounts are protected from this operation.

An Admin cannot disable their own account using this option.

## Change Password

Admin can securely change their own password.

---

# 17. Separation of User Access and Bank Account Status

The system intentionally distinguishes three different concepts.

## User Login Status

Stored in:

```text
users.txt
```

Controlled by:

```text
Administrator
```

```text
active = 1 → system access allowed
active = 0 → system access denied
```

## Bank Account Status

Stored in:

```text
accounts.txt
```

Controlled temporarily by:

```text
Manager
```

```text
ACTIVE ⇄ INACTIVE
```

A Customer with an inactive bank account may still have a valid user identity, but banking operations are blocked.

## Permanent Bank Account Closure

Controlled by:

```text
Bank Employee
```

```text
ACTIVE/INACTIVE → CLOSED
```

`CLOSED` is permanent.

This creates a clear separation of responsibilities:

```text
Admin
→ controls whether the PERSON can access the system

Manager
→ controls whether the BANK ACCOUNT is temporarily usable

Employee
→ permanently closes a bank account
```

---

# 18. User IDs

Role-specific user ID ranges are used:

```text
Customers   → 1001–1999
Employees   → 2001–2999
Managers    → 3001–3999
Admins      → 4001–4999
```

New IDs are generated automatically.

---

# 19. Account IDs

Bank Account IDs begin around:

```text
50001
```

User ID and Account ID are different.

```text
User ID    → identifies login identity
Account ID → identifies bank account
```

---

# 20. Loan IDs

Loan IDs begin around:

```text
70001
```

and are generated automatically.

---

# 21. Feedback IDs

Feedback IDs begin around:

```text
80001
```

and are generated automatically.

---

# 22. Persistent Storage

The system uses text files for persistence.

## users.txt

Conceptual format:

```text
user_id|username|password_hash|role|active|full_name
```

## accounts.txt

Conceptual fields:

```text
account_id|user_id|balance|status|created_at
```

## transactions.txt

Stores:

```text
transaction_id
reference_id
account_id
transaction_type
amount
balance_after
details
timestamp
performed_by
```

## loans.txt

Current format:

```text
loan_id
|user_id
|account_id
|amount
|tenure
|interest
|status
|assigned_employee_id
|employee_recommendation
|employee_notes
|created_at
|updated_at
```

## feedback.txt

Format:

```text
feedback_id|user_id|message|timestamp
```

---

# 23. Fixed-Size Account Records

Accounts use fixed-size textual records to support reliable byte-range locking.

Each account record is:

```text
65 bytes
```

Example:

```text
0000050001|0000001001|00000000000000250000|1|2026-10-25 10:00:00
```

This allows in-place updates using:

```c
pwrite()
```

without shifting other records.

File integrity can be checked using:

```bash
stat -c %s storage/accounts.txt
```

The size should be divisible by:

```text
65
```

---

# 24. Money Representation

All monetary values are stored as integer paise using a 64-bit integer type.

Example:

```text
₹1234.56
```

is stored as:

```text
123456
```

This prevents floating-point rounding errors.

---

# 25. Concurrency Control

Concurrency is a major System Software component of the project.

The server uses both:

```text
pthread_mutex_t
```

and:

```text
fcntl()
```

locking.

## pthread mutexes

Used to synchronize threads inside the same server process.

This is necessary because traditional POSIX `fcntl()` locks are process-associated.

Multiple worker threads belong to the same process, so `fcntl()` alone is not sufficient for thread-level mutual exclusion.

## fcnt locking

Used to protect persistent file records.

Examples include:

- balance updates
- account closure
- account activation/deactivation
- loan updates
- user updates
- transaction writes

---

# 26. Account Locking Strategy

Account operations conceptually follow:

```text
pthread mutex
      ↓
locate account
      ↓
fcntl record lock
      ↓
re-read latest record
      ↓
validate
      ↓
modify
      ↓
pwrite
      ↓
fsync
      ↓
unlock
```

Checks such as insufficient balance are performed after locking.

---

# 27. Transfer Deadlock Prevention

Transfers modify two accounts.

To prevent deadlocks, account locks are always acquired in deterministic Account ID order.

Example:

```text
Alice → Bob
Bob → Alice
```

Both operations still lock the smaller Account ID first.

Therefore circular waiting is avoided.

---

# 28. Transaction Consistency

Transfers create paired records:

```text
TRANSFER_OUT
TRANSFER_IN
```

using the same reference ID.

Loan approval creates:

```text
LOAN_DISBURSEMENT
```

Rejected loans create no financial transaction.

If transaction logging fails after a balance update, the implementation performs a best-effort rollback.

---

# 29. File Rewrite Strategy

Files containing variable-length records, such as:

```text
users.txt
loans.txt
```

are modified using a temporary-file rewrite strategy.

```text
lock file
    ↓
read original
    ↓
write temporary file
    ↓
fsync
    ↓
rename
```

This avoids shifting or corrupting following records.

---

# 30. Duplicate Username Protection

Usernames are globally unique across all roles.

If a username already belongs to any existing user, another Customer or Employee cannot be created using the same username.

---

# 31. Server-Side Authorization

Authorization is enforced by the server.

For example:

```text
Customer → cannot add employees
Employee → cannot activate accounts
Manager → cannot add employees
Admin → cannot perform customer banking operations
```

Even if a modified client sends an unauthorized request manually, the server validates the current session role.

---

# 32. Concurrency Tests Performed

The system was tested using multiple clients connected to the same server.

Successful tests included:

## Different users logged in simultaneously

Multiple Customers, Employees, Managers, and Admins can connect concurrently.

## Duplicate session prevention

The same user cannot log in twice simultaneously.

## Concurrent operations on different accounts

Operations proceed correctly without corruption.

## Multiple transfers to the same destination

Destination balance is updated correctly without lost updates.

## Opposite-direction transfers

Example:

```text
Alice → Bob
Bob → Alice
```

Both complete without deadlock.

## Abrupt client disconnect

If a client terminates unexpectedly, its session is cleaned up and the user can log in again.

## Loan processing

A loan cannot be approved or rejected more than once.

Approved loans credit the account exactly once.

## Admin disables active user

A user who is already logged in loses access on their next request after being disabled.

Re-enabling the user restores login access without deleting any historical data.

---

# 33. Edge Cases Tested

The implementation handles cases such as:

- invalid username/password
- duplicate login
- duplicate username
- insufficient funds
- invalid monetary amount
- transfer to same account
- transfer to nonexistent account
- transfer to inactive/closed account
- closing non-zero-balance account
- reopening closed account
- assigning nonexistent loan
- assigning loan to non-employee
- assigning already-assigned loan
- processing loan assigned to another Employee
- approving/rejecting loan twice
- disabling nonexistent user
- disabling already-disabled user
- enabling already-enabled user
- disabling Manager/Admin account
- Admin attempting to disable self
- disabling an already logged-in Customer/Employee
- invalid password
- password containing protocol delimiter `|`

---

# 34. Build and Run Example

Terminal 1:

```bash
cd ~/Documents/banking-management-system
make
./banking_server
```

Terminal 2:

```bash
cd ~/Documents/banking-management-system
./banking_client
```

Terminal 3:

```bash
cd ~/Documents/banking-management-system
./banking_client
```

---

# 35. Cleaning Build Files

```bash
make clean
```

Then rebuild:

```bash
make
```

---

# 36. Inspecting Storage

During testing, storage files can be inspected using:

```bash
cat storage/users.txt
cat storage/accounts.txt
cat storage/transactions.txt
cat storage/loans.txt
cat storage/feedback.txt
```

These files should normally be modified only by the server.

---

# 37. Security Notes

This is an academic System Software project and not a production banking application.

Implemented security and consistency measures include:

- password hashing
- unique usernames
- one active session per user
- server-side authorization
- user login enable/disable control
- input validation
- record locking
- mutex synchronization
- concurrent-access protection
- deadlock avoidance
- no plaintext password storage

Not implemented:

- TLS encryption
- production-grade database
- multi-factor authentication
- certificate-based authentication
- distributed transaction processing
- enterprise-grade journaling

TCP communication itself is not encrypted.

---

# 38. Important Design Decisions

## Server owns storage

Clients never directly read or modify persistent files.

## User identity and bank account are separate

```text
User
→ authentication/system identity

Bank Account
→ financial state
```

Therefore:

```text
disabled user
```

does not mean:

```text
deleted bank account
```

and:

```text
inactive bank account
```

does not necessarily mean:

```text
disabled login
```

## Disabled users are preserved

Users are disabled instead of deleted because historical records may reference their User ID.

This preserves referential consistency across:

```text
accounts
transactions
loans
feedback
```

## Roles are stable identities

A Customer is not dynamically converted into an Employee.

Employees are created through the Admin workflow.

Customers are created through the Employee workflow.

This avoids ambiguous situations where one identity would simultaneously retain Customer banking data while functioning as an Employee.

## Money uses integer paise

Floating-point values are avoided for balances.

## Fixed-size account records

Used for byte-range locking and in-place modification.

## Variable-length records use file rewriting

Used where record size may change.

## Single active session per user

Prevents concurrent duplicate logins.

## Server-side authorization

Client menus are not relied upon for security.

---

# 39. Final Role Summary

| Feature | Customer | Employee | Manager | Admin |
|---|:---:|:---:|:---:|:---:|
| View own balance | ✅ | ❌ | ❌ | ❌ |
| Deposit | ✅ | ❌ | ❌ | ❌ |
| Withdraw | ✅ | ❌ | ❌ | ❌ |
| Transfer | ✅ | ❌ | ❌ | ❌ |
| View own transactions | ✅ | ❌ | ❌ | ❌ |
| Apply for loan | ✅ | ❌ | ❌ | ❌ |
| View own loan status | ✅ | ❌ | ❌ | ❌ |
| Submit feedback | ✅ | ❌ | ❌ | ❌ |
| Add customer | ❌ | ✅ | ❌ | ❌ |
| View customer details | ❌ | ✅ | ❌ | ❌ |
| Modify customer details | ❌ | ✅ | ❌ | ✅ |
| Close customer account | ❌ | ✅ | ❌ | ❌ |
| View customer transactions | ❌ | ✅ | ❌ | ❌ |
| View assigned loans | ❌ | ✅ | ❌ | ❌ |
| Approve/reject assigned loan | ❌ | ✅ | ❌ | ❌ |
| Activate/deactivate bank account | ❌ | ❌ | ✅ | ❌ |
| View pending loans | ❌ | ❌ | ✅ | ❌ |
| Assign loans | ❌ | ❌ | ✅ | ❌ |
| Review feedback | ❌ | ❌ | ✅ | ❌ |
| Add employee | ❌ | ❌ | ❌ | ✅ |
| View user details | ❌ | ❌ | ❌ | ✅ |
| Enable/disable user login | ❌ | ❌ | ❌ | ✅ |
| Change own password | ✅ | ✅ | ✅ | ✅ |
| Logout | ✅ | ✅ | ✅ | ✅ |
| Exit | ✅ | ✅ | ✅ | ✅ |

---

# 40. System Software Concepts Demonstrated

This project demonstrates practical use of:

```text
socket()
bind()
listen()
accept()
connect()
send()
recv()

pthread_create()
pthread_mutex_lock()
pthread_mutex_unlock()

open()
read()
write()
pread()
pwrite()
lseek()
fsync()
close()

fcntl()
F_SETLK
F_SETLKW
F_RDLCK
F_WRLCK
F_UNLCK
```

as well as:

- persistent TCP connections
- multithreaded servers
- thread synchronization
- file locking
- fixed-size record management
- persistent storage
- race-condition prevention
- deadlock avoidance
- role-based access control
- session management
- password hashing
- transaction consistency
- user access management

---

# 41. Conclusion

The Banking Management System provides a complete multi-role client-server banking simulation while demonstrating core Linux System Software concepts.

The completed system includes:

- persistent user/account/transaction/loan/feedback storage
- secure password hashing
- one-session-per-user authentication
- four distinct user roles
- Customer banking operations
- Employee customer and account management
- Manager account and loan management
- Admin employee and user-access management
- user enable/disable functionality
- loan assignment and approval
- automatic loan disbursement
- transaction history
- concurrent multi-client support
- mutex synchronization
- `fcntl()` locking
- deterministic locking for deadlock prevention
- abrupt-disconnect session cleanup

