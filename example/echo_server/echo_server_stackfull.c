// iocp_echo_server.c
// -----------------------------------------------------------------------------

#include "universal_fiber.h"

#include <stdio.h>
#include <stdlib.h>


// -----------------------------------------------------------------------------

enum // configuration
{
   MAX_BUF = 1024,
   SERVER_ADDRESS = INADDR_ANY,
   SERVICE_PORT = 50001
};

// -----------------------------------------------------------------------------

// the completion port
static HANDLE cpl_port;

// the listening socket
static SOCKET listener;

// -----------------------------------------------------------------------------

// prototypes - main functions
static void init(void);
// prototypes - helper functions

static void bind_listening_socket(void);
static SOCKET create_accepting_socket(void);
static void create_io_completion_port(void);
static void create_listening_socket(void);
static BOOL get_completion_status(DWORD*, ULONG_PTR*, OVERLAPPED**);
static void init_winsock(void);
static void prepare_endpoint(struct sockaddr_in*, u_long, u_short);
static void start_listening(void);

// -----------------------------------------------------------------------------

int main(void)
{
   init();
   run_event_loop(cpl_port);
   return 0;
}

// -----------------------------------------------------------------------------

static void bind_listening_socket(void)
{
   struct sockaddr_in sin;

   prepare_endpoint(&sin, SERVER_ADDRESS, SERVICE_PORT);
   int v = 1;
   setsockopt(SOCKET_get_fd(listener), SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof v);
   if (bind(SOCKET_get_fd(listener), (struct sockaddr*) &sin, sizeof(sin)) == SOCKET_ERROR)
   {
      printf("* error in bind!\n");
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static SOCKET create_accepting_socket(void)
{
   SOCKET acceptor = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
   if (acceptor == INVALID_SOCKET)
   {
      printf("* error creating accept socket!\n");
      exit(1);
   }
   return acceptor;
}

// -----------------------------------------------------------------------------

static void create_io_completion_port(void)
{
   cpl_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
   if (!cpl_port)
   {
      int err = WSAGetLastError();
      printf("* error %d in line %d CreateIoCompletionPort\n", err, __LINE__);
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static void create_listening_socket(void)
{
   listener = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0 , 0, WSA_FLAG_OVERLAPPED);
   if (listener == INVALID_SOCKET)
   {
      printf("* error creating listening socket!\n");
      exit(1);
   }

   if (CreateIoCompletionPort((HANDLE)listener, cpl_port, 0, 0) != cpl_port)
   {
      int err = WSAGetLastError();
      printf("* error %d in listener\n", err);
      exit(1);
   }
}

static void echo_sever_client_session(void* param)
{
   SOCKET client_sock = (SOCKET) param;

   char buf1[1024];

   WSABUF wsabuf1 = {
      .len = 1024,
      .buf = buf1
   };

   FiberOVERLAPPED ov;
   memset(&ov, 0, sizeof(ov));

   DWORD recv_bytes = 0, flag = 0;

   if (WSARecv(client_sock, &wsabuf1, 1, &recv_bytes, &flag, &ov.ov, NULL) == SOCKET_ERROR)
   {
      int err = WSAGetLastError();
      if (err != WSA_IO_PENDING)
      {
         printf("*error %d in WSARecv\n", err);
         closesocket(client_sock);
         return;
      }
   }

   recv_bytes = get_overlapped_result(&ov);
   printf("* read operation completed\n");

   char buf2[1024];
   int content_length_line_len = snprintf(buf2, 80, "Content-Length: %d\r\n\r\n", ov.byte_transfered);

   WSABUF wsabuf[3] = {
      { .len = 17, .buf = (char*) "HTTP/1.1 200 OK\r\n" },
      { .len = (size_t) content_length_line_len, .buf = buf2 },
      { ov.byte_transfered, buf1 }
   };

   memset(&ov, 0, sizeof(ov));

   if (WSASend(client_sock, wsabuf, 3, NULL, 0, &ov.ov, NULL) == SOCKET_ERROR)
   {
      int err = WSAGetLastError();
      if (err != WSA_IO_PENDING)
      {
         printf("*error %d in WSASend\n", err);
         closesocket(client_sock);
         return;
      }
   }

   get_overlapped_result(&ov);
   printf("* write operation completed\n");

   closesocket(client_sock);
   printf("client_coroutine exit\n");
   return;
}

static void accept_coroutine(void* param)
{
   char addr_buf[1024];

	for (;;)
	{
		SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
		FiberOVERLAPPED ov = { {0}, 0 };
		DWORD ignore = 0;

		BOOL result = AcceptEx(listener, client_socket, addr_buf, 0, sizeof (SOCKADDR_IN)+16, sizeof (SOCKADDR_IN)+16, &ignore, &ov.ov);
		ov.last_error = WSAGetLastError();

		if (!(!result && ov.last_error != WSA_IO_PENDING))
		{
			get_overlapped_result(&ov);
			if (ov.last_error)
			{
				closesocket(client_socket);
				continue;
			}

			HANDLE read_port = CreateIoCompletionPort((HANDLE)(client_socket), cpl_port, 0, 0);
			// 开新协程处理连接.
         create_detached_coroutine(echo_sever_client_session, (LPVOID)client_socket);
      }
		else
		{
			closesocket(client_socket);
		}
	}
}

static void init(void)
{
   init_winsock();
   create_io_completion_port();
   create_listening_socket();
   bind_listening_socket();
   start_listening();

#ifdef _WIN32
   ConvertThreadToFiber(0);
#endif
   // 并发投递 64 个 accept 操作。加快 accept 速度.
   for (int i=0; i < 64; i++)
      create_detached_coroutine(accept_coroutine, listener);

   printf("back\n");
}

// -----------------------------------------------------------------------------

static void init_winsock(void)
{
   WSADATA wsaData;

   if (WSAStartup(MAKEWORD(2,2), &wsaData))
   {
      printf("* error in WSAStartup!\n");
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static void prepare_endpoint(struct sockaddr_in* sin, u_long address,
   u_short port)
{
   sin->sin_family = AF_INET;
   sin->sin_addr.s_addr = htonl(address);
   sin->sin_port = htons(port);
}

static void start_listening(void)
{
   if (listen(SOCKET_get_fd(listener), 100) == SOCKET_ERROR)
   {
      printf("* error in listen!\n");
      exit(1);
   }
   printf("* started listening for connection requests...\n");
}
// -----------------------------------------------------------------------------
// the end