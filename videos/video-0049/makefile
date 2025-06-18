# Makefile for the epoll server and client

# Compiler and flags
CC = gcc
# CFLAGS for C17 standard, all warnings, debug symbols, and feature test macros.
# _POSIX_C_SOURCE=200809L enables POSIX.1-2008 functions (e.g., nanosleep, strndup).
# _DEFAULT_SOURCE enables additional features on glibc systems (e.g., strndup).
CFLAGS = -std=c17 -Wall -Wextra -pedantic -g \
         -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Linker flags (if any)
LDFLAGS =

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Executable names
SERVER_EXE = server
CLIENT_EXE = client

# All target to build both executables
all: $(SERVER_EXE) $(CLIENT_EXE)

# Rule to build the server
$(SERVER_EXE): $(SERVER_SRC) message.h
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_EXE) $(LDFLAGS)

# Rule to build the client
$(CLIENT_EXE): $(CLIENT_SRC) message.h
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_EXE) $(LDFLAGS)

# Clean up build artifacts
clean:
	rm -f $(SERVER_EXE) $(CLIENT_EXE) *.o

# Phony targets
.PHONY: all clean