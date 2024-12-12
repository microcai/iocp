﻿/*
* In the linker options (on the project right-click, linker, input) you need add wsock32.lib or ws2_32.lib to the list of input files.
*/
#define DISABLE_THREADS 1
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <array>
#include <time.h>

#include "universal_async.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <winbase.h>

LPFN_DISCONNECTEX DisconnectEx = nullptr;
#define WSA_FLAG_FAKE_CREATION 0

#define SOCKET_get_fd(s) (s)
#define MSG_NOSIGNAL 0
#define getcwd(a,b) GetCurrentDirectory(b,a)
#else
#include "iocp.h"
#endif

#define forever while(true)
#define DEFAULT_ERROR_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
#define DEFAULT_PORT 50001
#define REQUEST_BUFFER_SIZE 1024
#define FAIL_CODE -1
#define BUFFER_N 1024

using namespace std;

class request {
public:
	enum {
		GET = 0,
		POST = 1,
		UNKNOW = -1
	};
	string typeName[2] = { "GET","POST" };
	size_t messageLength;
	int requestType = UNKNOW;
	char clientIP[16];
	u_short clientPort;
	string filePath;
	string body;

	request(char* buffer, size_t messageLength)
		: messageLength(messageLength)
	{
		string_view requestStr { buffer , messageLength };
		int index = 0;
		index = setRequestType(requestStr, index);
		index = setFilePath(requestStr, index);
	}
private:
	void setIpandPort(sockaddr_in& setting) {
		inet_ntop(AF_INET, &setting.sin_addr, clientIP, sizeof(clientIP));
		clientPort = htons(setting.sin_port);
	}
	void destructStr(char* buffer) {

	}
	int setRequestType(string_view str, int start) {
		int firstSpaceIndex = str.find(" ", start);
		if (firstSpaceIndex == -1)
			throw std::runtime_error{"invalid protocol"};
		string_view type = str.substr(0, firstSpaceIndex);
		if (type == "GET")
			requestType = GET;
		else if (type == "POST")
			requestType = POST;
		else
			requestType = UNKNOW;
		return firstSpaceIndex;
	}
	int setFilePath(string_view& str, int start) {
		int nextSpaceIndex = str.find(" ", start + 1);
		filePath = str.substr(start + 1, nextSpaceIndex - start - 1);
		return nextSpaceIndex;
	}
};

struct auto_handle
{
	HANDLE _h;
	auto_handle(HANDLE h) : _h(h){}
	~auto_handle(){ CloseHandle(_h);}
};

struct auto_sockethandle
{
	SOCKET _s;
	auto_sockethandle(SOCKET s) : _s(s){}
	~auto_sockethandle(){ closesocket(_s);}
};


struct response
{
	HANDLE iocp;
	vector<string> GetRouters;
	vector<string> PostRouters;
	response(HANDLE iocp)
		: iocp(iocp)
	{
		// add routers
		GetRouters.push_back("/getFile/");
	}

	ucoro::awaitable<void> runGetRoute(SOCKET& socket, string route) {
		for (auto i : GetRouters) {
			if (0 == route.find(i)) {
				matchStr = i;
				goto routers;
			}
		}
		co_await notFound(socket);
		co_return;
	routers:
		if (matchStr == "/getFile/")
			co_await getFile(socket, route);
		else
			co_await notFound(socket);
	}

	string matchStr;
	ucoro::awaitable<int> getFile(SOCKET& socket, string& route) {
		string header = "HTTP/1.1 200 OK\r\nContent-Type: " + getContentType(route) + "\r\nConnection: close\r\n\r\n";
		string path = route.substr(matchStr.length(), route.length());
		string curFilePath = getCurFilePath();
		string goalFilePth = curFilePath + path;

		int sendResult;

		HANDLE file = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, INVALID_HANDLE_VALUE);
		if (file == INVALID_HANDLE_VALUE)
			co_return co_await notFound(socket);

		auto_handle auto_close(file);

		CreateIoCompletionPort(file, iocp, 0, 0);

		WSABUF wsabuf { .len = static_cast<DWORD>(header.length()) , .buf = header.data() };

		awaitable_overlapped ov;

		sendResult = WSASend(socket, &wsabuf, 1, 0, 0, &ov, NULL);
		ov.last_error = WSAGetLastError();

	    if (sendResult != 0 && ov.last_error != WSA_IO_PENDING)
		{
			// printf("Error sending header, reconnecting...\n");
			co_return -1;
		}

		co_await get_overlapped_result(ov);

		awaitable_overlapped file_ov;
		file_ov.set_offset(0);

		std::array<char[BUFFER_N], 2> buffer;

		char * pri_buf = buffer[0];
		char * back_buf = buffer[1];

		DWORD readLength = 0, back_readLength = 0, sent = 0;
		file_ov.add_offset(readLength);
		auto ret = ReadFile(file, pri_buf, BUFFER_N, &readLength , &file_ov);
		file_ov.last_error = GetLastError();
		if (!(!ret && file_ov.last_error != ERROR_IO_PENDING && file_ov.last_error != ERROR_MORE_DATA))
			readLength = co_await get_overlapped_result(file_ov);

		while (readLength > 0)
		{
			file_ov.add_offset(readLength);
			ret = ReadFile(file, back_buf, BUFFER_N, &back_readLength , &file_ov);
			file_ov.last_error = GetLastError();

			bool read_file_pending = !(!ret && file_ov.last_error != ERROR_IO_PENDING && file_ov.last_error != ERROR_MORE_DATA);

			WSABUF wsabuf{ .len = readLength , .buf = pri_buf };

			auto result = WSASend(socket, &wsabuf, 1, 0, 0, &ov, 0);
			ov.last_error = WSAGetLastError();
  			if (result != 0 && ov.last_error != WSA_IO_PENDING)
			{
				co_return -1;
			}
			else
			{
				co_await get_overlapped_result(ov);
			}

			if (ov.last_error)
			{
				#ifdef _DEBUG
				printf("Error sending body, cancel FILE read...\n");
				#endif
				if (read_file_pending)
				{
					// CancelIoEx(file, &file_ov);
					back_readLength = co_await get_overlapped_result(file_ov);
				}
				#ifdef _DEBUG
				printf("Error sending body, canceled FILE read...\n");
				#endif
				co_return -1;
			}

			if (read_file_pending)
			{
				back_readLength = co_await get_overlapped_result(file_ov);
			}
			readLength = back_readLength;
			std::swap(pri_buf, back_buf);

		};

		auto disconnect_result = DisconnectEx(socket, &ov, 0, 0);
		ov.last_error = ::WSAGetLastError();
		if (!(disconnect_result && ov.last_error != WSA_IO_PENDING))
			co_await get_overlapped_result(ov);
		#ifdef _DEBUG
		printf("file sent successfull...\n");
		#endif
		co_return 1;
	}

	static ucoro::awaitable<int> notFound(SOCKET& socket) {
		WSABUF wsabuf { .len = sizeof(DEFAULT_ERROR_404) - 1 , .buf = (char*) DEFAULT_ERROR_404 };

		awaitable_overlapped ov;

		auto result = WSASend(socket, &wsabuf, 1, 0, 0, &ov, 0);
		ov.last_error = WSAGetLastError();
		if (!(result != 0 && ov.last_error != WSA_IO_PENDING))
		{
			co_await get_overlapped_result(ov);
		}
		else
		{
			printf("send successfull without await\n");
		}
		co_return 1;
	}
	string getCurFilePath() {
		char filename[1024] = { 0 };
#pragma warning(disable: 6031)
		getcwd(filename, 1024);
		if (filename[strlen(filename)] != '\\')
			strcat(filename, "\\");
		return filename;
	}
	string getContentType(string route)
	{
		int index = route.find_last_of('.');
		if (index == -1)
			return "*/*; charset=UTF-8";
		string extension = route.substr(index);
		if (extension == ".html")
			return "text/html; charset=UTF-8";
		else if (extension == ".md" || extension == ".txt" || extension == ".cmake")
			return "text/plain; charset=UTF-8";
		else if (extension == ".ico")
			return "image/webp";
		else if (extension == ".css")
			return "text/css";
		else if (extension == ".jpg")
			return "image/jpeg";
		else if (extension == ".js")
			return "text/javascript; charset=UTF-8";
		return "application/octet-stream";
	}
};

class httpServer {
public:
	struct sockaddr_in localSocketSetting = { AF_INET };
	struct sockaddr_in6 localSocketSetting6 = { AF_INET6 };
	SOCKET listenSocket6, listenSocket;
	WSADATA windowsSocketData;
	HANDLE eventQueue;
#ifndef DISABLE_THREADS
	std::array<HANDLE, 24> eventQueues;
#endif

	void start() {
		printf("Start.......\n");

#ifdef DISABLE_THREADS
		for (int i=0; i < 32; i++)
		{
			start_accept(listenSocket6, AF_INET6, eventQueue).detach();
			start_accept(listenSocket, AF_INET, eventQueue).detach();
		}
#else
		int batch = 8 * eventQueues.size();

		for (int i=0; i < batch; i++)
		{
			start_accept(listenSocket6, AF_INET6, eventQueues[i % eventQueues.size()]).detach();
			start_accept(listenSocket, AF_INET, eventQueues[i % eventQueues.size()]).detach();
		}

		for (int i=0; i < eventQueues.size(); i++)
			std::thread(&run_event_loop, eventQueues[i]).detach();

#endif

		run_event_loop(eventQueue);
	}

	ucoro::awaitable<int> start_accept(SOCKET listen_sock, int family, HANDLE binded_event_queue)
	{
		for (;;)
		{
			// 注意 这个 WSA_FLAG_FAKE_CREATION
			// 记得在 win 上给 定义为 0
			SOCKET socket = WSASocket(family, SOCK_STREAM , IPPROTO_TCP, 0 , 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_FAKE_CREATION);
			awaitable_overlapped ov;
			char outputbuffer[128];
			DWORD accepted_size = 0;
			auto result = AcceptEx(listen_sock, socket, outputbuffer, 0,sizeof (sockaddr_in6)+16, sizeof (sockaddr_in6)+16, &accepted_size, &ov);
			ov.last_error = WSAGetLastError();
			if (!result && ov.last_error != WSA_IO_PENDING)
				accepted_size = co_await get_overlapped_result(ov);

			if (ov.last_error == WSAECANCELLED || ov.last_error == ERROR_OPERATION_ABORTED || ov.last_error == ERROR_NETNAME_DELETED)
			{
				printf("requested quit, exiting accept loop\n");
				break;
			}
			else if (ov.last_error == 0)
			{
				CreateIoCompletionPort((HANDLE)socket, binded_event_queue, 0, 0);
				#ifdef _WIN32
				sockaddr* localaddr, *remoteaddr;
				socklen_t localaddr_len, remoteaddr_len;
				GetAcceptExSockaddrs(outputbuffer, accepted_size, sizeof (sockaddr_in6)+16, sizeof (sockaddr_in6)+16, &localaddr, &localaddr_len, &remoteaddr, &remoteaddr_len);
				auto err = setsockopt(socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&listen_sock, sizeof(listen_sock) );
				#endif

				handle_connection(socket, binded_event_queue).detach(binded_event_queue);
			}
			else
			{
				printf("over error\n");
			}
		}
		co_return 1;
	}

	ucoro::awaitable<void> handle_connection(SOCKET socket, HANDLE iocp)
	{
#ifndef DISABLE_THREADS
		co_await run_on_iocp_thread(iocp);
#endif

		auto_sockethandle auto_close(socket);

		char buffer[BUFFER_N + 1];
		buffer[BUFFER_N] = 0;

		WSABUF wsaBuf { .len = BUFFER_N, .buf = buffer };

		DWORD flags = 0;
		awaitable_overlapped ov;
		DWORD recv_bytes = BUFFER_N;
 		auto result = WSARecv(socket, &wsaBuf,1, &recv_bytes, &flags, &ov, NULL);
		ov.last_error = GetLastError();
		if (result != 0 && ov.last_error != WSA_IO_PENDING)
		{
			co_return;
		}
		else
		{
			recv_bytes = co_await get_overlapped_result(ov);
		}

		if (ov.last_error == 0 && recv_bytes > 10)
		{
			request req = request(buffer, recv_bytes);
			if (req.requestType < 0) {
				co_return;
			}

			// cout << req.typeName[req.requestType] << " : " << req.filePath << endl;
			co_await responseClient(req, socket, iocp);
		}
	}


#pragma warning(disable: 26495)
	httpServer() {
		if (WSAStartup(MAKEWORD(2, 2), &windowsSocketData) == SOCKET_ERROR)
			errorHandle("WSAStartup");
		// IOCP

		eventQueue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
		if (eventQueue == NULL)
			errorHandle("IOCP create");
#ifndef DISABLE_THREADS
		for (int i=0; i < eventQueues.size(); i ++)
		{
			eventQueues[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
			if (eventQueues[i] == NULL)
				errorHandle("IOCP create");
		}
#endif
		// Fill in the address structure
		localSocketSetting6.sin6_port = htons(DEFAULT_PORT);
		localSocketSetting.sin_port = htons(DEFAULT_PORT);

		listenSocket6 = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (listenSocket6 == INVALID_SOCKET)
			errorHandle("socket");
		listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (listenSocket == INVALID_SOCKET)
			errorHandle("socket");

#ifdef _WIN32
		GUID disconnectex = WSAID_DISCONNECTEX;
		DWORD BytesReturned;

		WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&disconnectex, sizeof(GUID), &DisconnectEx, sizeof(DisconnectEx),
			&BytesReturned, 0, 0);
#endif
		if (CreateIoCompletionPort((HANDLE)listenSocket6, eventQueue, (ULONG_PTR)0, 0) == NULL)
			errorHandle("IOCP bind socket6");
		if (CreateIoCompletionPort((HANDLE)listenSocket, eventQueue, (ULONG_PTR)0, 0) == NULL)
			errorHandle("IOCP bind socket");

		int v = 1;
		setsockopt(listenSocket6, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof (v));
		#ifdef IPV6_V6ONLY
		setsockopt(listenSocket6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v, sizeof (v));
		#endif
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof (v));

		if (::bind(listenSocket6, (sockaddr*) &localSocketSetting6, sizeof(localSocketSetting6)) == SOCKET_ERROR)
			errorHandle("bind6");
		if (::bind(listenSocket, (sockaddr*) &localSocketSetting, sizeof(localSocketSetting)) == SOCKET_ERROR)
			errorHandle("bind");

		if (listen(listenSocket6, SOMAXCONN) == SOCKET_ERROR)
			errorHandle("listen");
		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			errorHandle("listen");

	}
	~httpServer() {
		//PostQueuedCompletionStatus(eventQueue, 0, NULL, NULL);
		closesocket(listenSocket6);
		closesocket(listenSocket);
		WSACleanup();
	}
private:
	void errorHandle(string str) {
		std::cout << "error : " << str << endl;
		exit(FAIL_CODE);
	}

	ucoro::awaitable<void> responseClient(request req, SOCKET& messageSocket, HANDLE iocp) {
		response response{iocp};
		if (req.requestType == req.GET)
			co_return co_await response.runGetRoute(messageSocket, req.filePath);
		else if (req.requestType == req.POST)
		{
			// close all listen socket
			CancelIo((HANDLE)listenSocket);
			closesocket(listenSocket);
			CancelIo((HANDLE)listenSocket6);
			closesocket(listenSocket6);

#ifndef DISABLE_THREADS
			for (HANDLE p : eventQueues)
				exit_event_loop_when_empty(p);
#endif
			exit_event_loop_when_empty(eventQueue);
		}
		else
			co_await response.runGetRoute(messageSocket, "/404");
	}
};

void startServer() {
	httpServer server;
	server.start();
}

int main(int argc, char** argv)
{
	startServer();
}
