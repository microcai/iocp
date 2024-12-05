

// #define __SINGAL_THREADED 1

#include "universal_async.hpp"

#include <stdio.h>

#define PORT 50001
#define CON_BUFFSIZE 1024

ucoro::awaitable<void> echo_sever_client_session(SOCKET client_socket)
{
	char buf[CON_BUFFSIZE];

	WSABUF wsa_buf[2];
	wsa_buf[1].len = sizeof(buf);
	wsa_buf[1].buf = buf;

	DWORD flags = 0, bytes_read;

	awaitable_overlapped ov;

	WSARecv(client_socket, &wsa_buf[1], 1, &bytes_read, &flags, &ov, NULL);
	bytes_read = co_await wait_overlapped(ov);

	wsa_buf[1].len = bytes_read;
	wsa_buf[0].buf = (char*)"HTTP/1.1 200 OK\r\n\r\n";
	wsa_buf[0].len = 19;
	WSASend(client_socket, wsa_buf, 2, NULL, 0, &ov, NULL);
	co_await wait_overlapped(ov);

	closesocket(client_socket);
}

ucoro::awaitable<void> accept_coro(SOCKET slisten, HANDLE iocp)
{
	char addr_buff[1024];

	for (;;)
	{
		SOCKET client_socket = WSASocket(AF_INET6, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
		awaitable_overlapped ov;
		DWORD ignore = 0;

		AcceptEx(slisten, client_socket, addr_buff, 0, sizeof (sockaddr_in6)+16, sizeof (sockaddr_in6)+16, &ignore, &ov);

		co_await wait_overlapped(ov);

		// printf("New con: %p\n", client_socket);

		LPSOCKADDR local_addr = 0;
		int local_addr_length = 0;
		LPSOCKADDR remote_addr = 0;
		int remote_addr_length = 0;
		DWORD address_length = sizeof(sockaddr_in6) + 16 ;

		GetAcceptExSockaddrs(addr_buff, 0, address_length, address_length, &local_addr,
								&local_addr_length, &remote_addr, &remote_addr_length);

		HANDLE read_port = CreateIoCompletionPort((HANDLE)(client_socket), iocp, 0, 0);

		echo_sever_client_session(client_socket).detach();
	}
}

int main()
{
	// Listening socket
	SOCKET listener6, listener;

	// meeeeehhhh
	WSADATA wsaData;

	// Windows socket startup
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		fprintf(stderr, "WSAStartup failed.\n");
		exit(1);
	}

	// Create listening socket and
	// put it in overlapped mode - WSA_FLAG_OVERLAPPED
	listener6 = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listener6 == INVALID_SOCKET)
	{
		printf("Socket creation failed: %d\n", WSAGetLastError());
	}
	listener = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listener == INVALID_SOCKET)
	{
		printf("Socket creation failed: %d\n", WSAGetLastError());
	}

	{
		// Addr of listening socket
		SOCKADDR_IN6 addr = {0};
		// Setup address and port of
		// listening socket
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(PORT);
		int v = 1;
		setsockopt(listener6, SOL_SOCKET, SO_REUSEADDR, (char*) &v, sizeof (v));
		// Bind listener to address and port
		if (bind(listener6, (sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR)
		{
			puts("Socket binding failed");
		}
	}
	{
		// Addr of listening socket
		SOCKADDR_IN addr = {0};
		// Setup address and port of
		// listening socket
		addr.sin_family = AF_INET;
		addr.sin_port = htons(PORT);
		int v = 1;
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*) &v, sizeof (v));
		// Bind listener to address and port
		if (bind(listener, (sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR)
		{
			puts("Socket binding failed");
		}
	}

	// Start listening
	listen(listener6, 1024);
	listen(listener, 1024);

	// Completion port for newly accepted sockets
	// Link it to the main completion port
	HANDLE iocp_handle = CreateIoCompletionPort((HANDLE)(listener6), NULL, 0, 1);
	CreateIoCompletionPort((HANDLE)(listener), iocp_handle, 0, 1);

	printf("Listening on %d\n", PORT);

	// 开 4个 acceptor
	for (int i = 0; i < 32; i++)
		accept_coro(listener6, iocp_handle).detach();
	for (int i = 0; i < 32; i++)
		accept_coro(listener, iocp_handle).detach();

	// 进入 event loop
	run_event_loop(iocp_handle);
	closesocket(listener);
	closesocket(listener6);
	CloseHandle(iocp_handle);
	return 0;
}
