#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <inttypes.h>
#include <errno.h>

#include "message.h"

#define PORT 4242
#define SERVER_IP "127.0.0.1"
#define RESPONSE_BUFFER_SIZE 4096

// Global flag for signal handling
volatile sig_atomic_t keep_running = 1;

// --- Utility Functions (from server, adapted for client) ---

int read_full(int fd, void *buf, size_t count) {
    ssize_t bytes_read;
    size_t total_bytes_read = 0;
    char *ptr = buf;

    while (total_bytes_read < count) {
        bytes_read = read(fd, ptr + total_bytes_read, count - total_bytes_read);
        if (bytes_read == 0) return -1; // EOF
        if (bytes_read < 0) {
            perror("read");
            return -2;
        }
        total_bytes_read += bytes_read;
    }
    return 0;
}

int write_full(int fd, const void *buf, size_t count) {
    ssize_t bytes_written;
    size_t total_bytes_written = 0;
    const char *ptr = buf;

    while (total_bytes_written < count) {
        bytes_written = write(fd, ptr + total_bytes_written, count - total_bytes_written);
        if (bytes_written < 0) {
            perror("write");
            return -1;
        }
        total_bytes_written += bytes_written;
    }
    return 0;
}


// Signal handler to gracefully shut down
void handle_signal(int signum) {
    // In a real application, you might need to be careful what you do here.
    // Setting a flag is one of the few safe operations.
    (void)signum;
    keep_running = 0;
}

void setup_signal_handlers(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

// Main client function
int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    setup_signal_handlers();

    // Seed random number generator
    srand(time(NULL));

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Starting message loop (press Ctrl+C to exit).\n");
    
    char response_buffer[RESPONSE_BUFFER_SIZE];

    while (keep_running) {
        // 1. Pause for a random time between 0.3 and 1.0 seconds
        long nano_sleep = (long)((0.3 + (double)rand() / RAND_MAX * 0.7) * 1e9);
        struct timespec sleep_time = { .tv_sec = 0, .tv_nsec = nano_sleep };
        nanosleep(&sleep_time, NULL);
        
        if (!keep_running) break;

        // 2. Select a random message type and create the message
        MessageHeader header;
        char *body = NULL;
        const char* type_str = "";
        
        header.type = rand() % 3;

        switch (header.type) {
            case MSG_ECHO: {
                const char* msg = "A journey of a thousand miles begins with a single step.";
                body = strdup(msg);
                header.message_len = strlen(body) + 1;
                type_str = "ECHO";
                break;
            }
            case MSG_REVERSE: {
                const char* msg = "!dlroW ,olleH";
                body = strdup(msg);
                header.message_len = strlen(body) + 1;
                type_str = "REVERSE";
                break;
            }
            case MSG_TIME: {
                body = NULL; // No body needed for time request
                header.message_len = 0;
                type_str = "TIME";
                break;
            }
        }
        
        printf("\nSending %s request...\n", type_str);

        // 3. Send the message to the server
        if (write_full(sockfd, &header, sizeof(MessageHeader)) != 0) {
            fprintf(stderr, "Failed to write header. Shutting down.\n");
            break;
        }
        if (header.message_len > 0) {
            if (write_full(sockfd, body, header.message_len) != 0) {
                fprintf(stderr, "Failed to write body. Shutting down.\n");
                free(body);
                break;
            }
        }
        free(body);

        // 4. Read the response from the server
        MessageHeader response_header;
        if (read_full(sockfd, &response_header, sizeof(MessageHeader)) != 0) {
            fprintf(stderr, "Server closed connection or error while reading response header. Shutting down.\n");
            break;
        }

        if (response_header.message_len > 0 && response_header.message_len < RESPONSE_BUFFER_SIZE) {
            if (read_full(sockfd, response_buffer, response_header.message_len) != 0) {
                fprintf(stderr, "Error reading response body. Shutting down.\n");
                break;
            }
            // Ensure null termination for printing
            response_buffer[response_header.message_len - 1] = '\0';
            printf("Server response: [Type: %" PRIu32 ", Len: %" PRIu32 "] Body: \"%s\"\n", 
                   response_header.type, response_header.message_len, response_buffer);
        } else if (response_header.message_len >= RESPONSE_BUFFER_SIZE) {
            fprintf(stderr, "Server response too large for buffer. Shutting down.\n");
            break;
        } else {
            // Response with no body, e.g. an ACK
            printf("Server response: [Type: %" PRIu32 ", Len: 0] (No body)\n", response_header.type);
        }
    }

    printf("\nSignal caught, shutting down gracefully...\n");
    close(sockfd);
    return EXIT_SUCCESS;
}