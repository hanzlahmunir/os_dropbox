#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

Queue *queue_create()
{
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q)
        return NULL;

    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    q->shutdown = 0;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);

    return q;
}

void queue_destroy(Queue *q)
{
    if (!q)
        return;

    pthread_mutex_lock(&q->mutex);

    // Free all remaining nodes
    QueueNode *current = q->head;
    while (current)
    {
        QueueNode *next = current->next;
        free(current);
        current = next;
    }

    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);

    free(q);
}

int queue_push(Queue *q, void *data)
{
    if (!q || !data)
        return -1;

    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));
    if (!node)
        return -1;

    node->data = data;
    node->next = NULL;

    pthread_mutex_lock(&q->mutex);

    if (q->tail)
    {
        q->tail->next = node;
        q->tail = node;
    }
    else
    {
        q->head = node;
        q->tail = node;
    }

    q->size++;

    // Signal waiting threads
    pthread_cond_signal(&q->cond);

    pthread_mutex_unlock(&q->mutex);

    return 0;
}

void *queue_pop(Queue *q)
{
    if (!q)
        return NULL;

    pthread_mutex_lock(&q->mutex);

    // Wait while queue is empty and not shutting down
    while (q->size == 0 && !q->shutdown)
    {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    // If shutting down and queue is empty, return NULL
    if (q->shutdown && q->size == 0)
    {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    QueueNode *node = q->head;
    void *data = node->data;

    q->head = node->next;
    if (!q->head)
    {
        q->tail = NULL;
    }

    q->size--;

    pthread_mutex_unlock(&q->mutex);

    free(node);
    return data;
}

size_t queue_size(Queue *q)
{
    if (!q)
        return 0;

    pthread_mutex_lock(&q->mutex);
    size_t size = q->size;
    pthread_mutex_unlock(&q->mutex);

    return size;
}

void queue_shutdown(Queue *q)
{
    if (!q)
        return;

    pthread_mutex_lock(&q->mutex);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->cond); // Wake all waiting threads
    pthread_mutex_unlock(&q->mutex);
}