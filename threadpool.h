#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "queue.h"

typedef void (*thread_func_t)(void *arg);

typedef struct ThreadPool
{
    pthread_t *threads;
    size_t thread_count;
    Queue *task_queue;
    thread_func_t worker_func;
    void *worker_arg;
    int shutdown;
} ThreadPool;

// Threadpool operations
ThreadPool *threadpool_create(size_t thread_count, thread_func_t worker_func, void *worker_arg);
void threadpool_destroy(ThreadPool *pool);
int threadpool_add_task(ThreadPool *pool, void *task);

#endif // THREADPOOL_H