#ifndef WORKER_H
#define WORKER_H

#include "queue.h"

typedef struct {
    int num_threads;
    pthread_t* threads;
    queue_t* queue;
} worker_pool_t;

worker_pool_t* worker_pool_create(int num_threads, queue_t* queue);
void worker_pool_destroy(worker_pool_t* pool);

#endif // WORKER_H