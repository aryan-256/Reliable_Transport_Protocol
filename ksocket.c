/*===================================== 
Mini Project 1 Submission 
Group Details: 
Member 1 Aryan Yadav
Member 1 Roll number: 23CS10003
Member 2 Name: Mayank Modi
Member 2 Roll number: 23CS10089 
=====================================*/

#include "ksocket.h"
#include <stdio.h>
#include <string.h>
#define MAX_CLOSE_WAIT_SEC (3 * T)

int ktp_error = 0;
shared_memory_t *SM = NULL;

// Fucntion:create a shared memory to be used for communication
static void attach_shm(void)
{
    if (SM != NULL)
        return;
    int shmid = shmget(SHM_KEY, sizeof(shared_memory_t), 0666);
    if (shmid < 0)
    {
        perror("shmget - is initksocket running?");
        exit(1);
    }
    SM = (shared_memory_t *)shmat(shmid, NULL, 0);
    if ((void *)SM == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
}

// Fucntion:Simulating and actual packet drop environment
int dropMessage(float p)
{
    float r = (float)rand() / (float)RAND_MAX;
    return (r < p) ? 1 : 0;
}

// Fucntion: Intializes required fields if a free space in SM found else returns ENOSPACE
int k_socket(int domain, int type, int protocol)
{
    if (type != SOCK_KTP)
    {
        ktp_error = ENOTBOUND;
        return -1;
    }

    if(domain==-1 || protocol==-1){
        return -1;
    }
    attach_shm();

    for (int i = 0; i < MAX_KTP_SOCKETS; i++)
    {
        // Locking mutex to prevent race conditions
        pthread_mutex_lock(&SM->sockets[i].mutex);
        if (SM->sockets[i].is_free)
        {
            SM->sockets[i].is_free = 0;
            SM->sockets[i].pid = getpid();
            SM->sockets[i].is_bound = 0;
            SM->sockets[i].send_count = 0;
            SM->sockets[i].recv_count = 0;
            SM->sockets[i].swnd.size = WINDOW_SIZE;
            SM->sockets[i].swnd.count = 0;
            SM->sockets[i].swnd.next_seq = 1;
            SM->sockets[i].rwnd.size = WINDOW_SIZE;
            SM->sockets[i].rwnd.expected_seq = 1;
            SM->sockets[i].rwnd.nospace_flag = 0;
            SM->sockets[i].total_messages = 0;
            SM->sockets[i].total_transmissions = 0;
            SM->needs_bind[i] = 0;
            SM->bind_done[i] = 0;
            SM->bind_result[i] = 0;
            pthread_mutex_unlock(&SM->sockets[i].mutex);
            return i;
        }
        pthread_mutex_unlock(&SM->sockets[i].mutex);
    }
    ktp_error = ENOSPACE;
    return -1;
}

// Function: Works similar to bind of UDP
int k_bind(int sockfd,
           const struct sockaddr *src_addr, socklen_t src_len,
           const struct sockaddr *dest_addr, socklen_t dest_len)
{
    attach_shm();
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || SM->sockets[sockfd].is_free)
        return -1;

    pthread_mutex_lock(&SM->sockets[sockfd].mutex);
    memcpy(&SM->sockets[sockfd].local_addr, src_addr, src_len);
    memcpy(&SM->sockets[sockfd].remote_addr, dest_addr, dest_len);
    SM->bind_done[sockfd] = 0;
    SM->needs_bind[sockfd] = 1;
    pthread_mutex_unlock(&SM->sockets[sockfd].mutex);

    while (!SM->bind_done[sockfd])
        usleep(5000);

    if (SM->bind_result[sockfd] == 0)
    {
        pthread_mutex_lock(&SM->sockets[sockfd].mutex);
        SM->sockets[sockfd].is_bound = 1;
        pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
    }
    return SM->bind_result[sockfd];
}

// Fucntion:k_sendto function makes the required message packet with appropriate header and pushes it in the network
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                 const struct sockaddr *dest_addr, socklen_t addrlen)
{

    if(flags==-1 || addrlen==0){
        return -1;
    }
    attach_shm();
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || SM->sockets[sockfd].is_free)
        return -1;

    pthread_mutex_lock(&SM->sockets[sockfd].mutex);

    // Error Check: if sockfd is not bounded or bind to a diffeerent port
    struct sockaddr_in *dest = (struct sockaddr_in *)dest_addr;
    if (!SM->sockets[sockfd].is_bound ||
        dest->sin_addr.s_addr != SM->sockets[sockfd].remote_addr.sin_addr.s_addr ||
        dest->sin_port != SM->sockets[sockfd].remote_addr.sin_port)
    {
        ktp_error = ENOTBOUND;
        pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
        return -1;
    }

    // Error Check: If sender can't buffer enough space

    if (SM->sockets[sockfd].send_count >= WINDOW_SIZE)
    {
        ktp_error = ENOSPACE;
        pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
        return -1;
    }

    // Normal Fucntion: Make requried packet and push

    ktp_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.is_ack = 0;
    msg.payload_len = (int)((len > MSG_SIZE) ? MSG_SIZE : len);
    memcpy(msg.payload, buf, msg.payload_len);

    SM->sockets[sockfd].send_buffer[SM->sockets[sockfd].send_count] = msg;
    SM->sockets[sockfd].send_count++;

    pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
    return msg.payload_len;
}

// Fucntion: k_recvfrom, checks for errors as in comments below and then receives messages

ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                   struct sockaddr *src_addr, socklen_t *addrlen)
{

    if(flags==-1){
        return -1;
    }
    attach_shm();
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || SM->sockets[sockfd].is_free)
        return -1;

    pthread_mutex_lock(&SM->sockets[sockfd].mutex);

    // Error Check: No message to receive
    if (SM->sockets[sockfd].recv_count == 0)
    {
        ktp_error = ENOMESSAGE;
        pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
        return -1;
    }
    // Copy the outsanding messages to our receive buffer
    ktp_msg_t msg = SM->sockets[sockfd].recv_buffer[0];
    int copy_len = (msg.payload_len < (int)len) ? msg.payload_len : (int)len;
    memcpy(buf, msg.payload, copy_len);

    for (int i = 0; i < SM->sockets[sockfd].recv_count - 1; i++)
        SM->sockets[sockfd].recv_buffer[i] = SM->sockets[sockfd].recv_buffer[i + 1];
    SM->sockets[sockfd].recv_count--;

    SM->sockets[sockfd].rwnd.size++;
    if (SM->sockets[sockfd].rwnd.size > WINDOW_SIZE)
        SM->sockets[sockfd].rwnd.size = WINDOW_SIZE;

    if (src_addr)
    {
        memcpy(src_addr, &SM->sockets[sockfd].remote_addr,
               sizeof(struct sockaddr_in));
        if (addrlen)
            *addrlen = sizeof(struct sockaddr_in);
    }

    pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
    return copy_len;
}

// Fucntion: Closing the socket but with an important work still left to receive the acknowledgments for all the outstanding messages
int k_close(int sockfd)
{
    attach_shm();
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS || SM->sockets[sockfd].is_free)
        return -1;

    const int POLL_US = 200000;
    const int MAX_CYCLES = (MAX_CLOSE_WAIT_SEC * 1000000) / POLL_US;
    int cycles = 0;
    int last_total = -1;

    while (cycles < MAX_CYCLES)
    {
        pthread_mutex_lock(&SM->sockets[sockfd].mutex);
        int send_pending = SM->sockets[sockfd].send_count;
        int swnd_pending = SM->sockets[sockfd].swnd.count;
        pthread_mutex_unlock(&SM->sockets[sockfd].mutex);

        int total = send_pending + swnd_pending;
        // Check for outstanding unacknowledged message and wait fro its acknowledgment before closing
        if (total != last_total)
        {
            if (total > 0)
                printf("[k_close] Waiting: send_buf=%d  in_flight=%d\n",
                       send_pending, swnd_pending);
            last_total = total;
        }

        if (total == 0)
            break;

        usleep(POLL_US);
        cycles++;
    }

    if (cycles >= MAX_CYCLES)
        printf("[k_close] Timeout after %ds — closing anyway.\n",
               MAX_CLOSE_WAIT_SEC);

    pthread_mutex_lock(&SM->sockets[sockfd].mutex);
    if (SM->sockets[sockfd].total_messages > 0)
    {
        printf("\n================ KTP STATISTICS ================\n");
        printf("Total Unique Messages Sent : %d\n",
               SM->sockets[sockfd].total_messages);
        printf("Total UDP Transmissions    : %d\n",
               SM->sockets[sockfd].total_transmissions);
        printf("Average Transmissions/Msg  : %.3f\n",
               (float)SM->sockets[sockfd].total_transmissions /
                   (float)SM->sockets[sockfd].total_messages);
        printf("================================================\n\n");
    }
    SM->sockets[sockfd].is_free = 1;
    SM->sockets[sockfd].is_bound = 0;
    pthread_mutex_unlock(&SM->sockets[sockfd].mutex);
    return 0;
}