#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mqueue.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include "message.h"
#define SHM_KEY 10
#define MAX_CONNECTIONS 4
#define MAXLINE 1024

int flag = 1, cfd, g_fid;
message *shmptr;
mqd_t fq_id;
char fqname[20];

static void tfunc(union sigval sv)
{
	flag = 0;
	mqd_t msq_id = *((mqd_t*)(sv.sival_ptr));

	struct mq_attr attr;
	if(mq_getattr(msq_id, &attr) < 0)
	{
		perror("mq_getattr");
		exit(1);
	}

	// Reregister for new messages on Q
	struct sigevent sev;
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = tfunc;
	sev.sigev_notify_attributes = NULL;
	sev.sigev_value.sival_ptr = sv.sival_ptr;
	if(mq_notify(msq_id, &sev) < 0)
	{
		perror("mq_notify");
		exit(EXIT_FAILURE);
	}

	// Read new message on the Q
	char* buffer = (char *)malloc(attr.mq_msgsize);
	memset(buffer, 0, attr.mq_msgsize);
	if(mq_receive(msq_id, buffer, attr.mq_msgsize, 0) < 0)
	{
		if(errno != EAGAIN)
		{
			perror("mq_receive");
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		if(buffer[0] == '/')
		{
			char connect_name[30];
			memset(connect_name, 0, sizeof(connect_name));
			sscanf(buffer, "%s %s %d", fqname, connect_name, &g_fid);
			strcpy(buffer, "Success ");
			strcat(buffer, connect_name);
			write(cfd, buffer, attr.mq_msgsize);
			while(mq_receive(msq_id, buffer, attr.mq_msgsize, 0) >= 0)
			{
			}
		}
		else
		{
			write(cfd, buffer, attr.mq_msgsize);
			if(strcmp(buffer, "#Logout") == 0)
			{
				usleep(1000);
				/* Close connection */
				close(cfd);
			}
			while(mq_receive(msq_id, buffer, attr.mq_msgsize, 0) >= 0)
			{
			}
		}
	}
}

int main(int argc, char *argv[])
{
	int sfd, len, cpid, shmid;
	char buf[MAXLINE];

	typedef struct sockaddr_in sock_addr;
	sock_addr server_addr, client_addr;

	if(argc < 3)
	{
		printf("Insufficient arguments\n");
		exit(1);
	}
	
	/* Server start message */
	printf("Server running...\n");

	/* Create shared memory */
	shmid = shmget(SHM_KEY, 4096, IPC_CREAT | 0660);
	if(shmid == -1)
	{
		perror("shmget");
		exit(1);
	}
	
	/* Attach shared memory */
	shmptr = shmat(shmid, 0, 0);
	if(shmptr == (message *) -1)
	{
		perror("shmat");
		exit(1);
	}
	
	/* Initialize shared memory struct members */
	shmptr->online_count = 0;

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

	/* Bind server to socket */
	if(bind(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1)
	{
		perror("bind");
		exit(1);
	}

	/* Listen to client connections on socket */
	if(listen(sfd, MAX_CONNECTIONS) == -1)
	{
		perror("listen");
		exit(1);
	}

	/* Accept connection requests on socket */
	len = sizeof(sock_addr);
	while(1)
	{
		cfd = accept(sfd, (struct sockaddr *) &client_addr, &len);
		if(cfd == -1)
		{
			perror("accept");
			exit(1);
		}

		/* Create handler process */
		cpid = fork();
		if(cpid == -1)
		{
			perror("fork");
			exit(1);
		}

		if(cpid == 0)
		{
			int i, pid, ret, cid, fid;
			char qname[20];
			
			pid = getpid();

			/* Attach shared memory */
			shmptr = shmat(shmid, 0, 0);
			if(shmptr == (message *) -1)
			{
				perror("shmat");
				exit(1);
			}

			/* Create and attach Message Queue */
			struct mq_attr attr;
			memset(&attr, 0, sizeof attr);
			attr.mq_msgsize = 1024;
			attr.mq_flags = 0;
			attr.mq_maxmsg = 1;
			
			strcpy(qname, "/q");
			char temp[20];
			sprintf(temp, "%d", pid);
			strcat(qname, temp);
			mqd_t msq_id = mq_open(qname, O_RDWR | O_CREAT | O_NONBLOCK, 0660, &attr);
			if(msq_id == (mqd_t) -1)
			{				
				perror("msq_id");
				exit(1);
			}
			
			/* Register Notify Event */
			struct sigevent sev;
			sev.sigev_notify = SIGEV_THREAD;
			sev.sigev_notify_function = tfunc;
			sev.sigev_notify_attributes = NULL;
			sev.sigev_value.sival_ptr = &msq_id;
			if(mq_notify(msq_id, &sev) < 0)
			{
				perror("mq_notify");
				exit(EXIT_FAILURE);
			}
			
			/* Ensure Queue is empty */
			while(mq_receive(msq_id, buf, attr.mq_msgsize, 0) >= 0)
			{
			}

			/* Read client info */
			read(cfd, buf, sizeof(buf));
			printf("%d Client ID : %s\n", pid, buf);
			
			/* Update online list */
			strcpy(shmptr->online_list[shmptr->online_count].username, buf);
			strcpy(shmptr->online_list[shmptr->online_count].qname, qname);
			cid = shmptr->online_count;
			shmptr->online_list[cid].status = 0;
			shmptr->online_count++;

			/* Acknowledge client connection */
			while(flag)
			{
				printf("%d Sending client list : no. of clients = %d\n", pid, shmptr->online_count);
				for(i = 0; i < shmptr->online_count; i++)
				{
					strcpy(buf, shmptr->online_list[i].username);
					if(shmptr->online_list[i].status == 0)
					{
						strcat(buf, " - online");
					}
					else if(shmptr->online_list[i].status == 1)
					{
						strcat(buf, " - busy");
					}
					else
					{
						strcat(buf, " - offline");
					}
					write(cfd, buf, sizeof(buf));
					usleep(1000); // To flush TCP buffer
				}
				char termination_string[] = "END_OF_CLIST";
				write(cfd, termination_string, sizeof(termination_string));
				
				/* Read client request */
				read(cfd, &fid, sizeof(fid));
				if(!flag)
				{
					break;
				}
				
				if(fid)
				{
					fid = fid - 1;
					g_fid = fid;
					if(shmptr->online_list[fid].status)
					{
						if(shmptr->online_list[fid].status == 1)
						{
							write(cfd, "User_Busy", 10);
							printf("%d User Busy - Refreshing\n", pid);
						}
						else if(shmptr->online_list[fid].status == 2)
						{
							write(cfd, "User_Offline", 13);
							printf("%d User Offline - Refreshing\n", pid);
						}
						read(cfd, &fid, sizeof(fid));
					}
					else
					{
						/* Open Message Queue*/
						char msg[30];
						char temp[3];
					    strcpy(msg, qname);
						strcat(msg, " ");
						strcat(msg, shmptr->online_list[cid].username);
						strcpy(fqname, shmptr->online_list[fid].qname);
						strcat(msg, " ");
						sprintf(temp, "%d", cid);
						strcat(msg, temp);
					   	fq_id = mq_open(fqname, O_RDWR, 0660, NULL);
	
						/* Send fqname to message queue */
						if(mq_send(fq_id, msg, strlen(msg), 0) < 0)
						{
							perror("mq_send");
							exit(EXIT_FAILURE);
						}
						
						/* Send success and sender tag */
						strcpy(buf, "# ");
						strcat(buf, shmptr->online_list[fid].username);
						ret = write(cfd, buf, sizeof(buf));
						
						/* Change status to busy */
						shmptr->online_list[cid].status = 1;
						shmptr->online_list[fid].status = 1;
						printf("%d Connecting - %s : %s\n", pid, shmptr->online_list[cid].username, shmptr->online_list[fid].username);
						break;
					}
				}
				else
				{
					printf("%d Requesting refreshed list\n", pid);
				}
			}

			/* Communication begins */
			if(fq_id == 0)
			{
				fq_id = mq_open(fqname, O_RDWR, 0660, NULL);
			}
			printf("%d Starting Communication\n", pid);
			
			while(ret = read(cfd, buf, sizeof(buf)))
			{
				if(ret == -1)
				{
					perror("read");
					exit(1);
				}
				if(strcmp(buf, "#Logout") == 0)
				{
					shmptr->online_list[cid].status = 2;
					shmptr->online_list[g_fid].status = 2;
					printf("%d Status changed - %s : offline :: %s : offline\n",pid, shmptr->online_list[cid].username, shmptr->online_list[g_fid].username);
					mq_send(fq_id, buf, sizeof(buf), 0);
					usleep(1000);
					
					/* Close connection */
					close(cfd);
					break;
				}
				mq_send(fq_id, buf, sizeof(buf), 0);
			}
			
			/* Terminate process */
			exit(0);
		}
	}
}
