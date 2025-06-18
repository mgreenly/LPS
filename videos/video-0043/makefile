# Compiler and flags
CC = gcc
# CFLAGS for production: C17 standard, all warnings, pedantic, optimized, with debug symbols.
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2 -g
# LDFLAGS for linking libraries, if any were needed.
LDFLAGS =

# Target executables
TARGETS = server client

# Phony targets
.PHONY: all clean

# Default rule
all: $(TARGETS)

# Rule to build the server
server: server.c message.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Rule to build the client
client: client.c message.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Rule to clean up build artifacts
clean:
	rm -f $(TARGETS) *.o