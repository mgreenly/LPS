#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "network.h"
#include "common/log.h"

#define MAX_BODY_LEN (1024 * 1024) // 1 MB limit

static void make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        log_error("fcntl F_GETFL failed");
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1) {
        log_error("fcntl F_SETFL failed");
    }
}

int create_and_bind_socket(uint16_t port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        log_error("socket() failed");
        return -1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on localhost
    serv_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        log_error("bind() failed");
        close(listen_fd);
        return -1;
    }

    make_socket_non_blocking(listen_fd);

    if (listen(listen_fd, SOMAXCONN) == -1) {
        log_error("listen() failed");
        close(listen_fd);
        return -1;
    }

    log_info("Server listening on port %u", port);
    return listen_fd;
}

void handle_new_connection(int epoll_fd, int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (conn_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // This can happen due to the non-blocking nature, not an error.
        } else {
            log_error("accept() failed");
        }
        return;
    }

    make_socket_non_blocking(conn_fd);

    client_context_t* ctx = calloc(1, sizeof(client_context_t));
    if (!ctx) {
        log_error("Failed to allocate memory for new client context");
        close(conn_fd);
        return;
    }
    ctx->fd = conn_fd;

    struct epoll_event event;
    event.data.ptr = ctx;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event) == -1) {
        log_error("epoll_ctl ADD failed for fd %d", conn_fd);
        free(ctx);
        close(conn_fd);
        return;
    }
    log_info("Accepted new connection on fd %d", conn_fd);
}

void cleanup_client(client_context_t* ctx, int epoll_fd) {
    assert(ctx != NULL);
    log_info("Cleaning up client fd %d", ctx->fd);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ctx->fd, NULL);
    close(ctx->fd);
    if (ctx->body_buf) {
        free(ctx->body_buf);
    }
    free(ctx);
}

static void rearm_epoll(int epoll_fd, client_context_t* ctx) {
    struct epoll_event event;
    event.data.ptr = ctx;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ctx->fd, &event);
}

// Handles the logic for reading data for a single client
void handle_client_data(client_context_t* ctx, queue_t* queue, int epoll_fd) {
    assert(ctx != NULL && queue != NULL);
    int done = 0;

    while (!done) {
        ssize_t count;
        if (!ctx->header_received) {
            // Reading header
            size_t to_read = sizeof(message_header_t) - ctx->bytes_read;
            count = read(ctx->fd, ctx->header_buf + ctx->bytes_read, to_read);

            if (count > 0) {
                ctx->bytes_read += count;
                if (ctx->bytes_read == sizeof(message_header_t)) {
                    // Full header received
                    memcpy(&ctx->header, ctx->header_buf, sizeof(message_header_t));

                    if (ctx->header.length > MAX_BODY_LEN) {
                        log_error("Client fd %d sent message body too large: %u", ctx->fd, ctx->header.length);
                        cleanup_client(ctx, epoll_fd);
                        return;
                    }

                    ctx->header_received = 1;
                    ctx->bytes_read = 0; // Reset for body
                    if (ctx->header.length > 0) {
                        ctx->body_buf = malloc(ctx->header.length);
                        if (!ctx->body_buf) {
                           log_error("Failed to allocate body buffer for fd %d", ctx->fd);
                           cleanup_client(ctx, epoll_fd);
                           return;
                        }
                    }
                }
            }
        } else {
            // Reading body
            size_t to_read = ctx->header.length - ctx->bytes_read;
            count = read(ctx->fd, ctx->body_buf + ctx->bytes_read, to_read);

            if (count > 0) {
                ctx->bytes_read += count;
            }
        }

        if (count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                log_error("read() error on fd %d", ctx->fd);
                cleanup_client(ctx, epoll_fd);
            }
            done = 1; // Stop reading for now
        } else if (count == 0) {
            // Client disconnected
            cleanup_client(ctx, epoll_fd);
            done = 1;
        } else if (ctx->header_received && ctx->bytes_read == ctx->header.length) {
            // Full message received
            message_t* msg = malloc(sizeof(message_t));
            if (!msg) {
                log_error("Failed to allocate memory for message object");
                cleanup_client(ctx, epoll_fd);
                return;
            }
            msg->client_fd = ctx->fd;
            msg->header = ctx->header;
            msg->body = ctx->body_buf;

            log_debug("Full message received from fd %d, pushing to queue.", ctx->fd);
            queue_push(queue, msg);

            // Reset context for the next message from this client
            ctx->header_received = 0;
            ctx->bytes_read = 0;
            ctx->body_buf = NULL; // Ownership transferred to message_t
        }
    }
    // With EPOLLET, we must re-arm the event if we are not closing the fd
    rearm_epoll(epoll_fd, ctx);
}
