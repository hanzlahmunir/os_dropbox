# Dropbox Clone Server - Phase 1

A multi-threaded file storage server implementing core Dropbox-like functionality using C.

## Architecture Overview

### Three-Layer Design

1. **Main Accept Thread**
   - Listens for TCP connections on port 8080
   - Pushes accepted sockets to Client Queue

2. **Client Threadpool** (5 threads)
   - Handles authentication (signup/login)
   - Parses commands from clients
   - Creates tasks and pushes to Task Queue
   - Busy-waits for task completion
   - Sends responses back to clients

3. **Worker Threadpool** (4 threads)
   - Dequeues tasks from Task Queue
   - Performs file I/O operations
   - Updates user quotas
   - Marks tasks as completed

### Data Structures

- **Client Queue**: Thread-safe FIFO for socket descriptors
- **Task Queue**: Thread-safe FIFO for file operation tasks
- **User Database**: Hash table with per-user file mutexes
- **Task Structure**: Contains command, user, filename, data, and completion flag

## Features Implemented (Phase 1)

- ✅ User signup and login
- ✅ Per-user storage directories
- ✅ Storage quota management (100MB default)
- ✅ File operations: UPLOAD, DOWNLOAD, DELETE, LIST
- ✅ Thread-safe queues with mutex + condition variables
- ✅ Client and Worker threadpools
- ✅ Per-user file operation locking
- ✅ Busy-wait task completion (Phase 1 approach)

## Building the Project

### Prerequisites
- GCC compiler
- POSIX threads library
- Linux/Unix environment

### Compile
```bash
make
```

This creates two executables:
- `server` - The file storage server
- `client` - Test client for interacting with server

### Clean Build
```bash
make clean      # Remove binaries and objects
make distclean  # Also remove storage directory
```

## Running the Server

### Start Server
```bash
./server
```

The server will:
1. Initialize user database and queues
2. Create client and worker threadpools
3. Listen on port 8080
4. Print status messages as clients connect

### Stop Server
Press `Ctrl+C` to stop the server

## Using the Client

The client is a command-line tool for testing server functionality.

### Authentication Commands

**Signup:**
```bash
./client signup <username> <password>
```

**Login:**
```bash
./client login <username> <password>
```

Note: The client creates a new connection for each command. In Phase 1, you must login before each file operation.

### File Operation Commands

**Upload a file:**
```bash
./client upload <local_filename>
```

**Download a file:**
```bash
./client download <remote_filename> <output_filename>
```

**Delete a file:**
```bash
./client delete <filename>
```

**List all files:**
```bash
./client list
```

## Testing

### Automated Test Script

Run the Phase 1 test suite:

```bash
# Terminal 1: Start server
./server

# Terminal 2: Run tests
chmod +x test_phase1.sh
./test_phase1.sh
```

The script tests:
1. User signup
2. User login
3. File upload
4. File listing
5. File download
6. File deletion

### Manual Testing

```bash
# Terminal 1: Start server
./server

# Terminal 2: Test operations
./client signup alice secret123
./client login alice secret123

# Create a test file
echo "Hello World" > hello.txt

# Upload it
./client upload hello.txt

# List files
./client list

# Download it
./client download hello.txt hello_downloaded.txt

# Delete it
./client delete hello.txt
```

## Memory and Concurrency Checks

### Valgrind (Memory Leaks)

```bash
# Terminal 1: Run server with valgrind
make valgrind-server

# Terminal 2: Run client operations
./test_phase1.sh
# Then Ctrl+C the server

# Check valgrind output for leaks
```

### ThreadSanitizer (Data Races)

```bash
# Build with ThreadSanitizer
make tsan

# Terminal 1: Run server
./server

# Terminal 2: Run tests
./test_phase1.sh

# Check output for any race conditions
```

## File Structure

```
.
├── Makefile                # Build configuration
├── README.md              # This file
├── test_phase1.sh         # Automated test script
├── queue.h / queue.c      # Thread-safe queue implementation
├── user.h / user.c        # User management and authentication
├── task.h / task.c        # Task structure and operations
├── threadpool.h / threadpool.c  # Generic threadpool
├── server.c               # Main server logic
├── client.c               # Test client
└── storage/               # Created at runtime
    └── <username>/        # Per-user directories
```

## Synchronization Details

### Queue Synchronization
- **Mutex**: Protects queue data structure
- **Condition Variable**: Signals waiting threads when items are added
- Threads block on `queue_pop()` when empty

### User Database
- **Global db_mutex**: Protects user database during signup/login
- **Per-user file_mutex**: Serializes file operations for each user

### Task Completion
- Worker threads set `task->completed = 1` when done
- Client threads busy-wait checking this flag (with 1ms sleep to reduce CPU usage)

## Known Limitations (Phase 1)

1. **Single session per user**: Multiple clients for the same user work, but not optimally tested
2. **Busy-waiting**: Client threads use CPU while waiting (addressed in Phase 2 bonus)
3. **No graceful shutdown**: Ctrl+C leaves threads hanging (improved in Phase 2)
4. **Simple authentication**: Passwords stored in plaintext
5. **No persistent storage**: User data lost on restart

## Design Decisions

### Why Two Queues?
- **Client Queue**: Decouples connection acceptance from authentication
- **Task Queue**: Separates network I/O from disk I/O

### Why Per-User Mutexes?
- Allows concurrent operations for different users
- Serializes only conflicting operations for the same user

### Why Busy-Wait in Phase 1?
- Simplest implementation for proof-of-concept
- Easy to upgrade to condition variables in Phase 2

# Dropbox Clone Server - Phase 2

A multi-threaded file storage server with user authentication, file operations, and concurrent client support.

## Features Implemented

### Phase 2 
- **Eliminated busy waiting** - Uses condition variables for task completion signaling
- **Worker→Client communication** - Tasks signal completion via condition variables
- **Multiple concurrent clients** - Supports many simultaneous connections
- **Same user, multiple sessions** - Proper serialization of conflicting operations
- **Fixed deadlock bug** - Removed nested mutex locks in quota checking
- **Clean protocol** - Line-based command parsing with binary data separation

## Architecture

```
┌─────────────┐
│ Main Thread │  Accepts connections
└──────┬──────┘
       │ pushes socket
       ▼
┌──────────────┐
│ Client Queue │  Thread-safe queue
└──────┬───────┘
       │ consumed by
       ▼
┌─────────────────┐
│ Client Threads  │  Handle auth & commands
└────────┬────────┘
         │ create & push tasks
         ▼
┌──────────────┐
│  Task Queue  │  Thread-safe queue
└──────┬───────┘
       │ consumed by
       ▼
┌─────────────────┐
│ Worker Threads  │  Execute file I/O
└────────┬────────┘
         │ signal completion
         ▼
┌──────────────────┐
│ Condition Var    │  Wakes waiting client
└──────────────────┘
```

## Key Design Decisions

### Worker→Client Communication
**Chosen approach**: Condition variables per task
- Each Task has its own mutex + condition variable
- Worker signals completion after processing
- Client thread waits on condition variable (no busy waiting!)
- **Pros**: Simple, efficient, no polling overhead
- **Cons**: Tasks must be heap-allocated

### Concurrency Control
**Per-user file mutex**: Serializes all operations for a single user
- Prevents concurrent modifications to same files
- Ensures quota consistency
- Multiple users can operate in parallel
- **Trade-off**: Less parallelism for same user, but simpler correctness

### Protocol Design
**Line-based commands + binary data**:
- Commands end with `\n` for clear separation
- Binary data (file content, sizes) sent separately
- Prevents buffer mixing issues

## Building

```bash
# Standard build
make

# Build with ThreadSanitizer
make tsan

# Clean
make clean
```

## Running

### Start Server
```bash
./server
```

Server listens on port 8080.

### Start Client
```bash
./client
```

### Commands
```
signup <username> <password>  - Create new account
login <username> <password>   - Authenticate
upload <filename>             - Upload file
download <filename> <output>  - Download file
delete <filename>             - Delete file
list                          - List all files
quit                          - Disconnect
help                          - Show commands
```

## Testing

### Manual Testing
```bash
# Terminal 1
./server

# Terminal 2
./client
> signup alice password123
> login alice password123
> upload test.txt
> list
> download test.txt output.txt
> delete test.txt
> quit
```

### Concurrent Testing
```bash
# Run automated test suite
chmod +x test_concurrent.sh
./test_concurrent.sh
```

Tests include:
- Single client operations
- Multiple concurrent users
- Same user with multiple sessions
- Conflicting operations (concurrent read/write/delete)
- Heavy load (10+ concurrent clients)

### Memory Leak Detection
```bash
make
valgrind --leak-check=full --show-leak-kinds=all ./server

# In another terminal, run client tests
./client
# ... perform operations ...
# Ctrl+C to stop server

# Check valgrind output for leaks
```

### Race Condition Detection
```bash
make tsan
./server_tsan

# In other terminals, run multiple clients
./client_tsan
./client_tsan
./client_tsan

.
├── server.c          # Main server implementation
├── client.c          # Client implementation
├── queue.c/h         # Thread-safe queue with condition variables
├── user.c/h          # User database and authentication
├── task.c/h          # Task structure with condition variables
├── Makefile          # Build configuration
├── test_concurrent.sh # Automated test suite
└── README.md         # This file
```

## Synchronization Summary

| Resource | Protection | Purpose |
|----------|-----------|---------|
| Client Queue | `mutex + cond` | Producer-consumer for sockets |
| Task Queue | `mutex + cond` | Producer-consumer for tasks |
| User Database | `db_mutex` | Protects user hash table |
| User Files | `file_mutex` | Serializes file ops per user |
| Task Completion | `task->mutex + cond` | Worker signals client |

## Known Limitations

1. **Same-user parallelism**: Operations for one user are serialized
2. **No persistent storage**: User data lost on restart (fix: use database)
3. **No encryption**: Passwords stored in plaintext (fix: use bcrypt)
4. **Fixed thread pools**: Pool sizes hardcoded (fix: make configurable)
5. **No graceful shutdown**: Ctrl+C leaves resources unclean (fix: signal handling)


## Testing Checklist

- [x] Single client can signup, login, upload, list, download, delete
- [x] Multiple clients with different users work concurrently
- [x] Same user with multiple sessions doesn't corrupt data
- [x] Conflicting operations are properly serialized
- [x] No busy waiting (condition variables used)
- [x] No memory leaks (Valgrind clean)
- [x] No data races (ThreadSanitizer clean)
- [x] Worker→client communication works reliably
- [x] Quota enforcement works correctly
