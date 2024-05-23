#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// #include <zlib.h> // gzip compression
#endif

#ifdef linux
#include <sys/socket.h> // internet protocol family
#include <netinet/in.h> // internet address family
#include <netinet/ip.h> // internet protocol family
#include <arpa/inet.h>	// definitions for internet operations
#include <unistd.h>		// standard symbolic constants and types
#include <pthread.h>	// threads
#include <dirent.h>		// format of directory entries
#include <zlib.h>		// gzip compression
#include "tpool.h"
#endif

#include "threadpool.h"
#define SIZE 8192
#define QUEUES 64

#include <stdio.h>	// standard buffered input/output
#include <stdlib.h> // standard library definitions
#include <string.h> // string operations
#include <errno.h>	// error return value
#include <stdint.h> // integer types
#include <ctype.h>	// character types
#include "colors.h"

// #define MAX_THREADS 4

#define C_OK 0
#define C_ERR 1
#define PORT 4221
#define FLAG_DIRECTORY "--directory"

#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 1024
#define REQEUST_BUFFER_SIZE 1024
#define RESPONSE_BUFFER_SIZE 4096

#define STATUS_OK "HTTP/1.1 200 OK\r\n"
#define STATUS_CREATED "HTTP/1.1 201 Created\r\n\r\n"
#define STATUS_NOT_FOUND "HTTP/1.1 404 Not Found\r\n\r\n"
#define STATUS_INTERNAL_SERVER_ERROR "HTTP/1.1 500 Internal Server Error\r\n\r\n"
#define STATUS_METHOD_NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\n\r\n"

#define CONTENT_LENGTH "Content-Length: "
#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_FILE "Content-Type: application/octet-stream\r\n"

#define CONTENT_ENCODING_GZIP "Content-Encoding: gzip\r\n"

#define CLRF "\r\n"

struct ThreadArgs
{
	struct sockaddr_in client_addr;
	int client_fd;
} ThreadArgs;

struct Request
{
	char *method;
	char *path;
	char *version;
	char *accept;
	char *accept_encoding;
	char *host;
	char *user_agent;
	char *content_length;
	char *body;
	size_t size;
} Request;

char *option_directory = NULL;

void strremove(char *s, const char *toremove)
{
	while ((s = strstr(s, toremove)))
		memmove(s, s + strlen(toremove), 1 + strlen(s + strlen(toremove)));
}

#ifdef linux
int compressToGzip(const char *input, int inputSize, char *output, int outputSize)
{
	z_stream zs = {0};
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	zs.avail_in = (uInt)inputSize;
	zs.next_in = (Bytef *)input;
	zs.avail_out = (uInt)outputSize;
	zs.next_out = (Bytef *)output;

	deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
	deflate(&zs, Z_FINISH);
	deflateEnd(&zs);
	return zs.total_out;
}
#endif

void request_print(const struct Request *request)
{
	printf(YELLOW "%s\n" RESET, request->method);
	printf(YELLOW "%s\n" RESET, request->path);
	printf(YELLOW "%s\n" RESET, request->version);
	printf(YELLOW "%s\n" RESET, request->accept);
	printf(YELLOW "%s\n" RESET, request->accept_encoding);
	printf(YELLOW "%s\n" RESET, request->host);
	printf(YELLOW "%s\n" RESET, request->user_agent);
	printf(YELLOW "%s\n" RESET, request->content_length);
	printf(YELLOW "%s\n" RESET, request->body);
}

void request_parse(char *buffer, struct Request *request)
{ // TODO: optimize

	char *token = strtok(buffer, " ");
	request->method = token;
	token = strtok(NULL, " ");
	request->path = token;
	token = strtok(NULL, " ");
	request->version = token;

	token = strtok(NULL, "\r\n");
	while (token != NULL)
	{

		if (!request->accept)
		{
			if (strstr(token, "Accept:") != NULL || strstr(token, "accept:") != NULL)
			{
				request->accept = token;
			}
		}

		if (!request->accept_encoding)
		{
			if (strstr(token, "Accept-Encoding:") != NULL || strstr(token, "accept-encoding:") != NULL)
			{
				request->accept_encoding = token;
			}
		}

		if (!request->user_agent)
		{

			if (strstr(token, "User-Agent:") != NULL || strstr(token, "user-agent:") != NULL)
			{
				request->user_agent = token;
			}
		}

		if (!request->host)
		{
			if (strstr(token, "Host:") != NULL || strstr(token, "host:") != NULL)
			{
				request->host = token;
			}
		}

		if (strstr(token, "Content-Length:") != NULL || strstr(token, "content-length:") != NULL)
		{
			request->content_length = token;
		}
		else
		{
			request->body = token;
		}

		token = strtok(NULL, "\r\n");
	}
}

void response_build(char *buffer, struct Request *request)
{ // TODO: optimize

	// printf(CYAN "Request Buffer:\n" YELLOW "%s\n" RESET, buffer); // debug null

	if (!request->path)
	{
		sprintf(buffer,
				"%s",
				STATUS_INTERNAL_SERVER_ERROR);
	}
	else if (strcmp(request->path, "/") == 0)
	{
		sprintf(buffer,
				"%s%s",
				STATUS_OK,
				CLRF);
	}
	else if (strstr(request->path, "/user-agent") != NULL)
	{
		strremove(request->user_agent, "user-agent: ");
		strremove(request->user_agent, "User-Agent: ");
		sprintf(buffer,
				"%s%s%s%zd\r\n\r\n%s\r\n",
				STATUS_OK,
				CONTENT_TYPE_TEXT,
				CONTENT_LENGTH,
				strlen(request->user_agent),
				request->user_agent);
	}
	else if (strstr(request->path, "/echo/") != NULL)
	{
		if (request->accept_encoding && strstr(request->accept_encoding, "gzip") != NULL)
		{
			// strremove(request->path, "/echo/");

			// char body[BUFFER_SIZE];
			// int len = compressToGzip(request->path, strlen(request->path), body, 1024);
			// if (len < 0)
			// {
			// 	printf(RED "Compression failed: %s...\n" RESET, strerror(errno));
			// }

			// sprintf(buffer,
			// 		"%s%s%s%s%d\r\n\r\n",
			// 		STATUS_OK,
			// 		CONTENT_TYPE_TEXT,
			// 		CONTENT_ENCODING_GZIP,
			// 		CONTENT_LENGTH,
			// 		len);

			// memcpy(buffer + strlen(buffer), body, len);
			// request->size = len;
			// }
			// else
			// {
			strremove(request->path, "/echo/");
			sprintf(buffer,
					"%s%s%s%zd\r\n\r\n%s\r\n",
					STATUS_OK,
					CONTENT_TYPE_TEXT,
					CONTENT_LENGTH,
					strlen(request->path),
					request->path);
			// }
		}
		else if (strstr(request->path, "/files/") != NULL)
		{

			if (strstr(request->method, "GET") != NULL) // GET
			{
				char filepath[1024] = {0};
				request->path++; // move pointer forward one
				strcat(filepath, option_directory);
				strcat(filepath, request->path);
				strremove(filepath, "files/");

				FILE *file_ptr = fopen(filepath, "r");
				if (file_ptr != NULL)
				{
					fseek(file_ptr, 0, SEEK_END);
					int size = ftell(file_ptr);
					char data[1000] = {0};
					fseek(file_ptr, 0, SEEK_SET);
					fread(data, sizeof(char), size, file_ptr);
					fclose(file_ptr);

					sprintf(buffer,
							"%s%s%s%d\r\n\r\n%s\r\n",
							STATUS_OK,
							CONTENT_TYPE_FILE,
							CONTENT_LENGTH,
							size,
							data);
				}
				else
				{
					sprintf(buffer,
							"%s",
							STATUS_NOT_FOUND);
				}
			}
			else if (strstr(request->method, "POST") != NULL) // POST
			{
				char filepath[1024] = {0};
				request->path++; // move pointer forward one
				strcat(filepath, option_directory);
				strcat(filepath, request->path);
				strremove(filepath, "files/");

				FILE *file_prt;
				file_prt = fopen(filepath, "w");
				fprintf(file_prt, request->body);
				fclose(file_prt);

				sprintf(buffer,
						"%s",
						STATUS_CREATED);
			}
			else
			{
				sprintf(buffer,
						"%s",
						STATUS_METHOD_NOT_ALLOWED);
			}
		}
		else
		{
			sprintf(buffer,
					"%s",
					STATUS_NOT_FOUND);
		}
	}
}

#ifdef linux
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
#elif _WIN32
int server_listen()
{

	WSADATA wsaData;
	int iResult;

	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET ClientSocket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo hints;

	int iSendResult;
	char recvbuf[BUFFER_SIZE];
	int recvbuflen = BUFFER_SIZE;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(NULL, 4221, &hints, &result);
	if (iResult != 0)
	{
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	// Create a SOCKET for the server to listen for client connections.
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET)
	{
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	// Setup the TCP listening socket
	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	// Accept a client socket
	ClientSocket = accept(ListenSocket, NULL, NULL);
	if (ClientSocket == INVALID_SOCKET)
	{
		printf("accept failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}

	// No longer need server socket
	closesocket(ListenSocket);

	// Receive until the peer shuts down the connection
	do
	{

		iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
		if (iResult > 0)
		{
			printf("Bytes received: %d\n", iResult);

			// Echo the buffer back to the sender
			iSendResult = send(ClientSocket, recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR)
			{
				printf("send failed with error: %d\n", WSAGetLastError());
				closesocket(ClientSocket);
				WSACleanup();
				return 1;
			}
			printf("Bytes sent: %d\n", iSendResult);
		}
		else if (iResult == 0)
			printf("Connection closing...\n");
		else
		{
			printf("recv failed with error: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			WSACleanup();
			return 1;
		}

	} while (iResult > 0);

	// shutdown the connection since we're done
	iResult = shutdown(ClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR)
	{
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(ClientSocket);
		WSACleanup();
		return 1;
	}

	// cleanup
	closesocket(ClientSocket);
	WSACleanup();
}
#endif

void server_process_client(void *arg)
{
	struct ThreadArgs *thread_args = (struct ThreadArgs *)(arg);
	char response_buffer[RESPONSE_BUFFER_SIZE];
	char request_buffer[REQEUST_BUFFER_SIZE];

	if (recv(thread_args->client_fd, request_buffer, sizeof(request_buffer), 0) == -1)
	{
		printf(RED "Recieved failed: %s...\n" RESET, strerror(errno));
	}
	// printf(CYAN "Request Buffer:\n" YELLOW "%s\n" RESET, request_buffer);

	struct Request request;
	request_parse(request_buffer, &request);
	response_build(response_buffer, &request);

	// request_print(&request);
	// printf(CYAN "Response Buffer:\n" YELLOW "%s\n" RESET, response_buffer);

	if (send(thread_args->client_fd, response_buffer, strlen(response_buffer) + request.size, 0) == -1)
	{
		printf(RED "Send failed: %s...\n" RESET, strerror(errno));
	}
	// printf(GREEN "Message sent: %s:%d <----------\n" RESET, inet_ntoa(thread_args->client_addr.sin_addr), ntohs(thread_args->client_addr.sin_port));
	// close(thread_args->client_fd);
}

int main(int argc, char *argv[])
{

	if (argv[1] != NULL)
	{
		if (strcmp(argv[1], FLAG_DIRECTORY) == 0)
		{
			option_directory = argv[2];
			printf(YELLOW "Directory path set: " RESET "%s\n", option_directory);
		}
	}

	setbuf(stdout, NULL);

	threadpool_t *thread_pool = threadpool_create(MAX_THREADS, SIZE, 0);
	printf(GREEN "Thread pool created: %d threads\n" RESET, MAX_THREADS);

	// int server_fd = server_listen();
	// for (;;)
	// {
	// 	void *thread_args_ptr = malloc(sizeof(struct ThreadArgs));
	// 	struct ThreadArgs *thread_args = (struct ThreadArgs *)(thread_args_ptr);

	// 	socklen_t client_addr_len = sizeof(thread_args->client_addr);
	// 	thread_args->client_fd = accept(server_fd, (struct sockaddr *)&thread_args->client_addr, &client_addr_len);
	// 	if (thread_args->client_fd == -1)
	// 	{
	// 		printf(RED "Client connection failed: %s \n" RESET, strerror(errno));
	// 	}
	// 	// printf(CYAN "Client connected: %s:%d <----------\n" RESET, inet_ntoa(thread_args->client_addr.sin_addr), ntohs(thread_args->client_addr.sin_port));

	// 	if (threadpool_add(thread_pool, server_process_client, (void *)thread_args, 0) != 0)
	// 	{
	// 		printf(RED "Failed to create thread: %s\n" RESET, strerror(errno));
	// 		free(thread_args);
	// 		free(thread_args_ptr);
	// 	}
	// }

	printf(YELLOW "Killing threadpool...\n" RESET);
	threadpool_destroy(thread_pool, 0);
	printf(RED "Closing server socket...\n" RESET);
	// close(server_fd);

	return C_OK;
}
