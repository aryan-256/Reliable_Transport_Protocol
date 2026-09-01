# Reliable Transport Protocol (RTP)

A Reliable Transport Protocol implemented over UDP sockets in C. This project was built as part of a mini-project by Aryan Yadav and Mayank Modi. It provides reliable data transfer with sliding window mechanisms, thread-safe shared memory, and connection states.

## Authors
- **Aryan Yadav** (23CS10003)
- **Mayank Modi** (23CS10089)

## Features
- **Reliable Data Transfer over UDP**: Simulates a reliable connection over an inherently unreliable protocol (UDP).
- **Sliding Window Protocol**: Implements Sender (`swnd_t`) and Receiver (`rwnd_t`) window state management for efficient data flow.
- **Shared Memory IPC**: Utilizes a global shared memory segment for process-shared states across multiple application instances.
- **Simulated Packet Loss**: Integrated random packet dropping (`dropMessage(p)`) to test reliability under adverse network conditions.
- **Garbage Collection**: Background daemon threads clean up zombie socket entries when a user process unexpectedly dies.

## Architecture

The project consists of a background daemon process and a user library API:

### 1. Daemon Process (`initksocket.c`)
Manages the OS-level UDP sockets and background tasks.
- `bind_handler`: Polls for socket bind requests.
- `thread_R`: Listens for incoming UDP packets and handles ACKs.
- `thread_S`: Retransmits unacknowledged frames and transmits new pending messages.
- `garbage_collector`: Monitors application process health and reclaims resources if necessary.

### 2. Library API (`ksocket.c` & `ksocket.h`)
Exposes socket operations to the user application.
- `k_socket()`: Allocates a new socket entry from shared memory.
- `k_bind()`: Requests the daemon to bind the socket to an address.
- `k_sendto()`: Queues application data to be sent.
- `k_recvfrom()`: Reads buffered, in-order incoming messages.
- `k_close()`: Cleans up resources after all pending data is sent.

## Building and Running

The project includes a `makefile` to build the daemon and the sample user applications (`user1` and `user2`).

### Compilation
To compile all the necessary components, simply run:
```bash
make all
```
This will compile the daemon `initksocket`, the library `libksocket.a`, and the test user applications `user1` and `user2`.

### Execution
1. First, start the background daemon:
```bash
./initksocket
```
2. Then, run the user applications in separate terminals:
```bash
./user1
./user2
```

### Cleanup
To clean up compiled binaries and shared memory, run:
```bash
make clean
```

## Performance Analysis
Tested with a Timeout (T) of 5s:

| Drop Probability (p) | Average Transmissions per Message |
|----------------------|-----------------------------------|
| 0.05                 | 1.212                             |
| 0.10                 | 1.308                             |
| 0.15                 | 1.904                             |
| 0.20                 | 2.214                             |
| 0.25                 | 2.673                             |
| 0.30                 | 2.797                             |
| 0.35                 | 2.913                             |
| 0.40                 | 3.236                             |
| 0.45                 | 3.675                             |
| 0.50                 | 4.134                             |
