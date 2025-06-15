#define _GNU_SOURCE // For strcasestr, if needed, though not used here.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>

#include "message.h"

#define MAX_EVENTS 128
#define PORT 4242
#define READ_BUFFER_SIZE 4096

// --- Utility Functions ---

// Sets a file descriptor to non-blocking mode.
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}

// Reads exactly 'count' bytes from a file descriptor into 'buf'.
// Handles short reads. Returns 0 on success, -1 on EOF, or -2 on error.
int read_full(int fd, void *buf, size_t count) {
    ssize_t bytes_read;
    size_t total_bytes_read = 0;
    char *ptr = buf;

    while (total_bytes_read < count) {
        bytes_read = read(fd, ptr + total_bytes_read, count - total_bytes_read);
        if (bytes_read == 0) { // EOF
            return -1;
        }
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // This shouldn't happen in a blocking context, but good practice.
                // For non-blocking, this indicates we need to wait.
                // In this server, we only call this function when data is expected.
                // However, we handle it defensively.
                return -2;
            }
            perror("read");
            return -2;
        }
        total_bytes_read += bytes_read;
    }
    return 0;
}

// Writes exactly 'count' bytes to a file descriptor from 'buf'.
// Handles short writes. Returns 0 on success, -1 on error.
int write_full(int fd, const void *buf, size_t count) {
    ssize_t bytes_written;
    size_t total_bytes_written = 0;
    const char *ptr = buf;

    while (total_bytes_written < count) {
        bytes_written = write(fd, ptr + total_bytes_written, count - total_bytes_written);
        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // The write buffer is full, we should wait for EPOLLOUT.
                // For this simple server, we'll treat it as an error.
                fprintf(stderr, "Write would block, closing connection\n");
                return -1;
            }
            perror("write");
            return -1;
        }
        total_bytes_written += bytes_written;
    }
    return 0;
}


// --- Message Handlers ---

void handle_echo(int client_fd, const MessageHeader *header, const char *body) {
    printf("  -> Echoing %" PRIu32 " bytes back to fd %d\n", header->message_len, client_fd);
    if (write_full(client_fd, header, sizeof(MessageHeader)) != 0) return;
    if (write_full(client_fd, body, header->message_len) != 0) return;
}

void handle_reverse(int client_fd, const MessageHeader *header, const char *body) {
    printf("  -> Reversing string for fd %d\n", client_fd);
    char *reversed_body = malloc(header->message_len);
    if (!reversed_body) {
        perror("malloc");
        return; // Can't respond, will likely lead to client timeout
    }

    uint32_t len = header->message_len - 1; // Exclude null terminator
    for (uint32_t i = 0; i < len; ++i) {
        reversed_body[i] = body[len - 1 - i];
    }
    reversed_body[len] = '\0';
    
    if (write_full(client_fd, header, sizeof(MessageHeader)) != 0) {
        free(reversed_body);
        return;
    }
    if (write_full(client_fd, reversed_body, header->message_len) != 0) {
        free(reversed_body);
        return;
    }

    free(reversed_body);
}

void handle_time(int client_fd) {
    printf("  -> Sending time to fd %d\n", client_fd);
    char time_buf[128];
    time_t now = time(NULL);
    struct tm gmt;
    
    // Use gmtime_r for thread-safety (good practice)
    gmtime_r(&now, &gmt);
    size_t len = strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    time_buf[len] = '\0'; // strftime might not null-terminate if buffer is full

    MessageHeader response_header = {
        .type = MSG_TIME,
        .message_len = (uint32_t)(strlen(time_buf) + 1)
    };

    if (write_full(client_fd, &response_header, sizeof(MessageHeader)) != 0) return;
    if (write_full(client_fd, time_buf, response_header.message_len) != 0) return;
}

void process_message(int client_fd) {
    MessageHeader header;

    // 1. Read the fixed-size header
    int read_status = read_full(client_fd, &header, sizeof(MessageHeader));
    if (read_status == -1) { // EOF
        printf("Client fd %d disconnected while reading header.\n", client_fd);
        close(client_fd);
        return;
    } else if (read_status == -2) { // Error
        fprintf(stderr, "Error reading header from fd %d.\n", client_fd);
        close(client_fd);
        return;
    }

    // Sanity check message length
    if (header.message_len > READ_BUFFER_SIZE) {
        fprintf(stderr, "Client fd %d sent oversized message (%" PRIu32 " bytes), closing.\n", client_fd, header.message_len);
        close(client_fd);
        return;
    }
    if (header.message_len == 0 && header.type != MSG_TIME) {
         fprintf(stderr, "Client fd %d sent zero-length message for non-TIME type, closing.\n", client_fd);
        close(client_fd);
        return;
    }

    // 2. Read the variable-size body
    char *body = NULL;
    if (header.message_len > 0) {
        body = malloc(header.message_len);
        if (!body) {
            perror("malloc body");
            close(client_fd);
            return;
        }

        read_status = read_full(client_fd, body, header.message_len);
        if (read_status != 0) {
            fprintf(stderr, "Failed to read body from fd %d. Closing.\n", client_fd);
            free(body);
            close(client_fd);
            return;
        }
        
        // Ensure null termination for safety, though protocol expects it
        body[header.message_len - 1] = '\0';
    }


    // 3. Dispatch to handler
    printf("Received message from fd %d: type %" PRIu32 ", len %" PRIu32 "\n", client_fd, header.type, header.message_len);
    switch (header.type) {
        case MSG_ECHO:
            handle_echo(client_fd, &header, body);
            break;
        case MSG_REVERSE:
            handle_reverse(client_fd, &header, body);
            break;
        case MSG_TIME:
            handle_time(client_fd);
            break;
        default:
            fprintf(stderr, "Unknown message type %" PRIu32 " from fd %d\n", header.type, client_fd);
            break;
    }

    free(body);
}

// --- Main Server Logic ---

int main(void) {
    int listen_fd, epoll_fd;
    struct sockaddr_in server_addr;

    printf("Hello from the server\n");

    // 1. Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 2. Bind to address and port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    // 3. Set to non-blocking and listen
    if (set_nonblocking(listen_fd) == -1) {
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 4. Create epoll instance and add listening socket
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-Triggered
    event.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl: listen_fd");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    printf("Server listening on localhost:%d\n", PORT);

    // 5. Main event loop
    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events == -1) {
            perror("epoll_wait");
            continue;
        }

        // Per requirement: handle new connections first
        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd == listen_fd) {
                // Accept all pending connections
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

                    if (conn_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // We have processed all incoming connections.
                            break;
                        } else {
                            perror("accept");
                            break;
                        }
                    }

                    printf("Accepted new connection on fd %d\n", conn_fd);
                    set_nonblocking(conn_fd);
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = conn_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event) == -1) {
                        perror("epoll_ctl: conn_fd");
                        close(conn_fd);
                    }
                }
            }
        }

        // Per requirement: handle reads next
        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd != listen_fd) {
                // Per requirement: Handle EPOLLIN even after EPOLLHUP
                // This logic handles both cases. A read on a HUP socket
                // will drain remaining data then return 0 (EOF).
                 if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLHUP)) {
                    process_message(events[i].data.fd);
                } else if (events[i].events & EPOLLERR) {
                    fprintf(stderr, "Epoll error on fd %d. Closing.\n", events[i].data.fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return EXIT_SUCCESS;
}