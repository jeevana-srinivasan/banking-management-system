# Banking Management System — Design

## 1. Platform and architecture

- Language: C
- Operating system: Linux
- Networking: TCP sockets
- Server concurrency: pthreads
- Synchronization: pthread mutexes and fcntl record locks
- Persistence: local text files
- Monetary representation: integer paise
- User-facing currency: Indian rupees (INR)

The client sends requests to the server. The server authenticates
the user, checks permissions, validates the operation and accesses
the storage layer. Clients never modify storage files directly.

## 2. Roles

1. Customer
2. Bank Employee
3. Manager
4. Administrator

The server determines a user's role from the authenticated user
record. A role selected in the client menu is not proof of identity
or authorization.

## 3. Data-file conventions

- One record per line.
- Pipe-delimited fields.
- User-provided fields must be escaped or rejected if they contain
  unsupported delimiters or newline characters.
- IDs must be unique and positive.
- Monetary amounts are stored as integer paise.
- Timestamps use a documented, consistent format.
- Records are validated when read; malformed records must not be
  silently interpreted as valid banking data.

## 4. Files

- users.txt: user identity, role, account access and password hash
- accounts.txt: owner, balance and account status
- transactions.txt: financial transaction records
- loans.txt: loan applications, assignment and decision history
- feedback.txt: feedback and review state
- journal.txt: recoverable transaction operations
- audit.log: security and administrative events

## 5. Core security rules

- Passwords are not stored in plaintext.
- Authentication and authorization occur on the server.
- A user may have at most one active session.
- Inactive users cannot authenticate.
- Customers may only access their own accounts.
- Employees may process only loans assigned to them.
- Administrative operations require explicit role permission.
- Session state is invalidated on logout or disconnection.

## 6. Core financial rules

- Amounts must be positive and within configured limits.
- Withdrawals cannot exceed available funds.
- Both accounts in a transfer must exist and be eligible.
- A transfer cannot target the same account as its source.
- Transfers must debit and credit as one logical operation.
- Duplicate loan disbursements are prohibited.
- Closed accounts cannot be used for new transactions.
- Failed operations must not leave partial financial updates.

## 7. Concurrency and recovery

- A mutex protects shared in-process state.
- File locks coordinate access to shared records across processes.
- A single server instance owns the storage directory.
- Multi-record transactions use a journal and defined commit protocol.
- Committed data is synchronized to disk.
- Startup recovery examines incomplete journal entries.

