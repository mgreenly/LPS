#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <inttypes.h>

#include "message.h"

#define PORT 4242
#define INITIAL_FDS_CAPACITY 16
#define ISO_8601_LEN 21 // "YYYY-MM-DDTHH:MM:SSZ\0"

// --- Utility Functions for Robust I/O ---

// Reads exactly 'count' bytes from a file descriptor into 'buf'.
// Handles short reads and EINTR.
// Returns 0 on success, -1 on EOF, or -2 on error.
static int read_all(int fd, void *buf, size_t count) {
    char *p = buf;
    size_t bytes_read = 0;
    while (bytes_read < count) {
        ssize_t result = read(fd, p + bytes_read, count - bytes_read);
        if (result == 0) return -1; // EOF
        if (result < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -2; // Error
        }
        bytes_read += result;
    }
    return 0;
}

// Writes exactly 'count' bytes to a file descriptor from 'buf'.
// Handles short writes and EINTR.
// Returns 0 on success, -1 on error.
static int write_all(int fd, const void *buf, size_t count) {
    const char *p = buf;
    size_t bytes_written = 0;
    while (bytes_written < count) {
        ssize_t result = write(fd, p + bytes_written, count - bytes_written);
        if (result < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return -1;
        }
        bytes_written += result;
    }
    return 0;
}

// --- Message Handling Logic ---

// Reverses a string in-place (excluding the null terminator).
static void reverse_string(char *str, size_t len) {
    if (len == 0) return;
    char *start = str;
    char *end = str + len - 1;
    while (start < end) {
        char temp = *start;
        *start++ = *end;
        *end-- = temp;
    }
}

// Handles a single client connection that has pending data.
static void handle_client_data(int client_fd) {
    MessageHeader header;

    // 1. Read the fixed-size header
    int read_status = read_all(client_fd, &header, sizeof(header));
    if (read_status != 0) {
        if (read_status == -1) {
            printf("Client fd %d disconnected.\n", client_fd);
        } else {
            fprintf(stderr, "Error reading header from client fd %d.\n", client_fd);
        }
        close(client_fd);
        return;
    }

    // Convert from network to host byte order
    header.type = ntohl(header.type);
    header.body_length = ntohl(header.body_length);

    // 2. Read the variable-length body
    char *body = NULL;
    if (header.body_length > 0) {
        body = malloc(header.body_length);
        if (!body) {
            perror("malloc for body");
            close(client_fd);
            return;
        }
        if (read_all(client_fd, body, header.body_length) != 0) {
            fprintf(stderr, "Error reading body from client fd %d.\n", client_fd);
            free(body);
            close(client_fd);
            return;
        }
    }
    
    printf("Received message from fd %d: type=%" PRIu32 ", len=%" PRIu32 "\n",
           client_fd, header.type, header.body_length);

    // 3. Process message and prepare response
    MessageHeader res_header = header;
    char *res_body = NULL;

    switch (header.type) {
        case MSG_ECHO:
            // Response is identical to request
            res_body = body;
            body = NULL; // Prevent double free
            break;

        case MSG_REVERSE:
            reverse_string(body, header.body_length > 0 ? header.body_length - 1 : 0);
            res_body = body;
            body = NULL;
            break;

        case MSG_TIME:
            res_header.body_length = ISO_8601_LEN;
            res_body = malloc(res_header.body_length);
            if (!res_body) {
                perror("malloc for time response");
                break;
            }
            time_t now = time(NULL);
            struct tm gmt;
            gmtime_r(&now, &gmt);
            strftime(res_body, res_header.body_length, "%Y-%m-%dT%H:%M:%SZ", &gmt);
            break;

        default:
            fprintf(stderr, "Unknown message type: %" PRIu32 "\n", header.type);
            break;
    }

    // 4. Send the response
    if (res_body) {
        // Convert response header fields to network byte order
        res_header.type = htonl(res_header.type);
        res_header.body_length = htonl(res_header.body_length);

        if (write_all(client_fd, &res_header, sizeof(res_header)) == 0) {
            uint32_t len = ntohl(res_header.body_length);
            if (len > 0) {
                write_all(client_fd, res_body, len);
            }
        }
        free(res_body);
    }

    free(body); // Free original body if not used in response
}

// --- Main Server Loop ---

int main(void) {
    printf("Hello from the server\n");

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces
    serv_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // Setup for poll()
    size_t fds_capacity = INITIAL_FDS_CAPACITY;
    struct pollfd *fds = malloc(fds_capacity * sizeof(*fds));
    if (!fds) {
        perror("malloc for fds");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // The first fd is always the listening socket
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    size_t nfds = 1;

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        int poll_count = poll(fds, nfds, -1); // -1 means wait indefinitely
        if (poll_count < 0) {
            perror("poll");
            break;
        }

        // Check for new connections on the listening socket
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }

            printf("Accepted new connection on fd %d\n", client_fd);

            // Add new client to the pollfd array
            if (nfds == fds_capacity) {
                fds_capacity *= 2;
                fds = realloc(fds, fds_capacity * sizeof(*fds));
                if (!fds) {
                    perror("realloc fds");
                    break; // Critical error
                }
            }
            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        // Check existing clients for data or disconnections
        for (size_t i = 1; i < nfds; ++i) {
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (fds[i].revents & POLLIN) {
                    handle_client_data(fds[i].fd);
                }

                // If client closed or error, remove it from the set.
                // We use the "swap with last" trick for O(1) removal.
                if (fds[i].revents & (POLLHUP | POLLERR)) {
                    printf("Client fd %d connection closed or error.\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i] = fds[nfds - 1]; // Swap
                    nfds--;
                    i--; // Re-check the swapped element at the same index
                }
            }
        }
    }

    for (size_t i = 0; i < nfds; ++i) {
        close(fds[i].fd);
    }
    free(fds);
    return EXIT_SUCCESS;
}
