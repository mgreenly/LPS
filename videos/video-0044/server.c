#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <inttypes.h>

#include "message.h"

#define PORT 4242
#define MAX_CLIENTS 128
#define BUFFER_SIZE 4096
#define ISO_TIME_LEN 20 // "YYYY-MM-DDTHH:MM:SSZ" + null

// State for each client connection
typedef struct {
    int fd;
    enum {
        STATE_READING_HEADER,
        STATE_READING_BODY
    } state;
    uint8_t buffer[BUFFER_SIZE];
    size_t bytes_received;
} client_connection_t;

// --- Forward Declarations ---
static void handle_client_data(struct pollfd *pfd, client_connection_t *client);
static void process_message(client_connection_t *client);
static void close_client_connection(struct pollfd *pfd, client_connection_t *client);
static int write_all(int fd, const void *buf, size_t count);

int main(void) {
    printf("Hello from the server\n");

    int listener_fd;
    struct sockaddr_in serv_addr;

    struct pollfd fds[MAX_CLIENTS + 1];
    client_connection_t clients[MAX_CLIENTS + 1];

    // Create listener socket
    if ((listener_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Allow reuse of local addresses
    int opt = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    // Bind the socket to the address
    if (bind(listener_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(listener_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Initialize pollfd and client arrays
    fds[0].fd = listener_fd;
    fds[0].events = POLLIN;
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        fds[i].fd = -1; // -1 indicates an unused slot
        clients[i].fd = -1;
    }

    while (1) {
        int poll_count = poll(fds, MAX_CLIENTS + 1, -1);
        if (poll_count < 0) {
            if (errno == EINTR) continue;
            perror("poll failed");
            break;
        }

        // Check for new connection on listener socket
        if (fds[0].revents & POLLIN) {
            int new_socket = accept(listener_fd, NULL, NULL);
            if (new_socket < 0) {
                perror("accept failed");
            } else {
                int i;
                for (i = 1; i <= MAX_CLIENTS; i++) {
                    if (fds[i].fd == -1) {
                        fds[i].fd = new_socket;
                        fds[i].events = POLLIN;
                        clients[i].fd = new_socket;
                        clients[i].state = STATE_READING_HEADER;
                        clients[i].bytes_received = 0;
                        printf("Accepted new connection on fd %d\n", new_socket);
                        break;
                    }
                }
                if (i > MAX_CLIENTS) {
                    fprintf(stderr, "Max clients reached. Rejecting connection.\n");
                    close(new_socket);
                }
            }
        }

        // Check client sockets for data
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            if (fds[i].fd != -1 && (fds[i].revents & (POLLIN | POLLERR | POLLHUP))) {
                if (fds[i].revents & POLLIN) {
                    handle_client_data(&fds[i], &clients[i]);
                } else {
                    // POLLERR, POLLHUP, or other error
                    printf("Client on fd %d disconnected or errored.\n", fds[i].fd);
                    close_client_connection(&fds[i], &clients[i]);
                }
            }
        }
    }

    close(listener_fd);
    return 0;
}

static void handle_client_data(struct pollfd *pfd, client_connection_t *client) {
    ssize_t nread = read(client->fd, client->buffer + client->bytes_received, BUFFER_SIZE - client->bytes_received);

    if (nread <= 0) {
        if (nread == 0) {
            printf("Client on fd %d disconnected gracefully.\n", client->fd);
        } else {
            perror("read error");
        }
        close_client_connection(pfd, client);
        return;
    }

    client->bytes_received += nread;

    if (client->state == STATE_READING_HEADER) {
        if (client->bytes_received >= sizeof(message_header_t)) {
            message_header_t *header = (message_header_t *)client->buffer;
            if (ntohl(header->length) > (BUFFER_SIZE - sizeof(message_header_t))) {
                fprintf(stderr, "Message too long from fd %d. Closing connection.\n", client->fd);
                close_client_connection(pfd, client);
                return;
            }
            client->state = STATE_READING_BODY;
        }
    }

    if (client->state == STATE_READING_BODY) {
        message_header_t *header = (message_header_t *)client->buffer;
        uint32_t total_msg_len = sizeof(message_header_t) + ntohl(header->length);
        if (client->bytes_received >= total_msg_len) {
            process_message(client);
            // Reset for the next message
            client->bytes_received = 0;
            client->state = STATE_READING_HEADER;
        }
    }
}

static void process_message(client_connection_t *client) {
    message_header_t *header = (message_header_t *)client->buffer;
    char *body = (char *)(client->buffer + sizeof(message_header_t));
    uint32_t body_len = ntohl(header->length);

    // Ensure body is null-terminated for safety, even though client should do it.
    body[body_len - 1] = '\0';
    
    printf("Received message type %d, len %" PRIu32 " from fd %d\n", header->type, body_len, client->fd);

    switch (header->type) {
        case MSG_ECHO:
            // Respond with the exact same message
            if (write_all(client->fd, client->buffer, sizeof(message_header_t) + body_len) < 0) {
                fprintf(stderr, "Failed to echo message to fd %d\n", client->fd);
            }
            break;

        case MSG_REVERSE: {
            // Reverse the string in place
            for (uint32_t i = 0, j = body_len - 2; i < j; i++, j--) {
                char temp = body[i];
                body[i] = body[j];
                body[j] = temp;
            }
            // Send back the modified buffer
            if (write_all(client->fd, client->buffer, sizeof(message_header_t) + body_len) < 0) {
                fprintf(stderr, "Failed to send reversed message to fd %d\n", client->fd);
            }
            break;
        }

        case MSG_TIME: {
            char time_buf[ISO_TIME_LEN];
            time_t now = time(NULL);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
            
            uint32_t time_body_len = strlen(time_buf) + 1; // +1 for null terminator
            uint32_t total_len = sizeof(message_header_t) + time_body_len;
            uint8_t response_buf[total_len];

            message_header_t resp_header;
            resp_header.type = MSG_TIME;
            resp_header.length = htonl(time_body_len);

            memcpy(response_buf, &resp_header, sizeof(resp_header));
            memcpy(response_buf + sizeof(resp_header), time_buf, time_body_len);

            if (write_all(client->fd, response_buf, total_len) < 0) {
                fprintf(stderr, "Failed to send time message to fd %d\n", client->fd);
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown message type %d from fd %d\n", header->type, client->fd);
            break;
    }
}

static void close_client_connection(struct pollfd *pfd, client_connection_t *client) {
    close(pfd->fd);
    pfd->fd = -1;
    client->fd = -1;
}

static int write_all(int fd, const void *buf, size_t count) {
    size_t bytes_written = 0;
    while (bytes_written < count) {
        ssize_t res = write(fd, (const char*)buf + bytes_written, count - bytes_written);
        if (res < 0) {
            if (errno == EINTR) continue;
            perror("write failed");
            return -1;
        }
        bytes_written += res;
    }
    return 0;
}