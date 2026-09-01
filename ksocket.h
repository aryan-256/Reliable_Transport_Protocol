/*===================================== 
Mini Project 1 Submission 
Group Details: 
Member 1 Aryan Yadav
Member 1 Roll number: 23CS10003
Member 2 Name: Mayank Modi
Member 2 Roll number: 23CS10089 
=====================================*/

#ifndef KSOCKET_H
#define KSOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#define T 5
#define DROP_PROB 0.05f

#define SOCK_KTP 100
#define MAX_KTP_SOCKETS 10
#define MSG_SIZE 512
#define WINDOW_SIZE 10
#define SHM_KEY 0x5678

#define ENOSPACE 1001
#define ENOTBOUND 1002
#define ENOMESSAGE 1003

extern int ktp_error;

typedef struct
{
    uint8_t seq_no;
    uint8_t is_ack;
    uint8_t rwnd_size;
    char payload[MSG_SIZE];
    int payload_len;
} ktp_msg_t;

typedef struct
{
    int size;
    int count;
    uint8_t next_seq;
    ktp_msg_t buffer[WINDOW_SIZE];
    uint8_t unacked_seq[WINDOW_SIZE];
    time_t send_times[WINDOW_SIZE];
} swnd_t;

typedef struct
{
    int size;
    uint8_t expected_seq;
    int nospace_flag;
} rwnd_t;

typedef struct
{
    pthread_mutex_t mutex;
    int is_free;
    pid_t pid;
    int udp_sockfd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    int is_bound;

    ktp_msg_t send_buffer[WINDOW_SIZE];
    int send_count;

    ktp_msg_t recv_buffer[WINDOW_SIZE];
    int recv_count;

    swnd_t swnd;
    rwnd_t rwnd;

    int total_messages;
    int total_transmissions;
} ktp_socket_entry_t;

typedef struct
{
    ktp_socket_entry_t sockets[MAX_KTP_SOCKETS];
    int needs_bind[MAX_KTP_SOCKETS];
    int bind_done[MAX_KTP_SOCKETS];
    int bind_result[MAX_KTP_SOCKETS];
} shared_memory_t;

int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd,
           const struct sockaddr *src_addr, socklen_t src_len,
           const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                 const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                   struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float p);

#endif