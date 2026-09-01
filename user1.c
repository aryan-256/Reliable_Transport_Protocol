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
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/*The implemtation sequence follows the same format as it was in the UDP protocols*/
int main(int argv, char *argc[])
{

    if (argv != 3)
    {
        printf("Usage: ./user1 <SENDERPORT> <RECEIVER PORT>");
        exit(0);
    }

    int sender_port = atoi(argc[1]);
    int receiver_port = atoi(argc[2]);

    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[user1] k_socket failed (ktp_error=%d)\n", ktp_error);
        return 1;
    }

    struct sockaddr_in src_addr, dest_addr;
    memset(&src_addr, 0, sizeof(src_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));

    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(sender_port);
    src_addr.sin_addr.s_addr = INADDR_ANY;

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(receiver_port);
    inet_pton(AF_INET, "127.0.0.1", &dest_addr.sin_addr);

    if (k_bind(sockfd,
               (struct sockaddr *)&src_addr, sizeof(src_addr),
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
        fprintf(stderr, "[user1] k_bind failed\n");
        return 1;
    }

    int fd = open("largefile.txt", O_RDONLY);
    if (fd < 0)
    {
        perror("[user1] open largefile.txt");
        return 1;
    }

    char buf[MSG_SIZE];
    ssize_t bytes_read;
    long total_bytes = 0;
    int seg_enqueued = 0;

    while ((bytes_read = read(fd, buf, MSG_SIZE)) > 0)
    {
        int retries = 0;
        while (k_sendto(sockfd, buf, bytes_read, 0,
                        (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
        {
            if (ktp_error == ENOSPACE)
            {
                retries++;
                usleep(100000);
            }
            else
            {
                fprintf(stderr, "[user1] k_sendto fatal (ktp_error=%d)\n", ktp_error);
                close(fd);
                k_close(sockfd);
                return 1;
            }
        }
        seg_enqueued++;
        total_bytes += bytes_read;
        printf("[user1] Enqueued seg %4d (%4zd B, %d retries) | total %4d segs / %ld B\n",
               seg_enqueued, bytes_read, retries, seg_enqueued, total_bytes);
    }
    close(fd);

    printf("[user1] Sending EOF packet...\n");
    while (k_sendto(sockfd, buf, 0, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
        if (ktp_error == ENOSPACE)
        {
            usleep(100000);
        }
        else
        {
            fprintf(stderr, "[user1] EOF k_sendto fatal (ktp_error=%d)\n", ktp_error);
            break;
        }
    }
    printf("[user1] All %d segments enqueued (%ld bytes). Closing...\n",
           seg_enqueued, total_bytes);

    k_close(sockfd);
    printf("[user1] Done - all segments delivered.\n");
    return 0;
}