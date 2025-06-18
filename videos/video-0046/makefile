# Compiler and flags
CC = gcc
# C17 standard, enable warnings, add debug symbols, and define feature test macros.
# _POSIX_C_SOURCE and _DEFAULT_SOURCE are defined here, not in source files.
# -DDEBUG enables the DEBUG_LOG macros.
CFLAGS = -std=c17 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DDEBUG
LDFLAGS =

# Source and object files
SERVER_SRC = server.c
SERVER_OBJ = $(SERVER_SRC:.c=.o)
SERVER_BIN = server

CLIENT_SRC = client.c
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
CLIENT_BIN = client

# Targets
all: $(SERVER_BIN) $(CLIENT_BIN)

# Rule to link the server executable
$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Rule to link the client executable
$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Generic rule to compile .c to .o
# The object files depend on the shared message.h header
%.o: %.c message.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Phony targets
.PHONY: all clean

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(SERVER_OBJ) $(CLIENT_OBJ)