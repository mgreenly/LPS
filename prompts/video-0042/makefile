# Makefile for building the client and server applications.
# Designed for production use, supporting cross-compilation.

# Compiler and flags
# Use 'gcc' by default. Can be overridden, e.g., 'make CC=clang'
CC = gcc

# CFLAGS: Compiler flags
# -std=c17: Adhere to the C17 standard.
# -Wall -Wextra -Wpedantic: Enable a comprehensive set of warnings for code quality.
# -Wshadow: Warn about shadowed variables.
# -O2: Optimization level for release builds.
# -g: Include debug symbols for production debugging (gdb, core dumps).
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Wshadow -O2 -g

# LDFLAGS: Linker flags. Not needed for these simple programs but good practice.
LDFLAGS =

# The final executable targets.
TARGETS = server client

# The default target, 'all', builds all specified targets.
# .PHONY ensures this rule runs even if a file named 'all' exists.
.PHONY: all
all: $(TARGETS)

# Generic pattern rule to build any executable from its .c file.
# '$@' is an automatic variable for the target name (e.g., 'server').
# '$<' is an automatic variable for the first prerequisite (e.g., 'server.c').
# This is more scalable than writing a separate rule for each target.
%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# The 'clean' target removes all built executables.
# .PHONY ensures this rule runs even if a file named 'clean' exists.
# The '-f' flag to rm suppresses errors if the files don't exist.
.PHONY: clean
clean:
	rm -f $(TARGETS)