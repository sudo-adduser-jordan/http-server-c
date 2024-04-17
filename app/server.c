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
#define RESPONSE_BUFFER_SIZE 4096
#define FILE_BUFFER_SIZE 4096

#define STATUS_OK "HTTP/1.1 200 OK\r\n"
#define STATUS_NOT_FOUND "HTTP/1.1 404 NOT FOUND\r\n"

#define CONTENT_LENGTH "Content-Length: "
#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_FILE "Content-Type: application/octet-stream\r\n"

#define CLRF "\r\n"

struct Request
{
	char *method;
	char *path;
	char *version;
	char *host;
	char *user_agent;
	char *accept;
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
void request_scan(char *buffer, struct Request *request);

void response_print(const struct Response *response);
void response_scan(struct Response *response, struct Request *request);
void response_send(char *buffer, int *client_fd, struct Response *response);

void request_print(const struct Request *request)
{
	printf(GREEN "\nRequest\n" RESET);
	printf(YELLOW "Method: " RESET "%s\n", request->method ? request->method : "(not specified)");
	printf(YELLOW "Path: " RESET "%s\n", request->path ? request->path : "(not specified)");
	printf(YELLOW "Version: " RESET "%s\n", request->version ? request->version : "(not specified)");
	printf(YELLOW "Host: " RESET "%s\n", request->host ? request->host : "(not specified)");
	printf(YELLOW "User-Agent: " RESET "%s\n", request->user_agent ? request->user_agent : "(not specified)");
	printf(YELLOW "Accept: " RESET "%s\n", request->accept ? request->accept : "(not specified)");
	printf(YELLOW "Body: " RESET "%s\n", request->body ? request->body : "(not specified)");
}

void request_scan(char *buffer, struct Request *request)
{

	printf(print_raw_string(buffer));
	printf("\n");

	char *token = strtok(buffer, "\r\n");

	char *method = token;
	printf("	%s\n", method);
	printf("hit\n");

	while (token != NULL)
	{
		for (int i = 0; token[i]; i++) // flatten
		{
			token[i] = tolower(token[i]);
		}

		if (strstr(token, "accept:") != NULL)
		{
			printf("hit\n");
		}
		else if (strstr(token, "accept-encoding:") != NULL)
		{
			printf("hit\n");
		}
		else if (strstr(token, "user-agent:") != NULL)
		{
			printf("hit\n");
		}
		else if (strstr(token, "host:") != NULL)
		{
			printf("hit\n");
		}
		else if (strstr(token, "content-length:") != NULL)
		{
			printf("hit\n");
		}

		token = strtok(NULL, "\r\n");
		if (token != NULL)
		{
			printf("	%s\n", token);
		}
	}
}

void response_print(const struct Response *response)
{
	printf(GREEN "\nResponse\n" RESET);
	printf(YELLOW "Status:" RESET "%s", response->status ? response->status : "(not specified)\n");
	printf(YELLOW "Content-Type:" RESET " %s", response->content_type ? response->content_type : "(not specified)\n");
	printf(YELLOW "%s" RESET, response->content_length ? response->content_length : "Conetent-Length: (not specified)\n");
	printf(YELLOW "Body: " RESET "%s", response->body ? response->body : "(not specified)\n");
}

void response_scan(struct Response *response, struct Request *request)
{

	if (strstr(request->path, "/files") != NULL)
	{
	}
	else if (strstr(request->path, "/user-agent") != NULL)
	{
		response->status = STATUS_OK;
		response->content_type = CONTENT_TYPE_TEXT;
		response->body = request->user_agent;

		// char content_length[100];
		// sprintf(content_length,
		// 		CONTENT_LENGTH "%zd" CLRF CLRF,
		// 		strlen(response->body));

		// response->content_length = content_length;
	}
	else if (strstr(request->path, "/echo/") != NULL)
	{
		response->status = STATUS_OK;
		response->content_type = CONTENT_TYPE_TEXT;

		strremove(request->path, "/echo/");
		response->body = request->path;

		// char content_length[100];
		// sprintf(content_length,
		// 		CONTENT_LENGTH "%zd" CLRF CLRF,
		// 		strlen(response->body));

		// response->content_length = content_length;
	}
	else if (strcmp(request->path, "/") == 0)
	{
		response->status = STATUS_OK CLRF;
		response->content_type = NULL;
		response->content_length = NULL;
		response->body = NULL;
	}
	else
	{
		response->status = STATUS_NOT_FOUND;
		response->content_type = NULL;
		response->content_length = NULL;
		response->body = NULL;
	}
}

void response_send(char *buffer, int *client_fd, struct Response *response)
{
	sprintf(buffer, "%s%s%s%s",
			response->status,
			response->content_type,
			response->content_length,
			response->body);

	ssize_t error = send((int)*client_fd, buffer, strlen(buffer), 0);
	if (error == -1)
	{
		printf(RED "Send failed: %s...\n" RESET, strerror(errno));
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
	// char response_buffer[RESPONSE_BUFFER_SIZE];
	char request_buffer[REQEUST_BUFFER_SIZE];

	if (recv(client_fd, request_buffer, sizeof(request_buffer), 0) != -1)
	{
		printf(GREEN "Message received.\n" RESET);
	}
	// printf(print_raw_string(request_buffer));
	// printf("\n\n");

	struct Request request;
	request_scan(request_buffer, &request);
	// request_print(&request);

	// struct Response response;
	// response_scan(&response, &request);
	// response_print(&response);
	// response_send(response_buffer, &client_fd, &response);

	close(client_fd);
}

int main(int argc, char *argv[])
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

	for (;;)
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
