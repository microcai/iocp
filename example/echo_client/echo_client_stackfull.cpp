

#include "universal_fiber.hpp"

#ifdef _WIN32
LPFN_DISCONNECTEX DisconnectEx = nullptr;
LPFN_CONNECTEX WSAConnectEx = nullptr;
#endif

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
	SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	auto_sockethandle auto_close(sock);

	CreateIoCompletionPort((HANDLE)(sock), iocp_handle, 0, 0);

	SOCKADDR_IN server_addr = {AF_INET, htons(50001), inet_addr(lp_server_addr) };

	FiberOVERLAPPED ov;

	auto result = WSAConnectEx(sock, (const SOCKADDR*) &server_addr, INET_ADDRSTRLEN, 0, 0, 0, &ov.ov);
	ov.last_error = WSAGetLastError();

	if (!(!result && ov.last_error != WSA_IO_PENDING))
	{
		get_overlapped_result(&ov);
	}

	if (ov.last_error)
	{
		printf("connection failed\n");
		exit(1);
	}

	// 发送 hello
	WSABUF buf = { .len = 6, .buf = (char*) "Hello!" };

	DWORD sent = 0;

	result = WSASend(sock, &buf, 1, &sent, 0, &ov.ov, 0);
	ov.last_error = WSAGetLastError();
	if (!(!result && ov.last_error != WSA_IO_PENDING))
	{
		sent = get_overlapped_result(&ov);
	}

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
	{
		auto_sockethandle auto_close(sock);

		SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		GUID disconnectex = WSAID_DISCONNECTEX;
		GUID connect_ex_guid = WSAID_CONNECTEX;
		DWORD BytesReturned;

		WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&disconnectex, sizeof(GUID), &DisconnectEx, sizeof(DisconnectEx),
			&BytesReturned, 0, 0);

		WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&connect_ex_guid, sizeof(GUID), &WSAConnectEx, sizeof(WSAConnectEx), &BytesReturned, 0, 0);
	}

#endif

	create_detached_coroutine(echo_client, iocp_handle, (const char*)(argc == 2 ? argv[1] : "127.0.0.1"));

	run_event_loop(iocp_handle);
	CloseHandle(iocp_handle);
	return 0;
}
