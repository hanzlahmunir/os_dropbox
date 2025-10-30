#ifndef TASK_H
#define TASK_H

#include "user.h"
#include <stddef.h>
#include <pthread.h>

#define MAX_FILENAME 256

typedef enum
{
    TASK_UPLOAD,
    TASK_DOWNLOAD,
    TASK_DELETE,
    TASK_LIST
} TaskType;

typedef struct Task
{
    TaskType type;
    User *user;
    int client_socket;
    char filename[MAX_FILENAME];

    // For UPLOAD
    void *upload_data;
    size_t upload_size;

    // For results (filled by worker)
    int status;        // 0 = success, -1 = error
    char *result_data; // For DOWNLOAD content or LIST output
    size_t result_size;
    char error_msg[256];

    // Phase 2: Condition variable for signaling completion (no busy waiting)
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile int completed;
} Task;

// Task operations
Task *task_create(TaskType type, User *user, int client_socket, const char *filename);
void task_destroy(Task *task);

#endif // TASK_H