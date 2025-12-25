

#include "universal_async.hpp"

#include <stdio.h>

#define PORT 50001
#define CON_BUFFSIZE 1024

struct auto_sockethandle
{
	SOCKET _s;
	auto_sockethandle(SOCKET s) : _s(s){}
	~auto_sockethandle(){ closesocket(_s);}
};

ucoro::awaitable<void> echo_sever_client_session(SOCKET client_socket)
{
	char buf1[CON_BUFFSIZE];

	auto_sockethandle auto_close(client_socket);

	WSABUF recv_wsa = { .len = sizeof(buf1), .buf = buf1 };

	DWORD flags = 0, bytes_read = 0;

	awaitable_overlapped ov;

	int result;

	result = WSARecv(client_socket, &recv_wsa, 1, &bytes_read, &flags, &ov, NULL);
	ov.last_error = WSAGetLastError();
	if (!(result !=0 && ov.last_error != WSA_IO_PENDING))
		bytes_read = co_await get_overlapped_result(ov);

	if (ov.last_error || bytes_read == 0)
	{
		co_return ;
	}

   char buf2[1024];
   int content_length_line_len = snprintf(buf2, 80, "Content-Length: %d\r\n\r\n", ov.byte_transfered);

   WSABUF wsa_buf[3] = {
      { .len = 17, .buf = (char*) "HTTP/1.1 200 OK\r\n" },
      { .len = (ULONG) content_length_line_len, .buf = buf2 },
      { ov.byte_transfered, buf1 }
   };

	DWORD bytes_written;
	result = WSASend(client_socket, wsa_buf, 3, &bytes_written, 0, &ov, NULL);
	ov.last_error = WSAGetLastError();
	if (!(result !=0 && ov.last_error != WSA_IO_PENDING))
		bytes_written = co_await get_overlapped_result(ov);

	auto disconnect_result = DisconnectEx(client_socket, &ov, 0, 0);
	ov.last_error = ::WSAGetLastError();
	if (!(disconnect_result && ov.last_error != WSA_IO_PENDING))
		co_await get_overlapped_result(ov);
}

ucoro::awaitable<void> accept_coro(SOCKET slisten, HANDLE iocp, int af_family)
{
	char addr_buf[1024];

	for (;;)
	{
		SOCKET client_socket = WSASocketW(af_family, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
		awaitable_overlapped ov;
		DWORD ignore = 0;

		auto result = AcceptEx(slisten, client_socket, addr_buf, 0, sizeof (sockaddr_in6)+16, sizeof (sockaddr_in6)+16, &ignore, &ov);
		ov.last_error = WSAGetLastError();

		if (!(result && ov.last_error != WSA_IO_PENDING))
		{
			co_await get_overlapped_result(ov);
			if (ov.last_error)
			{
				closesocket(client_socket);
				continue;
			}

			bind_stackless_iocp((HANDLE)(client_socket), iocp);
			// 开新协程处理连接.
			echo_sever_client_session(client_socket).detach();
		}
		else
		{
			closesocket(client_socket);
		}
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

	HANDLE iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// Create listening socket and
	// put it in overlapped mode - WSA_FLAG_OVERLAPPED
	listener6 = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listener6 == INVALID_SOCKET)
	{
		printf("Socket creation failed: %d\n", WSAGetLastError());
	}

	bind_stackless_iocp((HANDLE)(listener6), iocp_handle);

#ifdef _WIN32
	init_winsock_api_pointer();
#endif
	{
		// Addr of listening socket
		sockaddr_in6 addr = {};
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(PORT);
		DWORD v = 0;
		#ifdef IPV6_V6ONLY
		setsockopt(listener6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v, sizeof (v));
		#endif
		v = 1;
		setsockopt(listener6, SOL_SOCKET, SO_REUSEADDR, (char*) &v, sizeof (v));
		// Bind listener to address and port
		if (bind(listener6, (sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR)
		{
			puts("Socket binding failed");
		}
	}

	// Start listening
	if (listen(listener6, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("bind fained\n");
		return 1;
	}

	printf("Listening on %d\n", PORT);

	// 开 4个 acceptor
	for (int i = 0; i < 80; i++)
	{
		accept_coro(listener6, iocp_handle, AF_INET6).detach();
	}

	// 进入 event loop
	run_event_loop(iocp_handle);
	// closesocket(listener);
	closesocket(listener6);
	CloseHandle(iocp_handle);
	return 0;
}
