#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <signal.h>

#include "common/log.h"
#include "common/message.h"
#include "signal.h"
#include "network.h"
#include "queue.h"
#include "worker.h"

#define PORT 4242
#define MAX_EVENTS 64
#define NUM_WORKER_THREADS 4
#define QUEUE_CAPACITY 256

int main(void) {
    setup_signal_handlers();

    queue_t* msg_queue = queue_create(QUEUE_CAPACITY);
    if (!msg_queue) {
        log_error("Failed to create message queue. Exiting.");
        return EXIT_FAILURE;
    }

    worker_pool_t* worker_pool = worker_pool_create(NUM_WORKER_THREADS, msg_queue);
    if (!worker_pool) {
        log_error("Failed to create worker pool. Exiting.");
        queue_destroy(msg_queue);
        return EXIT_FAILURE;
    }

    int listen_fd = create_and_bind_socket(PORT);
    if (listen_fd < 0) {
        log_error("Failed to create listening socket. Exiting.");
        worker_pool_destroy(worker_pool);
        queue_destroy(msg_queue);
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        log_error("epoll_create1 failed");
        close(listen_fd);
        worker_pool_destroy(worker_pool);
        queue_destroy(msg_queue);
        return EXIT_FAILURE;
    }

    struct epoll_event event;
    event.data.fd = listen_fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        log_error("epoll_ctl on listen_fd failed");
        close(listen_fd);
        close(epoll_fd);
        worker_pool_destroy(worker_pool);
        queue_destroy(msg_queue);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];

    log_info("Server started successfully. Waiting for connections...");

    while (!g_shutdown_flag) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // Wait indefinitely
        if (n < 0) {
            if (g_shutdown_flag) break;
            log_error("epoll_wait failed");
            continue;
        }

        for (int i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                log_error("epoll error on fd %d", ((client_context_t*)events[i].data.ptr)->fd);
                cleanup_client((client_context_t*)events[i].data.ptr, epoll_fd);
            } else if (events[i].data.fd == listen_fd) {
                // New connection
                while (!g_shutdown_flag) {
                    handle_new_connection(epoll_fd, listen_fd);
                    // In ET mode, we must accept all pending connections
                    // handle_new_connection will return on EAGAIN
                }
            } else {
                // Data from a client
                client_context_t* ctx = (client_context_t*)events[i].data.ptr;
                handle_client_data(ctx, msg_queue, epoll_fd);
            }
        }
    }

    log_info("Shutdown initiated. Cleaning up resources...");
    close(listen_fd);
    close(epoll_fd);
    worker_pool_destroy(worker_pool);
    queue_destroy(msg_queue);

    log_info("Server shutdown complete.");
    return EXIT_SUCCESS;
}
