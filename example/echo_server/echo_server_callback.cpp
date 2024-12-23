// iocp_echo_server.cpp
// -----------------------------------------------------------------------------
#include "easy_iocp.hpp"
#include <memory>

#include <stdio.h>
#include <stdlib.h>
// -----------------------------------------------------------------------------

enum // configuration
{
   MAX_BUF = 1024,
   SERVER_ADDRESS = INADDR_ANY,
   SERVICE_PORT = 50001
};

typedef struct _SocketState // socket state & control
{
   char operation;
   SOCKET socket;
   DWORD length;
   char buf[MAX_BUF];
   char buf2[MAX_BUF];
} SocketState;

// -----------------------------------------------------------------------------

// the completion port
static HANDLE cpl_port;

// the listening socket
static SOCKET listener;
static SocketState listener_state;

// -----------------------------------------------------------------------------

// prototypes - main functions

static void start_accepting(void);
static void accept_completed(DWORD, DWORD, SocketState*);

static void start_reading(SocketState*);
static void read_completed(DWORD last_error, DWORD length, SocketState* socketState);

static void start_writing(SocketState*);
static void write_completed(DWORD last_error, DWORD length, SocketState* socketState);

static void init(void);
static void run(void);

// prototypes - helper functions

static void bind_listening_socket(void);
static SOCKET create_accepting_socket(void);
static void create_io_completion_port(void);
static void create_listening_socket(void);
static void destroy_connection(SocketState*);
static void init_winsock(void);
static SocketState* new_socket_state(void);
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

static void accept_completed(DWORD last_error, DWORD length, SocketState* socketState)
{
   SocketState* newSocketState;

   if (!last_error)
   {
      printf("* new connection created\n");

      // "updates the context" (whatever that is...)
    //   setsockopt(socketState->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
    //      (char *)&listener, sizeof(listener));

      // associates new socket with completion port
      newSocketState = new_socket_state();
      newSocketState->socket = socketState->socket;
      if (CreateIoCompletionPort((HANDLE)newSocketState->socket, cpl_port, 0, 0) != cpl_port)
      {
         int err = WSAGetLastError();
         printf("* error %d in CreateIoCompletionPort in line %d\n", err, __LINE__);
         exit(1);
      }

      // starts receiving from the new connection
      start_reading(newSocketState);

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

   if (CreateIoCompletionPort((HANDLE)listener, cpl_port,
      (ULONG_PTR)&listener_state, 0) != cpl_port)
   {
      int err = WSAGetLastError();
      printf("* error %d in listener\n", err);
      exit(1);
   }
}

// -----------------------------------------------------------------------------

static void destroy_connection(SocketState* socketState)
{
   closesocket(socketState->socket);
   free(socketState);
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

#ifdef _WIN32
      init_winsock_api_pointer();
#endif

}

// -----------------------------------------------------------------------------

static SocketState* new_socket_state(void)
{
   return (SocketState*)calloc(1, sizeof(SocketState));
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

static void read_completed(DWORD last_error, DWORD length, SocketState* socketState)
{
   if (!last_error)
   {
      if (length > 0)
      {
         printf("* read operation completed, %d bytes read\n", length);

         // starts another write
         socketState->length = length;
         start_writing(socketState);
      }
      else // length == 0
      {
         printf("* connection closed by client\n");
         destroy_connection(socketState);
      }
   }
   else // !resultOk, assumes connection was reset
   {  int err = GetLastError();
      printf("* error %d in recv, assuming connection was reset by client\n",
         err);
      destroy_connection(socketState);
   }
}

// -----------------------------------------------------------------------------

static void run(void)
{
   run_event_loop(cpl_port);
}
// -----------------------------------------------------------------------------

static void start_accepting(void)
{
   DWORD expected = sizeof(struct sockaddr_in) + 16;

   printf("* started accepting connections...\n");

   auto listener_ovl = std::make_shared<iocp_callback>();

   listener_state.socket = create_accepting_socket();

   listener_ovl->cb = [listener_ovl](auto e, auto b){
      accept_completed(e, b, &listener_state);
   };

   // starts asynchronous accept
   if (!AcceptEx(listener, listener_state.socket, listener_state.buf, 0 /* no recv */,
      expected, expected, NULL, listener_ovl.get()))
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

static void start_reading(SocketState* socketState)
{
   DWORD flags = 0;
   WSABUF wsabuf = { MAX_BUF, socketState->buf };

   auto ovl_ptr = std::make_shared<iocp_callback>();

   ovl_ptr->cb = [socketState, ovl_ptr](auto e, auto b)
   {
      read_completed(e, b, socketState);
   };

   if (WSARecv(socketState->socket, &wsabuf, 1, NULL, &flags, ovl_ptr.get(), NULL)
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

static void start_writing(SocketState* socketState)
{
   auto content_length_line_len = snprintf(socketState->buf2, 80, "Content-Length: %d\r\n\r\n", socketState->length);
   WSABUF wsabuf[3] = {
      { .len = 17, .buf = (char*) "HTTP/1.1 200 OK\r\n" },
      { .len = (size_t) content_length_line_len, .buf = socketState->buf2 },
      { socketState->length, socketState->buf }
   };

   auto ovl_ptr = std::make_shared<iocp_callback>();

   ovl_ptr->cb = [ovl_ptr, socketState](auto e, auto b){
      write_completed(e, b, socketState);
   };

   if (WSASend(socketState->socket, wsabuf, 3, NULL, 0, ovl_ptr.get(), NULL)
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

static void write_completed(DWORD last_error, DWORD length, SocketState* socketState)
{
   if (!last_error)
   {
      if (length > 0)
      {
         printf("* write operation completed\n");
         start_reading(socketState); // starts another read
      }
      else // length == 0 (strange!)
      {
         printf("* connection closed by client!\n");
         destroy_connection(socketState);
      }
   }
   else // !resultOk, assumes connection was reset
   {
      int err = GetLastError();
      printf("* error %d on send, assuming connection was reset!\n", err);
      destroy_connection(socketState);
   }
}

// -----------------------------------------------------------------------------
// the end