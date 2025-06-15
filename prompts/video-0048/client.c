#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include "message.h"

#ifdef DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

// Global flag to control the main loop, modified by the signal handler.
// volatile sig_atomic_t is required for safe signal handling.
static volatile sig_atomic_t running = 1;

void handle_signal(int signal) {
    (void)signal; // Unused parameter
    running = 0;
}

// Helper to write the entire buffer, handling short writes
ssize_t full_write(int fd, const void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);
    
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t written = write(fd, (const uint8_t*)buf + total_written, count - total_written);
        if (written < 0) {
            perror("write");
            return -1;
        }
        total_written += written;
    }
    return total_written;
}

// Helper to read the entire buffer, handling short reads
ssize_t full_read(int fd, void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);

    size_t total_read = 0;
    while (total_read < count) {
        ssize_t bytes_read = read(fd, (uint8_t*)buf + total_read, count - total_read);
        if (bytes_read < 0) {
            perror("read");
            return -1;
        }
        if (bytes_read == 0) { // Server closed connection
            return total_read;
        }
        total_read += bytes_read;
    }
    return total_read;
}

int main(void) {
    // Set up signal handlers for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Seed the random number generator
    srand(time(NULL));

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to server. Starting message loop. Press Ctrl+C to exit.\n");

    while (running) {
        // 1. Pause for a random interval (0.3s to 1.0s)
        long random_ns = (long)(300000000 + (rand() / (double)RAND_MAX) * 700000000);
        struct timespec sleep_time = { .tv_sec = 0, .tv_nsec = random_ns };
        nanosleep(&sleep_time, NULL);
        if (!running) break;

        // 2. Randomly select and create a message
        MessageHeader header;
        char body[MAX_BODY_SIZE] = "default message";
        
        header.type = rand() % 3; // Randomly pick one of the 3 message types

        switch(header.type) {
            case MSG_ECHO:
                strcpy(body, "This is an echo test.");
                break;
            case MSG_REVERSE:
                strcpy(body, "gnirts esrever");
                break;
            case MSG_TIME:
                // Body is irrelevant, server will generate it
                strcpy(body, "");
                break;
        }
        header.length = strlen(body) + 1; // Include null terminator

        // 3. Send the message
        size_t msg_size = sizeof(MessageHeader) + header.length;
        uint8_t send_buf[msg_size];
        memcpy(send_buf, &header, sizeof(MessageHeader));
        memcpy(send_buf + sizeof(MessageHeader), body, header.length);

        printf("Sending message type %" PRIu32 " ('%s')... ", header.type, body);
        fflush(stdout);

        if (full_write(sock_fd, send_buf, msg_size) < 0) {
            fprintf(stderr, "Failed to send message.\n");
            break;
        }

        // 4. Read the response
        MessageHeader resp_header;
        if (full_read(sock_fd, &resp_header, sizeof(MessageHeader)) != sizeof(MessageHeader)) {
             fprintf(stderr, "Failed to read response header or server disconnected.\n");
             break;
        }

        if (resp_header.length > MAX_BODY_SIZE) {
            fprintf(stderr, "Response body too large: %" PRIu32 "\n", resp_header.length);
            break;
        }

        char resp_body[MAX_BODY_SIZE];
        if (full_read(sock_fd, resp_body, resp_header.length) != (ssize_t)resp_header.length) {
            fprintf(stderr, "Failed to read response body or server disconnected.\n");
            break;
        }

        printf("Received response: '%s'\n", resp_body);
    }

    printf("\nSignal caught, shutting down gracefully.\n");
    close(sock_fd);
    return EXIT_SUCCESS;
}