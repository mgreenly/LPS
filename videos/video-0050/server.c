// server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>

#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "message.h"

// -- Globals --
// Global flag to control the main loop, marked volatile and sig_atomic_t
// for safe access from a signal handler.
static volatile sig_atomic_t g_running = 1;

// -- Logging Utility --
#define log_info(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define log_error(fmt, ...) fprintf(stderr, "[ERROR] " fmt ": %s\n", ##__VA_ARGS__, strerror(errno))

// -- Client State --
// Represents the state of a single client connection.
typedef struct {
    int fd;
    uint8_t buffer[MAX_MSG_LEN];
    size_t bytes_read;
} ClientConnection;

// -- Work Queue Item --
// Represents a task for a worker thread.
typedef struct {
    int client_fd;
    MessageType type;
    char body[MAX_MSG_BODY_LEN];
} WorkItem;

// -- Thread-Safe Queue --
typedef struct QueueNode {
    WorkItem *item;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode *head;
    QueueNode *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Queue;

Queue* queue_create() {
    Queue *q = malloc(sizeof(Queue));
    assert(q != NULL);
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

void queue_destroy(Queue *q) {
    assert(q != NULL);
    while (q->head != NULL) {
        QueueNode *temp = q->head;
        q->head = q->head->next;
        free(temp->item);
        free(temp);
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    free(q);
}

void queue_push(Queue *q, WorkItem *item) {
    assert(q != NULL);
    QueueNode *newNode = malloc(sizeof(QueueNode));
    assert(newNode != NULL);
    newNode->item = item;
    newNode->next = NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->tail == NULL) {
        q->head = q->tail = newNode;
    } else {
        q->tail->next = newNode;
        q->tail = newNode;
    }
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

WorkItem* queue_pop(Queue *q) {
    assert(q != NULL);
    pthread_mutex_lock(&q->mutex);
    while (q->head == NULL) {
        // Check for shutdown condition while waiting
        if (!g_running) {
            pthread_mutex_unlock(&q->mutex);
            return NULL;
        }
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    QueueNode *temp = q->head;
    WorkItem *item = temp->item;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    free(temp);

    pthread_mutex_unlock(&q->mutex);
    return item;
}

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

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// -- Worker Thread Logic --
void send_response(int client_fd, MessageType type, const char *body) {
    assert(body != NULL);
    size_t body_len = strlen(body) + 1; // Include null terminator
    MessageHeader header = { .type = type, .length = (uint32_t)body_len };

    size_t total_len = sizeof(header) + body_len;
    uint8_t *response_buf = malloc(total_len);
    assert(response_buf != NULL);

    memcpy(response_buf, &header, sizeof(header));
    memcpy(response_buf + sizeof(header), body, body_len);

    ssize_t bytes_sent = 0;
    while(bytes_sent < (ssize_t)total_len) {
        ssize_t n = write(client_fd, response_buf + bytes_sent, total_len - bytes_sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // This shouldn't happen often with blocking sockets but handle it.
                continue;
            }
            log_error("Failed to write to client %d", client_fd);
            break;
        }
        bytes_sent += n;
    }

    free(response_buf);
}

void* worker_thread(void *arg) {
    Queue *q = (Queue*)arg;
    assert(q != NULL);
    log_info("Worker thread %ld started.", pthread_self());

    while (g_running) {
        WorkItem *item = queue_pop(q);
        if (item == NULL) { // Poison pill or shutdown signal
            break;
        }

        log_info("Worker processing message type %d for client %d", item->type, item->client_fd);

        switch (item->type) {
            case MSG_ECHO:
                send_response(item->client_fd, MSG_ECHO, item->body);
                break;
            case MSG_REVERSE: {
                size_t len = strlen(item->body);
                char *reversed_body = strdup(item->body);
                assert(reversed_body != NULL);
                for (size_t i = 0; i < len / 2; ++i) {
                    char temp = reversed_body[i];
                    reversed_body[i] = reversed_body[len - 1 - i];
                    reversed_body[len - 1 - i] = temp;
                }
                send_response(item->client_fd, MSG_REVERSE, reversed_body);
                free(reversed_body);
                break;
            }
            case MSG_TIME: {
                char time_buf[128];
                time_t now = time(NULL);
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                send_response(item->client_fd, MSG_TIME, time_buf);
                break;
            }
            default:
                log_error("Unknown message type: %d", item->type);
                break;
        }
        free(item);
    }

    log_info("Worker thread %ld finished.", pthread_self());
    return NULL;
}


// -- Main Server Logic --
void process_client_data(ClientConnection* conn, Queue* work_queue) {
    assert(conn != NULL);
    assert(work_queue != NULL);

    ssize_t n = read(conn->fd, conn->buffer + conn->bytes_read, MAX_MSG_LEN - conn->bytes_read);

    if (n == 0) {
        log_info("Client %d disconnected.", conn->fd);
        close(conn->fd);
        free(conn);
        return;
    }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Read error from client %d", conn->fd);
            close(conn->fd);
            free(conn);
        }
        return;
    }

    conn->bytes_read += n;

    // Process all complete messages in the buffer
    while (conn->bytes_read >= sizeof(MessageHeader)) {
        MessageHeader *header = (MessageHeader*)conn->buffer;
        if (conn->bytes_read >= sizeof(MessageHeader) + header->length) {
            // We have a full message
            size_t msg_total_len = sizeof(MessageHeader) + header->length;

            WorkItem *item = malloc(sizeof(WorkItem));
            assert(item != NULL);
            item->client_fd = conn->fd;
            item->type = header->type;
            memcpy(item->body, conn->buffer + sizeof(MessageHeader), header->length);

            queue_push(work_queue, item);

            // Shift remaining data in buffer to the front
            size_t remaining_bytes = conn->bytes_read - msg_total_len;
            memmove(conn->buffer, conn->buffer + msg_total_len, remaining_bytes);
            conn->bytes_read = remaining_bytes;
        } else {
            // Not enough data for the full body yet
            break;
        }
    }
}

int main() {
    setup_signal_handlers();

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        log_error("Failed to create listen socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(HOST);
    serv_addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        log_error("Failed to bind socket");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        log_error("Failed to listen on socket");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    set_nonblocking(listen_fd);

    // Create worker pool
    Queue* work_queue = queue_create();
    pthread_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; ++i) {
        if (pthread_create(&workers[i], NULL, worker_thread, work_queue) != 0) {
            log_error("Failed to create worker thread %d", i);
            g_running = 0; // Trigger shutdown
            break;
        }
    }

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        log_error("Failed to create epoll instance");
        g_running = 0; // Trigger shutdown
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        log_error("Failed to add listen_fd to epoll");
        g_running = 0; // Trigger shutdown
    }

    log_info("Server listening on %s:%d", HOST, PORT);

    struct epoll_event events[128];
    while (g_running) {
        int n_events = epoll_wait(epoll_fd, events, 128, 500); // 500ms timeout
        if (n_events < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, loop will terminate
            log_error("epoll_wait failed");
            break;
        }

        for (int i = 0; i < n_events; ++i) {
            if (events[i].data.fd == listen_fd) {
                // New connection
                struct sockaddr_in cli_addr;
                socklen_t cli_len = sizeof(cli_addr);
                int client_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_len);
                if (client_fd < 0) {
                    log_error("Failed to accept connection");
                    continue;
                }

                set_nonblocking(client_fd);

                ClientConnection* conn = malloc(sizeof(ClientConnection));
                assert(conn != NULL);
                conn->fd = client_fd;
                conn->bytes_read = 0;

                event.events = EPOLLIN | EPOLLET; // Edge-triggered
                event.data.ptr = conn;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                    log_error("Failed to add client_fd to epoll");
                    close(client_fd);
                    free(conn);
                } else {
                    log_info("Accepted new connection from %s:%d on fd %d",
                             inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), client_fd);
                }
            } else {
                // Data from existing client
                ClientConnection* conn = (ClientConnection*)events[i].data.ptr;
                if (events[i].events & EPOLLIN) {
                    process_client_data(conn, work_queue);
                }
            }
        }
    }

    log_info("Shutting down server...");

    // Signal workers to terminate by pushing NULL "poison pills"
    for (int i = 0; i < NUM_WORKERS; ++i) {
        queue_push(work_queue, NULL);
    }
    // Wait for all worker threads to finish
    for (int i = 0; i < NUM_WORKERS; ++i) {
        pthread_join(workers[i], NULL);
    }

    queue_destroy(work_queue);
    close(epoll_fd);
    close(listen_fd);
    log_info("Server shut down cleanly.");

    return EXIT_SUCCESS;
}
