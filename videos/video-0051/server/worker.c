#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "worker.h"
#include "common/log.h"

// Helper to reverse a string in place
static void reverse_string(char* str) {
    if (!str) return;
    int len = strlen(str);
    for (int i = 0; i < len / 2; i++) {
        char temp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = temp;
    }
}

// Helper to write data robustly, handling partial writes
static int write_all(int fd, const void* buf, size_t count) {
    size_t bytes_written = 0;
    while (bytes_written < count) {
        ssize_t res = write(fd, (const char*)buf + bytes_written, count - bytes_written);
        if (res < 0) {
            log_error("Failed to write to socket %d", fd);
            return -1;
        }
        bytes_written += res;
    }
    return 0;
}

// Processes a single message and sends a response
static void process_message(message_t* request) {
    assert(request != NULL);
    log_debug("Worker thread processing message type %u for fd %d", request->header.type, request->client_fd);

    message_t response;
    response.client_fd = request->client_fd;
    response.header.type = request->header.type;

    char time_buf[100]; // For time message

    switch (request->header.type) {
        case MSG_ECHO:
            response.header.length = request->header.length;
            response.body = request->body; // Re-use the body
            break;
        case MSG_REVERSE:
            reverse_string(request->body);
            response.header.length = request->header.length;
            response.body = request->body;
            break;
        case MSG_TIME:
            {
                time_t now = time(NULL);
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
                response.body = time_buf;
                response.header.length = strlen(time_buf) + 1; // Include null terminator
            }
            break;
        default:
            log_error("Unknown message type: %u", request->header.type);
            // Free request memory and do not send a response
            free(request->body);
            free(request);
            return;
    }

    // Send header
    if (write_all(response.client_fd, &response.header, sizeof(message_header_t)) != 0) {
        log_error("Failed to send response header to fd %d", response.client_fd);
    } else if (write_all(response.client_fd, response.body, response.header.length) != 0) {
        // Send body
        log_error("Failed to send response body to fd %d", response.client_fd);
    } else {
        log_info("Sent response type %u to fd %d", response.header.type, response.client_fd);
    }

    // Cleanup request memory
    free(request->body);
    free(request);
}


static void* worker_thread_func(void* arg) {
    queue_t* queue = (queue_t*)arg;
    while (1) {
        message_t* msg = queue_pop(queue);
        if (msg == NULL) { // NULL is the poison pill
            log_debug("Worker thread received poison pill. Exiting.");
            break;
        }
        process_message(msg);
    }
    return NULL;
}

worker_pool_t* worker_pool_create(int num_threads, queue_t* queue) {
    assert(num_threads > 0 && queue != NULL);
    worker_pool_t* pool = malloc(sizeof(worker_pool_t));
    if (!pool) {
        log_error("Failed to allocate memory for worker pool");
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->queue = queue;
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        log_error("Failed to allocate memory for worker threads");
        free(pool);
        return NULL;
    }

    for (int i = 0; i < num_threads; ++i) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_func, queue) != 0) {
            log_error("Failed to create worker thread %d", i);
            // Cleanup already created threads
            pool->num_threads = i;
            worker_pool_destroy(pool);
            return NULL;
        }
    }
    log_info("Worker pool with %d threads created.", num_threads);
    return pool;
}

void worker_pool_destroy(worker_pool_t* pool) {
    assert(pool != NULL);
    log_info("Destroying worker pool...");

    // Send a "poison pill" to each worker to make it exit its loop
    for (int i = 0; i < pool->num_threads; ++i) {
        queue_push(pool->queue, NULL);
    }

    // Wait for all threads to finish
    for (int i = 0; i < pool->num_threads; ++i) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    free(pool);
    log_info("Worker pool destroyed.");
}
