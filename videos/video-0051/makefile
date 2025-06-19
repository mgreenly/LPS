# Makefile for the epoll server and client project

# Compiler and flags
CC = gcc
# CFLAGS:
# -std=c18: Use the C18 standard.
# -Wall -Wextra -pedantic: Enable a wide range of warnings for robust code.
# -g: Include debugging information.
# -D_DEFAULT_SOURCE: Use modern POSIX features, as requested.
# -DLOG_LEVEL=2: Set log level (0=NONE, 1=ERROR, 2=INFO, 3=DEBUG)
CFLAGS = -std=c18 -Wall -Wextra -g -D_DEFAULT_SOURCE -DLOG_LEVEL=2
# LDFLAGS: Linker flags
LDFLAGS_SERVER = -lpthread

# Source and object files
# Common headers are dependencies for almost everything
COMMON_HDRS = common/message.h common/log.h

# Server files
SERVER_SRCS = server/server.c server/network.c server/queue.c server/worker.c server/signal.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_EXEC = bin/server

# Client files
CLIENT_SRCS = client/client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_EXEC = bin/client

# Phony targets
.PHONY: all clean docs server client

# Default target: build everything
all: server client docs

# Target to build the server
server: $(SERVER_EXEC)

# Target to build the client
client: $(CLIENT_EXEC)

# Linking the server executable
$(SERVER_EXEC): $(SERVER_OBJS)
	@echo "==> Linking $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_SERVER)

# Linking the client executable
$(CLIENT_EXEC): $(CLIENT_OBJS)
	@echo "==> Linking $@"
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule to compile .c files to .o files
# This rule applies to both server and client source files.
%.o: %.c $(COMMON_HDRS)
	@echo "==> Compiling $<"
	$(CC) $(CFLAGS) -I. -Icommon/ -c -o $@ $<

# Target to generate documentation
# Requires graphviz to be installed (sudo apt-get install graphviz)
docs: flow.png

flow.png: flow.dot
	@echo "==> Generating flow diagram from .dot file"
	dot -Tpng flow.dot -o flow.png

# Target to clean up the project directory
clean:
	@echo "==> Cleaning up build artifacts"
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC)
	rm -f server/*.o client/*.o
	rm -f flow.png
