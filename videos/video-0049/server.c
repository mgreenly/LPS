// server.c

#define _GNU_SOURCE // For strndup
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>

#include "message.h"

#define PORT 4242
#define MAX_EVENTS 64
#define MAX_CONNECTIONS 1024
#define TIME_BUFFER_SIZE 128

// --- Forward Declarations ---
static int create_and_bind_socket(void);
static void set_non_blocking(int fd);
static void handle_new_connection(int epoll_fd, int listen_fd);
static void handle_client_event(int epoll_fd, struct epoll_event *event);
static void process_message(int client_fd, const MessageHeader *header, const char *body);
static void reverse_string(char *str, size_t len);
static ssize_t read_full(int fd, void *buf, size_t count);
static ssize_t send_full(int fd, const void *buf, size_t count);

int main(void) {
    puts("Hello from the server");

    int listen_fd = create_and_bind_socket();
    if (listen_fd < 0) {
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl: listen_fd");
        close(listen_fd);
        close(epoll_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    fprintf(stderr, "Server listening on localhost:%d\n", PORT);

    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events == -1) {
            perror("epoll_wait");
            break; 
        }

        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd == listen_fd) {
                handle_new_connection(epoll_fd, listen_fd);
            } else {
                handle_client_event(epoll_fd, &events[i]);
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    puts("Server shutting down.");

    return EXIT_SUCCESS;
}

// Sets up the listening socket.
static int create_and_bind_socket(void) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return -1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, MAX_CONNECTIONS) == -1) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    set_non_blocking(listen_fd);
    return listen_fd;
}

// Sets a file descriptor to non-blocking mode.
static void set_non_blocking(int fd) {
    assert(fd >= 0);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
    }
}

// Handles a new incoming connection.
static void handle_new_connection(int epoll_fd, int listen_fd) {
    assert(epoll_fd >= 0);
    assert(listen_fd >= 0);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd == -1) {
        perror("accept");
        return;
    }

    set_non_blocking(client_fd);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        perror("epoll_ctl: client_fd");
        close(client_fd);
        return;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    fprintf(stderr, "debug: Accepted connection from %s on fd %d\n", client_ip, client_fd);
}

// Disconnects a client and removes it from epoll.
static void disconnect_client(int epoll_fd, int client_fd) {
    assert(epoll_fd >= 0);
    assert(client_fd >= 0);

    fprintf(stderr, "debug: Client on fd %d disconnected.\n", client_fd);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

// Handles I/O events on a client socket.
static void handle_client_event(int epoll_fd, struct epoll_event *event) {
    assert(epoll_fd >= 0);
    assert(event != NULL);

    int client_fd = event->data.fd;

    // Check for hang-up or error conditions first.
    if ((event->events & EPOLLHUP) || (event->events & EPOLLERR)) {
        fprintf(stderr, "debug: EPOLLHUP or EPOLLERR on fd %d\n", client_fd);
        disconnect_client(epoll_fd, client_fd);
        return;
    }

    // Check for incoming data.
    if (event->events & EPOLLIN) {
        fprintf(stderr, "debug: EPOLLIN on fd %d\n", client_fd);

        MessageHeader header;
        ssize_t bytes_read = read_full(client_fd, &header, sizeof(header));

        if (bytes_read <= 0) {
            // 0 means clean disconnect, -1 means error (e.g. ECONNRESET)
            disconnect_client(epoll_fd, client_fd);
            return;
        }

        // Convert from network byte order to host byte order
        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        fprintf(stderr, "debug: Received header on fd %d: type=%" PRIu32 ", len=%" PRIu32 "\n",
                client_fd, header.type, header.length);

        if (header.length > 0) {
            char *body = malloc(header.length);
            if (!body) {
                perror("malloc");
                disconnect_client(epoll_fd, client_fd);
                return;
            }

            bytes_read = read_full(client_fd, body, header.length);
            if (bytes_read <= 0) {
                fprintf(stderr, "debug: Failed to read body or client disconnected after header on fd %d\n", client_fd);
                free(body);
                disconnect_client(epoll_fd, client_fd);
                return;
            }
            process_message(client_fd, &header, body);
            free(body);
        } else {
             // Handle messages with no body, like a potential future PING
             process_message(client_fd, &header, NULL);
        }
    }
}

// Processes a fully received message and sends a response.
static void process_message(int client_fd, const MessageHeader *header, const char *body) {
    assert(client_fd >= 0);
    assert(header != NULL);
    // body can be NULL if header->length is 0

    MessageHeader response_header;
    response_header.type = htonl(header->type);

    switch (header->type) {
        case MSG_ECHO: {
            fprintf(stderr, "debug: Processing ECHO for fd %d\n", client_fd);
            response_header.length = htonl(header->length);
            // Send header and body back as is
            send_full(client_fd, &response_header, sizeof(response_header));
            if (header->length > 0) {
                send_full(client_fd, body, header->length);
            }
            break;
        }
        case MSG_REVERSE: {
            fprintf(stderr, "debug: Processing REVERSE for fd %d\n", client_fd);
            char *reversed_body = strndup(body, header->length);
            if (!reversed_body) break;
            // Reverse in place, excluding the null terminator
            reverse_string(reversed_body, strnlen(reversed_body, header->length));

            response_header.length = htonl(header->length);
            send_full(client_fd, &response_header, sizeof(response_header));
            send_full(client_fd, reversed_body, header->length);
            free(reversed_body);
            break;
        }
        case MSG_TIME: {
            fprintf(stderr, "debug: Processing TIME for fd %d\n", client_fd);
            char time_str[TIME_BUFFER_SIZE];
            time_t now = time(NULL);
            struct tm *t = gmtime(&now);
            strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", t);

            uint32_t body_len = strlen(time_str) + 1; // Include null terminator
            response_header.length = htonl(body_len);
            send_full(client_fd, &response_header, sizeof(response_header));
            send_full(client_fd, time_str, body_len);
            break;
        }
        default:
            fprintf(stderr, "Unknown message type: %" PRIu32 " on fd %d\n", header->type, client_fd);
            break;
    }
}

// Reverses a string in place.
static void reverse_string(char *str, size_t len) {
    if (!str || len == 0) return;
    char *start = str;
    char *end = str + len - 1;
    char temp;
    while (start < end) {
        temp = *start;
        *start++ = *end;
        *end-- = temp;
    }
}

// Helper to read exactly 'count' bytes from a file descriptor.
// Returns bytes read, 0 on EOF, or -1 on error.
static ssize_t read_full(int fd, void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);

    ssize_t bytes_read = 0;
    size_t total_read = 0;
    char *ptr = buf;

    while (total_read < count) {
        bytes_read = read(fd, ptr + total_read, count - total_read);
        if (bytes_read == 0) { // EOF
            return total_read > 0 ? total_read : 0;
        }
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Should not happen with this blocking design, but good practice
                continue;
            }
            perror("read");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

// Helper to send exactly 'count' bytes to a file descriptor.
// Returns bytes sent, or -1 on error.
static ssize_t send_full(int fd, const void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);

    ssize_t bytes_sent = 0;
    size_t total_sent = 0;
    const char *ptr = buf;

    while (total_sent < count) {
        bytes_sent = send(fd, ptr + total_sent, count - total_sent, 0);
        if (bytes_sent < 0) {
            perror("send");
            return -1;
        }
        total_sent += bytes_sent;
    }
    return total_sent;
}
