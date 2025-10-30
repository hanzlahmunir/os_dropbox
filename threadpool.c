#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

// Worker thread function
static void *thread_worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;

    while (1)
    {
        void *task = queue_pop(pool->task_queue);

        if (!task)
        {
            // Queue shutdown, exit thread
            break;
        }

        // Execute worker function
        pool->worker_func(task);
    }

    return NULL;
}

ThreadPool *threadpool_create(size_t thread_count, thread_func_t worker_func, void *worker_arg)
{
    if (thread_count == 0 || !worker_func)
        return NULL;

    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (!pool)
        return NULL;

    pool->thread_count = thread_count;
    pool->worker_func = worker_func;
    pool->worker_arg = worker_arg;
    pool->shutdown = 0;

    pool->task_queue = queue_create();
    if (!pool->task_queue)
    {
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t *)malloc(thread_count * sizeof(pthread_t));
    if (!pool->threads)
    {
        queue_destroy(pool->task_queue);
        free(pool);
        return NULL;
    }

    // Create worker threads
    for (size_t i = 0; i < thread_count; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, pool) != 0)
        {
            // Failed to create thread, cleanup
            pool->thread_count = i;
            threadpool_destroy(pool);
            return NULL;
        }
    }

    return pool;
}

void threadpool_destroy(ThreadPool *pool)
{
    if (!pool)
        return;

    pool->shutdown = 1;
    queue_shutdown(pool->task_queue);

    // Wait for all threads to finish
    for (size_t i = 0; i < pool->thread_count; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    queue_destroy(pool->task_queue);
    free(pool);
}

int threadpool_add_task(ThreadPool *pool, void *task)
{
    if (!pool || !task)
        return -1;
    return queue_push(pool->task_queue, task);
}