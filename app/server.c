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

#include "utils.h"
#include "tpool.h"
#include "options.h"

#define MAX_THREADS 4

#define PORT 4221
#define C_OK 0
#define C_ERR 1
#define FUNCTION_ERR -1

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

int server_listen();
void server_process_client(void *arg);

void request_print(const struct Request *request);
void request_scan(char *buffer, struct Request *request);

void response_print(const struct Response *response);
void response_scan(struct Response *response, struct Request *request);
void response_send(char *buffer, int *client_fd, struct Response *response);

void request_print(const struct Request *request)
{
	printf("\nRequest\n");
	printf("Method: %s\n", request->method ? request->method : "(not specified)");
	printf("Path: %s\n", request->path ? request->path : "(not specified)");
	printf("Host: %s\n", request->host ? request->host : "(not specified)");
	printf("User-Agent: %s\n", request->user_agent ? request->user_agent : "(not specified)");
	printf("Accept: %s\n", request->accept ? request->accept : "(not specified)");
	printf("Body: %s\n", request->body ? request->body : "(not specified)");
}

void request_scan(char *buffer, struct Request *request)
{
	strremove(buffer, "Host:");
	strremove(buffer, "User-Agent: ");
	strremove(buffer, "Accept-Encoding: gzip");
	strremove(buffer, "Accept: ");

	char *tokens;
	tokens = strtok(buffer, " \r\n");
	request->method = tokens;
	tokens = strtok(NULL, " \r\n");
	request->path = tokens;
	tokens = strtok(NULL, " \r\n");
	request->host = tokens;
	tokens = strtok(NULL, " \r\n");
	request->user_agent = tokens;
	tokens = strtok(NULL, " \r\n");
	request->accept = tokens;
	tokens = strtok(NULL, " \r\n");
	request->body = tokens;
	tokens = strtok(NULL, " \r\n");
}

void response_print(const struct Response *response)
{
	printf("\nResponse\n");
	printf("Status: %s", response->status ? response->status : "(not specified)\n");
	printf("Content-Type: %s", response->content_type ? response->content_type : "(not specified)\n");
	printf("%s", response->content_length ? response->content_length : "Conetent-Length: (not specified)\n");
	printf("Body: %s", response->body ? response->body : "(not specified)\n");
}

void response_scan(struct Response *response, struct Request *request)
{

	if (strstr(request->path, "/files") != NULL)
	{

		// We pass in absolute path to your program using the-- directory flag

		// check if file exists
		// return200 or 400
		// strremove(request->path, "/files");
		// response->status = STATUS_OK;
		// response->content_type = CONTENT_TYPE_FILE;
		// response->content_length = NULL;
		// response->body = NULL;
	}
	else if (strstr(request->path, "/user-agent") != NULL)
	{
		response->status = STATUS_OK;
		response->content_type = CONTENT_TYPE_TEXT;
		response->body = request->user_agent;

		// char *content_length;
		// sprintf(content_length,
		// 		CONTENT_LENGTH "%zd" CLRF CLRF,
		// 		strlen(request->user_agent));

		// response->content_length = content_length;
	}
	else if (strstr(request->path, "/echo/") != NULL) // contains
	{
		response->status = STATUS_OK;
		response->content_type = CONTENT_TYPE_TEXT;

		strremove(request->path, "/echo/");
		response->body = request->path;

		// char *content_length;
		// sprintf(content_length,
		// 		CONTENT_LENGTH "%zd" CLRF CLRF,
		// 		strlen(request->body));
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
		printf("Send failed: %s...\n", strerror(errno));
	}
	printf("\nMessage sent\n");
}

int server_listen()
{
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return C_ERR;
	}
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		return C_ERR;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(PORT),
		.sin_addr = {htonl(INADDR_ANY)},
	};
	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("Bind failed: %s \n", strerror(errno));
		return C_ERR;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
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
		printf("Message received.\n");
	}
	// printf(print_raw_string(request_buffer)); // fails oha test
	printf("\n\n");

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
	setbuf(stdout, NULL);

	threadpool thread_pool = thread_pool_init(MAX_THREADS);
	printf("Thread pool created: 4 threads\n");

	int server_fd = server_listen();
	printf("Server listening...\n");

	for (;;)
	{
		socklen_t client_addr_len;
		struct sockaddr_in client_addr;

		client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == FUNCTION_ERR)
		{
			printf("Connection Accepted.\n");
		}

		thread_pool_add_work(thread_pool, server_process_client, (void *)(uintptr_t)client_fd);
	}

	printf("Waiting for thread pool work to complete...");
	thread_pool_wait(thread_pool);
	printf("Killing threadpool");
	thread_pool_destroy(thread_pool);
	close(server_fd);

	return C_OK;
}
