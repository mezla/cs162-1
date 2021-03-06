#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"

#define BUFF_SIZE 1024
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
int is_File(char *path){
	struct stat path_stat;
	stat(path,&path_stat);
	return S_ISREG(path_stat.st_mode);
}

int is_Directory(char *path){
	struct stat path_stat;
	stat(path,&path_stat);
	return S_ISDIR(path_stat.st_mode);
}

void write_from_file_to_fd(int fd,char *file_path){
	int file_des = open(file_path,O_RDONLY);
	if(file_des == -1){
			printf("fail to open file\n");
	}
  char buffer[1024];
	while(read(file_des,buffer,sizeof(buffer)) != 0){
		http_send_string(fd,buffer);
	}
	close(file_des);
}

int read_from_target_write_to_client(int target_fd,int client_fd){
	char buffer[BUFF_SIZE];
	int nbytes;
	nbytes = read(target_fd,buffer,BUFF_SIZE);
	printf("reveived %d bytes.\n",nbytes);
	printf("%s",buffer);
	if(nbytes < 0){
		perror("read");
		exit(EXIT_FAILURE);
	}
	else if (nbytes == 0){
		return -1;
	}
	else{
		http_send_string(client_fd,buffer);
		return 0;
	}
}

void handle_files_request(int fd)
{
  /* YOUR CODE HERE */
  struct http_request *request = http_request_parse(fd);
  http_start_response(fd, 200);
	char* file_path = strcat(server_files_directory,request->path);
	printf("file path is %s\n",file_path);
	if(is_Directory(file_path)){
 	 	http_send_header(fd, "Content-type", "text/html");
 		http_end_headers(fd);
		file_path = strcat(file_path,"/index.html");
		write_from_file_to_fd(fd,file_path);
	}
  else if(is_File(file_path)){
  	http_send_header(fd, "Content-type", http_get_mime_type(request->path));
  	http_end_headers(fd);
		write_from_file_to_fd(fd,file_path);
	}
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd)
{
  /* YOUR CODE HERE */
	int target_fd;
	struct addrinfo hints,*targetinfo,*p;
  int rv;
	memset(&hints,0,sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if((rv = getaddrinfo(server_proxy_hostname,"http",&hints,&targetinfo)) != 0 ){
		fprintf(stderr,"getaddrinfo: %s\n",gai_strerror(rv));
		exit(1);
	}

	for(p = targetinfo; p != NULL; p = p->ai_next){
		if(p->ai_next != NULL){
			continue;
		}
		struct sockaddr_in addr =*( (struct sockaddr_in*)p->ai_addr);
		addr.sin_port = htons(server_proxy_port);

		size_t addr_length = sizeof(addr);
		if((target_fd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))==-1){
			perror("socket");
			continue;
		}
		if(connect(target_fd,(struct sockaddr*)&addr,(socklen_t)addr_length) == -1){
			close(target_fd);
			perror("connect");
			continue;
		}
		break;
	}
	struct sockaddr_in *addr = (struct sockaddr_in*)p->ai_addr;
	printf("connect to %s on port %d\n",inet_ntoa(addr->sin_addr),server_proxy_port);
	if(p == NULL){
		printf("failed to connect\n");
		exit(2);
	}

	//write(target_fd,"GET /\r\n",strlen("GET /\r\n"));	
	fd_set read_fd_set;
	fd_set master;
	FD_ZERO(&master);
	FD_ZERO(&read_fd_set);
	FD_SET(target_fd,&master);
	FD_SET(fd,&master);
	int r;
	char buf[10000];
	while(1){
		read_fd_set = master;
		r = select(FD_SETSIZE,&read_fd_set,NULL,NULL,NULL);
		if(r == 0){
			printf("error\n");
			perror("select");
			exit(EXIT_FAILURE);
		}	
		else if( FD_ISSET(fd,&read_fd_set) ){
			r = read(fd,buf,sizeof(buf));
			if( r <= 0)  break;
			r = write(target_fd,buf,r);
			if( r <= 0) break;
			printf("client %d bytes\n",r);
		}
		else if( FD_ISSET( target_fd,&read_fd_set) ){
			r = read(target_fd,buf,sizeof(buf));
			printf("%s",buf);
			if( r <= 0 ) break;
			r = write(fd,buf,r);
			if (r <= 0) break;
		}
	}
	close(target_fd);
	freeaddrinfo(targetinfo);
}


/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int* socket_number, void (*request_handler)(int))
{

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;
  pid_t pid;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    fprintf(stderr, "Failed to set socket options: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr*) &server_address,
        sizeof(server_address)) == -1) {
    fprintf(stderr, "Failed to bind on socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    fprintf(stderr, "Failed to listen on socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  while (1) {

    client_socket_number = accept(*socket_number, (struct sockaddr*) &client_address,
        (socklen_t*) &client_address_length);
    if (client_socket_number < 0) {
      fprintf(stderr, "Error accepting socket: error %d: %s\n", errno, strerror(errno));
      continue;
    }

    printf("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    pid = fork();
    if (pid > 0) {
      close(client_socket_number);
    } else if (pid == 0) {
      signal(SIGINT, SIG_DFL); // Un-register signal handler (only parent should have it)
      close(*socket_number);
      request_handler(client_socket_number);
      close(client_socket_number);
      exit(EXIT_SUCCESS);
    } else {
      fprintf(stderr, "Failed to fork child: error %d: %s\n", errno, strerror(errno));
      exit(errno);
    }
  }

  close(*socket_number);

}

int server_fd;
void signal_callback_handler(int signum)
{
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(signum);
}

char *USAGE = "Usage: ./httpserver --files www_directory/ --port 8000\n"
              "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{

  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  server_files_directory = malloc(1024);
  getcwd(server_files_directory, 1024);
  server_proxy_hostname = "inst.eecs.berkeley.edu";
  server_proxy_port = 80;

  void (*request_handler)(int) = handle_files_request;

  int i;
  for (i = 1; i < argc; i++)
  {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;

}
