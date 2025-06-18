# Makefile for the epoll server and client

# Compiler and flags
CC = gcc
# CFLAGS for C17 standard, all warnings, optimizations, and feature-test macros.
# _POSIX_C_SOURCE=200809L enables POSIX.1-2008 features (e.g., epoll, sigaction).
# _DEFAULT_SOURCE enables additional features like nanosleep on glibc.
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -g \
         -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

# Linker flags (not strictly needed here, but good practice)
LDFLAGS =

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Executable names
SERVER_EXE = server
CLIENT_EXE = client

# Graphviz dot file for the server's flow
DOT_FILE = server_flow.dot
PNG_FILE = server_flow.png

.PHONY: all clean png

# Default target
all: $(SERVER_EXE) $(CLIENT_EXE)

# Rule to build the server executable
$(SERVER_EXE): $(SERVER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Rule to build the client executable
$(CLIENT_EXE): $(CLIENT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Generic rule to compile .c files to .o files
# Depends on message.h as it's the shared contract
%.o: %.c message.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Rule to generate PNG from DOT file
png: $(PNG_FILE)

$(PNG_FILE): $(DOT_FILE)
	@echo "Generating PNG from DOT file..."
	@dot -Tpng $(DOT_FILE) -o $(PNG_FILE)
	@echo "Created $(PNG_FILE). Open it to see the server's logical flow."

# Target to clean up the directory
clean:
	@echo "Cleaning up..."
	rm -f $(SERVER_EXE) $(CLIENT_EXE) $(SERVER_OBJ) $(CLIENT_OBJ) $(PNG_FILE)