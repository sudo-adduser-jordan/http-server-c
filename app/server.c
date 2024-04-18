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
	char *url;
	char *method;
	char *path;
	char *version;
	char *accept;
	char *accept_encoding;
	char *host;
	char *user_agent;
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
void request_free(struct Request *request);
void request_scan(char *buffer, struct Request *request);

void response_print(const struct Response *response);
void response_scan(struct Response *response, struct Request *request);
void response_send(char *buffer, int *client_fd, struct Response *response);

void request_free(struct Request *request)
{
	free(request->url);
	free(request->method);
	free(request->path);
	free(request->version);
	free(request->accept);
	free(request->accept_encoding);
	free(request->host);
	free(request->user_agent);
	free(request->content_length);
	free(request->body);

	free(request);
}

void request_print(const struct Request *request)
{
	printf(YELLOW "\nRequest\n" RESET);
	printf("%s\n", request->url ? request->url : "(not specified)");
	printf("%s\n", request->accept ? request->accept : "(not specified)");
	printf("%s\n", request->accept_encoding ? request->accept_encoding : "(not specified)");
	printf("%s\n", request->host ? request->host : "(not specified)");
	printf("%s\n", request->user_agent ? request->user_agent : "(not specified)");
	printf("%s\n", request->content_length ? request->content_length : "(not specified)");
	printf("%s\n", request->body ? request->body : "(not specified)");

	printf(YELLOW "\Method\n" RESET);
	printf("%s\n", request->method ? request->method : "(not specified)");
	printf("%s\n", request->path ? request->path : "(not specified)");
	printf("%s\n", request->version ? request->version : "(not specified)");
}

void request_scan(char *buffer, struct Request *request)
{

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
			request->content_length = token;
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
	printf("Conetent-Length: %s", response->content_length ? response->content_length : "(not specified)\n");
	printf("Body:  %s", response->body ? response->body : "(not specified)\n");
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

		char *token = strtok(request->user_agent, ":");
		response->body = strtok(NULL, " ");

		char content_length[100];
		sprintf(content_length,
				CONTENT_LENGTH "%zd" CLRF CLRF,
				strlen(response->body));

		response->content_length = content_length;
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
	char response_buffer[RESPONSE_BUFFER_SIZE];
	char request_buffer[REQEUST_BUFFER_SIZE];

	if (recv(client_fd, request_buffer, sizeof(request_buffer), 0) != -1)
	{
		printf(GREEN "Message received.\n" RESET);
	}
	// printf(YELLOW "Raw Request: " RESET "\n%s\n", print_raw_string(request_buffer)); // can throw mem error

	struct Request request;
	request_scan(request_buffer, &request);
	request_print(&request);

	struct Response response;
	response_scan(&response, &request);
	response_print(&response);
	response_send(response_buffer, &client_fd, &response);

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
