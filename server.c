#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <sys/stat.h>
#include "queue.h"
#include "user.h"
#include "task.h"

#define PORT 8080
#define CLIENT_POOL_SIZE 5
#define WORKER_POOL_SIZE 4
#define BUFFER_SIZE 4096
#define USER_TABLE_SIZE 100

// Global structures
UserDB* user_db = NULL;
Queue* task_queue = NULL;

// Forward declarations
void worker_thread_func(void* arg);
void* worker_thread_loop(void* arg);
void client_thread_func(void* arg);
void* client_thread_loop(void* arg);

// Helper function to read a line from socket
ssize_t read_line(int socket, char* buffer, size_t max_len) {
    size_t pos = 0;
    while (pos < max_len - 1) {
        ssize_t n = recv(socket, &buffer[pos], 1, 0);
        if (n <= 0) {
            return n;
        }
        if (buffer[pos] == '\n') {
            buffer[pos] = '\0';
            return pos;
        }
        pos++;
    }
    buffer[pos] = '\0';
    return pos;
}

// Worker thread loop - continuously processes tasks from queue
void* worker_thread_loop(void* arg) {
    Queue* task_queue = (Queue*)arg;
    
    printf("[Worker Thread %lu] Started\n", pthread_self());
    
    while (1) {
        Task* task = (Task*)queue_pop(task_queue);
        if (!task) {
            // Queue shutdown
            printf("[Worker Thread %lu] Queue shutdown, exiting\n", pthread_self());
            break;
        }
        
        printf("[Worker Thread %lu] Got task type %d, processing...\n", pthread_self(), task->type);
        worker_thread_func(task);
        printf("[Worker Thread %lu] Task processing complete\n", pthread_self());
    }
    
    return NULL;
}

// Worker thread function - processes tasks
void worker_thread_func(void* arg) {
    Task* task = (Task*)arg;
    
    printf("[Worker] Processing task for user '%s'\n", task->user->username);
    
    // Lock the user's file mutex for the entire operation
    pthread_mutex_lock(&task->user->file_mutex);
    
    char filepath[512];
    
    switch (task->type) {
        case TASK_UPLOAD: {
            snprintf(filepath, sizeof(filepath), "%s/%s", task->user->storage_path, task->filename);
            
            // Check quota (we already hold the mutex, so check directly)
            if (task->user->quota_used + task->upload_size > task->user->quota_limit) {
                snprintf(task->error_msg, sizeof(task->error_msg), "Quota exceeded");
                task->status = -1;
                break;
            }
            
            FILE* fp = fopen(filepath, "wb");
            if (!fp) {
                snprintf(task->error_msg, sizeof(task->error_msg), "Failed to create file");
                task->status = -1;
                break;
            }
            
            size_t written = fwrite(task->upload_data, 1, task->upload_size, fp);
            fclose(fp);
            
            if (written != task->upload_size) {
                snprintf(task->error_msg, sizeof(task->error_msg), "Failed to write file");
                task->status = -1;
                remove(filepath);
            } else {
                // Update quota directly (we hold the mutex)
                task->user->quota_used += task->upload_size;
                task->status = 0;
                printf("[Worker] Uploaded file: %s (%zu bytes)\n", task->filename, task->upload_size);
            }
            break;
        }
        
        case TASK_DOWNLOAD: {
            snprintf(filepath, sizeof(filepath), "%s/%s", task->user->storage_path, task->filename);
            
            FILE* fp = fopen(filepath, "rb");
            if (!fp) {
                snprintf(task->error_msg, sizeof(task->error_msg), "File not found");
                task->status = -1;
                break;
            }
            
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            task->result_data = malloc(file_size);
            if (!task->result_data) {
                fclose(fp);
                snprintf(task->error_msg, sizeof(task->error_msg), "Memory allocation failed");
                task->status = -1;
                break;
            }
            
            size_t read_size = fread(task->result_data, 1, file_size, fp);
            fclose(fp);
            
            if (read_size != (size_t)file_size) {
                free(task->result_data);
                task->result_data = NULL;
                snprintf(task->error_msg, sizeof(task->error_msg), "Failed to read file");
                task->status = -1;
            } else {
                task->result_size = file_size;
                task->status = 0;
                printf("[Worker] Downloaded file: %s (%ld bytes)\n", task->filename, file_size);
            }
            break;
        }
        
        case TASK_DELETE: {
            snprintf(filepath, sizeof(filepath), "%s/%s", task->user->storage_path, task->filename);
            
            struct stat st;
            if (stat(filepath, &st) != 0) {
                snprintf(task->error_msg, sizeof(task->error_msg), "File not found");
                task->status = -1;
                break;
            }
            
            if (remove(filepath) != 0) {
                snprintf(task->error_msg, sizeof(task->error_msg), "Failed to delete file");
                task->status = -1;
            } else {
                // Update quota directly (we hold the mutex)
                if (task->user->quota_used >= (size_t)st.st_size) {
                    task->user->quota_used -= st.st_size;
                } else {
                    task->user->quota_used = 0;
                }
                task->status = 0;
                printf("[Worker] Deleted file: %s\n", task->filename);
            }
            break;
        }
        
        case TASK_LIST: {
            DIR* dir = opendir(task->user->storage_path);
            if (!dir) {
                snprintf(task->error_msg, sizeof(task->error_msg), "Failed to open directory");
                task->status = -1;
                break;
            }
            
            char* list_buffer = malloc(BUFFER_SIZE);
            if (!list_buffer) {
                closedir(dir);
                snprintf(task->error_msg, sizeof(task->error_msg), "Memory allocation failed");
                task->status = -1;
                break;
            }
            
            size_t offset = 0;
            struct dirent* entry;
            
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                
                snprintf(filepath, sizeof(filepath), "%s/%s", task->user->storage_path, entry->d_name);
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    int written = snprintf(list_buffer + offset, BUFFER_SIZE - offset,
                                         "%s (%ld bytes)\n", entry->d_name, st.st_size);
                    if (written > 0 && offset + written < BUFFER_SIZE) {
                        offset += written;
                    }
                }
            }
            
            closedir(dir);
            
            if (offset == 0) {
                strcpy(list_buffer, "No files\n");
                offset = strlen(list_buffer);
            }
            
            task->result_data = list_buffer;
            task->result_size = offset;
            task->status = 0;
            printf("[Worker] Listed files for user: %s\n", task->user->username);
            break;
        }
    }
    
    pthread_mutex_unlock(&task->user->file_mutex);
    
    // Mark task as completed and signal the waiting client thread
    pthread_mutex_lock(&task->mutex);
    task->completed = 1;
    pthread_cond_signal(&task->cond);
    pthread_mutex_unlock(&task->mutex);
    
    printf("[Worker] Task marked as completed\n");
}

// Client thread loop - continuously processes clients from queue
void* client_thread_loop(void* arg) {
    Queue* client_queue = (Queue*)arg;
    
    printf("[Client Thread %lu] Started and waiting for connections\n", pthread_self());
    
    while (1) {
        int* socket_ptr = (int*)queue_pop(client_queue);
        if (!socket_ptr) {
            // Queue shutdown
            printf("[Client Thread %lu] Queue shutdown, exiting\n", pthread_self());
            break;
        }
        
        printf("[Client Thread %lu] Got socket %d from queue\n", pthread_self(), *socket_ptr);
        client_thread_func(socket_ptr);
    }
    
    return NULL;
}

// Client thread function - handles client communication
void client_thread_func(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    printf("[Client Thread] Handling socket %d\n", client_socket);
    
    char buffer[BUFFER_SIZE];
    User* current_user = NULL;
    
    // Authentication loop
    while (!current_user) {
        ssize_t n = read_line(client_socket, buffer, BUFFER_SIZE);
        
        if (n <= 0) {
            printf("[Client Thread] Connection closed during auth (socket %d)\n", client_socket);
            close(client_socket);
            return;
        }
        
        char command[32], username[MAX_USERNAME], password[MAX_PASSWORD];
        if (sscanf(buffer, "%31s %63s %63s", command, username, password) != 3) {
            send(client_socket, "ERROR Invalid format\n", 21, 0);
            continue;
        }
        
        if (strcmp(command, "SIGNUP") == 0) {
            if (user_signup(user_db, username, password) == 0) {
                send(client_socket, "OK Signup successful\n", 21, 0);
            } else {
                send(client_socket, "ERROR User already exists\n", 26, 0);
            }
        } else if (strcmp(command, "LOGIN") == 0) {
            current_user = user_login(user_db, username, password);
            if (current_user) {
                send(client_socket, "OK Login successful\n", 20, 0);
            } else {
                send(client_socket, "ERROR Invalid credentials\n", 26, 0);
            }
        } else {
            send(client_socket, "ERROR Unknown command\n", 22, 0);
        }
    }
    
    printf("[Client Thread] User '%s' authenticated\n", current_user->username);
    
    // Command processing loop
    while (1) {
        ssize_t n = read_line(client_socket, buffer, BUFFER_SIZE);
        if (n <= 0) {
            break;
        }
        
        char command[32], filename[MAX_FILENAME];
        memset(filename, 0, sizeof(filename));
        sscanf(buffer, "%31s %255s", command, filename);
        
        if (strcmp(command, "UPLOAD") == 0) {
            // Receive file size
            uint32_t file_size;
            n = recv(client_socket, &file_size, sizeof(file_size), 0);
            if (n != sizeof(file_size)) {
                send(client_socket, "ERROR Failed to receive file size\n", 34, 0);
                continue;
            }
            
            // Receive file data
            void* file_data = malloc(file_size);
            if (!file_data) {
                send(client_socket, "ERROR Memory allocation failed\n", 31, 0);
                continue;
            }
            
            size_t total_received = 0;
            while (total_received < file_size) {
                n = recv(client_socket, (char*)file_data + total_received, file_size - total_received, 0);
                if (n <= 0) {
                    free(file_data);
                    send(client_socket, "ERROR Failed to receive file data\n", 34, 0);
                    goto cleanup;
                }
                total_received += n;
            }
            
            // Create task with condition variable
            Task* task = task_create(TASK_UPLOAD, current_user, client_socket, filename);
            task->upload_data = file_data;
            task->upload_size = file_size;
            
            // Add to task queue
            queue_push(task_queue, task);
            
            // Wait for completion using condition variable (Phase 2 - no busy waiting!)
            pthread_mutex_lock(&task->mutex);
            while (!task->completed) {
                pthread_cond_wait(&task->cond, &task->mutex);
            }
            pthread_mutex_unlock(&task->mutex);
            
            // Send response
            if (task->status == 0) {
                send(client_socket, "OK File uploaded\n", 17, 0);
            } else {
                char error_response[300];
                snprintf(error_response, sizeof(error_response), "ERROR %s\n", task->error_msg);
                send(client_socket, error_response, strlen(error_response), 0);
            }
            
            task_destroy(task);
            
        } else if (strcmp(command, "DOWNLOAD") == 0) {
            Task* task = task_create(TASK_DOWNLOAD, current_user, client_socket, filename);
            
            queue_push(task_queue, task);
            
            // Wait for completion
            pthread_mutex_lock(&task->mutex);
            while (!task->completed) {
                pthread_cond_wait(&task->cond, &task->mutex);
            }
            pthread_mutex_unlock(&task->mutex);
            
            if (task->status == 0) {
                uint32_t file_size = task->result_size;
                send(client_socket, &file_size, sizeof(file_size), 0);
                send(client_socket, task->result_data, task->result_size, 0);
            } else {
                uint32_t file_size = 0;
                send(client_socket, &file_size, sizeof(file_size), 0);
                char error_response[300];
                snprintf(error_response, sizeof(error_response), "ERROR %s\n", task->error_msg);
                send(client_socket, error_response, strlen(error_response), 0);
            }
            
            task_destroy(task);
            
        } else if (strcmp(command, "DELETE") == 0) {
            Task* task = task_create(TASK_DELETE, current_user, client_socket, filename);
            
            queue_push(task_queue, task);
            
            // Wait for completion
            pthread_mutex_lock(&task->mutex);
            while (!task->completed) {
                pthread_cond_wait(&task->cond, &task->mutex);
            }
            pthread_mutex_unlock(&task->mutex);
            
            if (task->status == 0) {
                send(client_socket, "OK File deleted\n", 16, 0);
            } else {
                char error_response[300];
                snprintf(error_response, sizeof(error_response), "ERROR %s\n", task->error_msg);
                send(client_socket, error_response, strlen(error_response), 0);
            }
            
            task_destroy(task);
            
        } else if (strcmp(command, "LIST") == 0) {
            Task* task = task_create(TASK_LIST, current_user, client_socket, NULL);
            
            queue_push(task_queue, task);
            
            // Wait for completion
            pthread_mutex_lock(&task->mutex);
            while (!task->completed) {
                pthread_cond_wait(&task->cond, &task->mutex);
            }
            pthread_mutex_unlock(&task->mutex);
            
            if (task->status == 0) {
                send(client_socket, task->result_data, task->result_size, 0);
            } else {
                char error_response[300];
                snprintf(error_response, sizeof(error_response), "ERROR %s\n", task->error_msg);
                send(client_socket, error_response, strlen(error_response), 0);
            }
            
            task_destroy(task);
            
        } else if (strcmp(command, "QUIT") == 0) {
            send(client_socket, "OK Goodbye\n", 11, 0);
            break;
        } else {
            send(client_socket, "ERROR Unknown command\n", 22, 0);
        }
    }
    
cleanup:
    printf("[Client Thread] Connection closed for user '%s'\n", current_user->username);
    close(client_socket);
}

int main() {
    printf("=== Dropbox Clone Server (Phase 2) ===\n");
    printf("Features: Condition Variables, No Busy Waiting\n\n");
    
    // Initialize user database
    user_db = user_db_create(USER_TABLE_SIZE);
    if (!user_db) {
        fprintf(stderr, "Failed to create user database\n");
        return 1;
    }
    printf("[Main] User database initialized\n");
    
    // Initialize task queue
    task_queue = queue_create();
    if (!task_queue) {
        fprintf(stderr, "Failed to create task queue\n");
        user_db_destroy(user_db);
        return 1;
    }
    printf("[Main] Task queue initialized\n");
    
    // Create worker threads
    pthread_t worker_threads[WORKER_POOL_SIZE];
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        if (pthread_create(&worker_threads[i], NULL, worker_thread_loop, task_queue) != 0) {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            queue_destroy(task_queue);
            user_db_destroy(user_db);
            return 1;
        }
    }
    printf("[Main] Worker threadpool created (%d threads)\n", WORKER_POOL_SIZE);
    
    // Create client queue
    Queue* client_queue = queue_create();
    if (!client_queue) {
        fprintf(stderr, "Failed to create client queue\n");
        queue_shutdown(task_queue);
        for (int i = 0; i < WORKER_POOL_SIZE; i++) {
            pthread_join(worker_threads[i], NULL);
        }
        queue_destroy(task_queue);
        user_db_destroy(user_db);
        return 1;
    }
    printf("[Main] Client queue initialized\n");
    
    // Create client threads
    pthread_t client_threads[CLIENT_POOL_SIZE];
    for (int i = 0; i < CLIENT_POOL_SIZE; i++) {
        if (pthread_create(&client_threads[i], NULL, client_thread_loop, client_queue) != 0) {
            fprintf(stderr, "Failed to create client thread %d\n", i);
            queue_destroy(client_queue);
            queue_shutdown(task_queue);
            for (int j = 0; j < WORKER_POOL_SIZE; j++) {
                pthread_join(worker_threads[j], NULL);
            }
            queue_destroy(task_queue);
            user_db_destroy(user_db);
            return 1;
        }
    }
    printf("[Main] Client threadpool created (%d threads)\n\n", CLIENT_POOL_SIZE);
    
    // Create server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_socket);
        return 1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }
    
    // Listen
    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }
    
    printf("[Main] Server listening on port %d\n", PORT);
    printf("[Main] Press Ctrl+C to shutdown\n\n");
    
    // Accept loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("[Main] New connection accepted (socket %d)\n", client_socket);
        
        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;
        queue_push(client_queue, socket_ptr);
    }
    
    // Cleanup (unreachable without signal handling)
    close(server_socket);
    queue_shutdown(client_queue);
    for (int i = 0; i < CLIENT_POOL_SIZE; i++) {
        pthread_join(client_threads[i], NULL);
    }
    queue_destroy(client_queue);
    
    queue_shutdown(task_queue);
    for (int i = 0; i < WORKER_POOL_SIZE; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    queue_destroy(task_queue);
    user_db_destroy(user_db);
    
    return 0;
}