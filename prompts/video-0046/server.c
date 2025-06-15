#define _GNU_SOURCE // For asprintf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

#include "message.h"

// --- Configuration ---
#define PORT 4242
#define MAX_CONNECTIONS 1024
#define MAX_EVENTS 64
#define MAX_MSG_BODY_LEN 4096 // Sanity limit for message body
#define READ_BUFFER_SIZE 8192

// --- Debug Logging ---
#ifdef DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt "\n", \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

// --- Connection State ---
typedef enum {
    STATE_READING_HEADER,
    STATE_READING_BODY
} ConnReadState;

typedef struct {
    int fd;
    ConnReadState read_state;
    uint8_t buffer[READ_BUFFER_SIZE];
    size_t bytes_read;
    MessageHeader current_header;
} ConnectionState;


// --- Forward Declarations ---
void handle_new_connection(int epoll_fd, int listen_fd);
void handle_client_event(ConnectionState *conn_state, uint32_t events, int epoll_fd);
void close_connection(ConnectionState *conn_state, int epoll_fd);
void process_message(ConnectionState *conn_state);
ssize_t robust_write(int fd, const void *buf, size_t count);


int main(void) {
    printf("Hello from the server\n");

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Allow reusing the address to avoid "Address already in use" errors on restart
    int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, MAX_CONNECTIONS) == -1) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event event = {0};
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl: listen_fd");
        close(listen_fd);
        close(epoll_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    DEBUG_LOG("Server listening on port %d", PORT);

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
                handle_client_event((ConnectionState *)events[i].data.ptr, events[i].events, epoll_fd);
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    return EXIT_SUCCESS;
}

void handle_new_connection(int epoll_fd, int listen_fd) {
    assert(epoll_fd >= 0);
    assert(listen_fd >= 0);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept4(listen_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
    if (client_fd == -1) {
        perror("accept4");
        return;
    }

    ConnectionState *conn_state = calloc(1, sizeof(ConnectionState));
    if (!conn_state) {
        perror("calloc for ConnectionState");
        close(client_fd);
        return;
    }

    conn_state->fd = client_fd;
    conn_state->read_state = STATE_READING_HEADER;
    conn_state->bytes_read = 0;

    struct epoll_event event = {0};
    event.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLET;
    event.data.ptr = conn_state;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        perror("epoll_ctl: client_fd");
        free(conn_state);
        close(client_fd);
        return;
    }

    DEBUG_LOG("Accepted new connection on fd %d", client_fd);
}

void handle_client_event(ConnectionState *conn_state, uint32_t events, int epoll_fd) {
    assert(conn_state != NULL);
    assert(epoll_fd >= 0);

    if ((events & EPOLLHUP) || (events & EPOLLRDHUP) || (events & EPOLLERR)) {
        DEBUG_LOG("Connection closed or error on fd %d", conn_state->fd);
        close_connection(conn_state, epoll_fd);
        return;
    }

    if (events & EPOLLIN) {
        ssize_t count = read(conn_state->fd, conn_state->buffer + conn_state->bytes_read, READ_BUFFER_SIZE - conn_state->bytes_read);
        if (count == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("read from client");
                close_connection(conn_state, epoll_fd);
            }
            return;
        }

        if (count == 0) { // Peer closed connection
            DEBUG_LOG("Peer gracefully closed connection on fd %d", conn_state->fd);
            close_connection(conn_state, epoll_fd);
            return;
        }

        conn_state->bytes_read += count;
        DEBUG_LOG("Read %zd bytes from fd %d. Total buffered: %zu", count, conn_state->fd, conn_state->bytes_read);
        
        // Process all complete messages in the buffer
        while (1) {
            if (conn_state->read_state == STATE_READING_HEADER) {
                if (conn_state->bytes_read < sizeof(MessageHeader)) {
                    break; // Not enough data for a full header
                }
                
                memcpy(&conn_state->current_header, conn_state->buffer, sizeof(MessageHeader));
                conn_state->current_header.type = ntohl(conn_state->current_header.type);
                conn_state->current_header.length = ntohl(conn_state->current_header.length);

                if (conn_state->current_header.length > MAX_MSG_BODY_LEN) {
                    fprintf(stderr, "ERROR: Message length %" PRIu32 " exceeds max %d on fd %d. Closing.\n",
                            conn_state->current_header.length, MAX_MSG_BODY_LEN, conn_state->fd);
                    close_connection(conn_state, epoll_fd);
                    return;
                }
                
                conn_state->read_state = STATE_READING_BODY;
                DEBUG_LOG("Header received on fd %d: type=%" PRIu32 ", len=%" PRIu32, conn_state->fd, conn_state->current_header.type, conn_state->current_header.length);
            }

            if (conn_state->read_state == STATE_READING_BODY) {
                size_t total_msg_len = sizeof(MessageHeader) + conn_state->current_header.length;
                if (conn_state->bytes_read < total_msg_len) {
                    break; // Not enough data for a full body
                }

                process_message(conn_state);

                // Shift remaining data in buffer to the front
                size_t remaining_bytes = conn_state->bytes_read - total_msg_len;
                if (remaining_bytes > 0) {
                    memmove(conn_state->buffer, conn_state->buffer + total_msg_len, remaining_bytes);
                }
                conn_state->bytes_read = remaining_bytes;
                conn_state->read_state = STATE_READING_HEADER;
            }
        }
    }
}

void close_connection(ConnectionState *conn_state, int epoll_fd) {
    assert(conn_state != NULL);
    assert(epoll_fd >= 0);

    if (conn_state->fd >= 0) {
        DEBUG_LOG("Closing connection fd %d", conn_state->fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn_state->fd, NULL);
        close(conn_state->fd);
    }
    free(conn_state);
}

void process_message(ConnectionState *conn_state) {
    assert(conn_state != NULL);

    char *body = (char*)conn_state->buffer + sizeof(MessageHeader);
    MessageType type = (MessageType)conn_state->current_header.type;
    uint32_t length = conn_state->current_header.length;
    
    DEBUG_LOG("Processing message fd=%d type=%u len=%" PRIu32, conn_state->fd, type, length);

    MessageHeader resp_header = {0};
    char *resp_body = NULL;

    switch (type) {
        case MSG_ECHO: {
            resp_header.type = htonl(MSG_ECHO);
            resp_header.length = htonl(length);
            resp_body = body; // Directly send back the received body
            break;
        }
        case MSG_REVERSE: {
            resp_header.type = htonl(MSG_REVERSE);
            resp_header.length = htonl(length);
            resp_body = body; // Reverse in-place
            // Reverse the null-terminated string
            if (length > 0) {
                for (uint32_t i = 0, j = length - 2; i < j; ++i, --j) {
                    char temp = resp_body[i];
                    resp_body[i] = resp_body[j];
                    resp_body[j] = temp;
                }
            }
            break;
        }
        case MSG_TIME: {
            char time_buf[128];
            time_t now = time(NULL);
            struct tm *t = gmtime(&now);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", t);
            
            uint32_t time_len = strlen(time_buf) + 1; // Include null terminator
            
            resp_header.type = htonl(MSG_TIME);
            resp_header.length = htonl(time_len);
            resp_body = time_buf;
            break;
        }
        default:
            fprintf(stderr, "Unknown message type %u from fd %d\n", type, conn_state->fd);
            return; // Don't respond to unknown types
    }
    
    // Send response
    robust_write(conn_state->fd, &resp_header, sizeof(resp_header));
    robust_write(conn_state->fd, resp_body, ntohl(resp_header.length));
    DEBUG_LOG("Sent response to fd %d", conn_state->fd);
}

ssize_t robust_write(int fd, const void *buf, size_t count) {
    assert(buf != NULL);
    assert(fd >= 0);

    size_t total_written = 0;
    while (total_written < count) {
        ssize_t written = write(fd, (const char*)buf + total_written, count - total_written);
        if (written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // This shouldn't happen often in a simple response, but is good practice.
                // A real-world server would buffer this output and use EPOLLOUT.
                DEBUG_LOG("Write would block on fd %d. Retrying later is needed.", fd);
                continue; 
            }
            perror("robust_write");
            return -1;
        }
        total_written += written;
    }
    return total_written;
}