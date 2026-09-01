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
#include <signal.h>
#include <sys/select.h>

extern shared_memory_t *SM;

static void init_attach_shm(void)
{
    int shmid = shmget(SHM_KEY, sizeof(shared_memory_t), IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("shmget");
        exit(1);
    }
    SM = (shared_memory_t *)shmat(shmid, NULL, 0);
    if ((void *)SM == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
}

void *bind_handler(void *arg)
{
    (void)arg;
    while (1)
    {
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (!SM->needs_bind[i])
                continue;
            if (SM->sockets[i].udp_sockfd >= 0)
                close(SM->sockets[i].udp_sockfd);

            int fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0)
            {
                perror("bind_handler: socket");
                SM->bind_result[i] = -1;
                SM->needs_bind[i] = 0;
                SM->bind_done[i] = 1;
                continue;
            }
            int opt = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            int ret = bind(fd,
                           (struct sockaddr *)&SM->sockets[i].local_addr,
                           sizeof(struct sockaddr_in));
            if (ret < 0)
            {
                perror("bind_handler: bind");
                close(fd);
                SM->sockets[i].udp_sockfd = -1;
            }
            else
            {
                SM->sockets[i].udp_sockfd = fd;
            }

            SM->bind_result[i] = ret;
            SM->needs_bind[i] = 0;
            SM->bind_done[i] = 1;
        }
        usleep(5000);
    }
    return NULL;
}

// Working ofn thread R as in problem stmt
void *thread_R(void *arg)
{
    (void)arg;
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    // Checks for any acctivity using select() with a timeout of 1 sec
    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            int fd = SM->sockets[i].udp_sockfd;
            if (fd >= 0)
            {
                FD_SET(fd, &readfds);
                if (fd > max_fd)
                    max_fd = fd;
            }
        }

        if (max_fd == -1)
        {
            sleep(1);
            continue;
        }

        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity == 0)
        {
            for (int i = 0; i < MAX_KTP_SOCKETS; i++)
            {
                if (SM->sockets[i].is_free)
                    continue;
                pthread_mutex_lock(&SM->sockets[i].mutex);
                if (SM->sockets[i].rwnd.nospace_flag &&
                    SM->sockets[i].rwnd.size > 0 &&
                    SM->sockets[i].rwnd.expected_seq > 1)
                {

                    ktp_msg_t dup_ack;
                    memset(&dup_ack, 0, sizeof(dup_ack));
                    dup_ack.is_ack = 1;
                    dup_ack.seq_no = (uint8_t)(SM->sockets[i].rwnd.expected_seq - 1);
                    dup_ack.rwnd_size = (uint8_t)SM->sockets[i].rwnd.size;

                    sendto(SM->sockets[i].udp_sockfd,
                           &dup_ack, sizeof(dup_ack), 0,
                           (struct sockaddr *)&SM->sockets[i].remote_addr,
                           sizeof(struct sockaddr_in));

                    SM->sockets[i].rwnd.nospace_flag = 0;
                }
                pthread_mutex_unlock(&SM->sockets[i].mutex);
            }
            continue;
        }

        if (activity < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            continue;
        }

        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            int fd = SM->sockets[i].udp_sockfd;
            if (fd < 0 || !FD_ISSET(fd, &readfds))
                continue;

            ktp_msg_t msg;
            struct sockaddr_in sender;
            socklen_t slen = sizeof(sender);

            ssize_t n = recvfrom(fd, &msg, sizeof(msg), 0,
                                 (struct sockaddr *)&sender, &slen);
            if (n <= 0)
                continue;
            if (dropMessage(DROP_PROB))
                continue;

            if (SM->sockets[i].is_free || !SM->sockets[i].is_bound)
                continue;

            pthread_mutex_lock(&SM->sockets[i].mutex);

            if (msg.is_ack)
            {
                SM->sockets[i].swnd.size = (int)msg.rwnd_size;
                int shift = 0;
                for (int j = 0; j < SM->sockets[i].swnd.count; j++)
                {
                    if (SM->sockets[i].swnd.unacked_seq[j] <= msg.seq_no)
                        shift++;
                }
                if (shift > 0)
                {
                    int rem = SM->sockets[i].swnd.count - shift;
                    for (int j = 0; j < rem; j++)
                    {
                        SM->sockets[i].swnd.unacked_seq[j] =
                            SM->sockets[i].swnd.unacked_seq[j + shift];
                        SM->sockets[i].swnd.buffer[j] =
                            SM->sockets[i].swnd.buffer[j + shift];
                        SM->sockets[i].swnd.send_times[j] =
                            SM->sockets[i].swnd.send_times[j + shift];
                    }
                    SM->sockets[i].swnd.count = rem;
                }
            }
            else
            {

                if (msg.seq_no == SM->sockets[i].rwnd.expected_seq &&
                    SM->sockets[i].recv_count < WINDOW_SIZE)
                {
                    SM->sockets[i].recv_buffer[SM->sockets[i].recv_count] = msg;
                    SM->sockets[i].recv_count++;
                    SM->sockets[i].rwnd.size--;
                    SM->sockets[i].rwnd.expected_seq++;

                    if (SM->sockets[i].rwnd.size == 0)
                        SM->sockets[i].rwnd.nospace_flag = 1;

                    ktp_msg_t ack;
                    memset(&ack, 0, sizeof(ack));
                    ack.is_ack = 1;
                    ack.seq_no = msg.seq_no;
                    ack.rwnd_size = (uint8_t)SM->sockets[i].rwnd.size;

                    sendto(fd, &ack, sizeof(ack), 0,
                           (struct sockaddr *)&sender, slen);
                }
                else if (msg.seq_no < SM->sockets[i].rwnd.expected_seq)
                {

                    ktp_msg_t ack;
                    memset(&ack, 0, sizeof(ack));
                    ack.is_ack = 1;
                    ack.seq_no = (uint8_t)(SM->sockets[i].rwnd.expected_seq - 1);
                    ack.rwnd_size = (uint8_t)SM->sockets[i].rwnd.size;

                    sendto(fd, &ack, sizeof(ack), 0,
                           (struct sockaddr *)&sender, slen);
                }
            }

            pthread_mutex_unlock(&SM->sockets[i].mutex);
        }
    }
    return NULL;
}

// Thread S to handle timeouts and retransmissions
void *thread_S(void *arg)
{
    (void)arg;
    struct timespec ts = {.tv_sec = T / 2, .tv_nsec = 0};

    while (1)
    {
        nanosleep(&ts, NULL);
        time_t now = time(NULL);

        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (SM->sockets[i].is_free)
                continue;
            pthread_mutex_lock(&SM->sockets[i].mutex);
            // if timeout occured.Retransmit
            if (SM->sockets[i].swnd.count > 0 &&
                (now - SM->sockets[i].swnd.send_times[0]) >= T)
            {

                for (int j = 0; j < SM->sockets[i].swnd.count; j++)
                {
                    sendto(SM->sockets[i].udp_sockfd,
                           &SM->sockets[i].swnd.buffer[j],
                           sizeof(ktp_msg_t), 0,
                           (struct sockaddr *)&SM->sockets[i].remote_addr,
                           sizeof(struct sockaddr_in));

                    SM->sockets[i].total_transmissions++;
                    SM->sockets[i].swnd.send_times[j] = now;
                }
            }

            while (SM->sockets[i].send_count > 0 &&
                   SM->sockets[i].swnd.count < SM->sockets[i].swnd.size)
            {

                ktp_msg_t msg = SM->sockets[i].send_buffer[0];
                msg.seq_no = SM->sockets[i].swnd.next_seq++;

                SM->sockets[i].total_messages++;

                int idx = SM->sockets[i].swnd.count;
                SM->sockets[i].swnd.buffer[idx] = msg;
                SM->sockets[i].swnd.unacked_seq[idx] = msg.seq_no;
                SM->sockets[i].swnd.send_times[idx] = time(NULL);
                SM->sockets[i].swnd.count++;

                for (int j = 0; j < SM->sockets[i].send_count - 1; j++)
                    SM->sockets[i].send_buffer[j] = SM->sockets[i].send_buffer[j + 1];
                SM->sockets[i].send_count--;

                sendto(SM->sockets[i].udp_sockfd,
                       &msg, sizeof(msg), 0,
                       (struct sockaddr *)&SM->sockets[i].remote_addr,
                       sizeof(struct sockaddr_in));

                SM->sockets[i].total_transmissions++;
            }

            pthread_mutex_unlock(&SM->sockets[i].mutex);
        }
    }
    return NULL;
}

//this thread helps to reclaim the dead Slots (the slots whose processes has been killed or work is done)
void *garbage_collector(void *arg)
{
    (void)arg;
    while (1)
    {
        sleep(10);
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (SM->sockets[i].is_free)
                continue;
            if (kill(SM->sockets[i].pid, 0) == -1 && errno == ESRCH)
            {
                pthread_mutex_lock(&SM->sockets[i].mutex);
                SM->sockets[i].is_free = 1;
                SM->sockets[i].is_bound = 0;
                SM->sockets[i].send_count = 0;
                SM->sockets[i].recv_count = 0;
                SM->sockets[i].swnd.count = 0;
                SM->sockets[i].swnd.size = WINDOW_SIZE;
                SM->sockets[i].swnd.next_seq = 1;
                SM->sockets[i].rwnd.size = WINDOW_SIZE;
                SM->sockets[i].rwnd.expected_seq = 1;
                SM->sockets[i].rwnd.nospace_flag = 0;
                SM->sockets[i].total_messages = 0;
                SM->sockets[i].total_transmissions = 0;
                SM->needs_bind[i] = 0;
                SM->bind_done[i] = 0;
                pthread_mutex_unlock(&SM->sockets[i].mutex);
                printf("[GC] Reclaimed slot %d (dead pid %d)\n",
                       i, SM->sockets[i].pid);
            }
        }
    }
    return NULL;
}


//the actual main funcntion just intializes the shared memory SM and all the pthreads required
int main(void)
{
    init_attach_shm();

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    for (int i = 0; i < MAX_KTP_SOCKETS; i++)
    {
        pthread_mutex_init(&SM->sockets[i].mutex, &attr);

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
        {
            perror("socket");
            exit(1);
        }
        SM->sockets[i].udp_sockfd = fd;

        SM->sockets[i].is_free = 1;
        SM->sockets[i].is_bound = 0;
        SM->sockets[i].send_count = 0;
        SM->sockets[i].recv_count = 0;
        SM->sockets[i].swnd.count = 0;
        SM->sockets[i].swnd.size = WINDOW_SIZE;
        SM->sockets[i].swnd.next_seq = 1;
        SM->sockets[i].rwnd.size = WINDOW_SIZE;
        SM->sockets[i].rwnd.expected_seq = 1;
        SM->sockets[i].rwnd.nospace_flag = 0;

        SM->needs_bind[i] = 0;
        SM->bind_done[i] = 0;
        SM->bind_result[i] = 0;
    }
    pthread_mutexattr_destroy(&attr);

    pthread_t r_tid, s_tid, gc_tid, bh_tid;
    pthread_create(&bh_tid, NULL, bind_handler, NULL);
    pthread_create(&r_tid, NULL, thread_R, NULL);
    pthread_create(&s_tid, NULL, thread_S, NULL);
    pthread_create(&gc_tid, NULL, garbage_collector, NULL);

    printf("initksocket daemon running (T=%ds, p=%.2f, N=%d)\n",
           T, DROP_PROB, MAX_KTP_SOCKETS);

    pthread_join(r_tid, NULL);
    pthread_join(s_tid, NULL);
    pthread_join(gc_tid, NULL);
    pthread_join(bh_tid, NULL);
    return 0;
}