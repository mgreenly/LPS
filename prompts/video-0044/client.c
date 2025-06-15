#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <inttypes.h>

#include "message.h"

#define SERVER_IP "127.0.0.1"
#define PORT 4242
#define RECV_BUFFER_SIZE 4096

// This global flag is the safe way to communicate from a signal handler
// to the main loop. It must be `volatile sig_atomic_t`.
static volatile sig_atomic_t keep_running = 1;

void handle_signal(int signal) {
    (void)signal;
    keep_running = 0;
}

// --- Forward Declarations ---
static int read_all(int fd, void *buf, size_t count);
static int write_all(int fd, const void *buf, size_t count);

int main(void) {
    // Setup signal handlers for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    srand(time(NULL));

    int sockfd;
    struct sockaddr_in serv_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("invalid address / address not supported");
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Starting message loop (press Ctrl+C to exit).\n");

    uint8_t recv_buffer[RECV_BUFFER_SIZE];

    while (keep_running) {
        // 1. Pause for a random duration
        long sleep_usec = 300000 + (rand() % 700001); // 0.3s to 1.0s
        usleep(sleep_usec);
        if (!keep_running) break;

        // 2. Randomly select and create a message
        message_type_t msg_type = rand() % 3;
        const char *body_str = "";
        switch (msg_type) {
            case MSG_ECHO: body_str = "This is an echo test message."; break;
            case MSG_REVERSE: body_str = "Able was I ere I saw Elba"; break;
            case MSG_TIME: body_str = ""; break; // Body is empty for time request
        }
        
        uint32_t body_len = strlen(body_str) + 1; // Include null terminator
        uint32_t total_len = sizeof(message_header_t) + body_len;
        uint8_t *send_buffer = malloc(total_len);
        if (!send_buffer) {
            perror("malloc failed");
            continue;
        }

        message_header_t *header = (message_header_t *)send_buffer;
        header->type = msg_type;
        header->length = htonl(body_len);
        memcpy(send_buffer + sizeof(message_header_t), body_str, body_len);

        // 3. Send the message to the server
        if (write_all(sockfd, send_buffer, total_len) < 0) {
            fprintf(stderr, "Failed to send message to server.\n");
            free(send_buffer);
            break;
        }
        free(send_buffer);

        // 4. Wait for and read the response
        message_header_t resp_header;
        if (read_all(sockfd, &resp_header, sizeof(resp_header)) < 0) {
            fprintf(stderr, "Failed to read response header from server.\n");
            break;
        }

        uint32_t resp_body_len = ntohl(resp_header.length);
        if (resp_body_len > RECV_BUFFER_SIZE) {
            fprintf(stderr, "Response body too large: %" PRIu32 "\n", resp_body_len);
            break;
        }

        if (read_all(sockfd, recv_buffer, resp_body_len) < 0) {
            fprintf(stderr, "Failed to read response body from server.\n");
            break;
        }
        
        // Ensure null termination for safety
        recv_buffer[resp_body_len - 1] = '\0';
        
        printf("Received response [Type: %d, Len: %" PRIu32 "]: '%s'\n", 
               resp_header.type, resp_body_len, (char*)recv_buffer);
    }

    printf("\nShutting down...\n");
    close(sockfd);
    return 0;
}

static int read_all(int fd, void *buf, size_t count) {
    size_t bytes_read = 0;
    while (bytes_read < count) {
        ssize_t res = read(fd, (char*)buf + bytes_read, count - bytes_read);
        if (res < 0) {
            if (errno == EINTR) continue;
            perror("read failed");
            return -1;
        }
        if (res == 0) {
            fprintf(stderr, "Server closed connection prematurely.\n");
            return -1;
        }
        bytes_read += res;
    }
    return 0;
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