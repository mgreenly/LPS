#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <sys/epoll.h>
#include "queue.h"

// State for each connected client
typedef struct {
    int fd;
    uint8_t header_buf[sizeof(message_header_t)];
    size_t bytes_read;
    message_header_t header;
    char* body_buf;
    int header_received;
} client_context_t;


int create_and_bind_socket(uint16_t port);
void handle_new_connection(int epoll_fd, int listen_fd);
void handle_client_data(client_context_t* ctx, queue_t* queue, int epoll_fd);
void cleanup_client(client_context_t* ctx, int epoll_fd);

#endif // NETWORK_H