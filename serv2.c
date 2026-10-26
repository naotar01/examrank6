#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

typedef struct s_client{
    int id;
    char buf[1024];
}   t_client;

t_client clients[1024];

fd_set all, rfds, wfds;
int maxfd = 0, next_id = 0;
char rbuf[120000], wbuf[120000];

void err(char *s)
{
    if (s)
        write(2, s, strlen(s));
    else
        write(2, "Fatal error", 11);
    write(2, "\n", 1);
    exit(1);
}

void send_all(int from)
{
    int fd = 0;

    while (fd <= maxfd)
    {
        if (FD_ISSET(fd, &wfds) && fd != from)
            send(fd, &wbuf, strlen(wbuf), 0);
        fd++;
    }
}

int main(int ac, char **av)
{
    if (ac != 2)
        err("wrong number argument");

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        err(NULL);

    FD_ZERO(&all);
    bzero(clients, sizeof(clients));
    bzero(&addr, sizeof(addr));
    maxfd = sockfd;
    FD_SET(sockfd, &all);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(2130706433);
    addr.sin_port = htons(atoi(av[1]));

    if ((bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr))) != 0)
        err(NULL);

    if (listen(sockfd, 10) != 0)
        err(NULL);

    while (1)
    {
        rfds = wfds = all;
        if (select(maxfd + 1, &rfds, &wfds, NULL, NULL) < 0)
            continue;
        int fd = 0;
        while (fd <= maxfd)
        {
            if (FD_ISSET(fd, &rfds) && fd == sockfd)
            {
                int newfd = accept(sockfd, (struct sockaddr *)&addr, &len);
                if (newfd >= 0)
                {
                    if (newfd > maxfd)
                        maxfd = newfd;
                    clients[newfd].id = next_id++;
                    FD_SET(newfd, &all);
                    sprintf(wbuf, "server: client %d just arrived\n", clients[newfd].id);
                    send_all(newfd);
                    break;
                }
            }
            if (FD_ISSET(fd, &rfds) && fd != sockfd)
            {
                int ret = recv(fd, rbuf, 70000, 0);
                if (ret <= 0)
                {
                    sprintf(wbuf, "server: client %d just left\n", clients[fd].id);
                    send_all(fd);
                    FD_CLR(fd, &all);
                    close(fd);
                    break;
                }
                else
                {
                    int i = 0;
                    int j = strlen(clients[fd].buf);

                    while (i < ret)
                    {
                        clients[fd].buf[j] = rbuf[i];
                        if (clients[fd].buf[j] == '\n')
                        {
                            clients[fd].buf[j] = '\0';
                            sprintf(wbuf, "client %d: %s\n", clients[fd].id, clients[fd].buf);
                            send_all(fd);
                            bzero(clients[fd].buf, strlen(clients[fd].buf));
                            j = -1;
                        }
                        i++;
                        j++;
                    }
                    break;
                }
            }
            fd++;
        }
    }
}
