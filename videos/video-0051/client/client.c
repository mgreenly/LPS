#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include "common/log.h"
#include "common/message.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4242
#define MAX_BODY_LEN 256

volatile sig_atomic_t g_shutdown_flag = 0;

static void signal_handler(int signum) {
    (void)signum;
    g_shutdown_flag = 1;
    const char msg[] = "\nClient caught signal, shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// Helper to write data robustly
static int write_all(int fd, const void* buf, size_t count) {
    size_t bytes_written = 0;
    while (bytes_written < count) {
        ssize_t res = write(fd, (const char*)buf + bytes_written, count - bytes_written);
        if (res < 0) {
            if (errno == EINTR) continue;
            log_error("write failed");
            return -1;
        }
        bytes_written += res;
    }
    return 0;
}

// Helper to read data robustly
static int read_all(int fd, void* buf, size_t count) {
    size_t bytes_read = 0;
    while (bytes_read < count) {
        ssize_t res = read(fd, (char*)buf + bytes_read, count - bytes_read);
        if (res < 0) {
            if (errno == EINTR) continue;
            log_error("read failed");
            return -1;
        }
        if (res == 0) {
            log_error("Server closed connection unexpectedly");
            return -1;
        }
        bytes_read += res;
    }
    return 0;
}


int main(void) {
    setup_signal_handlers();
    srand(time(NULL));

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        log_error("Socket creation failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        log_error("Invalid address/ Address not supported");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        log_error("Connection Failed");
        close(sock_fd);
        return EXIT_FAILURE;
    }
    log_info("Connected to server at %s:%d", SERVER_IP, SERVER_PORT);

    const char* sample_texts[] = { "hello world", "c programming", "epoll is fun" };
    int num_texts = sizeof(sample_texts) / sizeof(sample_texts[0]);

    while (!g_shutdown_flag) {
        // 1. Pause randomly
        long sleep_usec = 300000 + (rand() % 700001); // 0.3s to 1.0s
        usleep(sleep_usec);

        // 2. Create a random message
        message_t request;
        request.header.type = rand() % 3; // 0, 1, or 2
        
        char request_body[MAX_BODY_LEN];
        if (request.header.type == MSG_TIME) {
            request_body[0] = '\0'; // Time message has no body
        } else {
            strncpy(request_body, sample_texts[rand() % num_texts], MAX_BODY_LEN - 1);
            request_body[MAX_BODY_LEN - 1] = '\0';
        }
        request.header.length = strlen(request_body) + 1; // Include null terminator

        // 3. Send the message
        log_info("Sending message type %u with body: \"%s\"", request.header.type, request_body);
        if (write_all(sock_fd, &request.header, sizeof(message_header_t)) < 0) break;
        if (write_all(sock_fd, request_body, request.header.length) < 0) break;

        // 4. Receive the response
        message_header_t response_header;
        if (read_all(sock_fd, &response_header, sizeof(message_header_t)) < 0) break;

        char response_body[MAX_BODY_LEN] = {0};
        if (response_header.length > 0 && response_header.length <= MAX_BODY_LEN) {
            if (read_all(sock_fd, response_body, response_header.length) < 0) break;
        } else if (response_header.length > MAX_BODY_LEN) {
            log_error("Server sent response body too large: %" PRIu32, response_header.length);
            break;
        }

        log_info("Received response type %u with body: \"%s\"", response_header.type, response_body);
    }

    log_info("Client shutting down.");
    close(sock_fd);
    return EXIT_SUCCESS;
}