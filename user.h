#ifndef USER_H
#define USER_H

#include <pthread.h>
#include <stddef.h>

#define MAX_USERNAME 64
#define MAX_PASSWORD 64
#define MAX_PATH 256
#define DEFAULT_QUOTA (100 * 1024 * 1024) // 100 MB

typedef struct User
{
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    size_t quota_used;
    size_t quota_limit;
    char storage_path[MAX_PATH];
    pthread_mutex_t file_mutex; // Per-user file operations lock
    struct User *next;          // For hash table chaining
} User;

typedef struct UserDB
{
    User **users; // Hash table
    size_t table_size;
    pthread_mutex_t db_mutex;
} UserDB;

// User database operations
UserDB *user_db_create(size_t table_size);
void user_db_destroy(UserDB *db);
int user_signup(UserDB *db, const char *username, const char *password);
User *user_login(UserDB *db, const char *username, const char *password);
int user_check_quota(User *user, size_t file_size);
void user_update_quota(User *user, long long delta);

#endif // USER_H