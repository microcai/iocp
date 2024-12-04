


#include "universal_async.hpp"
#include "iocp.h"

#include <stdio.h>

#define PORT 50001
#define CON_BUFFSIZE 1024

ucoro::awaitable<void> echo_sever_client_session(SOCKET client_socket)
{
	char buf[CON_BUFFSIZE];

	WSABUF wsa_buf;
	wsa_buf.len = sizeof(buf);
	wsa_buf.buf = buf;

	unsigned int flags = 0, bytes_read;

	awaitable_overlapped ov;

	WSARecv(client_socket, &wsa_buf, 1, &bytes_read, &flags, &ov, NULL);
	bytes_read = co_await wait_overlapped(ov);

	wsa_buf.len = bytes_read;
	WSASend(client_socket, &wsa_buf, 1, NULL, 0, &ov, NULL);
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

		AcceptEx(slisten, client_socket, addr_buff, sizeof addr_buff, sizeof (sockaddr_in6), sizeof (sockaddr_in6), NULL, &ov);
		// wait for overlapped.
		co_await wait_overlapped(ov);

		printf("New con: %p\n", client_socket);

		LPSOCKADDR local_addr = 0;
		int local_addr_length = 0;
		LPSOCKADDR remote_addr = 0;
		int remote_addr_length = 0;
		DWORD address_length = sizeof(SOCKADDR);

		GetAcceptExSockaddrs(addr_buff, 0, address_length, address_length, &local_addr,
								&local_addr_length, &remote_addr, &remote_addr_length);

		HANDLE read_port = CreateIoCompletionPort(client_socket, iocp, 0, 0);

		echo_sever_client_session(client_socket).detach();
	}
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
	listen(listener, 1024);

	// Completion port for newly accepted sockets
	// Link it to the main completion port
	HANDLE sock_port = CreateIoCompletionPort(listener, comp_port, 0, 0);

	printf("Listening on %d\n", PORT);

	// 开 4个 acceptor
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();


	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();
	accept_coro(listener, comp_port).detach();

	// 进入 event loop
	run_event_loop(comp_port);
	closesocket(listener);
	CloseHandle(comp_port);
	return 0;
}
