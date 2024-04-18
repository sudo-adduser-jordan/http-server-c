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

#include "utils.h"
#include "tpool.h"
#include "colors.h"

#define MAX_THREADS 4

#define PORT 4221
#define C_OK 0
#define C_ERR 1
#define FLAG_DIRECTORY "--directory"

#define REQEUST_BUFFER_SIZE 4096
#define RESPONSE_BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 4096

#define STATUS_OK "HTTP/1.1 200 OK\r\n"
#define STATUS_NOT_FOUND "HTTP/1.1 404 NOT FOUND\r\n"

#define CONTENT_LENGTH "Content-Length: "
#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_FILE "Content-Type: application/octet-stream\r\n"

#define CLRF "\r\n"

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

typedef struct Response
{
	char *status;
	char *content_type;
	char *content_length;
	char *body;
} Response;

char *option_directory = NULL;

int server_listen();
void server_process_client(void *arg);

void request_print(const struct Request *request);
void request_parse(char *buffer, struct Request *request);
void response_print(const struct Response *response);

void request_print(const struct Request *request)
{
	printf(YELLOW "\nRequest\n" RESET);
	printf("%s\n", request->url ? request->url : "(not specified)");
	printf("%s\n", request->accept ? request->accept : "(not specified)");
	printf("%s\n", request->accept_encoding ? request->accept_encoding : "(not specified)");
	printf("%s\n", request->host ? request->host : "(not specified)");
	printf("%s\n", request->user_agent ? request->user_agent : "(not specified)");
	// printf("%s\n", request->content_length ? request->content_length : "(not specified)");
	printf("%s\n", request->content_length);
	printf("%s\n", request->body ? request->body : "(not specified)");

	printf(YELLOW "\nMethod\n" RESET);
	printf("%s\n", request->method ? request->method : "(not specified)");
	printf("%s\n", request->path ? request->path : "(not specified)");
	printf("%s\n", request->version ? request->version : "(not specified)");
}

void request_parse(char *buffer, struct Request *request)
{ // TODO: optimized

	char *token = strtok(buffer, "\r\n");
	request->url = token;

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
			// request->content_length = token;
			strcpy(request->content_length, token);
		}

		token = strtok(NULL, "\r\n");
	}

	token = strtok(request->url, " ");
	request->method = token;
	token = strtok(NULL, " ");
	request->path = token;
	token = strtok(NULL, " ");
	request->version = token;
}

void response_print(const struct Response *response)
{
	printf(GREEN "\nResponse\n" RESET);

	printf("Status: %s", response->status ? response->status : "(not specified)\n");
	printf("Content-Type:  %s", response->content_type ? response->content_type : "(not specified)\n");
	// printf("Content-Length: %s", response->content_length ? response->content_length : "(not specified)\n");
	printf("Content-Length: %s", response->content_length);
	printf("Body:  %s", response->body ? response->body : "(not specified)\n");
}

void response_build(char *buffer, struct Request *request)
{
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

	return server_fd;
}

void server_process_client(void *arg)
{
	int client_fd = (uintptr_t)arg;
	char response_buffer[RESPONSE_BUFFER_SIZE];
	char request_buffer[REQEUST_BUFFER_SIZE];

	if (recv(client_fd, request_buffer, sizeof(request_buffer), 0) != -1)
	{
		printf(GREEN "Message received.\n" RESET);
	}
	printf(YELLOW "Response:\n%s\n" RESET, request_buffer);

	struct Request request;
	request_parse(request_buffer, &request);
	response_build(response_buffer, &request);

	printf(YELLOW "Response:\n%s\n" RESET, response_buffer);
	ssize_t error = send(client_fd, response_buffer, strlen(response_buffer), 0); // removing the send function off the stack increased the amount of successful requests
	if (error == -1)
	{
		printf(RED "Send failed: %s...\n" RESET, strerror(errno));
	}
	printf(GREEN "Message sent.\n" RESET);

	close(client_fd);
}

int main(int argc, char *argv[]) // verify the stack is overflowing because the job queue overflows the stack
{
	if (strcmp(argv[1], FLAG_DIRECTORY) == 0)
	{
		option_directory = argv[2];
	}

	setbuf(stdout, NULL);

	threadpool thread_pool = thread_pool_init(MAX_THREADS);
	printf(GREEN "Thread pool created: 4 threads\n" RESET);

	int server_fd = server_listen();
	printf(CYAN "Server listening...\n" RESET);

	for (;;) // todo print ip and port
	{
		socklen_t client_addr_len;
		struct sockaddr_in client_addr;

		client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd != -1)
		{
			printf(GREEN "Connection Accepted.\n" RESET);
		}

		thread_pool_add_work(thread_pool, server_process_client, (void *)(uintptr_t)client_fd);
	}

	printf(YELLOW "Waiting for thread pool work to complete..." RESET);
	thread_pool_wait(thread_pool);
	printf(RED "Killing threadpool..." RESET);
	thread_pool_destroy(thread_pool);
	printf(RED "Closing server socket..." RESET);
	close(server_fd);

	return C_OK;
}
