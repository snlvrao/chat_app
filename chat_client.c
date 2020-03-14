#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio_ext.h>
#define MAXLINE 1024

int main(int argc, char *argv[])
{
	int sfd, len, ret, online_count, fid;
	char sender[20];
	char buf[MAXLINE];

	typedef struct sockaddr_in sock_addr;
	sock_addr server_addr, client_addr;

	if(argc < 4)
	{
		printf("Insufficient arguments\n");
		exit(1);
	}

	/* Initialize server_addr struct members */
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[2]));
	server_addr.sin_addr.s_addr = inet_addr(argv[1]);

	/* Create TCP socket */
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd == -1)
	{
		perror("socket");
		exit(1);
	}
	
	len = sizeof(sock_addr);

	/* Connect to server */
	if(connect(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
	{
		perror("connect");
		exit(1);
	}

	/* Send credentials to server */
	write(sfd, argv[3], sizeof(argv[3]));

	/* Receive reply from server */
	while(1)
	{
		int i = 0, selfid;
		
		online_count = 0;
		memset(buf, 0, sizeof(buf));
		ret = read(sfd, &online_count, sizeof(online_count));
		printf("\n%d Clients Online\n", online_count);
		
		while(ret = read(sfd, buf, sizeof(buf)))
		{
			if(strcmp(buf, "END_OF_CLIST") == 0)
			{
				break;
			}
			printf("%d : %s\n", ++i, buf);
			
			/* Blacklisting self ID */
			if(strcmp(buf, argv[3]) == 0)
			{
				selfid = i;
			}	
		}
		printf("\nSelect friend ID to connect or press 0 to refresh list\n");
		
		/* Monitor both stdin and socket simultaneously */
		fd_set master;
		fd_set read_fds;
		
		FD_ZERO(&master);
		FD_ZERO(&read_fds);
		
		FD_SET(0, &master);
		FD_SET(sfd, &master);
		
		read_fds = master;
		if(select(sfd + 1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			exit(1);
		}
		// if there are any data ready to read from the socket
		if(FD_ISSET(sfd, &read_fds))
		{
			read(sfd, buf, sizeof(buf));
			if(strcmp(buf, "Success") == 0)
			{
				read(sfd, buf, sizeof(buf));
				strcpy(sender, buf);
				printf("\nConnecting to %s...\n", buf);
				break;
			}
		}
		// if there is something in stdin
		if(FD_ISSET(0, &read_fds))
		{
			__fpurge(stdin);
			scanf("%d", &fid);
			if(fid == selfid)
			{
				printf("\nCannot select self! Choose another ID\n");
				fid = 0;
			}
			if(fid == 0)
			{
				write(sfd, &fid, sizeof(fid));
				continue;
			}
			write(sfd, &fid, sizeof(fid));
			
			read(sfd, buf, sizeof(buf));
			if(buf[0] == '#')
			{
				sscanf(buf, "%*s %s", sender);
				break;
			}
			else if(strcmp(buf, "User_Busy") == 0)
			{
				fid = 0;
				printf("\nUser Busy\n");
				write(sfd, &fid, sizeof(fid));
			}
		}
	}
	
	printf("\nConnection Established\n\n");
	write(sfd, "", 1);

	/* Communication begins - Monitor both stdin and socket simultaneously */
	fd_set master;
	fd_set read_fds;
	
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	
	FD_SET(0, &master);
	FD_SET(sfd, &master);
	while(1)
	{
		read_fds = master;
		if(select(sfd + 1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			exit(1);
		}
		// if there are any data ready to read from the socket
		if(FD_ISSET(sfd, &read_fds))
		{
			memset(buf, 0, sizeof(buf));
			ret = read(sfd, buf, sizeof(buf));
			if(ret == -1)
			{
				perror("read");
				exit(1);
			}
			printf("\n%s : %s\n", sender, buf);
			if(ret == -1)
			{
				perror("write");
				exit(1);
			}
		}
		// if there is something in stdin
		if(FD_ISSET(0, &read_fds))
		{
			memset(buf, 0, sizeof(buf));
			//ret = read(0, buf, sizeof(buf));
			__fpurge(stdin);
			scanf("%[^\n]s", buf);
			if(ret == -1)
			{
				perror("read");
				exit(1);
			}
			ret = write(sfd, buf, sizeof(buf));
			memset(buf, 0, sizeof(buf));
			if(ret == -1)
			{
				perror("write");
				exit(1);
			}
		}
	}

	while(1)
	{
	}
	
	/* Close connection */
	close(sfd);
}
