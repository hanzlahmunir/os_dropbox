#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

// Generic queue node
typedef struct QueueNode
{
    void *data;
    struct QueueNode *next;
} QueueNode;

// Thread-safe queue
typedef struct Queue
{
    QueueNode *head;
    QueueNode *tail;
    size_t size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown; // Flag for graceful shutdown
} Queue;

// Queue operations
Queue *queue_create();
void queue_destroy(Queue *q);
int queue_push(Queue *q, void *data);
void *queue_pop(Queue *q);
size_t queue_size(Queue *q);
void queue_shutdown(Queue *q);

#endif // QUEUE_H