# Makefile for the hello-world program.
# Designed for portability and production use.

# Compiler and Target
# Using variables allows for easy override, e.g., 'make CC=clang'
CC = gcc
TARGET = hello-world

# Source and Object files
# This pattern allows for easy extension with more source files.
SRCS = hello-world.c
OBJS = $(SRCS:.c=.o)

# Compiler Flags (CFLAGS)
# -std=c17: Enforce the C17 standard.
# -Wall -Wextra -Wpedantic: Enable a comprehensive set of warnings.
# -Werror: Treat all warnings as errors to maintain code quality.
# -O2: Standard optimization level for release builds.
# -g: Include debug symbols for post-mortem analysis (e.g., gdb on a core dump).
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -g

# Linker Flags (LDFLAGS) - not needed for this simple case but good practice
LDFLAGS =

# Default target: 'all'
# This is the first target, so it runs when 'make' is invoked without arguments.
.PHONY: all
all: $(TARGET)

# Linking step: Create the final executable from object files.
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

# Compilation step: Create object files from source files.
# This is a pattern rule that applies to all .c files.
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target: Remove all generated build artifacts.
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)

# Install target: Copy the binary to a standard location.
.PHONY: install
INSTALL_DIR = /usr/local/bin
install: $(TARGET)
	install -m 755 $(TARGET) $(INSTALL_DIR)

# Uninstall target: Remove the installed binary.
.PHONY: uninstall
uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET)