// client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include "message.h"

#define SERVER_IP "127.0.0.1"
#define PORT 4242
#define MAX_BODY_SIZE 1024

// This flag is used to signal the main loop to terminate.
// 'volatile sig_atomic_t' is the required type for signal-safe variables.
static volatile sig_atomic_t stop_flag = 0;

// --- Forward Declarations ---
static void handle_signal(int signal);
static int connect_to_server(void);
static void sleep_randomly(void);
static void create_and_send_random_message(int server_fd);
static void read_and_print_response(int server_fd);
static ssize_t read_full(int fd, void *buf, size_t count);
static ssize_t send_full(int fd, const void *buf, size_t count);

int main(void) {
    // Seed the random number generator
    srand(time(NULL));

    // Set up signal handlers for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int server_fd = connect_to_server();
    if (server_fd < 0) {
        return EXIT_FAILURE;
    }

    printf("Connected to server. Starting message loop (press Ctrl+C to exit).\n");

    while (!stop_flag) {
        sleep_randomly();
        if (stop_flag) break; // Check again after sleeping
        create_and_send_random_message(server_fd);
        read_and_print_response(server_fd);
    }
    
    printf("\nGracefully shutting down...\n");
    close(server_fd);

    return EXIT_SUCCESS;
}

// Signal handler to set the stop flag.
static void handle_signal(int signal) {
    (void)signal; // Unused parameter
    stop_flag = 1;
}

// Connects to the server and returns the socket fd.
static int connect_to_server(void) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

// Pauses execution for a random duration between 0.3 and 1.0 seconds.
static void sleep_randomly(void) {
    double random_val = (double)rand() / RAND_MAX; // 0.0 to 1.0
    double sleep_duration_sec = 0.3 + (random_val * 0.7); // Scale to 0.3 - 1.0

    struct timespec ts;
    ts.tv_sec = (time_t)sleep_duration_sec;
    ts.tv_nsec = (long)((sleep_duration_sec - ts.tv_sec) * 1e9);

    nanosleep(&ts, NULL);
}

// Creates a message of a random type and sends it.
static void create_and_send_random_message(int server_fd) {
    assert(server_fd >= 0);

    MessageHeader header;
    char body[MAX_BODY_SIZE];
    const char *message_type_str = "UNKNOWN";

    MessageType type = rand() % 3;
    header.type = type;

    switch (type) {
        case MSG_ECHO:
            snprintf(body, MAX_BODY_SIZE, "This is an echo test.");
            message_type_str = "ECHO";
            break;
        case MSG_REVERSE:
            snprintf(body, MAX_BODY_SIZE, "Please reverse this string.");
            message_type_str = "REVERSE";
            break;
        case MSG_TIME:
            body[0] = '\0'; // Time message has no body in the request
            message_type_str = "TIME";
            break;
    }
    
    header.length = strlen(body) + 1; // Include null terminator

    printf("\n--> Sending %s request. Body: \"%s\"\n", message_type_str, body);

    // Prepare message for network transmission (host to network byte order)
    uint32_t net_type = htonl(header.type);
    uint32_t net_length = htonl(header.length);

    // Send header
    send_full(server_fd, &net_type, sizeof(net_type));
    send_full(server_fd, &net_length, sizeof(net_length));
    
    // Send body
    if (header.length > 0) {
        send_full(server_fd, body, header.length);
    }
}

// Reads the server's response and prints it.
static void read_and_print_response(int server_fd) {
    assert(server_fd >= 0);

    MessageHeader header;
    if (read_full(server_fd, &header, sizeof(header)) <= 0) {
        fprintf(stderr, "Server closed connection while waiting for response header.\n");
        stop_flag = 1;
        return;
    }
    
    // Convert from network byte order to host byte order
    header.type = ntohl(header.type);
    header.length = ntohl(header.length);

    if (header.length > MAX_BODY_SIZE) {
        fprintf(stderr, "Received oversized response body (len=%" PRIu32 "), disconnecting.\n", header.length);
        stop_flag = 1;
        return;
    }

    char body[MAX_BODY_SIZE];
    if (header.length > 0) {
        if (read_full(server_fd, body, header.length) <= 0) {
            fprintf(stderr, "Server closed connection while waiting for response body.\n");
            stop_flag = 1;
            return;
        }
    } else {
        body[0] = '\0';
    }

    printf("<-- Received response. Body: \"%s\"\n", body);
}

// --- IO Helpers (Identical to server for consistency) ---

static ssize_t read_full(int fd, void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);
    ssize_t bytes_read = 0;
    size_t total_read = 0;
    char *ptr = buf;
    while (total_read < count) {
        bytes_read = read(fd, ptr + total_read, count - total_read);
        if (bytes_read <= 0) { return bytes_read; }
        total_read += bytes_read;
    }
    return total_read;
}

static ssize_t send_full(int fd, const void *buf, size_t count) {
    assert(fd >= 0);
    assert(buf != NULL);
    ssize_t bytes_sent = 0;
    size_t total_sent = 0;
    const char *ptr = buf;
    while (total_sent < count) {
        bytes_sent = send(fd, ptr + total_sent, count - total_sent, 0);
        if (bytes_sent < 0) { return -1; }
        total_sent += bytes_sent;
    }
    return total_sent;
}