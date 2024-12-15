#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <stdio.h>
#include <time.h>
#pragma comment(lib, "ws2_32.lib")
#define SOCKET_get_fd(x) (x)
#else
#include "iocp.h"
#include <time.h>
#include <limits.h>
#include <stdio.h>
#include <inttypes.h>
#endif

#include <thread>

#define MSGSIZE 64

enum EOP
{
	E_ACCEPT,
	E_RECV,
	E_SEND
};

struct PerData
{
	OVERLAPPED overLap;
	WSABUF buf;
	SOCKET sock;
	SOCKADDR_IN addr;
	DWORD flags;
	int opType;
};

const WORD PORT = 8001;

DWORD WINAPI ServerWrokThread(HANDLE hComHandle)
{
	PerData* clientData = NULL;
	ULONG_PTR completkey;
	// ZeroMemory(&clientData, sizeof(clientData));

	DWORD dwRecvBytes = 0;
	while (true)
	{
		BOOL bOK = GetQueuedCompletionStatus(hComHandle, &dwRecvBytes, (PULONG_PTR)&completkey, (LPOVERLAPPED*)&clientData,
											 WSA_INFINITE);
		if (!bOK)
		{
			int nErr = WSAGetLastError();
			closesocket(clientData->sock);
			continue;
		}
		int a = 0;
		for (int i = 1; i < INT_MAX / 10; i++)
		{
			a = i;
		}
		time_t t = time(NULL);
		if (clientData->opType == E_RECV)
		{
			printf("%s flag: %d time: %d\n", clientData->buf.buf, a, t);
			WSARecv(clientData->sock, &clientData->buf, 1, (LPDWORD) &clientData->buf.len, &clientData->flags, &clientData->overLap, 0);
		}
	}
	return 0;
}

void CreateServer()
{
	WORD version = MAKEWORD(2, 2);
	WSADATA wsData;
	WSAStartup(version, &wsData);
	SOCKET listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	// SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htons(INADDR_ANY);
	addr.sin_port = htons(PORT);
	int nErr = bind(listenSock, (sockaddr*)&addr, sizeof(addr));
	if (nErr == SOCKET_ERROR)
	{
		nErr = WSAGetLastError();
		return;
	}
	nErr = listen(listenSock, 5);
	if (nErr == SOCKET_ERROR)
	{
		nErr = WSAGetLastError();
		return;
	}
	HANDLE hComHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	for (int i = 0; i < std::thread::hardware_concurrency() * 2; i++)
	{
		std::thread(ServerWrokThread, hComHandle).detach();
	}
	PerData* clientData = new PerData;
	ZeroMemory(clientData, sizeof(*clientData));
	int addrLen = sizeof(clientData->addr);
	while (true)
	{
		//
		clientData->sock = WSAAccept(listenSock, (sockaddr*)&clientData->addr, &addrLen, NULL, 0);
		// clientData->sock = accept(SOCKET_get_fd(listenSock), (sockaddr*)&clientData->addr, &addrLen);
		CreateIoCompletionPort((HANDLE)clientData->sock, hComHandle, (ULONG_PTR)clientData->sock, 0);
		nErr = WSAGetLastError();
		clientData->opType = E_RECV;
		clientData->buf.buf = new char[MSGSIZE];
		clientData->buf.len = MSGSIZE;
		DWORD dwRecv = 0;
		DWORD dwFlags = 0;
		int n = WSARecv(clientData->sock, &clientData->buf, 1, &dwRecv, &dwFlags, &clientData->overLap, 0);
		nErr = WSAGetLastError();
	}
	closesocket(listenSock);
	WSACleanup();
}

void ConnectServer()
{
	WORD version = MAKEWORD(2, 2);
	WSADATA wsData;
	WSAStartup(version, &wsData);
	// SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons(PORT);
	int nErr = connect(SOCKET_get_fd(sock), (const sockaddr*)&addr, sizeof(addr));
	if (nErr == SOCKET_ERROR)
	{
		nErr = WSAGetLastError();
		closesocket(sock);
		return;
	}
	char buf[100][MSGSIZE] = {};
	int nTemp = 0;
	for (int i = 0; i < 100; i++)
	{
		for (int j = 0; j < MSGSIZE; j++)
		{
			if (j == MSGSIZE - 1)
			{
				buf[i][j] = '\0';
				continue;
			}
			buf[i][j] = 48 + (nTemp % 48);
		}
		nTemp = ++nTemp <= 9 ? nTemp : 0;
	}
	for (size_t i = 0; i < 100; i++)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = buf[i];
		wsaBuf.len = sizeof(buf[i]);
		int nSendByest = send(SOCKET_get_fd(sock), buf[i], sizeof(buf[i]), 0);
		printf("send bytes: %d\n", nSendByest);
	}
	char recBuf[MSGSIZE] = {};
	recv(SOCKET_get_fd(sock), recBuf, MSGSIZE, 0);
	closesocket(sock);
	WSACleanup();
}

int main()
{
	if (true)
	{
		CreateServer();
	}
	else
	{
		ConnectServer();
	}
	return 0;
}
