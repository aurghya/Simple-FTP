#include <stdio.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <assert.h>

#define BACKLOG 5
#define BUFFLEN 70
#define PACKET 80
#define CMDLEN 1024
#define ARGMAX 5
#define LBSOCK 1025
#define UBSOCK 65534

int num = 0;
typedef unsigned short int usi;

// break a string according to a delimiter
int parse_cmd(char **list,char *str,const char *delim){	
	int i=0;
	list[i]=strtok(str,delim);
	while(list[i]!=NULL){
		i++;
		list[i]=strtok(NULL,delim);
	}
	return i;
}

int isPresent(char c, char *str){
	int i, n=strlen(str);
	for(i=0; i<n; i++){
		if(str[i]==c)return 1;
	}
	return 0;
}

// establish a connection with the client and receive the first command
int connection(int newsockfd){	

	char cmd[CMDLEN],c;
	int i=0;
	usi code = 200;
	memset(cmd,0,sizeof(cmd));
	while(recv(newsockfd, &c, 1, 0)){
		if(c=='\n' || c=='\0'){
			cmd[i++]='\0';
			break;
		}
		cmd[i++]=c;
	}

	char *args[ARGMAX];
	int argc = parse_cmd(args, cmd, " \n");
	if(argc!=2 || strcmp("port",args[0])){
		code = 503;
		usi scode = htons(code);
		send(newsockfd, &scode, sizeof(usi), 0);
		exit(0);
	}

	int client_port = atoi(args[1]);
	if(client_port<=LBSOCK || client_port>=UBSOCK){
		code = 550;
		usi scode = htons(code);
		send(newsockfd, &scode, sizeof(usi), 0);
		exit(0);
	}

	code = 200;
	usi scode = htons(code);
	send(newsockfd, &scode, sizeof(usi), 0);

	return client_port;
}


int create_server(int server_port){
	int sockfd, newsockfd;
	int clilen;
	struct sockaddr_in cliaddr, servaddr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Cannot create socket!\n");
		exit(0);
	}

    int reuse_address = 1;
    int set_sock = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
    if (set_sock == -1){
        perror("setsockopt");
        exit(0);
    }

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(server_port);

	if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		perror("Unable to bind local address\n");
		exit(0);
	}

	listen(sockfd, BACKLOG);
	printf("Server Running...\n");
	return sockfd;
}

int connect_to_server(int server_port){

	int	sockfd ;
	struct sockaddr_in	serv_addr;
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Unable to create socket!\n");
		exit(1);
	}

	serv_addr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &serv_addr.sin_addr);
	serv_addr.sin_port = htons(server_port);

	// num++; printf("%d",num);
	if ((connect(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr))) < 0) {
		// perror("Unable to connect to server!\n");
		exit(1);
	}
	return sockfd;
}

void send_single_block(int sockfd, char c, usi size, char *buf){
	char data[PACKET];
	data[0]=c;
	usi s = htons(size);
	data[2]=(s >> 8) & 0xFF;
	data[1]=s & 0xFF;
	int i;
	for(i=0; i<size; i++){
		data[3+i]=buf[i];
	}
	send(sockfd, data, size+3, 0);
}

void send_block(int sockfd, char c, usi size, char *buf){
	send(sockfd, &c, 1,0);
	usi s = htons(size);
	send(sockfd, &s, sizeof(usi), 0);
	send(sockfd, buf, size, 0);	
}

// send file
void sendfile(int sockfd, int fd){

	char c; char buf[BUFFLEN];

	usi size; c='N';
	while((size = read(fd, buf, BUFFLEN))==BUFFLEN){
		send_block(sockfd, c, size, buf);
		// send_single_block(sockfd, c, size, buf);
	}
	c = 'L';
	send_block(sockfd, c, size, buf);
	// send_single_block(sockfd, c, size, buf);
	close(sockfd);

	close(fd);
	return;
}

usi readShort(int sockfd){
	char b0,b1;
	recv(sockfd, &b0, 1, 0);
	recv(sockfd, &b1, 1, 0);
	usi size = (int)b1;
	size = size << 8;
	size += b0;
	return size;
}

void getfile(int sockfd, char *filename){
	int i=0,fd; 
	char c,l;
	usi size,s;

	while(recv(sockfd, &l, 1, 0)){
		if(!i)fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC,0666);
		i++;
		// recv(sockfd, &size, sizeof(usi), 0);
		size = readShort(sockfd);
		// printf("%c %d\n",l,ntohs(size));
		for(s=0; s<ntohs(size); s++){
			recv(sockfd, &c, 1, 0);
			write(fd, &c, 1);
		}
		if(l=='L')break;
	}

	close(fd);
	return;
}

int main(int argc, char *argv[]){

	int server_port;

	if(argc!=2){
		printf("To run : ./server <port>\n");
		exit(0);
	}
	else server_port = atoi(argv[1]);

	int sockfd, newsockfd;
	int clilen;
	struct sockaddr_in cliaddr, servaddr;

	sockfd = create_server(server_port);

	while (1)
	{
		clilen = sizeof(cliaddr);
		newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
		if (newsockfd < 0)
		{
			perror("Accept error!\n");
			exit(0);
		}

		if(fork()==0){
			int client_port = connection(newsockfd);
			// printf("connection done");

			while(1){
				char cmd[CMDLEN];
				recv(newsockfd, cmd, CMDLEN, 0);
				// printf("%s",cmd);
				
				int status;
				usi code;
				char *args[ARGMAX];
				int argc = parse_cmd(args, cmd, " \n");

				// get a file from server
				if(!strcmp("get",args[0])){
					if(argc == 2){
						if(fork()==0){
							usleep(10000);

							// File checking
							int fd = open(args[1], O_RDONLY);
							if(fd < 0)exit(1);

							// printf("%s", args[1]);

							int clientfd = connect_to_server(client_port);
							sendfile(clientfd, fd);
							exit(0);
						}
						else {
							wait(&status);
							if(WIFEXITED(status)){
								if(WEXITSTATUS(status)) code = 550;
								else code = 250;
							}
						}
					}
					else {
						code = 501;
					}
					usi scode = htons(code);
					send(newsockfd, &scode, sizeof(usi), 0);
				}
				else if(!strcmp("put",args[0])){
					if(argc==2 && !isPresent('/',args[1])){
						if(fork()==0){
							usleep(10000);
							int clientfd = connect_to_server(client_port);
							getfile(clientfd, args[1]);
							exit(0);
						}
						else {
							wait(&status);
							if(WIFEXITED(status)){
								if(WEXITSTATUS(status)) code = 550;
								else code = 250;
							}
						}
					}
					else code = 501;
					usi scode = htons(code);
					send(newsockfd, &scode, sizeof(usi), 0);
				}
				else if(!strcmp("cd",args[0])){
					if(argc==2){
						if(!chdir(args[1]))
							code=200;
						else code = 501;
					}
					else code = 501;
					usi scode = htons(code);
					send(newsockfd, &scode, sizeof(usi), 0);
				}
				else if(!strcmp("quit",args[0])){
					if(argc==1)code = 421;
					else code = 501;
					usi scode = htons(code);
					send(newsockfd, &scode, sizeof(usi), 0);
					if(code ==421)break;
				}
				else {
					code = 502;
					usi scode = htons(code);
					send(newsockfd, &scode, sizeof(usi), 0);
				}
			}
			close(newsockfd);
			exit(0);
		}
	}
	close(sockfd);
	exit(0);
}
