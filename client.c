#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 4096

int sock = -1;
char current_user[64] = "";

int connect_to_server() {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    
    printf("Connected to server\n");
    return 0;
}

int do_signup(const char* username, const char* password) {
    char buffer[BUFFER_SIZE];
    
    snprintf(buffer, BUFFER_SIZE, "SIGNUP %s %s\n", username, password);
    send(sock, buffer, strlen(buffer), 0);
    
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("Server: %s", buffer);
    
    return strncmp(buffer, "OK", 2) == 0 ? 0 : -1;
}

int do_login(const char* username, const char* password) {
    char buffer[BUFFER_SIZE];
    
    snprintf(buffer, BUFFER_SIZE, "LOGIN %s %s\n", username, password);
    send(sock, buffer, strlen(buffer), 0);
    
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("Server: %s", buffer);
    
    if (strncmp(buffer, "OK", 2) == 0) {
        strncpy(current_user, username, sizeof(current_user) - 1);
        return 0;
    }
    return -1;
}

int do_upload(const char* filename) {
    if (current_user[0] == '\0') {
        printf("ERROR: Not logged in\n");
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    
    // Read file
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    void* file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(fp);
        return -1;
    }
    
    fread(file_data, 1, file_size, fp);
    fclose(fp);
    
    // Extract filename from path
    char* fname = strrchr(filename, '/');
    if (fname) {
        fname++;
    } else {
        fname = (char*)filename;
    }
    
    // Send command with newline
    snprintf(buffer, BUFFER_SIZE, "UPLOAD %s\n", fname);
    send(sock, buffer, strlen(buffer), 0);
    
    // Send file size
    uint32_t size = file_size;
    send(sock, &size, sizeof(size), 0);
    
    // Send file data
    send(sock, file_data, file_size, 0);
    free(file_data);
    
    // Receive response
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("Server: %s", buffer);
    
    return strncmp(buffer, "OK", 2) == 0 ? 0 : -1;
}

int do_list() {
    if (current_user[0] == '\0') {
        printf("ERROR: Not logged in\n");
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    
    snprintf(buffer, BUFFER_SIZE, "LIST\n");
    send(sock, buffer, strlen(buffer), 0);
    
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("Files:\n%s", buffer);
    
    return 0;
}

int do_download(const char* filename, const char* output) {
    if (current_user[0] == '\0') {
        printf("ERROR: Not logged in\n");
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    
    // Send command with newline
    snprintf(buffer, BUFFER_SIZE, "DOWNLOAD %s\n", filename);
    send(sock, buffer, strlen(buffer), 0);
    
    // Receive file size
    uint32_t file_size;
    recv(sock, &file_size, sizeof(file_size), 0);
    
    if (file_size == 0) {
        // Error occurred
        memset(buffer, 0, BUFFER_SIZE);
        recv(sock, buffer, BUFFER_SIZE - 1, 0);
        printf("Server: %s", buffer);
        return -1;
    }
    
    // Receive file data
    void* file_data = malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }
    
    size_t total_received = 0;
    while (total_received < file_size) {
        ssize_t n = recv(sock, (char*)file_data + total_received, file_size - total_received, 0);
        if (n <= 0) break;
        total_received += n;
    }
    
    // Write to file
    FILE* fp = fopen(output, "wb");
    if (fp) {
        fwrite(file_data, 1, total_received, fp);
        fclose(fp);
        printf("File downloaded successfully (%zu bytes)\n", total_received);
    } else {
        perror("Failed to create output file");
    }
    
    free(file_data);
    return 0;
}

int do_delete(const char* filename) {
    if (current_user[0] == '\0') {
        printf("ERROR: Not logged in\n");
        return -1;
    }
    
    char buffer[BUFFER_SIZE];
    
    snprintf(buffer, BUFFER_SIZE, "DELETE %s\n", filename);
    send(sock, buffer, strlen(buffer), 0);
    
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("Server: %s", buffer);
    
    return strncmp(buffer, "OK", 2) == 0 ? 0 : -1;
}

void print_help() {
    printf("Commands:\n");
    printf("  signup <username> <password>\n");
    printf("  login <username> <password>\n");
    printf("  upload <filename>\n");
    printf("  download <filename> <output>\n");
    printf("  delete <filename>\n");
    printf("  list\n");
    printf("  quit\n");
    printf("  help\n");
}

int main() {
    if (connect_to_server() < 0) {
        return 1;
    }
    
    printf("Session-based client started\n");
    printf("Type 'help' for commands\n\n");
    
    char line[1024];
    char cmd[64], arg1[256], arg2[256];
    
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        if (strlen(line) == 0) continue;
        
        memset(cmd, 0, sizeof(cmd));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        
        sscanf(line, "%63s %255s %255s", cmd, arg1, arg2);
        
        if (strcmp(cmd, "signup") == 0) {
            if (arg1[0] && arg2[0]) {
                do_signup(arg1, arg2);
            } else {
                printf("Usage: signup <username> <password>\n");
            }
        } else if (strcmp(cmd, "login") == 0) {
            if (arg1[0] && arg2[0]) {
                do_login(arg1, arg2);
            } else {
                printf("Usage: login <username> <password>\n");
            }
        } else if (strcmp(cmd, "upload") == 0) {
            if (arg1[0]) {
                do_upload(arg1);
            } else {
                printf("Usage: upload <filename>\n");
            }
        } else if (strcmp(cmd, "download") == 0) {
            if (arg1[0] && arg2[0]) {
                do_download(arg1, arg2);
            } else {
                printf("Usage: download <filename> <output>\n");
            }
        } else if (strcmp(cmd, "delete") == 0) {
            if (arg1[0]) {
                do_delete(arg1);
            } else {
                printf("Usage: delete <filename>\n");
            }
        } else if (strcmp(cmd, "list") == 0) {
            do_list();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else {
            printf("Unknown command: %s\n", cmd);
            print_help();
        }
    }
    
    close(sock);
    return 0;
}