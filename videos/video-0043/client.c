#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include "message.h"

#define SERVER_IP "127.0.0.1"
#define PORT 4242

// --- Utility Functions for Robust I/O ---

// Reads exactly 'count' bytes from a file descriptor into 'buf'.
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

// --- Test Function ---

static int send_and_receive(int sock_fd, MessageType type, const char *text) {
    // 1. Prepare and send the request message
    MessageHeader req_header;
    req_header.type = type;
    req_header.body_length = (text == NULL) ? 0 : (strlen(text) + 1); // +1 for null terminator

    printf("\n--- Sending request ---\n");
    printf("Type: %d, Body: \"%s\", Body Length: %" PRIu32 "\n", 
           req_header.type, text ? text : "(none)", req_header.body_length);

    // Convert to network byte order before sending
    req_header.type = htonl(req_header.type);
    req_header.body_length = htonl(req_header.body_length);

    if (write_all(sock_fd, &req_header, sizeof(req_header)) != 0) {
        fprintf(stderr, "Failed to write request header\n");
        return -1;
    }
    if (text != NULL) {
        if (write_all(sock_fd, text, strlen(text) + 1) != 0) {
            fprintf(stderr, "Failed to write request body\n");
            return -1;
        }
    }

    // 2. Receive the response message
    MessageHeader res_header;
    if (read_all(sock_fd, &res_header, sizeof(res_header)) != 0) {
        fprintf(stderr, "Failed to read response header\n");
        return -1;
    }

    // Convert from network to host byte order
    res_header.type = ntohl(res_header.type);
    res_header.body_length = ntohl(res_header.body_length);
    
    char *res_body = NULL;
    if (res_header.body_length > 0) {
        res_body = malloc(res_header.body_length);
        if (!res_body) {
            perror("malloc for response body");
            return -1;
        }
        if (read_all(sock_fd, res_body, res_header.body_length) != 0) {
            fprintf(stderr, "Failed to read response body\n");
            free(res_body);
            return -1;
        }
    }

    printf("--- Received response ---\n");
    printf("Type: %" PRIu32 ", Body: \"%s\", Body Length: %" PRIu32 "\n",
           res_header.type, res_body ? res_body : "(none)", res_header.body_length);
           
    free(res_body);
    return 0;
}

int main(void) {
    printf("Hello from the client\n");

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // Send one of each message type to test the server
    send_and_receive(sock_fd, MSG_ECHO, "The quick brown fox jumps over the lazy dog.");
    send_and_receive(sock_fd, MSG_REVERSE, "Hello, World!");
    send_and_receive(sock_fd, MSG_TIME, NULL); // Time request has no body

    close(sock_fd);
    return EXIT_SUCCESS;
}