CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
TSAN_FLAGS = -fsanitize=thread -g
VALGRIND = valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes

# Targets
all: server client

server: server.c queue.c user.c task.c
	$(CC) $(CFLAGS) -o server server.c queue.c user.c task.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

# ThreadSanitizer build
tsan: server.c queue.c user.c task.c client.c
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o server_tsan server.c queue.c user.c task.c
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o client_tsan client.c
	@echo "Built with ThreadSanitizer. Run ./server_tsan and ./client_tsan"

# Run with Valgrind
valgrind: server
	$(VALGRIND) ./server

# Clean
clean:
	rm -f server client server_tsan client_tsan
	rm -rf storage/

# Create test file
test_file:
	echo "This is a test file for upload." > test_upload.txt

.PHONY: all clean valgrind tsan test_file