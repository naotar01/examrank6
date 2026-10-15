#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/*
** Chaque client possède :
**
** id  : l'id donné par le serveur
** buf : les données reçues mais pas encore terminées par '\n'
** msg : les données qui doivent encore être envoyées
*/
typedef struct s_client
{
	int		id;
	char	*buf;
	char	*msg;
}	t_client;

/*
** Tableau dynamique des clients.
** On utilise directement le fd comme index.
*/
t_client	*c = 0;

/*
** Taille actuelle du tableau c.
*/
int			size = 0;

/*
** Plus grand fd utilisé.
*/
int			maxfd;

/*
** Prochain id à donner.
*/
int			next_id = 0;

/*
** Ensemble des fd surveillés en lecture.
*/
fd_set		rfds;

/*
** Ensemble des fd surveillés en écriture.
*/
fd_set		wfds;


/*
** Affiche l'erreur demandée par le sujet
** puis quitte le programme.
*/
void	fatal(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}


/*
** Agrandit le tableau c pour pouvoir utiliser fd.
**
** Exemple :
** si fd == 10 et que size == 5,
** on agrandit le tableau jusqu'à l'index 10.
*/
void	expand(int fd)
{
	int	i;

	/* Le tableau est déjà assez grand. */
	if (fd < size)
		return ;

	/* Agrandir le tableau. */
	c = realloc(c, sizeof(t_client) * (fd + 1));
	if (!c)
		fatal();

	/* Initialiser les nouvelles cases. */
	i = size;
	size = fd + 1;
	while (i < size)
	{
		/* -1 signifie que ce fd n'est pas un client. */
		c[i].id = -1;

		/* Aucune donnée reçue. */
		c[i].buf = 0;

		/* Aucun message à envoyer. */
		c[i].msg = 0;

		i++;
	}
}


/*
** Concatène buf et add dans une nouvelle chaîne.
**
** L'ancien buf est libéré.
**
** Exemple :
** buf = "hello"
** add = " world"
** résultat = "hello world"
*/
char	*str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	/* Longueur de l'ancien buffer. */
	if (buf)
		len = strlen(buf);
	else
		len = 0;

	/* +1 pour le '\0'. */
	newbuf = malloc(len + strlen(add) + 1);
	if (!newbuf)
		return (0);

	newbuf[0] = 0;

	/* Copier l'ancien buffer. */
	if (buf)
		strcat(newbuf, buf);

	/* Ajouter les nouvelles données. */
	strcat(newbuf, add);

	/* L'ancien buffer n'est plus nécessaire. */
	free(buf);

	return (newbuf);
}


/*
** Cherche une ligne terminée par '\n'.
**
** Retourne :
**  1  -> une ligne a été trouvée
**  0  -> aucune ligne complète
** -1  -> erreur malloc
**
** Exemple :
**
** buf = "hello\nworld"
**
** msg = "hello\n"
** buf = "world"
*/
int	extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int		i;

	*msg = 0;

	/* Aucun buffer. */
	if (!*buf)
		return (0);

	i = 0;

	/* Chercher le premier '\n'. */
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			/*
			** Créer un nouveau buffer avec
			** ce qui vient après le '\n'.
			*/
			newbuf = malloc(strlen(*buf + i + 1) + 1);
			if (!newbuf)
				return (-1);

			strcpy(newbuf, *buf + i + 1);

			/*
			** L'ancien buffer devient le message.
			*/
			*msg = *buf;

			/*
			** Couper le message juste après '\n'.
			*/
			(*msg)[i + 1] = 0;

			/*
			** Le reste devient le nouveau buffer.
			*/
			*buf = newbuf;

			return (1);
		}
		i++;
	}

	/* Pas encore de '\n'. */
	return (0);
}


/*
** Ajoute un message à tous les clients
** sauf celui qui a envoyé le message.
**
** Le message est ajouté dans msg.
** Il sera réellement envoyé lorsque select()
** indiquera que le fd est prêt en écriture.
*/
void	send_all(int fd, char *s)
{
	int	i;

	i = 0;
	while (i <= maxfd)
	{
		/*
		** i != fd :
		** ne pas envoyer au client qui a envoyé le message.
		**
		** c[i].id != -1 :
		** le fd correspond à un client connecté.
		*/
		if (i != fd && i < size && c[i].id != -1)
		{
			/* Ajouter le message à sa file d'attente. */
			c[i].msg = str_join(c[i].msg, s);

			if (!c[i].msg)
				fatal();

			/*
			** Demander à select() de surveiller
			** ce fd en écriture.
			*/
			FD_SET(i, &wfds);
		}
		i++;
	}
}


/*
** Supprime un client déconnecté.
*/
void	remove_client(int fd)
{
	/*
	** Taille fixe suffisante : le texte fait environ
	** 28 caractères et un int au plus 11 caractères.
	*/
	char	s[64];

	/* Préparer le message de départ. */
	sprintf(s, "server: client %d just left\n", c[fd].id);

	/* Prévenir les autres clients. */
	send_all(fd, s);

	/* Libérer les buffers du client. */
	free(c[fd].buf);
	free(c[fd].msg);

	c[fd].buf = 0;
	c[fd].msg = 0;
	c[fd].id = -1;

	/* Fermer la connexion. */
	close(fd);

	/* Ne plus surveiller ce fd. */
	FD_CLR(fd, &rfds);
	FD_CLR(fd, &wfds);
}


/*
** Lit les données envoyées par un client.
*/
void	read_client(int fd)
{
	char	b[4096];
	char	*out;
	char	*line;
	int		ret;
	int		extract_ret;

	/*
	** Lire les données.
	**
	** Le sujet garantit que recv() ne bloque pas
	** si select() a indiqué que le fd est prêt.
	** On lit au plus 4095 octets pour garder
	** la place du '\0' final.
	*/
	ret = recv(fd, b, 4095, 0);

	/*
	** recv() == 0 :
	** le client a fermé la connexion.
	**
	** recv() < 0 :
	** erreur de réception.
	*/
	if (ret <= 0)
	{
		remove_client(fd);
		return ;
	}

	/* Ajouter le '\0' pour avoir une chaîne. */
	b[ret] = 0;

	/*
	** Ajouter les données au buffer du client.
	**
	** Un message peut arriver en plusieurs recv().
	*/
	c[fd].buf = str_join(c[fd].buf, b);
	if (!c[fd].buf)
		fatal();

	/*
	** Extraire la première ligne complète.
	*/
	extract_ret = extract_message(&c[fd].buf, &line);

	/*
	** extract_message() peut échouer à cause de malloc().
	*/
	if (extract_ret == -1)
		fatal();

	/*
	** Tant qu'il reste des lignes complètes,
	** on les traite une par une.
	*/
	while (extract_ret == 1)
	{
		/*
		** Le tampon est alloué selon la taille de la ligne :
		** une ligne peut être très longue (elle peut s'être
		** construite sur plusieurs recv()), un tableau de
		** taille fixe pourrait déborder.
		** +64 : place pour "client %d: " et le '\0'.
		*/
		out = malloc(strlen(line) + 64);
		if (!out)
			fatal();

		/*
		** Ajouter le préfixe :
		** "client X: "
		*/
		sprintf(out, "client %d: ", c[fd].id);

		/* Ajouter la ligne reçue. */
		strcat(out, line);

		/* Mettre le message en file pour les autres clients. */
		send_all(fd, out);

		/* Le message et la ligne ont été traités. */
		free(out);
		free(line);

		/*
		** Chercher la ligne suivante.
		*/
		extract_ret = extract_message(&c[fd].buf, &line);

		if (extract_ret == -1)
			fatal();
	}
}


/*
** Envoie les données en attente d'un client.
*/
void	write_client(int fd)
{
	int		ret;
	int		len;
	char	*tmp;

	/*
	** Rien à envoyer.
	*/
	if (!c[fd].msg)
	{
		FD_CLR(fd, &wfds);
		return ;
	}

	/* Taille du message à envoyer. */
	len = strlen(c[fd].msg);

	/*
	** Envoyer les données.
	**
	** MSG_NOSIGNAL : si le client a fermé la connexion,
	** on ne reçoit pas le signal SIGPIPE (qui tuerait
	** tout le serveur), send() renvoie simplement -1.
	*/
	ret = send(fd, c[fd].msg, len, MSG_NOSIGNAL);

	/*
	** Échec de l'envoi : on ne déconnecte PAS le client
	** (un client lent ne doit jamais être déconnecté).
	** On garde le message et on réessaiera plus tard.
	** Si la connexion est vraiment fermée, recv() le
	** détectera et remove_client() fera le nettoyage.
	*/
	if (ret <= 0)
		return ;

	/*
	** Tout le message a été envoyé.
	*/
	if (ret == len)
	{
		free(c[fd].msg);
		c[fd].msg = 0;

		/* Plus besoin de surveiller l'écriture. */
		FD_CLR(fd, &wfds);
	}
	else
	{
		/*
		** send() n'a envoyé qu'une partie.
		**
		** On garde uniquement ce qui reste.
		*/
		tmp = malloc(len - ret + 1);
		if (!tmp)
			fatal();

		strcpy(tmp, c[fd].msg + ret);

		free(c[fd].msg);
		c[fd].msg = tmp;
	}
}


/*
** Programme principal.
*/
int	main(int ac, char **av)
{
	int					sockfd;
	int					fd;
	int					newfd;
	char				s[64];
	struct sockaddr_in	addr;
	fd_set				rr;
	fd_set				ww;

	/*
	** Le programme doit recevoir exactement
	** un argument : le port.
	*/
	if (ac != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}

	/*
	** Créer le socket serveur.
	*/
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		fatal();

	/*
	** Initialiser la structure sockaddr_in.
	*/
	bzero(&addr, sizeof(addr));

	/* IPv4. */
	addr.sin_family = AF_INET;

	/*
	** 127.0.0.1 = 0x7F000001 = 2130706433.
	**
	** Le serveur écoute uniquement en local.
	** htonl() met les octets dans l'ordre réseau.
	*/
	addr.sin_addr.s_addr = htonl(2130706433);

	/*
	** Convertir le port dans le format réseau.
	*/
	addr.sin_port = htons(atoi(av[1]));

	/*
	** Associer le socket à l'adresse et au port.
	*/
	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		fatal();

	/*
	** Mettre le socket en mode écoute.
	*/
	if (listen(sockfd, 128) < 0)
		fatal();

	/*
	** Initialiser les fd_sets.
	*/
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	/*
	** Le socket serveur doit être surveillé
	** en lecture pour détecter les nouvelles connexions.
	*/
	FD_SET(sockfd, &rfds);

	/*
	** Au début, le plus grand fd est le socket serveur.
	*/
	maxfd = sockfd;

	/*
	** Préparer le tableau des clients.
	*/
	expand(sockfd);

	/*
	** Boucle principale.
	*/
	while (1)
	{
		/*
		** select() modifie les fd_sets.
		** On travaille donc sur des copies.
		*/
		rr = rfds;
		ww = wfds;

		/*
		** Attendre qu'un fd soit prêt.
		*/
		if (select(maxfd + 1, &rr, &ww, 0, 0) < 0)
			continue ;

		fd = 0;

		/*
		** Parcourir tous les fd.
		*/
		while (fd <= maxfd)
		{
			/*
			** Le fd est prêt en lecture.
			*/
			if (FD_ISSET(fd, &rr))
			{
				/*
				** Le socket serveur est prêt :
				** un nouveau client veut se connecter.
				*/
				if (fd == sockfd)
				{
					newfd = accept(sockfd, 0, 0);

					if (newfd >= 0)
					{
						/*
						** Agrandir le tableau si nécessaire.
						*/
						expand(newfd);

						/*
						** Donner un id au nouveau client.
						*/
						c[newfd].id = next_id++;

						/*
						** Surveiller le client en lecture.
						*/
						FD_SET(newfd, &rfds);

						/*
						** Mettre à jour maxfd.
						*/
						if (newfd > maxfd)
							maxfd = newfd;

						/*
						** Prévenir les autres clients.
						*/
						sprintf(s,
							"server: client %d just arrived\n",
							c[newfd].id);

						send_all(newfd, s);
					}
				}
				else
				{
					/*
					** C'est un client existant
					** qui a envoyé des données.
					*/
					read_client(fd);
				}
			}

			/*
			** Le fd est prêt en écriture.
			** (c[fd].id != -1 : le client n'a pas été
			** supprimé pendant ce tour de boucle.)
			*/
			if (fd <= maxfd && fd < size
				&& c[fd].id != -1 && FD_ISSET(fd, &ww))
			{
				write_client(fd);
			}

			fd++;
		}
	}

	return (0);
}