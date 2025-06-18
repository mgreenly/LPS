#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>

#include "message.h"

#define PORT 4242
#define MAX_EVENTS 64

// Holds the state for a single client connection
typedef struct {
    int fd;
    uint8_t read_buf[MAX_MSG_LEN];
    size_t bytes_read;
} client_conn_t;

// --- Function Prototypes ---
static int setup_listener(int port);
static void set_non_blocking(int fd);
static void handle_new_connection(int epoll_fd, int listener_fd);
static void handle_client_data(struct epoll_event *event);
static void process_message(client_conn_t *conn, message_header_t *header);
static void reverse_string(char *str);
static void send_full(int fd, const void *buf, size_t len);

int main(void) {
    printf("Hello from the server\n");

    int listener_fd = setup_listener(PORT);
    if (listener_fd < 0) {
        exit(EXIT_FAILURE);
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listener_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listener_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &event) == -1) {
        perror("epoll_ctl: listener_fd");
        close(listener_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd == listener_fd) {
                handle_new_connection(epoll_fd, listener_fd);
            } else {
                handle_client_data(&events[i]);
            }
        }
    }

    close(listener_fd);
    close(epoll_fd);
    return EXIT_SUCCESS;
}

// Sets up and returns a non-blocking listening socket
static int setup_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on localhost
    server_addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) == -1) {
        perror("listen");
        close(fd);
        return -1;
    }

    set_non_blocking(fd);
    printf("Server listening on port %d\n", port);
    return fd;
}

// Sets a file descriptor to non-blocking mode
static void set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
    }
}

// Accepts new connections and adds them to epoll
static void handle_new_connection(int epoll_fd, int listener_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listener_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // No more incoming connections
        }
        perror("accept");
        return;
    }

    set_non_blocking(client_fd);

    client_conn_t *conn = malloc(sizeof(client_conn_t));
    if (!conn) {
        fprintf(stderr, "Failed to allocate memory for new connection\n");
        close(client_fd);
        return;
    }
    conn->fd = client_fd;
    conn->bytes_read = 0;

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered for performance
    event.data.ptr = conn;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        perror("epoll_ctl: client_fd");
        free(conn);
        close(client_fd);
    } else {
        printf("Accepted new connection on fd %d\n", client_fd);
    }
}

// Reads from a client socket and processes complete messages
static void handle_client_data(struct epoll_event *event) {
    client_conn_t *conn = (client_conn_t *)event->data.ptr;
    
    ssize_t count = read(conn->fd, conn->read_buf + conn->bytes_read, MAX_MSG_LEN - conn->bytes_read);

    if (count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read");
            close(conn->fd);
            free(conn);
        }
        return;
    }

    if (count == 0) { // Connection closed by peer
        printf("Client on fd %d disconnected\n", conn->fd);
        close(conn->fd);
        free(conn);
        return;
    }

    conn->bytes_read += count;

    // Process all complete messages in the buffer
    while (conn->bytes_read >= sizeof(message_header_t)) {
        message_header_t header;
        memcpy(&header, conn->read_buf, sizeof(message_header_t));
        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.length > MAX_BODY_LEN) {
             fprintf(stderr, "Client fd %d: message too long (%" PRIu32 ")\n", conn->fd, header.length);
             close(conn->fd);
             free(conn);
             return;
        }

        size_t total_msg_len = sizeof(message_header_t) + header.length;
        if (conn->bytes_read < total_msg_len) {
            break; // Incomplete message, wait for more data
        }

        process_message(conn, &header);

        // Remove processed message from buffer
        size_t remaining_bytes = conn->bytes_read - total_msg_len;
        memmove(conn->read_buf, conn->read_buf + total_msg_len, remaining_bytes);
        conn->bytes_read = remaining_bytes;
    }
}

// Handles the logic for different message types
static void process_message(client_conn_t *conn, message_header_t *header) {
    char *body = (char *)(conn->read_buf + sizeof(message_header_t));
    
    printf("Processing message from fd %d: type %" PRIu32 ", len %" PRIu32 "\n", conn->fd, header->type, header->length);

    switch (header->type) {
        case MSG_ECHO:
            send_full(conn->fd, conn->read_buf, sizeof(message_header_t) + header->length);
            break;
            
        case MSG_REVERSE:
            reverse_string(body);
            send_full(conn->fd, conn->read_buf, sizeof(message_header_t) + header->length);
            break;

        case MSG_TIME: {
            char time_buf[128];
            time_t now = time(NULL);
            struct tm gmt;
            gmtime_r(&now, &gmt);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
            
            uint32_t resp_len = strlen(time_buf) + 1;
            uint8_t resp_buf[sizeof(message_header_t) + resp_len];
            
            message_header_t resp_header;
            resp_header.type = htonl(MSG_TIME);
            resp_header.length = htonl(resp_len);
            
            memcpy(resp_buf, &resp_header, sizeof(resp_header));
            memcpy(resp_buf + sizeof(resp_header), time_buf, resp_len);
            
            send_full(conn->fd, resp_buf, sizeof(resp_header) + resp_len);
            break;
        }

        default:
            fprintf(stderr, "Unknown message type %" PRIu32 " from fd %d\n", header->type, conn->fd);
            break;
    }
}

// Reverses a null-terminated string in-place
static void reverse_string(char *str) {
    if (!str) return;
    int len = strlen(str);
    char *start = str;
    char *end = str + len - 1;
    while (start < end) {
        char temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }
}

// Helper to ensure all data is sent, handling partial writes
static void send_full(int fd, const void *buf, size_t len) {
    const char *ptr = (const char*) buf;
    while (len > 0) {
        ssize_t sent = send(fd, ptr, len, 0);
        if (sent < 0) {
            // In a real non-blocking server, this would need to handle EAGAIN
            // by adding fd to epoll for EPOLLOUT. For this example, we simplify.
            if (errno == EINTR) continue;
            perror("send");
            return;
        }
        ptr += sent;
        len -= sent;
    }
}
