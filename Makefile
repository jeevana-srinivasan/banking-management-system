CC = gcc

CPPFLAGS = -Iinclude -Iserver -Imodules -Iclient
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -pthread
LDLIBS = -pthread -lcrypto

SERVER_SOURCES = \
	server/server.c \
	server/session.c \
	server/protocol.c \
	modules/auth.c \
	modules/users.c \
	modules/accounts.c \
	modules/transactions.c \
	modules/loans.c \
	modules/feedback.c \
	modules/storage.c

CLIENT_SOURCES = \
	client/client.c \
	client/client_ui.c \
	server/protocol.c

.PHONY: all clean

all: banking_server banking_client

banking_server: $(SERVER_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SERVER_SOURCES) $(LDLIBS)

banking_client: $(CLIENT_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CLIENT_SOURCES) $(LDLIBS)

clean:
	rm -f banking_server banking_client *.o

