// iocp_echo_server.c
// -----------------------------------------------------------------------------


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#else
#include "iocp.h"
#endif

#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------

enum // configuration
{
   MAX_BUF = 1024,
   SERVER_ADDRESS = INADDR_ANY,
   SERVICE_PORT = 50001
};

enum // socket operations
{
   OP_NONE,
   OP_ACCEPT,
   OP_READ,
   OP_WRITE
};

typedef struct _SocketState // socket state & control
{
   char operation;
   SOCKET socket;
   DWORD length;
   char buf[MAX_BUF];
   char buf2[MAX_BUF];
} SocketState;

struct myOVERLAPPED : OVERLAPPED
{
   void (*on_complete)(BOOL resultOk, DWORD byte_transfreed, ULONG_PTR completekey, OVERLAPPED*);
};

// -----------------------------------------------------------------------------

// the completion port
static HANDLE cpl_port;

// the listening socket
static SOCKET listener;
static SocketState listener_state;
static myOVERLAPPED listener_ovl;

// -----------------------------------------------------------------------------

// prototypes - main functions

static void start_accepting(void);
static void accept_completed(BOOL, DWORD, ULONG_PTR, OVERLAPPED*);

static void start_reading(SocketState*, myOVERLAPPED*);
static void read_completed(BOOL, DWORD, ULONG_PTR, OVERLAPPED*);

static void start_writing(SocketState*, myOVERLAPPED*);
static void write_completed(BOOL, DWORD, ULONG_PTR, OVERLAPPED*);

static void init(void);
static void run(void);

// prototypes - helper functions

static void bind_listening_socket(void);
static SOCKET create_accepting_socket(void);
static void create_io_completion_port(void);
static void create_listening_socket(void);
static void destroy_connection(SocketState*, OVERLAPPED*);
static BOOL get_completion_status(DWORD*, SocketState**,OVERLAPPED**);
static void init_winsock(void);
static SocketState* new_socket_state(void);
static myOVERLAPPED* new_overlapped(void);
static void prepare_endpoint(struct sockaddr_in*, u_long, u_short);
static void start_listening(void);

// -----------------------------------------------------------------------------

int main(void)
{
   init();
   run();
   return 0;
}
// -----------------------------------------------------------------------------

static void accept_completed(BOOL resultOk, DWORD length, ULONG_PTR key, OVERLAPPED* ovl)
{
   SocketState* socketState = reinterpret_cast<SocketState*>(key);
   SocketState* newSocketState;

   if (resultOk)
   {
      printf("* new connection created\n");

      // "updates the context" (whatever that is...)
    //   setsockopt(socketState->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
    //      (char *)&listener, sizeof(listener));

      // associates new socket with completion port
      newSocketState = new_socket_state();
      newSocketState->socket = socketState->socket;
      if (CreateIoCompletionPort((HANDLE)newSocketState->socket, cpl_port,
         (ULONG_PTR)newSocketState, 0) != cpl_port)
      {
         int err = WSAGetLastError();
         printf("* error %d in CreateIoCompletionPort in line %d\n", err, __LINE__);
         exit(1);
      }

      // starts receiving from the new connection
      auto new_ovl = new_overlapped();
      new_ovl->on_complete = read_completed;
      start_reading(newSocketState, new_ovl);

      // starts waiting for another connection request
      start_accepting();
   }

   else // !resultOk
   {
      int err = GetLastError();
      printf("* error (%d,%d) in accept, cleaning up and retrying!!!", err,
         length);
      closesocket(socketState->socket);
      start_accepting();
   }
}

// -----------------------------------------------------------------------------

static void bind_listening_socket(void)
{
   struct sockaddr_in sin;

   prepare_endpoint(&sin, SERVER_ADDRESS, SERVICE_PORT);
   int v = 1;
   setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&v, sizeof v);
   if (::bind(listener, (sockaddr*) &sin, sizeof(sin)) == SOCKET_ERROR)
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

   // for use by AcceptEx
   listener_state.socket = 0; // to be updated later
   listener_state.operation = OP_ACCEPT;

   if (CreateIoCompletionPort((HANDLE)listener, cpl_port,
      (ULONG_PTR)&listener_state, 0) != cpl_port)
   {
      int err = WSAGetLastError();
      printf("* error %d in listener\n", err);
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static void destroy_connection(SocketState* socketState, OVERLAPPED* ovl)
{
   closesocket(socketState->socket);
   free(socketState);
   free(ovl);
}

// -----------------------------------------------------------------------------

static BOOL get_completion_status(DWORD* length, PULONG_PTR socketState,
   OVERLAPPED** ovl_res)
{
   BOOL resultOk;
   *ovl_res = NULL;
   *socketState = NULL;

   resultOk = GetQueuedCompletionStatus(cpl_port, length, (PULONG_PTR)socketState,
      ovl_res,INFINITE);

   if (!resultOk)
   {
      DWORD err = GetLastError();
      printf("* error %d getting completion port status!!!\n", err);
   }

   if (!*socketState || !*ovl_res)
   {
      printf("* don't know what to do, aborting!!!\n");
      exit(1);
   }

   return resultOk;
}

// -----------------------------------------------------------------------------

static void init(void)
{
   init_winsock();
   create_io_completion_port();
   create_listening_socket();
   bind_listening_socket();
   start_listening();
   start_accepting();
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

static SocketState* new_socket_state(void)
{
   return (SocketState*)calloc(1, sizeof(SocketState));
}

// -----------------------------------------------------------------------------

static myOVERLAPPED* new_overlapped(void)
{
   return (myOVERLAPPED*)calloc(1, sizeof(myOVERLAPPED));
}

// -----------------------------------------------------------------------------

static void prepare_endpoint(struct sockaddr_in* sin, u_long address,
   u_short port)
{
   sin->sin_family = AF_INET;
   sin->sin_addr.s_addr = htonl(address);
   sin->sin_port = htons(port);
}

// -----------------------------------------------------------------------------

static void read_completed(BOOL resultOk, DWORD length, ULONG_PTR key, OVERLAPPED* ovl)
{
   SocketState* socketState = reinterpret_cast<SocketState*>(key);

   if (resultOk)
   {
      if (length > 0)
      {
         printf("* read operation completed, %d bytes read\n", length);

         // starts another write
         socketState->length = length;
         start_writing(socketState, (myOVERLAPPED*)ovl);
      }
      else // length == 0
      {
         printf("* connection closed by client\n");
         destroy_connection(socketState, ovl);
      }
   }
   else // !resultOk, assumes connection was reset
   {  int err = GetLastError();
      printf("* error %d in recv, assuming connection was reset by client\n",
         err);
      destroy_connection(socketState, ovl);
   }
}

// -----------------------------------------------------------------------------

static void run(void)
{
   DWORD length;
   BOOL resultOk;
   OVERLAPPED* ovl_res;
   ULONG_PTR complete_key;

   static const char * OP_STR[] = {
      "OP_NONE",
      "OP_ACCEPT",
      "OP_READ",
      "OP_WRITE"
   };

   for (;;)
   {
      resultOk = get_completion_status(&length, &complete_key, &ovl_res);

      if (ovl_res)
      {
         SocketState*s = reinterpret_cast<SocketState*>(complete_key);
         printf("* operation %s@%p completed\n", OP_STR[s->operation], ovl_res);
         reinterpret_cast<myOVERLAPPED*>(ovl_res)->on_complete(resultOk, length, complete_key, ovl_res);
      }
   } // for
}

// -----------------------------------------------------------------------------

static void start_accepting(void)
{
   SOCKET acceptor = create_accepting_socket();
   DWORD expected = sizeof(struct sockaddr_in) + 16;

   printf("* started accepting connections...\n");

   // uses listener's completion key and overlapped structure
   listener_state.socket = acceptor;
   memset(&listener_ovl, 0, sizeof(OVERLAPPED));

   listener_ovl.on_complete = accept_completed;

   // starts asynchronous accept
   if (!AcceptEx(listener, acceptor, listener_state.buf, 0 /* no recv */,
      expected, expected, NULL, &listener_ovl))
   {
      int err = WSAGetLastError();
      if (err != ERROR_IO_PENDING)
      {
         printf("* error %d in AcceptEx\n", err);
         exit(1);
      }
   }
}

// -----------------------------------------------------------------------------

static void start_listening(void)
{
   if (listen(listener, 100) == SOCKET_ERROR)
   {
      printf("* error in listen!\n");
      exit(1);
   } 
   printf("* started listening for connection requests...\n");
}

// -----------------------------------------------------------------------------

static void start_reading(SocketState* socketState, myOVERLAPPED* ovl)
{
   DWORD flags = 0;
   WSABUF wsabuf = { MAX_BUF, socketState->buf };

   memset(ovl, 0, sizeof(OVERLAPPED));
   ovl->on_complete= read_completed;

   socketState->operation = OP_READ;
   if (WSARecv(socketState->socket, &wsabuf, 1, NULL, &flags, ovl, NULL)
      == SOCKET_ERROR)
   {
      int err = WSAGetLastError();
      if (err != WSA_IO_PENDING)
      {
         printf("*error %d in WSARecv\n", err);
         exit(1);
      }
   }
}

// -----------------------------------------------------------------------------

static void start_writing(SocketState* socketState, myOVERLAPPED* ovl)
{
   auto content_length_line_len = snprintf(socketState->buf2, 80, "Content-Length: %d\r\n\r\n", socketState->length);
   WSABUF wsabuf[3] = {
      { .len = 17, .buf = (char*) "HTTP/1.1 200 OK\r\n" },
      { .len = (size_t) content_length_line_len, .buf = socketState->buf2 },
      { socketState->length, socketState->buf }
   };

   memset(ovl, 0, sizeof(OVERLAPPED));
   ovl->on_complete = write_completed;
   socketState->operation = OP_WRITE;

   if (WSASend(socketState->socket, wsabuf, 3, NULL, 0, ovl, NULL)
      == SOCKET_ERROR)
   {
      int err = WSAGetLastError();
      if (err != WSA_IO_PENDING)
      {
         printf("*error %d in WSASend\n", err);
         exit(1);
      }
   }
}

// -----------------------------------------------------------------------------

static void write_completed(BOOL resultOk, DWORD length, ULONG_PTR key, OVERLAPPED* ovl)
{
   SocketState* socketState = reinterpret_cast<SocketState*>(key);
   if (resultOk)
   {
      if (length > 0)
      {
         printf("* write operation completed\n");
         start_reading(socketState, (myOVERLAPPED*)ovl); // starts another read
      }
      else // length == 0 (strange!)
      {
         printf("* connection closed by client!\n");
         destroy_connection(socketState, ovl);
      }
   }
   else // !resultOk, assumes connection was reset
   {
      int err = GetLastError();
      printf("* error %d on send, assuming connection was reset!\n", err);
      destroy_connection(socketState, ovl);
   }
}

// -----------------------------------------------------------------------------
// the end