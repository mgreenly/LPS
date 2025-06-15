# Makefile for the epoll server and client
# Targets: x86_64-linux-gnu and aarch64-linux-gnu

# Compiler and Flags
CC = gcc
# CFLAGS for C17 standard, extensive warnings, optimization, and production-ready definitions.
# _POSIX_C_SOURCE and _DEFAULT_SOURCE are defined here, not in the code.
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -g \
         -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Linker flags (none needed for this simple project)
LDFLAGS =

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Executables
SERVER_BIN = server
CLIENT_BIN = client

# Default target
all: $(SERVER_BIN) $(CLIENT_BIN)

# Rule to build the server
$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Rule to build the client
$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Generic rule to compile .c to .o files
%.o: %.c message.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean up build artifacts
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(SERVER_OBJ) $(CLIENT_OBJ)

# Phony targets
.PHONY: all clean