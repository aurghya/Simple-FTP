#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define MAX 100
#define PACKET 80
#define BUFFLEN 70
#define IPLEN 20
#define CMDLEN 1024
#define ARGMAX 10
#define BACKLOG 10
#define PATHMAX 10

int server_port = 21;
typedef unsigned short int usi;

// break the string in accordance with the delimiter
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

int blank(char *str){
    int i,n=strlen(str);
    for(i=0; i<n; i++){
        if(str[i]!=' ' || str[i]!='\n')
            return 0;
    }
    return 1;
}

// create a server socket in server port
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
	return sockfd;
}

// connect to server at server_port
int connect_to_server(int server_port){

	int	sockfd ;
	struct sockaddr_in	serv_addr;
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Unable to create socket!\n");
		exit(0);
	}

	serv_addr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &serv_addr.sin_addr);
	serv_addr.sin_port = htons(server_port);

	if ((connect(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr))) < 0) {
		perror("Unable to connect to server!\n");
		exit(0);
	}
	return sockfd;
}

// send the port of the data channel
int send_port(int sockfd){
    int client_port;
    char cmd[CMDLEN]; 
	
    PORT:
	memset(cmd, 0, CMDLEN);
    printf("> "); fgets(cmd, CMDLEN, stdin);
	cmd[strlen(cmd)-1]='\0';
    char str[CMDLEN];strcpy(str,cmd);

    char *args[ARGMAX];
    // if(parse_cmd(args, str, " \n")>1){
    //     client_port = atoi(args[1]);
    // }
    int argc = parse_cmd(args, str, " \n");
    if(argc == 0)goto PORT;
    else if(argc>1){
        client_port = atoi(args[1]);
    }

    send(sockfd, cmd, strlen(cmd)+1, 0);

    usi rcode, code;
    recv(sockfd, &rcode, sizeof(usi), 0);
	code = ntohs(rcode);

	if(code!=200){
		printf("%d : Error encountered!\n", code);
		exit(0);
	}

	printf("200 : Port is accepted as port for data channel\n");

	return client_port;
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

// get the sent file from the server
void getfile(int sockfd, char *filename){
	int i=0,fd; 

	char c,l;
	usi size,s,sz;

	// receive the file following header and size
	while(recv(sockfd, &l, 1, 0)){
		if(!i)fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
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

// send the file to the server
void sendfile(int sockfd, int fd){

	char c; char buf[BUFFLEN];

	// reading and sending each block
	usi size; c='N';
	while((size = read(fd, buf, BUFFLEN))==BUFFLEN){
		send_block(sockfd, c, size, buf);	// choose whether data is sent in 3 send or single send
		// send_single_block(sockfd, c, size, buf);
	}
	c = 'L';
	send_block(sockfd, c, size, buf);			// choose whether data is sent in 3 send or single send
	// send_single_block(sockfd, c, size, buf);
	close(sockfd);

	close(fd);
	return;
}

// main function
int main(int argc, char *argv[]){

	// the port to connect to server
	if(argc!=2){
		printf("To run the client : ./client <port>");
		exit(0);
	}
    server_port = atoi(argv[1]);

	// connect to server
	int sockfd = connect_to_server(server_port);

    int client_port, response;
    char cmd[CMDLEN]; memset(cmd, 0, CMDLEN);

	// send the port command
	client_port = send_port(sockfd);

	int newsockfd;
	int clilen;
	struct sockaddr_in cliaddr, servaddr;

	while(1){

		printf("> "); 
		fflush(stdin); 
		fgets(cmd, CMDLEN, stdin);
		cmd[strlen(cmd)-1]='\0';
        if(blank(cmd))continue;

		send(sockfd, cmd, strlen(cmd)+1, 0);

		usi code, rcode;
		char *args[ARGMAX];
		int argc = parse_cmd(args, cmd, " \n");

		if(!strcmp("get",args[0])){
			pid_t p;
			if((p=fork())==0){

				// create data channel server
				int datafd = create_server(client_port);
				clilen = sizeof(cliaddr);
				newsockfd = accept(datafd, (struct sockaddr *)&cliaddr, &clilen);
				if (newsockfd < 0)
				{
					perror("Accept error!\n");
					exit(0);
				}

				// more than 1 argument is an error
				if(argc != 2)exit(1);

				char *file[ARGMAX]; int f=parse_cmd(file, args[1], " /");
				// printf("%s", args[1]);
				getfile(newsockfd, file[f-1]);

				close(newsockfd);
				close(datafd);
				exit(0);
			}
			else {
				// receive the return code
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				if(code == 250){	// success
					wait(NULL);
					printf("250 : File transfer succesful.\n");
				}				
				else {	// Failures
					kill(p, SIGKILL);
					wait(NULL);
					if(code == 501)
						printf("501 : Invalid arguments.\n");
					else {
						printf("550 : File transfer failed.\n");
					}
				}	
			}
		}
		else if(!strcmp("put",args[0])){	// put command

			pid_t p;
			
			// if there are invalid arguments, return with error
			if(argc!=2 || isPresent('/',args[1])){
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				printf("501 : Invalid arguments.\n");
				continue;
			}

			// try to open the file
			int fd = open(args[1], O_RDONLY);
			if(fd<0){
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				printf("550 : File transfer failed.\n");
				continue;
			}

			// fork and send the file through data channel
			if((p=fork())==0){
				int datafd = create_server(client_port);
				clilen = sizeof(cliaddr);
				newsockfd = accept(datafd, (struct sockaddr *)&cliaddr, &clilen);
				if (newsockfd < 0)
				{
					perror("Accept error!\n");
					exit(0);
				}					

				sendfile(newsockfd, fd);

				close(newsockfd);
				close(datafd);
				exit(0);
			}
			else {
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				if(code == 250){
					wait(NULL);
					printf("250 : File transfer succesful.\n");
				}
				else {
					kill(p, SIGKILL);
					wait(NULL);
					if(code = 501){
						printf("501 : Invalid arguments.\n");
					}
					else printf("550 : File transfer failed.\n");
				}
			}
		}
		else if(!strcmp("cd", args[0])){
			if(argc==2){
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				if(code==200){
					printf("200 : Directory changed succesfully.\n");
				}
				else printf("501 : Directory change failed.\n");
			}
			else {
				recv(sockfd, &rcode, sizeof(usi), 0);
				code = ntohs(rcode);
				printf("501 : Directory change failed.\n");			
			}
		}
		else if(!strcmp("quit",args[0])){
			recv(sockfd, &rcode, sizeof(usi), 0);
			code = ntohs(rcode);
			if(code == 501) {
				printf("501 : Invalid arguments.\n");
			}
			else {
				printf("421 : Exiting ...\n");
				close(sockfd);
				// printf("Bye\n");
				exit(0);
			}
		}
		else {
			recv(sockfd, &rcode, sizeof(usi), 0);
			code = ntohs(rcode);
			printf("%d : Invalid command\n", code);
		}

		// printf("%d\n", code);
		fflush(stdout);
	}
    return 0;
}
