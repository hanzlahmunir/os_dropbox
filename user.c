#include "user.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

// Simple hash function
static unsigned long hash(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

UserDB *user_db_create(size_t table_size)
{
    UserDB *db = (UserDB *)malloc(sizeof(UserDB));
    if (!db)
        return NULL;

    db->table_size = table_size;
    db->users = (User **)calloc(table_size, sizeof(User *));
    if (!db->users)
    {
        free(db);
        return NULL;
    }

    pthread_mutex_init(&db->db_mutex, NULL);

    // Create storage directory
    mkdir("./storage", 0755);

    return db;
}

void user_db_destroy(UserDB *db)
{
    if (!db)
        return;

    pthread_mutex_lock(&db->db_mutex);

    for (size_t i = 0; i < db->table_size; i++)
    {
        User *current = db->users[i];
        while (current)
        {
            User *next = current->next;
            pthread_mutex_destroy(&current->file_mutex);
            free(current);
            current = next;
        }
    }

    free(db->users);
    pthread_mutex_unlock(&db->db_mutex);
    pthread_mutex_destroy(&db->db_mutex);

    free(db);
}

int user_signup(UserDB *db, const char *username, const char *password)
{
    if (!db || !username || !password)
        return -1;
    if (strlen(username) == 0 || strlen(username) >= MAX_USERNAME)
        return -1;
    if (strlen(password) == 0 || strlen(password) >= MAX_PASSWORD)
        return -1;

    pthread_mutex_lock(&db->db_mutex);

    unsigned long idx = hash(username) % db->table_size;

    // Check if user already exists
    User *current = db->users[idx];
    while (current)
    {
        if (strcmp(current->username, username) == 0)
        {
            pthread_mutex_unlock(&db->db_mutex);
            return -1; // User already exists
        }
        current = current->next;
    }

    // Create new user
    User *user = (User *)malloc(sizeof(User));
    if (!user)
    {
        pthread_mutex_unlock(&db->db_mutex);
        return -1;
    }

    strncpy(user->username, username, MAX_USERNAME - 1);
    user->username[MAX_USERNAME - 1] = '\0';
    strncpy(user->password, password, MAX_PASSWORD - 1);
    user->password[MAX_PASSWORD - 1] = '\0';
    user->quota_used = 0;
    user->quota_limit = DEFAULT_QUOTA;
    snprintf(user->storage_path, MAX_PATH, "./storage/%s", username);
    pthread_mutex_init(&user->file_mutex, NULL);

    // Create user storage directory
    mkdir(user->storage_path, 0755);

    // Insert at head of chain
    user->next = db->users[idx];
    db->users[idx] = user;

    pthread_mutex_unlock(&db->db_mutex);

    printf("[UserDB] User '%s' created successfully\n", username);
    return 0;
}

User *user_login(UserDB *db, const char *username, const char *password)
{
    if (!db || !username || !password)
        return NULL;

    pthread_mutex_lock(&db->db_mutex);

    unsigned long idx = hash(username) % db->table_size;

    User *current = db->users[idx];
    while (current)
    {
        if (strcmp(current->username, username) == 0)
        {
            if (strcmp(current->password, password) == 0)
            {
                pthread_mutex_unlock(&db->db_mutex);
                printf("[UserDB] User '%s' logged in\n", username);
                return current;
            }
            break;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&db->db_mutex);
    return NULL; // User not found or wrong password
}

int user_check_quota(User *user, size_t file_size)
{
    if (!user)
        return -1;

    pthread_mutex_lock(&user->file_mutex);
    int result = (user->quota_used + file_size <= user->quota_limit) ? 0 : -1;
    pthread_mutex_unlock(&user->file_mutex);

    return result;
}

void user_update_quota(User *user, long long delta)
{
    if (!user)
        return;

    pthread_mutex_lock(&user->file_mutex);
    if (delta < 0 && user->quota_used < (size_t)(-delta))
    {
        user->quota_used = 0;
    }
    else
    {
        user->quota_used += delta;
    }
    pthread_mutex_unlock(&user->file_mutex);
}