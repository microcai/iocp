
#ifndef __IOCP__H__
#define __IOCP__H__

#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <error.h>

#define WINAPI

#ifndef __in
#define __in
#endif
#ifndef _In_
#define _In_
#endif

#ifndef _In_opt_
#define _In_opt_
#endif

#ifndef __out
#define __out
#endif

#ifndef _Out_
#define _Out_
#endif

#ifndef _Out_opt_
#define _Out_opt_
#endif

#ifndef _Inout_
#define _Inout_
#endif

#define FALSE false
#define TRUE true

#ifdef __cplusplus
#define IOCP_DECL extern "C"
#else
#define IOCP_DECL
#endif
typedef char CHAR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const wchar_t* LPCWSTR;

typedef int* LPINT;
typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef uint16_t WORD, LPWORD;
typedef uint32_t DWORD, *LPDWORD, *DWORD_PTR;
typedef bool BOOL; // bool is from stdbool if C99 mode.
typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef uint16_t TCHAR;
typedef struct sockaddr SOCKADDR, * LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;

struct base_handle;
typedef base_handle * HANDLE, * SOCKET, * WSAEVENT;

const HANDLE INVALID_HANDLE_VALUE = (HANDLE)-1;
const SOCKET INVALID_SOCKET = (SOCKET)-1;

#define INFINITE -1

typedef struct _OVERLAPPED
{
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	union {
		struct
		{
			DWORD Offset;
			DWORD OffsetHigh;
		};
		PVOID Pointer;
	};
	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED, WSAOVERLAPPED, *LPWSAOVERLAPPED;


IOCP_DECL BOOL WINAPI CloseHandle(__in HANDLE);

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(__in HANDLE FileHandle, __in HANDLE ExistingCompletionPort,
											   __in ULONG_PTR CompletionKey, __in DWORD NumberOfConcurrentThreads

);

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(__in HANDLE CompletionPort, __out LPDWORD lpNumberOfBytes,
												__out PULONG_PTR lpCompletionKey, __out LPOVERLAPPED* lpOverlapped,
												__in DWORD dwMilliseconds);

IOCP_DECL BOOL WINAPI PostQueuedCompletionStatus(
  _In_     HANDLE       CompletionPort,
  _In_     DWORD        dwNumberOfBytesTransferred,
  _In_     ULONG_PTR    dwCompletionKey,
  _In_opt_ LPOVERLAPPED lpOverlapped
);

IOCP_DECL BOOL WINAPI CancelIo(
  _In_ HANDLE hFile
);

IOCP_DECL BOOL WINAPI CancelIoEx(
  _In_     HANDLE       hFile,
  _In_opt_ LPOVERLAPPED lpOverlapped
);

// WSA Socket api(Overlapped) the work with iocp
#define SOCKET_ERROR -1

typedef struct WSAData
{
	WORD wVersion;
	WORD wHighVersion;
#define WSADESCRIPTION_LEN 255
	char szDescription[WSADESCRIPTION_LEN + 1];
#define WSASYS_STATUS_LEN 255
	char szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char* lpVendorInfo;
} WSADATA, *LPWSADATA;

IOCP_DECL int WSAStartup(_In_ WORD wVersionRequested, _Out_ LPWSADATA lpWSAData);

IOCP_DECL int WSACleanup();

struct _WSAPROTOCOL_INFO;
typedef struct _WSAPROTOCOL_INFO* LPWSAPROTOCOL_INFO;

enum
{
	WSA_FLAG_OVERLAPPED = 0x01,
#define FILE_FLAG_OVERLAPPED WSA_FLAG_OVERLAPPED
	WSA_FLAG_NO_HANDLE_INHERIT = 0x80,
#define WSA_FLAG_NO_HANDLE_INHERIT WSA_FLAG_NO_HANDLE_INHERIT
};

IOCP_DECL SOCKET WSASocket(_In_ int af, _In_ int type, _In_ int protocol, _In_ LPWSAPROTOCOL_INFO lpProtocolInfo,  _In_ SOCKET g, _In_ DWORD dwFlags);

#define WSASocketA WSASocket
#define WSASocketW WSASocket

IOCP_DECL BOOL AcceptEx(_In_ SOCKET sListenSocket, _In_ SOCKET sAcceptSocket, _In_ PVOID lpOutputBuffer,
						_In_ DWORD dwReceiveDataLength, _In_ DWORD dwLocalAddressLength,
						_In_ DWORD dwRemoteAddressLength, _Out_ LPDWORD lpdwBytesReceived,
						_In_ LPOVERLAPPED lpOverlapped);

IOCP_DECL BOOL WSAConnectEx(
  __in            SOCKET s,
  __in            const sockaddr *name,
  __in            int namelen,
  _In_opt_        PVOID lpSendBuffer,
  __in            DWORD dwSendDataLength,
  __out           LPDWORD lpdwBytesSent,
  __in            LPOVERLAPPED lpOverlapped
);

IOCP_DECL void GetAcceptExSockaddrs(
  __in  PVOID    lpOutputBuffer,
  __in  DWORD    dwReceiveDataLength,
  __in  DWORD    dwLocalAddressLength,
  __in  DWORD    dwRemoteAddressLength,
  __out sockaddr **LocalSockaddr,
  __out LPINT    LocalSockaddrLength,
  __out sockaddr **RemoteSockaddr,
  __out LPINT    RemoteSockaddrLength
);

typedef struct __WSABUF
{
	size_t len;
	char* buf;
} WSABUF, *LPWSABUF;

typedef void (*LPOVERLAPPED_COMPLETION_ROUTINE)(
  __in  DWORD dwErrorCode,
  __in  DWORD dwNumberOfBytesTransfered,
  _Inout_  LPOVERLAPPED lpOverlapped
);

typedef LPOVERLAPPED_COMPLETION_ROUTINE LPWSAOVERLAPPED_COMPLETION_ROUTINE;

IOCP_DECL int WSASend(_In_ SOCKET s, _In_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesSent, _In_ DWORD dwFlags, _In_ LPWSAOVERLAPPED lpOverlapped,
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

IOCP_DECL int WSARecv(_In_ SOCKET s, _Inout_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesRecvd, _Inout_ LPDWORD lpFlags,
					  _In_ LPWSAOVERLAPPED lpOverlapped,                          // must not NULL
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine // must be NULL
);

IOCP_DECL int WSASendTo(
    __in    SOCKET                             s,
    __in    LPWSABUF                           lpBuffers,
    __in    DWORD                              dwBufferCount,
    __out   LPDWORD                            lpNumberOfBytesSent,
    __in    DWORD                              dwFlags,
    __in    const sockaddr                     *lpTo,
    __in    int                                iTolen,
    __in    LPWSAOVERLAPPED                    lpOverlapped,
    __in    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
);

IOCP_DECL int WSARecvFrom(
    __in    SOCKET                             s,
    __in    LPWSABUF                           lpBuffers,
    __in    DWORD                              dwBufferCount,
    __out   LPDWORD                            lpNumberOfBytesRecvd,
    __in    LPDWORD                            lpFlags,
    __out   sockaddr                           *lpFrom,
    __in    LPINT                              lpFromlen,
    __in    LPWSAOVERLAPPED                    lpOverlapped,
    __in    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
);

enum {
	FD_CONNECT = 0x1,
	FD_ACCEPT = 0x2,
	FD_READ = 0x4,
	FD_WRITE = 0x8,

	FD_MAX_EVENTS = 5,
};


IOCP_DECL WSAEVENT WSACreateEvent();

IOCP_DECL int WSAEventSelect(
  _In_ SOCKET   s,
  _In_ WSAEVENT hEventObject,
  _In_ long     lNetworkEvents
);

IOCP_DECL DWORD WSAWaitForMultipleEvents(
  __in DWORD          cEvents,
  __in const WSAEVENT *lphEvents,
  __in BOOL           fWaitAll,
  __in DWORD          dwTimeout,
  __in BOOL           fAlertable
);

typedef struct _WSANETWORKEVENTS {
  long lNetworkEvents;
  int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

IOCP_DECL int WSAEnumNetworkEvents(
  __in  SOCKET             s,
  __in  WSAEVENT           hEventObject,
  __out LPWSANETWORKEVENTS lpNetworkEvents
);

enum
{
	WSA_WAIT_FAILED = -2,
	WSA_WAIT_TIMEOUT = -1,
	WSA_WAIT_EVENT_0 = 0,
	ERROR_HANDLE_EOF = 0x26,
	ERROR_BUSY = 0xAA,
	ERROR_IO_PENDING = 0xF1,
	ERROR_WAIT_TIMEOUT = 0x102,

};

#define WSA_IO_PENDING ERROR_IO_PENDING

#define WSA_INFINITE 0xFFFFFFFF

IOCP_DECL int SOCKET_get_fd(SOCKET);

IOCP_DECL int closesocket(SOCKET);

int bind(SOCKET, SOCKADDR_IN*, int);
int bind(SOCKET, SOCKADDR*, int);

int listen(SOCKET, int);
int setsockopt(SOCKET, int __level, int __optname, const void *__optval, socklen_t __optlen);

#define ZeroMemory(a, b) memset(a, 0, b)
#define GetLastError() errno

#define memcpy_s(a, b ,c ,d ) memcpy(a, c, d)

IOCP_DECL DWORD WSAGetLastError();
IOCP_DECL DWORD WSASetLastError(DWORD);

inline WORD MAKEWORD(uint a, uint b)
{
	return (a << 8) + b;
}

#define CONTAINING_RECORD(address,type,field) \
	((type*)((char*)(address)-(ULONG_PTR)(&((type*)0)->field)))


typedef struct _SECURITY_ATTRIBUTES {
  DWORD  nLength;
  LPVOID lpSecurityDescriptor;
  BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

enum {
	GENERIC_READ = 1,
	GENERIC_WRITE = 2,
	GENERIC_EXECUTE = 4,
};

enum {
	FILE_SHARE_READ = 1,
	FILE_SHARE_WRITE = 2,
	FILE_SHARE_DELETE = 4,
};

enum {
	CREATE_NEW = 1,
	CREATE_ALWAYS = 2,
	OPEN_EXISTING = 3,
	OPEN_ALWAYS = 4,
	TRUNCATE_EXISTING = 5
};

IOCP_DECL HANDLE CreateFileA(
  __in            LPCSTR                lpFileName,
  __in            DWORD                 dwDesiredAccess,
  __in            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in            DWORD                 dwCreationDisposition,
  __in            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile
);

IOCP_DECL HANDLE CreateFileW(
  __in            LPCWSTR               lpFileName,
  __in            DWORD                 dwDesiredAccess,
  __in            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in            DWORD                 dwCreationDisposition,
  __in            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile
);


#define CreateFile CreateFileA

IOCP_DECL BOOL ReadFile(
  __in                HANDLE       hFile,
  __out               LPVOID       lpBuffer,
  __in                DWORD        nNumberOfBytesToRead,
  _Out_opt_	          LPDWORD      lpNumberOfBytesRead,
  _Inout_             LPOVERLAPPED lpOverlapped
);

IOCP_DECL BOOL WriteFile(
  __in                 HANDLE       hFile,
  __in                 LPCVOID      lpBuffer,
  __in                 DWORD        nNumberOfBytesToWrite,
  _Out_opt_            LPDWORD      lpNumberOfBytesWritten,
  _Inout_              LPOVERLAPPED lpOverlapped
);


#endif //__IOCP__H__
