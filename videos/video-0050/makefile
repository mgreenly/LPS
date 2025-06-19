# Makefile

# -- Variables --
# Compiler and flags
CC = gcc
# CFLAGS:
# -std=c18: Use the 2018 C standard.
# -Wall -Wextra -Wpedantic: Enable a wide range of warnings for code quality.
# -g: Include debug symbols for use with gdb.
# -D_DEFAULT_SOURCE: Use modern POSIX feature test macro.
CFLAGS = -std=c18 -Wall -Wextra -g -Wpedantic -D_DEFAULT_SOURCE

# LDFLAGS: Linker flags.
# -lpthread: Link against the POSIX threads library (for the server).
LDFLAGS_SERVER = -lpthread

# Executable names
TARGETS = server client

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Object files (inferred from source)
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

# Header dependencies. If message.h changes, sources will recompile.
DEPS = message.h

# -- Targets --

# Default target: build all executables.
all: $(TARGETS)

# Rule to link the server executable.
# Depends on its object file.
server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_SERVER)

# Rule to link the client executable.
# Depends on its object file.
client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Pattern rule to compile .c files into .o files.
# Any .o file depends on its corresponding .c file and the shared headers.
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Target to generate the PNG diagram from the DOT file.
# Requires graphviz to be installed (`sudo apt-get install graphviz`).
graph: flow.png

flow.png: flow.dot
	@echo "Generating flow diagram..."
	dot -Tpng flow.dot -o flow.png

# Phony target for cleaning up build artifacts.
# .PHONY prevents make from confusing this with a file named "clean".
.PHONY: clean
clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(TARGETS) *.o flow.png
