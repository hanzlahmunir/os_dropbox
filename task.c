#include "task.h"
#include <stdlib.h>
#include <string.h>

Task *task_create(TaskType type, User *user, int client_socket, const char *filename)
{
    Task *task = (Task *)malloc(sizeof(Task));
    if (!task)
        return NULL;

    task->type = type;
    task->user = user;
    task->client_socket = client_socket;

    if (filename)
    {
        strncpy(task->filename, filename, MAX_FILENAME - 1);
        task->filename[MAX_FILENAME - 1] = '\0';
    }
    else
    {
        task->filename[0] = '\0';
    }

    task->upload_data = NULL;
    task->upload_size = 0;
    task->status = 0;
    task->result_data = NULL;
    task->result_size = 0;
    task->error_msg[0] = '\0';
    task->completed = 0;

    // Phase 2: Initialize condition variable and mutex
    pthread_mutex_init(&task->mutex, NULL);
    pthread_cond_init(&task->cond, NULL);

    return task;
}

void task_destroy(Task *task)
{
    if (!task)
        return;

    if (task->upload_data)
    {
        free(task->upload_data);
    }

    if (task->result_data)
    {
        free(task->result_data);
    }

    // Phase 2: Destroy condition variable and mutex
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->cond);

    free(task);
}