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
        fprintf(stderr, "[user2] k_socket failed (ktp_error=%d)\n", ktp_error);
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
        fprintf(stderr, "[user2] k_bind failed\n");
        return 1;
    }

    char filenmae[1024];
    snprintf(filenmae, 1024, "received_file_%d.txt", sender_port);
    int out_fd = open(filenmae, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd < 0)
    {
        perror("[user2] open received_file.txt");
        return 1;
    }

    char buf[MSG_SIZE];
    long total_bytes = 0;
    int seg_received = 0;
    printf("[user2] Waiting for segments...\n");

    while (1)
    {
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        ssize_t bytes = k_recvfrom(sockfd, buf, MSG_SIZE, 0,
                                   (struct sockaddr *)&sender, &slen);

        if (bytes == 0)
        {
            printf("[user2] EOF packet received. File transfer complete!\n");
            break;
        }
        else if (bytes > 0)
        {
            write(out_fd, buf, bytes);
            seg_received++;
            total_bytes += bytes;
            printf("[user2] Received seg %4d (%4zd B) | total %4d segs / %ld B\n",
                   seg_received, bytes, seg_received, total_bytes);
        }
        else if (bytes < 0 && ktp_error == ENOMESSAGE)
        {
            usleep(100000);
        }
        else
        {
            fprintf(stderr, "[user2] k_recvfrom error (ktp_error=%d)\n", ktp_error);
            break;
        }
    }

    close(out_fd);
    k_close(sockfd);
    return 0;
}