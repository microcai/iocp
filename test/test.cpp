
#include <boost/thread.hpp>
#include <errno.h>
#include <iostream>
#include <sys/socket.h>

#include "iocp.h"

#include <stdio.h>

#define PORT 50001
#define CON_BUFFSIZE 1024
#define DATA_BUFSIZE 128000

// Events on socket
#define SERVER_ACCEPT 100
#define SERVER_CLOSE 200
#define SERVER_READ 300
#define SERVER_WRITE 400

typedef struct _PER_IO_DATA
{
	SOCKET socket;
	OVERLAPPED overlapped;
	char data_buff[DATA_BUFSIZE];
	unsigned int buff_len;
	int type;
	char* sdata;
	unsigned int bytes_read;
} PER_IO_DATA, *LPPER_IO_DATA;

typedef struct _SOCK_DATA
{
	SOCKET socket;
	OVERLAPPED overlapped;
	char addr_buff[CON_BUFFSIZE];
	int type;
} SOCK_DATA, *LPSOCK_DATA;

int accept_con(SOCKET listener)
{
	SOCKET accept = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCK_DATA* sock_data = (SOCK_DATA*)malloc(sizeof(SOCK_DATA));
	sock_data->socket = accept;
	unsigned int bytes_read;
	ZeroMemory(&(sock_data->overlapped), sizeof(OVERLAPPED));
	sock_data->type = SERVER_ACCEPT;

	if (!AcceptEx(listener, accept, &(sock_data->addr_buff), 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				  &bytes_read, &(sock_data->overlapped)))
	{
		int a = WSAGetLastError();
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			closesocket(accept);
			printf("Accept filed with code %d\n", WSAGetLastError());
			free(sock_data);
			return 0;
		}
	}
	return 1;
}

int rec_data(SOCK_DATA* sock_data)
{
	PER_IO_DATA* per_io_data = (PER_IO_DATA*)malloc(sizeof(PER_IO_DATA));
	per_io_data->socket = sock_data->socket;
	ZeroMemory(&(per_io_data->overlapped), sizeof(OVERLAPPED));
	per_io_data->type = SERVER_READ;
	per_io_data->bytes_read = 0;

	WSABUF wsa_buf;
	wsa_buf.len = sizeof(per_io_data->data_buff);
	wsa_buf.buf = per_io_data->data_buff;

	unsigned int flags = 0, bytes_read;

	if (SOCKET_ERROR == WSARecv(per_io_data->socket, &wsa_buf, 1, &bytes_read, &flags, &per_io_data->overlapped, NULL))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf("Recv failed with: %d\n", WSAGetLastError());
			free(sock_data);
			free(per_io_data);
			return 0;
		}
	}
	return 1;
}

int send_data(SOCK_DATA* sock_data, PER_IO_DATA* per_io_data)
{

	ZeroMemory(&(per_io_data->overlapped), sizeof(OVERLAPPED));
	per_io_data->type = SERVER_WRITE;

	WSABUF wsa_buf;
	wsa_buf.len = per_io_data->bytes_read;
	wsa_buf.buf = per_io_data->sdata;

	unsigned int flags = 0;
	if (SOCKET_ERROR == WSASend(per_io_data->socket, &wsa_buf, 1, NULL, flags, &per_io_data->overlapped, NULL))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf("Send failed with: %d\n", WSAGetLastError());
			if (per_io_data->sdata)
			{
				free(per_io_data->sdata);
			}
			free(per_io_data);
			closesocket(sock_data->socket);
			free(sock_data);
			return 0;
		}
	}
	return 1;
}

int main()
{
	// Listening socket
	SOCKET listener;

	// Addr of listening socket
	SOCKADDR_IN addr;

	// meeeeehhhh
	WSADATA wsaData;

	// Windows socket startup
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		fprintf(stderr, "WSAStartup failed.\n");
		exit(1);
	}

	// Create a completion port
	HANDLE comp_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// Create listening socket and
	// put it in overlapped mode - WSA_FLAG_OVERLAPPED
	listener = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listener == INVALID_SOCKET)
	{
		printf("Socket creation failed: %d\n", WSAGetLastError());
	}

	// Setup address and port of
	// listening socket
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PORT);

	// Bind listener to address and port
	if (bind(listener, &addr, sizeof(addr)) == SOCKET_ERROR)
	{
		puts("Socket binding failed");
	}

	// Start listening
	listen(listener, 5);

	// Completion port for newly accepted sockets
	// Link it to the main completion port
	HANDLE sock_port = CreateIoCompletionPort(listener, comp_port, 0, 0);

	printf("Listening on %d\n", PORT);

	accept_con(listener);
	while (1)
	{
		unsigned int bytes_read;
		unsigned long comp_key;
		OVERLAPPED* ovl = NULL;
		SOCK_DATA* sock_data;
		PER_IO_DATA* per_io_data = NULL;
		int type;
		BOOL res;

		res = GetQueuedCompletionStatus(comp_port, &bytes_read, &comp_key, &ovl, INFINITE);
		if (!res)
		{
			continue;
		}

		if (comp_key == 0)
		{
			sock_data = (SOCK_DATA*)CONTAINING_RECORD(ovl, SOCK_DATA, overlapped);
			type = sock_data->type;
		}
		else
		{
			sock_data = (SOCK_DATA*)comp_key;
			per_io_data = (PER_IO_DATA*)CONTAINING_RECORD(ovl, PER_IO_DATA, overlapped);
			type = per_io_data->type;
		}

		if (bytes_read == 0 && (type == SERVER_READ || type == SERVER_WRITE))
		{
			puts("Accepted and closed");
			closesocket(sock_data->socket);
			free(sock_data);
			free(per_io_data);
			continue;
		}

		if (type == SERVER_ACCEPT)
		{
			printf("New con: %p\n", sock_data->socket);

			LPSOCKADDR local_addr = 0;
			int local_addr_length = 0;
			LPSOCKADDR remote_addr = 0;
			int remote_addr_length = 0;
			DWORD address_length = sizeof(SOCKADDR);

			GetAcceptExSockaddrs(sock_data->addr_buff, 0, address_length, address_length, &local_addr,
								 &local_addr_length, &remote_addr, &remote_addr_length);

			accept_con(listener);
			HANDLE read_port = CreateIoCompletionPort(sock_data->socket, comp_port, (ULONG_PTR)sock_data, 0);
			rec_data(sock_data);
			continue;
		}

		if (type == SERVER_READ)
		{
			printf("Received data to %d\n", sock_data->socket);

			per_io_data->bytes_read = bytes_read;
			char* sdata = (char*)malloc(sizeof(char) * bytes_read + 1);
			memcpy_s((void*)sdata, (sizeof(char) * bytes_read + 1), per_io_data->data_buff, bytes_read);

			sdata[bytes_read] = '\0';

			per_io_data->sdata = sdata;

			send_data(sock_data, per_io_data);
			continue;
		}

		if (type == SERVER_WRITE)
		{
			if (per_io_data->sdata)
			{
				free(per_io_data->sdata);
			}
			free(per_io_data);
			closesocket(sock_data->socket);
			free(sock_data);
			continue;
		}
	}
	CloseHandle(comp_port);
}
