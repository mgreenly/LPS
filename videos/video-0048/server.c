#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include "message.h"

#define MAX_EVENTS 64
#define READ_BUFFER_SIZE (sizeof(MessageHeader) + MAX_BODY_SIZE)

#ifdef DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

// --- Function Prototypes ---
static int create_and_bind_socket(uint16_t port);
static int set_nonblocking(int fd);
static void handle_client_data(int client_fd);
static void reverse_string(char *str, size_t len);
static void process_message(int client_fd, const MessageHeader *header, const char *body);

// --- Main Application ---
int main(void) {
    printf("Hello from the server\n");

    int listen_sock = create_and_bind_socket(SERVER_PORT);
    if (listen_sock == -1) {
        fprintf(stderr, "Failed to create and bind socket\n");
        return EXIT_FAILURE;
    }

    if (listen(listen_sock, SOMAXCONN) == -1) {
        perror("listen");
        close(listen_sock);
        return EXIT_FAILURE;
    }
    DEBUG_LOG("Server listening on port %d", SERVER_PORT);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_sock);
        return EXIT_FAILURE;
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        perror("epoll_ctl: listen_sock");
        close(listen_sock);
        close(epoll_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events == -1) {
            perror("epoll_wait");
            continue; // Continue on interrupt
        }

        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd == listen_sock) {
                // New connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }

                set_nonblocking(client_fd);
                event.events = EPOLLIN | EPOLLET; // Edge-triggered
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                } else {
                    DEBUG_LOG("Accepted connection on fd %d", client_fd);
                }
            } else {
                // Data from a client
                if ((events[i].events & EPOLLIN)) {
                    handle_client_data(events[i].data.fd);
                }
                
                // Check for errors or hang-ups
                if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                     DEBUG_LOG("EPOLLERR or EPOLLHUP on fd %d. Closing.", events[i].data.fd);
                     close(events[i].data.fd); // epoll automatically removes the fd
                }
            }
        }
    }

    // Cleanup (in practice, this part is unreachable in this simple server)
    close(listen_sock);
    close(epoll_fd);
    return EXIT_SUCCESS;
}


// --- Helper Implementations ---

static int create_and_bind_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return -1;
    }

    // Allow reuse of local addresses
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(sock);
        return -1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(sock);
        return -1;
    }
    
    set_nonblocking(sock);

    return sock;
}

static int set_nonblocking(int fd) {
    assert(fd >= 0);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0;
}

static void handle_client_data(int client_fd) {
    assert(client_fd >= 0);
    
    uint8_t buffer[READ_BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

    if (bytes_read == -1) {
        // If non-blocking, EAGAIN/EWOULDBLOCK are expected.
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read");
            close(client_fd); // Error, close connection
        }
        return; // Nothing to read or would block
    }

    if (bytes_read == 0) {
        // Connection closed by client
        DEBUG_LOG("Client fd %d disconnected.", client_fd);
        close(client_fd);
        return;
    }
    
    DEBUG_LOG("Read %zd bytes from fd %d", bytes_read, client_fd);

    // Process all complete messages in the buffer
    uint8_t *current_pos = buffer;
    ssize_t remaining_bytes = bytes_read;
    
    while(remaining_bytes >= (ssize_t)sizeof(MessageHeader)) {
        MessageHeader *header = (MessageHeader *)current_pos;

        // Check if the full message body is present
        if (remaining_bytes >= (ssize_t)(sizeof(MessageHeader) + header->length)) {
            char *body = (char*)(current_pos + sizeof(MessageHeader));
            
            // Defensively null-terminate the received body string
            if (header->length < MAX_BODY_SIZE) {
                body[header->length] = '\0'; 
            } else {
                body[MAX_BODY_SIZE - 1] = '\0';
            }

            process_message(client_fd, header, body);
            
            ssize_t total_msg_size = sizeof(MessageHeader) + header->length;
            current_pos += total_msg_size;
            remaining_bytes -= total_msg_size;
        } else {
            // Incomplete message in buffer, wait for more data
            DEBUG_LOG("Incomplete message received from fd %d. Waiting for more data.", client_fd);
            break;
        }
    }
}


static void process_message(int client_fd, const MessageHeader *header, const char *body) {
    assert(client_fd >= 0);
    assert(header != NULL);
    assert(body != NULL);

    DEBUG_LOG("Processing message type %" PRIu32 " with length %" PRIu32 " from fd %d",
              header->type, header->length, client_fd);

    switch (header->type) {
        case MSG_ECHO: {
            uint8_t response_buf[sizeof(MessageHeader) + header->length];
            memcpy(response_buf, header, sizeof(MessageHeader));
            memcpy(response_buf + sizeof(MessageHeader), body, header->length);
            write(client_fd, response_buf, sizeof(response_buf));
            break;
        }
        case MSG_REVERSE: {
            char reversed_body[header->length + 1];
            memcpy(reversed_body, body, header->length);
            reversed_body[header->length] = '\0'; // ensure null termination for reverse
            
            reverse_string(reversed_body, strlen(reversed_body));

            uint8_t response_buf[sizeof(MessageHeader) + header->length];
            memcpy(response_buf, header, sizeof(MessageHeader));
            memcpy(response_buf + sizeof(MessageHeader), reversed_body, header->length);
            write(client_fd, response_buf, sizeof(response_buf));
            break;
        }
        case MSG_TIME: {
            char time_str[128];
            time_t now = time(NULL);
            struct tm *t = gmtime(&now);
            strftime(time_str, sizeof(time_str) - 1, "%Y-%m-%dT%H:%M:%SZ", t);
            
            MessageHeader time_header;
            time_header.type = MSG_TIME;
            time_header.length = strlen(time_str) + 1; // include null terminator

            uint8_t response_buf[sizeof(MessageHeader) + time_header.length];
            memcpy(response_buf, &time_header, sizeof(MessageHeader));
            memcpy(response_buf + sizeof(MessageHeader), time_str, time_header.length);
            write(client_fd, response_buf, sizeof(response_buf));
            break;
        }
        default:
            DEBUG_LOG("Unknown message type %" PRIu32 " from fd %d", header->type, client_fd);
            break;
    }
}

static void reverse_string(char *str, size_t len) {
    assert(str != NULL);
    if (len < 2) return;
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
