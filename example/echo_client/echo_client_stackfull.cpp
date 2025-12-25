

#include "universal_fiber.hpp"

#include <stdio.h>

#define PORT 50001
#define CON_BUFFSIZE 1024

struct auto_sockethandle
{
	SOCKET _s;
	auto_sockethandle(SOCKET s) : _s(s){}
	~auto_sockethandle(){ closesocket(_s);}
};


static void echo_client(HANDLE iocp_handle, const char* lp_server_addr)
{
	SOCKET sock = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	auto_sockethandle auto_close(sock);

	bind_stackfull_iocp((HANDLE)(sock), iocp_handle, 0, 0);

	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(50001);
	server_addr.sin_addr.s_addr = inet_addr(lp_server_addr);

	FiberOVERLAPPED ov;

	auto result = WSAConnectEx(sock, (const SOCKADDR*) &server_addr, INET_ADDRSTRLEN, 0, 0, 0, &ov);

	float c = ov.last_error + 1;

	printf("c = %f before\n", c);
	c*=99;

	ov.last_error = WSAGetLastError();

	if (!(result && ov.last_error != WSA_IO_PENDING))
	{
		get_overlapped_result(ov);
	}

	c /= 99;

	printf("c = %f after\n", c);

	if (ov.last_error)
	{
		printf("connection failed\n");
		exit_event_loop_when_empty(iocp_handle);
		return;
	}

	// 发送 hello
	WSABUF buf = { .len = 6, .buf = (char*) "Hello!" };

	DWORD sent = 0;

	result = WSASend(sock, &buf, 1, &sent, 0, &ov, 0);
	ov.last_error = WSAGetLastError();
	if (!(result!=0 && ov.last_error != WSA_IO_PENDING))
	{
		sent = get_overlapped_result(ov);
	}

	DWORD read_bytes = 0, ignore = 0;

	char buffer[100];

	WSABUF readbuf = { .len = sizeof(buffer), .buf = buffer };

	result = WSARecv(sock, &readbuf, 1, &read_bytes, &ignore, &ov, 0);
	ov.last_error = WSAGetLastError();
	if (!(result!=0 && ov.last_error != WSA_IO_PENDING))
	{
		read_bytes = get_overlapped_result(ov);
	}

	printf("%d bytes readed from server\n", (int) read_bytes);

	exit_event_loop_when_empty(iocp_handle);
}

int main(int argc, char* argv[])
{
	// meeeeehhhh
	WSADATA wsaData;

	// Windows socket startup
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
	{
		fprintf(stderr, "WSAStartup failed.\n");
		exit(1);
	}

	HANDLE iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

#ifdef _WIN32
	init_winsock_api_pointer();
#endif

	float cc = 8;
	cc *= argc;
	printf("cc = %f before\n", cc);
	create_detached_coroutine(echo_client, iocp_handle, (const char*)(argc == 2 ? argv[1] : "127.0.0.1"));
	cc /= argc;

	printf("cc = %f after\n", cc);

	run_event_loop(iocp_handle);
	CloseHandle(iocp_handle);
	return 0;
}
