#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct s_client
{
	int		id;
	char	*buf;
	char	*msg;
}	t_client;

char	*str_join(char *buf, char *add);
int		extract_message(char **buf, char **msg);

t_client	*c = 0;
int			size = 0;
int			maxfd;
int			next_id = 0;
fd_set		rfds;
fd_set		wfds;

void	fatal(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

void	expand(int fd)
{
	int	i;

	if (fd < size)
		return ;
	c = realloc(c, sizeof(t_client) * (fd + 1));
	if (!c)
		fatal();
	i = size;
	size = fd + 1;
	while (i < size)
	{
		c[i].id = -1;
		c[i].buf = 0;
		c[i].msg = 0;
		i++;
	}
}

void	send_all(int fd, char *s)
{
	int	i;

	i = 0;
	while (i <= maxfd)
	{
		if (i != fd && i < size && c[i].id != -1)
		{
			c[i].msg = str_join(c[i].msg, s);
			if (!c[i].msg)
				fatal();
			FD_SET(i, &wfds);
		}
		i++;
	}
}

void	remove_client(int fd)
{
	char	s[64];

	sprintf(s, "server: client %d just left\n", c[fd].id);
	send_all(fd, s);
	free(c[fd].buf);
	free(c[fd].msg);
	c[fd].buf = 0;
	c[fd].msg = 0;
	c[fd].id = -1;
	close(fd);
	FD_CLR(fd, &rfds);
	FD_CLR(fd, &wfds);
}

void	read_client(int fd)
{
	char	b[4096];
	char	*out;
	char	*line;
	int		ret;
	int		extract_ret;

	ret = recv(fd, b, 4095, 0);
	if (ret <= 0)
	{
		remove_client(fd);
		return ;
	}
	b[ret] = 0;
	c[fd].buf = str_join(c[fd].buf, b);
	if (!c[fd].buf)
		fatal();
	extract_ret = extract_message(&c[fd].buf, &line);
	if (extract_ret == -1)
		fatal();
	while (extract_ret)
	{
		out = malloc(strlen(line) + 64);
		if (!out)
			fatal();
		sprintf(out, "client %d: ", c[fd].id);
		strcat(out, line);
		send_all(fd, out);
		free(out);
		free(line);
		extract_ret = extract_message(&c[fd].buf, &line);
		if (extract_ret == -1)
			fatal();
	}
}

void	write_client(int fd)
{
	int		ret;
	int		len;
	char	*tmp;

	if (!c[fd].msg)
	{
		FD_CLR(fd, &wfds);
		return ;
	}
	len = strlen(c[fd].msg);
	ret = send(fd, c[fd].msg, len, MSG_NOSIGNAL);
	if (ret <= 0)
	{
		return ;
	}
	if (ret == len)
	{
		free(c[fd].msg);
		c[fd].msg = 0;
		FD_CLR(fd, &wfds);
	}
	else
	{
		tmp = malloc(len - ret + 1);
		if (!tmp)
			fatal();
		strcpy(tmp, c[fd].msg + ret);
		free(c[fd].msg);
		c[fd].msg = tmp;
	}
}

char	*str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(len + strlen(add) + 1);
	if (!newbuf)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	strcat(newbuf, add);
	free(buf);
	return (newbuf);
}

int	extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int		i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = malloc(strlen(*buf + i + 1) + 1);
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

int	main(int ac, char **av)
{
	int					sockfd;
	int					fd;
	int					newfd;
	char				s[64];
	struct sockaddr_in	addr;
	fd_set				rr;
	fd_set				ww;

	if (ac != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		fatal();
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(2130706433);
	addr.sin_port = htons(atoi(av[1]));
	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		fatal();
	if (listen(sockfd, 128) < 0)
		fatal();

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_SET(sockfd, &rfds);
	maxfd = sockfd;
	expand(sockfd);

	while (1)
	{
		rr = rfds;
		ww = wfds;
		if (select(maxfd + 1, &rr, &ww, 0, 0) < 0)
			continue ;
		fd = 0;
		while (fd <= maxfd)
		{
			if (FD_ISSET(fd, &rr))
			{
				if (fd == sockfd)
				{
					newfd = accept(sockfd, 0, 0);
					if (newfd >= 0)
					{
						expand(newfd);
						c[newfd].id = next_id++;
						FD_SET(newfd, &rfds);
						if (newfd > maxfd)
							maxfd = newfd;
						sprintf(s, "server: client %d just arrived\n",
							c[newfd].id);
						send_all(newfd, s);
					}
				}
				else
					read_client(fd);
			}
			if (fd <= maxfd && fd < size
				&& c[fd].id != -1 && FD_ISSET(fd, &ww))
				write_client(fd);
			fd++;
		}
	}
}