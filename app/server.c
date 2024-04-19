#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "utils.h"
#include "tpool.h"
#include "colors.h"

#define MAX_THREADS 1
// #define MAX_THREADS 4 // one thread misses parse value, add graceful exit for canceled requests

#define PORT 4221
#define C_OK 0
#define C_ERR 1
#define FLAG_DIRECTORY "--directory"

#define REQEUST_BUFFER_SIZE 1024
#define RESPONSE_BUFFER_SIZE 4096
#define FILE_BUFFER_SIZE 1024

#define STATUS_OK "HTTP/1.1 200 OK\r\n"
#define STATUS_NOT_FOUND "HTTP/1.1 404 NOT FOUND\r\n"

#define CONTENT_LENGTH "Content-Length: "
#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_FILE "Content-Type: application/octet-stream\r\n"

#define CLRF "\r\n"

struct ThreadArgs
{
	struct sockaddr_in client_addr;
	int client_fd;
} ThreadArgs;

struct Request
{
	char *url;
	char *method;
	char *path;
	char *version;
	char *accept;
	char *accept_encoding;
	char *host;
	char *user_agent;
	// char content_length[100];
	char *content_length;
	char *body;
} Request;

char *option_directory = NULL;

int server_listen();
void server_process_client(void *arg);
void request_parse(char *buffer, struct Request *request);
void response_build(char *buffer, struct Request *request);

void request_parse(char *buffer, struct Request *request)
{ // TODO: optimized

	// printf(CYAN "Request Buffer:\n " YELLOW "%s\n" RESET, buffer);

	char *token = strtok(buffer, " ");
	request->method = token;
	token = strtok(NULL, " ");
	request->path = token;
	token = strtok(NULL, " ");
	request->version = token;

	token = strtok(NULL, "\r\n");
	while (token != NULL)
	{

		if (strstr(token, ":") == NULL)
		{
			request->body = token;
		}
		else
		{
			for (int i = 0; token[i]; i++) // flatten
			{
				token[i] = tolower(token[i]);
			}
		}

		if (strstr(token, "accept:") != NULL)
		{
			request->accept = token;
		}
		else if (strstr(token, "accept-encoding:") != NULL)
		{
			request->accept_encoding = token;
		}
		else if (strstr(token, "user-agent:") != NULL)
		{
			request->user_agent = token;
		}
		else if (strstr(token, "host:") != NULL)
		{
			request->host = token;
		}
		else if (strstr(token, "content-length:") != NULL)
		{
			request->content_length = token;
			// strcpy(request->content_length, token);
		}

		token = strtok(NULL, "\r\n");
	}
}

void response_build(char *buffer, struct Request *request)
{

	// printf(RED "path: %s\n" RESET, request->path);

	if (strstr(request->path, "/files/") != NULL)
	{
		// strremove();
		// strtok(); <-- probably faster

		strremove(request->path, "/files/");

		request->body = "file content";
		snprintf(buffer, RESPONSE_BUFFER_SIZE,
				 "%s%s%s%zd\r\n\r\n%s\r\n",
				 STATUS_OK,
				 CONTENT_TYPE_TEXT,
				 CONTENT_LENGTH,
				 strlen(request->body),
				 request->body);
	}
	else if (strstr(request->path, "/user-agent") != NULL)
	{
		snprintf(buffer, RESPONSE_BUFFER_SIZE,
				 "%s%s%s%zd\r\n\r\n%s\r\n",
				 STATUS_OK,
				 CONTENT_TYPE_TEXT,
				 CONTENT_LENGTH,
				 strlen(request->user_agent),
				 request->user_agent);
	}
	else if (strstr(request->path, "/echo/") != NULL)
	{
		strremove(request->path, "/echo/");
		snprintf(buffer, RESPONSE_BUFFER_SIZE,
				 "%s%s%s%zd\r\n\r\n%s\r\n",
				 STATUS_OK,
				 CONTENT_TYPE_TEXT,
				 CONTENT_LENGTH,
				 strlen(request->path),
				 request->path);
	}
	else if (strcmp(request->path, "/") == 0)
	{
		snprintf(buffer, RESPONSE_BUFFER_SIZE,
				 "%s%s",
				 STATUS_OK,
				 CLRF);
	}
	else
	{
		snprintf(buffer, RESPONSE_BUFFER_SIZE,
				 "%s",
				 STATUS_NOT_FOUND);
	}
}

int server_listen()
{
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf(RED "Socket creation failed: %s...\n" RESET, strerror(errno));
		return C_ERR;
	}
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		printf(RED "SO_REUSEPORT failed: %s \n" RESET, strerror(errno));
		return C_ERR;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(PORT),
		.sin_addr = {htonl(INADDR_ANY)},
	};
	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf(RED "Bind failed: %s \n" RESET, strerror(errno));
		return C_ERR;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		printf(RED "Listen failed: %s \n" RESET, strerror(errno));
		return C_ERR;
	}
	printf(CYAN "Server listening: %s:%d <----------\n" RESET, inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

	return server_fd;
}

void server_process_client(void *arg)
{
	int client_fd = (uintptr_t)arg;
	char response_buffer[RESPONSE_BUFFER_SIZE];
	char request_buffer[REQEUST_BUFFER_SIZE];

	size_t error = recv(client_fd, request_buffer, sizeof(request_buffer), 0);
	if (error == -1 || error == 0)
	{
		printf(RED "Recieved failed: %s...\n" RESET, strerror(errno));
	}

	if (strlen(request_buffer) < 1) // handle empty buffer
	{
		printf(RED "buffer empty: %s...\n" RESET, strerror(errno));
	}
	else
	{

		// printf(GREEN "Message received.\n" RESET);
		// printf(YELLOW "%s\n" RESET, request_buffer);

		struct Request request;
		request_parse(request_buffer, &request); // <------------------ goes in not empty, passed empty
		response_build(response_buffer, &request);
		// printf(YELLOW "%s\n" RESET, response_buffer);
		printf(CYAN "Response Buffer:\n " YELLOW "%s\n" RESET, response_buffer);

		error = send(client_fd, response_buffer, strlen(response_buffer), 0);
		if (error == -1)
		{
			printf(RED "Send failed: %s...\n" RESET, strerror(errno));
		}
		// printf(GREEN "Message sent.\n" RESET);
	}

	close(client_fd);
}

int main(int argc, char *argv[])
{
	// if (strcmp(argv[1], FLAG_DIRECTORY) == 0)
	// {
	// 	option_directory = argv[2];
	// }

	setbuf(stdout, NULL);

	threadpool thread_pool = thread_pool_init(MAX_THREADS);
	printf(GREEN "Thread pool created: %d threads\n" RESET, MAX_THREADS);

	int server_fd = server_listen();

	for (;;) // todo print ip and port
	{
		socklen_t client_addr_len;
		struct sockaddr_in client_addr;

		client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1)
		{
			printf(RED "Client connection failed: %s \n" RESET, strerror(errno));
		}
		// printf(CYAN "Client connected: %s:%d <----------\n" RESET, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		thread_pool_add_work(thread_pool, server_process_client, (void *)(uintptr_t)client_fd);
		// int error = thread_pool_add_work(thread_pool, server_process_client, (void *)(uintptr_t)client_fd);
		// if (error == -1)
		// {
		// 	printf(RED "Failed to create pthread: %s\n" RESET, strerror(errno));
		// 	close(client_fd);
		// }
	}

	printf(YELLOW "Waiting for thread pool work to complete..." RESET);
	thread_pool_wait(thread_pool);
	printf(RED "Killing threadpool..." RESET);
	thread_pool_destroy(thread_pool);
	printf(RED "Closing server socket..." RESET);
	close(server_fd);

	return C_OK;
}
