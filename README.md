# Banking Management System

A modular banking management simulation implemented in C on Linux.

## Features

- Role-based access for customers, bank employees, managers
  and administrators
- TCP client-server communication
- Account management and financial transactions
- Loan application, assignment and decision processing
- Feedback and transaction history
- File locking and thread synchronization
- Transaction journaling and recovery design

## Technology

- C (C11)
- Linux/POSIX system calls
- TCP sockets
- POSIX threads
- File locking
- Local text-file persistence
- OpenSSL cryptographic functions

## Currency

All user-facing amounts are expressed in Indian rupees (INR).
Internally, monetary values are represented as integer paise
to avoid floating-point rounding errors.

## Project Structure

- `client/`: client interface
- `server/`: server, session management and protocol
- `modules/`: authentication, accounts, transactions, loans,
  feedback, users and storage
- `include/`: shared configuration and definitions
- `storage/`: local runtime data, excluded from Git
- `docs/`: design and testing documentation
- `tests/`: test scripts

## Build

The project is being implemented incrementally. Once the
required source modules are complete, use:

```bash
make
```

Run the server and client in separate terminals after the
networking milestone is implemented.

## Disclaimer

This is an educational System Software project. It is not
intended for real banking operations or real customer data.

