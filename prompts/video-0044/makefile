# Compiler and flags
CC = gcc
# CFLAGS for C17 standard, extensive warnings, debugging symbols, and POSIX feature test macros.
# These flags are suitable for both x86_64 and AARCH64 on Linux.
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g

# Targets
TARGETS = server client
# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c
# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

.PHONY: all clean

# Default target
all: $(TARGETS)

# Rule to link the server executable
server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to link the client executable
client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule to compile .c files into .o files.
# Depends on the source file and the shared message.h header.
%.o: %.c message.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean up build artifacts
clean:
	rm -f $(TARGETS) $(SERVER_OBJ) $(CLIENT_OBJ)