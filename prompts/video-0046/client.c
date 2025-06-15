#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <inttypes.h>
#include <errno.h>

#include "message.h"

// --- Configuration ---
#define SERVER_IP "127.0.0.1"
#define PORT 4242
#define MIN_SLEEP_MS 300
#define MAX_SLEEP_MS 1000

// --- Debug Logging ---
#ifdef DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt "\n", \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

// --- Global for Signal Handling ---
// Must be volatile sig_atomic_t for safe signal handling
static volatile sig_atomic_t g_shutdown_flag = 0;

void signal_handler(int signum) {
    (void)signum;
    g_shutdown_flag = 1;
}

// --- Function Prototypes ---
ssize_t robust_read(int fd, void *buf, size_t count);
ssize_t robust_write(int fd, const void *buf, size_t count);

int main(void) {
    // Seed random number generator
    srand(time(NULL));

    // Setup signal handlers for graceful shutdown
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Client starting. Press Ctrl+C to exit.\n");

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
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

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to server at %s:%d\n", SERVER_IP, PORT);

    while (!g_shutdown_flag) {
        // 1. Pause for a random duration
        long sleep_us = (rand() % (MAX_SLEEP_MS - MIN_SLEEP_MS + 1) + MIN_SLEEP_MS) * 1000;
        struct timespec sleep_time = { .tv_sec = sleep_us / 1000000, .tv_nsec = (sleep_us % 1000000) * 1000 };
        nanosleep(&sleep_time, NULL);
        
        if (g_shutdown_flag) break;

        // 2. Create a random message
        MessageHeader header = {0};
        char *body = NULL;
        MessageType msg_type = rand() % 3;

        switch(msg_type) {
            case MSG_ECHO: {
                body = "Hello, this is an echo test.";
                header.type = MSG_ECHO;
                header.length = strlen(body) + 1;
                break;
            }
            case MSG_REVERSE: {
                body = "gnirts siht esrever esaelP";
                header.type = MSG_REVERSE;
                header.length = strlen(body) + 1;
                break;
            }
            case MSG_TIME: {
                body = ""; // Body is empty for a time request
                header.type = MSG_TIME;
                header.length = strlen(body) + 1;
                break;
            }
        }
        
        printf("\n---> Sending message (type: %d, len: %" PRIu32 ")\n", header.type, header.length);

        // 3. Send message to server (header then body)
        MessageHeader net_header = { .type = htonl(header.type), .length = htonl(header.length) };
        if (robust_write(sock_fd, &net_header, sizeof(net_header)) == -1) break;
        if (robust_write(sock_fd, body, header.length) == -1) break;
        DEBUG_LOG("Message sent successfully.");

        // 4. Read response from server
        MessageHeader resp_header = {0};
        if (robust_read(sock_fd, &resp_header, sizeof(resp_header)) == -1) break;

        resp_header.type = ntohl(resp_header.type);
        resp_header.length = ntohl(resp_header.length);

        if (resp_header.length > 4096) {
             fprintf(stderr, "Server response too large (%" PRIu32 " bytes). Aborting.\n", resp_header.length);
             break;
        }

        char resp_body[resp_header.length];
        if (robust_read(sock_fd, resp_body, resp_header.length) == -1) break;

        printf("<--- Received response (type: %" PRIu32 ", len: %" PRIu32 ")\n", resp_header.type, resp_header.length);
        printf("     Body: \"%s\"\n", resp_body);
    }

    printf("\nShutdown signal received. Closing connection.\n");
    close(sock_fd);
    return EXIT_SUCCESS;
}

ssize_t robust_read(int fd, void *buf, size_t count) {
    assert(buf != NULL);
    assert(fd >= 0);

    size_t total_read = 0;
    while (total_read < count) {
        ssize_t bytes_read = read(fd, (char*)buf + total_read, count - total_read);
        if (bytes_read == -1) {
            perror("robust_read");
            return -1;
        }
        if (bytes_read == 0) { // Peer closed connection
            fprintf(stderr, "Server closed the connection.\n");
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

ssize_t robust_write(int fd, const void *buf, size_t count) {
    assert(buf != NULL);
    assert(fd >= 0);

    size_t total_written = 0;
    while (total_written < count) {
        ssize_t written = write(fd, (const char*)buf + total_written, count - total_written);
        if (written == -1) {
            perror("robust_write");
            return -1;
        }
        total_written += written;
    }
    return total_written;
}