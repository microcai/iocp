// iocp_echo_server.c
// -----------------------------------------------------------------------------


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#else
#include "iocp.h"
#endif

#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------

enum // configuration
{
   MAX_BUF = 1024,
   SERVER_ADDRESS = INADDR_LOOPBACK,
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
} SocketState;

// -----------------------------------------------------------------------------

// the completion port
static HANDLE cpl_port;

// the listening socket
static SOCKET listener;
static SocketState listener_state;
static OVERLAPPED listener_ovl;

// -----------------------------------------------------------------------------

// prototypes - main functions

static void start_accepting(void);
static void accept_completed(BOOL, DWORD, SocketState*, OVERLAPPED*);

static void start_reading(SocketState*, OVERLAPPED*);
static void read_completed(BOOL, DWORD, SocketState*, OVERLAPPED*);

static void start_writing(SocketState*, OVERLAPPED*);
static void write_completed(BOOL, DWORD,SocketState*, OVERLAPPED*);

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
static OVERLAPPED* new_overlapped(void);
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

static void accept_completed(BOOL resultOk, DWORD length,
   SocketState* socketState, OVERLAPPED* ovl)
{
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
      start_reading(newSocketState, new_overlapped());

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
   
   if (bind(listener, (sockaddr*) &sin, sizeof(sin)) == SOCKET_ERROR)
   {
      printf("* error in bind!\n");
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static SOCKET create_accepting_socket(void)
{
   SOCKET acceptor = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);
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
   listener = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0 , 0, 0);
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

static BOOL get_completion_status(DWORD* length, SocketState** socketState,
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

static OVERLAPPED* new_overlapped(void)
{
   return (OVERLAPPED*)calloc(1, sizeof(OVERLAPPED));
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

static void read_completed(BOOL resultOk, DWORD length,
   SocketState* socketState, OVERLAPPED* ovl)
{
   if (resultOk)
   {
      if (length > 0)
      {
         printf("* read operation completed, %d bytes read\n", length);

         // starts another write
         socketState->length = length;
         start_writing(socketState, ovl);
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
   SocketState* socketState;

   for (;;)
   {
      resultOk = get_completion_status(&length, &socketState, &ovl_res);

      switch (socketState->operation)
      {
         case OP_ACCEPT:
            printf("* operation ACCEPT completed\n");
            accept_completed(resultOk, length, socketState, ovl_res);
            break;

         case OP_READ:
            printf("* operation READ completed\n");
            read_completed(resultOk, length, socketState, ovl_res);
            break;

         case OP_WRITE:
            printf("* operation WRITE completed\n");
            write_completed(resultOk, length, socketState, ovl_res);
            break;

         default:
            printf("* error, unknown operation!!!\n");
            destroy_connection(socketState, ovl_res); // hope for the best!
            break;
      } // switch
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

static void start_reading(SocketState* socketState, OVERLAPPED* ovl)
{
   DWORD flags = 0;
   WSABUF wsabuf = { MAX_BUF, socketState->buf };

   memset(ovl, 0, sizeof(OVERLAPPED));
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

static void start_writing(SocketState* socketState, OVERLAPPED* ovl)
{
   WSABUF wsabuf = { socketState->length, socketState->buf };

   memset(ovl, 0, sizeof(OVERLAPPED));
   socketState->operation = OP_WRITE;

   if (WSASend(socketState->socket, &wsabuf, 1, NULL, 0, ovl, NULL)
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

static void write_completed(BOOL resultOk, DWORD length,
   SocketState* socketState, OVERLAPPED* ovl)
{
   if (resultOk)
   {
      if (length > 0)
      {
         printf("* write operation completed\n");
         start_reading(socketState, ovl); // starts another read
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