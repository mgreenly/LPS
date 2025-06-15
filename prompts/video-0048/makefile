# Compiler and flags
CC = gcc
# CFLAGS for C17 standard, all warnings, errors on warnings, debug symbols, and optimization.
# Also define feature test macros for POSIX.1-2008 and default sources.
CFLAGS = -std=c17 -Wall -Wextra -Werror -g -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# For debug builds with extra logging, use: make CFLAGS+=-DDEBUG
# e.g. `make clean all CFLAGS+=-DDEBUG`

# Executables
TARGETS = server client

# Default target
all: $(TARGETS)

# Rule to build the server
server: server.c message.h
	$(CC) $(CFLAGS) -o server server.c

# Rule to build the client
client: client.c message.h
	$(CC) $(CFLAGS) -o client client.c

# Clean up build artifacts
clean:
	rm -f $(TARGETS)

.PHONY: all clean