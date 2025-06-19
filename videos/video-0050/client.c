// client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "message.h"

// -- Globals --
static volatile sig_atomic_t g_running = 1;

// -- Logging Utility --
#define log_info(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define log_error(fmt, ...) fprintf(stderr, "[ERROR] " fmt ": %s\n", ##__VA_ARGS__, strerror(errno))

// -- Utility Functions --
void handle_signal(int signal) {
    (void)signal;
    g_running = 0;
    log_info("Shutdown signal received, terminating gracefully.");
}

void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// Full read/write to handle partial sends/reads
ssize_t full_write(int fd, const void* buf, size_t count) {
    assert(buf != NULL);
    size_t bytes_sent = 0;
    while(bytes_sent < count) {
        ssize_t n = write(fd, (const char*)buf + bytes_sent, count - bytes_sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return bytes_sent; // Should not happen for blocking socket
        bytes_sent += n;
    }
    return bytes_sent;
}

ssize_t full_read(int fd, void* buf, size_t count) {
    assert(buf != NULL);
    size_t bytes_read = 0;
    while(bytes_read < count) {
        ssize_t n = read(fd, (char*)buf + bytes_read, count - bytes_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return bytes_read; // Peer closed connection
        bytes_read += n;
    }
    return bytes_read;
}

// -- Main Client Logic --
int main() {
    setup_signal_handlers();
    srand(time(NULL));

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_error("Failed to create socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) {
        log_error("Invalid address or address not supported");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        log_error("Connection failed");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    log_info("Connected to server at %s:%d", HOST, PORT);

    while (g_running) {
        // 1. Pause for 0.3 to 1.0 seconds
        double pause_duration = 0.3 + ((double)rand() / RAND_MAX) * 0.7;
        struct timespec ts = { .tv_sec = (time_t)pause_duration, .tv_nsec = (long)((pause_duration - (time_t)pause_duration) * 1e9) };
        nanosleep(&ts, NULL);
        if (!g_running) break;

        // 2. Select and create a random message
        MessageType type = (MessageType)(rand() % 3);
        const char* body = "Hello, World! This is a test message.";
        if (type == MSG_TIME) {
            body = ""; // Time message has no body from client
        }

        size_t body_len = strlen(body) + 1;
        MessageHeader header = { .type = type, .length = (uint32_t)body_len };

        size_t total_len = sizeof(header) + body_len;
        uint8_t *request_buf = malloc(total_len);
        assert(request_buf != NULL);

        memcpy(request_buf, &header, sizeof(header));
        memcpy(request_buf + sizeof(header), body, body_len);

        log_info("Sending message type %d with body '%s'", type, body);

        // 3. Send message to server
        if (full_write(sock_fd, request_buf, total_len) < 0) {
            log_error("Failed to send message");
            free(request_buf);
            break;
        }
        free(request_buf);

        // 4. Receive response from server
        MessageHeader resp_header;
        if (full_read(sock_fd, &resp_header, sizeof(resp_header)) != sizeof(resp_header)) {
            log_error("Failed to read response header or server disconnected");
            break;
        }

        if (resp_header.length > MAX_MSG_BODY_LEN) {
             log_error("Response body too long: %" PRIu32, resp_header.length);
             break;
        }

        char resp_body[MAX_MSG_BODY_LEN];
        if (full_read(sock_fd, resp_body, resp_header.length) != (ssize_t)resp_header.length) {
            log_error("Failed to read response body or server disconnected");
            break;
        }

        log_info("Received response type %d, length %" PRIu32 ", body: '%s'",
                 resp_header.type, resp_header.length, resp_body);
    }

    log_info("Shutting down client...");
    close(sock_fd);
    log_info("Client shut down cleanly.");

    return EXIT_SUCCESS;
}
