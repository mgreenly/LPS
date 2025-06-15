#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>

#include "message.h"

#define SERVER_IP "127.0.0.1"
#define PORT 4242

// A volatile sig_atomic_t is safe to use in a signal handler
static volatile sig_atomic_t running = 1;

void handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = 0;
    }
}

// Helper to ensure all data is read, handling partial reads
static int read_full(int fd, void *buf, size_t len) {
    char *ptr = (char *)buf;
    while (len > 0) {
        ssize_t received = recv(fd, ptr, len, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return -1;
        }
        if (received == 0) {
            fprintf(stderr, "Server closed connection while reading\n");
            return -1;
        }
        ptr += received;
        len -= received;
    }
    return 0;
}

// Helper to ensure all data is sent, handling partial writes
static int send_full(int fd, const void *buf, size_t len) {
    const char *ptr = (const char*) buf;
    while (len > 0) {
        ssize_t sent = send(fd, ptr, len, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return -1;
        }
        ptr += sent;
        len -= sent;
    }
    return 0;
}

int main(void) {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    srand(time(NULL));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Starting message loop (press Ctrl+C to exit).\n");

    while (running) {
        // 1. Pause for a random duration
        double delay = 0.3 + (rand() / (double)RAND_MAX) * 0.7;
        struct timespec ts;
        ts.tv_sec = (time_t)delay;
        ts.tv_nsec = (long)((delay - ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
        
        if (!running) break;

        // 2. Select and create a random message
        message_type_t type = rand() % 3;
        char body[MAX_BODY_LEN] = {0};
        uint32_t body_len = 0;

        switch (type) {
            case MSG_ECHO:
                snprintf(body, sizeof(body), "An echo message from the client");
                break;
            case MSG_REVERSE:
                snprintf(body, sizeof(body), "sdrawkcab gnihtemos etirw");
                break;
            case MSG_TIME:
                // Body is empty for a time request
                body[0] = '\0'; 
                break;
        }
        body_len = strlen(body) + 1; // Include null terminator

        // 3. Construct and send the message
        uint8_t send_buf[MAX_MSG_LEN];
        message_header_t header;
        header.type = htonl(type);
        header.length = htonl(body_len);

        memcpy(send_buf, &header, sizeof(header));
        memcpy(send_buf + sizeof(header), body, body_len);

        printf("Sending message type %u...\n", type);
        if (send_full(sock, send_buf, sizeof(header) + body_len) != 0) {
            fprintf(stderr, "Failed to send message, exiting.\n");
            break;
        }

        // 4. Receive and process the response
        message_header_t resp_header;
        if (read_full(sock, &resp_header, sizeof(resp_header)) != 0) {
            fprintf(stderr, "Failed to read response header, exiting.\n");
            break;
        }
        resp_header.type = ntohl(resp_header.type);
        resp_header.length = ntohl(resp_header.length);

        if (resp_header.length > MAX_BODY_LEN) {
            fprintf(stderr, "Received invalid response length, exiting.\n");
            break;
        }

        char resp_body[MAX_BODY_LEN];
        if (read_full(sock, resp_body, resp_header.length) != 0) {
            fprintf(stderr, "Failed to read response body, exiting.\n");
            break;
        }

        printf("  > Response type %" PRIu32 ", body: \"%s\"\n", resp_header.type, resp_body);
    }

    printf("\nSignal caught, shutting down gracefully...\n");
    close(sock);

    return EXIT_SUCCESS;
}