/*
* In the linker options (on the project right-click, linker, input) you need add wsock32.lib or ws2_32.lib to the list of input files.
*/
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <time.h>

#include "iocp.h"


#define forever while(true)
#define DEFAULT_ERROR_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
#define DEFAULT_PORT 50001
#define REQUEST_BUFFER_SIZE 1024
#define FAIL_CODE -1
#define BUFFER_N 1024
#define THREADPOOL_SIZE 10

using namespace std;

class ioInformation {
public:
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[BUFFER_N];
	SOCKET socket;
	ioInformation(SOCKET messageSocket) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
		wsaBuf.len = BUFFER_N;
		wsaBuf.buf = buffer;
		this->socket = messageSocket;
	}
	~ioInformation() {
		closesocket(socket);
	}
};

class request {
public:
	enum {
		GET = 0,
		POST = 1,
		UNKNOW = -1
	};
	string typeName[2] = { "GET","POST" };
	int messageLength;
	int requestType;
	char clientIP[16];
	u_short clientPort;
	string filePath;
	string body;
	request(sockaddr_in& setting, SOCKET& messageSocket) {
		setIpandPort(setting);
		char buffer[REQUEST_BUFFER_SIZE];
		messageLength = recv(messageSocket->native_handle(), buffer, sizeof(buffer), 0);
		if (messageLength == 0) return;
		destructStr(buffer);
	}
#pragma warning(disable: 26495)
	request(char* buffer, size_t size) {
		if (messageLength == 0) return;
		destructStr(buffer);
	}
private:
	void setIpandPort(sockaddr_in& setting) {
		inet_ntop(AF_INET, &setting.sin_addr, clientIP, sizeof(clientIP));
		clientPort = htons(setting.sin_port);
	}
	void destructStr(char* buffer) {
		string requestStr = buffer;
		int index = 0;
		index = setRequestType(requestStr, index);
		index = setFilePath(requestStr, index);
		index = setBody(requestStr, index);
	}
	int setRequestType(string& str, int start) {
		int firstSpaceIndex = str.find(" ", start);
		if (firstSpaceIndex == -1)
			throw;
		string type = str.substr(0, firstSpaceIndex);
		if (type == "GET")
			requestType = GET;
		else if (type == "POST")
			requestType = POST;
		else
			requestType = UNKNOW;
		return firstSpaceIndex;
	}
	int setFilePath(string& str, int start) {
		int nextSpaceIndex = str.find(" ", start + 1);
		filePath = str.substr(start + 1, nextSpaceIndex - start - 1);
		return nextSpaceIndex;
	}
	int setBody(string& str, int start) {
		//skip
		return 1;
	}
};

class response {
public:
	vector<string> GetRouters;
	vector<string> PostRouters;
	response() {
		// add routers
		GetRouters.push_back("/getFile/");
	}
	int runGetRoute(SOCKET& socket, string route) {
		for (auto i : GetRouters) {
			if (0 == route.find(i)) {
				matchStr = i;
				goto routers;
			}
		}
		return notFound(socket);
	routers:
		if (matchStr == "/getFile/")
			return getFile(socket, route);
		else
			return notFound(socket);
	}
private:
	string matchStr;
	int getFile(SOCKET& socket, string& route) {
		string header = "HTTP/1.1 200 OK\r\nContent-Type: " + getContentType(route) + "; charset=UTF-8\r\n\r\n";
		string path = route.substr(matchStr.length(), route.length());
		string curFilePath = getCurFilePath();
		string goalFilePth = curFilePath + path;
		const char* headerChr = header.c_str();
		char buffer[1024] = { 0 };
		FILE* file;
		int readLength;
		int sendResult;
		file = fopen(path.c_str(), "rb");
		if (file == NULL) return notFound(socket);
		sendResult = send(socket->_socket_fd, headerChr, strlen(headerChr), 0);
		if (sendResult == SOCKET_ERROR) {
			printf("Error sending header, reconnecting...\n");
			closesocket(socket);
			return -1;
		}
		while ((readLength = fread(buffer, 1, 1024, file)) > 0) {
			sendResult = send(socket->native_handle(), buffer, readLength, 0);
			if (sendResult == SOCKET_ERROR) {
				printf("Error sending body, reconnecting...\n");
				closesocket(socket);
				return -1;
			}
			else if (readLength <= 0)
			{
				printf("Read File Error, End The Program\n");
				closesocket(socket);
				return readLength;
			}
		}
		fclose(file);
		closesocket(socket);
		return 1;
	}
	int notFound(SOCKET& socket) {
		send(socket->native_handle(), DEFAULT_ERROR_404, strlen(DEFAULT_ERROR_404), 0);
		closesocket(socket);
		return 1;
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
			return "*/*";
		string extension = route.substr(index);
		if (extension == ".html")
			return "text/html";
		else if (extension == ".ico")
			return "image/webp";
		else if (extension == ".css")
			return "text/css";
		else if (extension == ".jpg")
			return "image/jpeg";
		else if (extension == ".js")
			return "text/javascript";
		return "*/*";
	}
};

class httpServer {
public:
	struct sockaddr_in localSocketSetting;
	SOCKET listenSocket;
	WSADATA windowsSocketData;
	HANDLE eventQueue, acceptEvent;
	thread workerThreads[THREADPOOL_SIZE];
	DWORD recvBytes = 0, flags = 0;
	void start() {
		printf("Start.......\n");
		forever
		{
			if (waitAcceptEvent()) {
				struct sockaddr_in clientSocketSetting;
				socklen_t clientSocketSettingLength;
				clientSocketSettingLength = sizeof(clientSocketSetting);
				int messageSocket_ = accept(listenSocket->native_handle(), (struct sockaddr*)&clientSocketSetting, &clientSocketSettingLength);
                SOCKET messageSocket = new SOCKET_emu_class(messageSocket_);
				ioInformation* ioInfo = new ioInformation(messageSocket);
				if (CreateIoCompletionPort((HANDLE)messageSocket, eventQueue, (ULONG_PTR)ioInfo, 0) == NULL)
					errorHandle("IOCP listen");
				WSARecv(messageSocket, &(ioInfo->wsaBuf),1, &recvBytes, &flags, &(ioInfo->overlapped), NULL);
			}
		}
	}
#pragma warning(disable: 26495)
	httpServer() {
		if (WSAStartup(MAKEWORD(2, 2), &windowsSocketData) == SOCKET_ERROR)
			errorHandle("WSAStartup");
		acceptEvent = WSACreateEvent();
		// Fill in the address structure
		localSocketSetting.sin_family = AF_INET;
		localSocketSetting.sin_addr.s_addr = INADDR_ANY;
		localSocketSetting.sin_port = htons(DEFAULT_PORT);
		listenSocket = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (listenSocket == INVALID_SOCKET)
			errorHandle("socket");
		if (bind(listenSocket, (struct sockaddr*)&localSocketSetting, sizeof(localSocketSetting)) == SOCKET_ERROR)
			errorHandle("bind");
		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			errorHandle("listen");
		if (WSAEventSelect(listenSocket, acceptEvent, FD_ACCEPT) == SOCKET_ERROR)
			errorHandle("select");
		// IOCP
		eventQueue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
		if (eventQueue == NULL)
			errorHandle("IOCP create");
		if (CreateIoCompletionPort((HANDLE)listenSocket, eventQueue, (ULONG_PTR)0, 0) == NULL)
			errorHandle("IOCP listen");
		// build worker thread
		for (int i = 0; i < THREADPOOL_SIZE; i++)
			workerThreads[i] = thread(&httpServer::workerThreadFunction, this, eventQueue);
	}
	~httpServer() {
		//PostQueuedCompletionStatus(eventQueue, 0, NULL, NULL);
		closesocket(listenSocket);
		CloseHandle(acceptEvent);
		WSACleanup();
	}
private:
	void errorHandle(string str) {
		std::cout << "error : " << str << endl;
		exit(FAIL_CODE);
	}
	int responseClient(request req, SOCKET& messageSocket) {
		response response;
		if (req.requestType == req.GET)
			return response.runGetRoute(messageSocket, req.filePath);
		else
			return response.runGetRoute(messageSocket, "/404");
	}
	void workerThreadFunction(LPVOID LPVOID) {
		ULONG_PTR* ipCompletionKey;
		WSAOVERLAPPED* ipOverlap;
		DWORD ipNumberOfBytes;
		int result;
		HANDLE eventQueue = (HANDLE)LPVOID;
		while (true)
		{
			// get IO status
			result = GetQueuedCompletionStatus(
				eventQueue,
				&ipNumberOfBytes,
				(PULONG_PTR)&ipCompletionKey,
				&ipOverlap,
				INFINITE);
			if (result == 0 || ipNumberOfBytes == 0)
				continue;
			ioInformation* ioInfo = (ioInformation*)ipCompletionKey;
			request req = request(ioInfo->wsaBuf.buf, ioInfo->wsaBuf.len);
			if (req.requestType < 0) {
				delete ioInfo;
				continue;
			}
			cout << req.typeName[req.requestType] << " : " << req.filePath << endl;
			int sentResult = responseClient(req, ioInfo->socket);
			if (sentResult <= 0) {
				printf("send error\n");
				break;
			}
			delete ioInfo;
		}
	}
	bool waitAcceptEvent() {
		DWORD ret = WSAWaitForMultipleEvents(1, &acceptEvent, FALSE, WSA_INFINITE, FALSE);
		if (WSA_WAIT_TIMEOUT == ret || WSA_WAIT_FAILED == ret)
			return false;
		WSANETWORKEVENTS events;
		int nRet = WSAEnumNetworkEvents(listenSocket, acceptEvent, &events);
		if (nRet == SOCKET_ERROR)
			return false;
		if (events.lNetworkEvents & FD_ACCEPT)
			return true;
		return false;
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
