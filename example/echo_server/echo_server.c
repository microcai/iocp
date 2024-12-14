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

static void client_coroutine(void* param)
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
   for (;;)
   {
      SOCKET new_client_socket = create_accepting_socket();
      DWORD expected = sizeof(struct sockaddr_in) + 16;

      printf("* started accepting connections...\n");

      FiberOVERLAPPED listener_ovl;
      memset(&listener_ovl, 0, sizeof(listener_ovl));
      // uses listener's completion key and overlapped structure
      char buf[1024];

      // starts asynchronous accept
      if (!AcceptEx(listener, new_client_socket, buf, 0 /* no recv */,
         expected, expected, NULL, &listener_ovl.ov))
      {
         int err = WSAGetLastError();
         if (err != ERROR_IO_PENDING)
         {
            printf("* error %d in AcceptEx\n", err);
            exit(1);
         }
      }

      get_overlapped_result(&listener_ovl);

      if (listener_ovl.resultOk)
      {
         CreateIoCompletionPort((HANDLE)new_client_socket, cpl_port, 0, 0);

         create_detached_coroutine(client_coroutine, (VOID*)new_client_socket,"client_coro");
      }
      else
      {
         closesocket(new_client_socket);
      }
   }

   return ;
}

static void init(void)
{
   init_winsock();
   create_io_completion_port();
   create_listening_socket();
   bind_listening_socket();
   start_listening();

   ConvertThreadToFiber(0);

   // 并发投递 64 个 accept 操作。加快 accept 速度.
   for (int i=0; i < 64; i++)
      create_detached_coroutine(accept_coroutine, 0, "accept_coro");

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