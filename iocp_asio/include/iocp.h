
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

#define WINAPI

#ifndef _In_
#define _In_
#endif

#ifndef _In_opt_
#define _In_opt_
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
#define IOCP_DECL extern
#endif
typedef char CHAR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const wchar_t* LPCWSTR;

typedef int* LPINT;
typedef unsigned long ULONG_PTR, *PULONG_PTR, ULONG, *PULONG;
typedef uint16_t WORD, *LPWORD;
typedef uint64_t DWORD_PTR;
typedef uint32_t DWORD, *LPDWORD;
typedef int32_t LONG, *PLONG;
typedef int64_t LONGLONG;

typedef bool BOOL; // bool is from stdbool if C99 mode.
typedef void VOID, *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef uint16_t TCHAR;
typedef struct sockaddr SOCKADDR, * LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6;
typedef unsigned int GROUP;

struct base_handle;
typedef struct base_handle * HANDLE, * SOCKET, * WSAEVENT;

const HANDLE INVALID_HANDLE_VALUE = (HANDLE)-1;
const SOCKET INVALID_SOCKET = (SOCKET)-1;

#define INFINITE -1

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  uint64_t QuadPart;
} ULARGE_INTEGER;

typedef struct _OVERLAPPED
{
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	union {
		struct
		{
#if __BYTE_ORDER == __LITTLE_ENDIAN
			DWORD Offset;
			DWORD OffsetHigh;
#else
			DWORD OffsetHigh;
			DWORD Offset;
#endif
		};
		PVOID Pointer;
    // this is not part of WIN32 abi.
    // but just internal quick usage.
    uint64_t offset_64;
	};
	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED, WSAOVERLAPPED, *LPWSAOVERLAPPED;

typedef struct _OVERLAPPED_ENTRY {
  ULONG_PTR    lpCompletionKey;
  LPOVERLAPPED lpOverlapped;
  ULONG_PTR    Internal;
  DWORD        dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;

IOCP_DECL BOOL WINAPI CloseHandle(_In_ HANDLE);

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(_In_ HANDLE FileHandle, _In_ HANDLE ExistingCompletionPort,
											   _In_ ULONG_PTR CompletionKey, _In_ DWORD NumberOfConcurrentThreads

);

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(_In_ HANDLE CompletionPort, _Out_ LPDWORD lpNumberOfBytes,
												_Out_ PULONG_PTR lpCompletionKey, _Out_ LPOVERLAPPED* lpOverlapped,
												_In_ DWORD dwMilliseconds);

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatusEx(
  _In_  HANDLE             CompletionPort,
  _Out_ LPOVERLAPPED_ENTRY lpCompletionPortEntries,
  _In_  ULONG              ulCount,
  _Out_ PULONG             ulNumEntriesRemoved,
  _In_  DWORD              dwMilliseconds,
  _In_  BOOL               fAlertable
);

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
#define NO_ERROR 0
#define INVALID_SET_FILE_POINTER EINVAL

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


typedef struct __WSABUF
{
	size_t len;
	char* buf;
} WSABUF, *LPWSABUF;

typedef enum _SERVICETYPE {
  SERVICETYPE_NOTRAFFIC,
  SERVICETYPE_BESTEFFORT,
  SERVICETYPE_CONTROLLEDLOAD,
  SERVICETYPE_GUARANTEED,
  SERVICETYPE_QUALITATIVE,
  SERVICETYPE_NETWORK_UNAVAILBLE,
  SERVICETYPE_NETWORK_CONTROL,
  SERVICETYPE_GENERAL_INFORMATION,
  SERVICETYPE_NOCHANGE,
  SERVICETYPE_NONCONFORMING,
  SERVICE_NO_TRAFFIC_CONTROL,
  SERVICE_NO_QOS_SIGNALING,

}SERVICETYPE;

typedef struct _flowspec {
  ULONG       TokenRate;
  ULONG       TokenBucketSize;
  ULONG       PeakBandwidth;
  ULONG       Latency;
  ULONG       DelayVariation;
  SERVICETYPE ServiceType;
  ULONG       MaxSduSize;
  ULONG       MinimumPolicedSize;
} FLOWSPEC, *PFLOWSPEC, *LPFLOWSPEC;

typedef struct _QualityOfService {
  FLOWSPEC SendingFlowspec;
  FLOWSPEC ReceivingFlowspec;
  WSABUF   ProviderSpecific;
} QOS, *LPQOS;

typedef void (*LPOVERLAPPED_COMPLETION_ROUTINE)(
  _In_  DWORD dwErrorCode,
  _In_  DWORD dwNumberOfBytesTransfered,
  _Inout_  LPOVERLAPPED lpOverlapped
);

typedef int (*LPCONDITIONPROC)(
  _In_     LPWSABUF    lpCallerId,
  _In_     LPWSABUF    lpCallerData,
  _Inout_  LPQOS       lpSQOS,
  _Inout_  LPQOS       lpGQOS,
  _In_     LPWSABUF    lpCalleeId,
  _In_     LPWSABUF    lpCalleeData,
  _Out_    GROUP * g,
  _In_     DWORD_PTR   dwCallbackData
);

typedef LPOVERLAPPED_COMPLETION_ROUTINE LPWSAOVERLAPPED_COMPLETION_ROUTINE;

IOCP_DECL int WSAStartup(_In_ WORD wVersionRequested, _Out_ LPWSADATA lpWSAData);

IOCP_DECL int WSACleanup();

struct _WSAPROTOCOL_INFO;
typedef struct _WSAPROTOCOL_INFO* LPWSAPROTOCOL_INFO;

enum
{
	WSA_FLAG_OVERLAPPED = 0x01,
  FILE_FLAG_NO_BUFFERING = 0x02,
  FILE_FLAG_SEQUENTIAL_SCAN = 0x4,
#define FILE_FLAG_OVERLAPPED WSA_FLAG_OVERLAPPED
	WSA_FLAG_NO_HANDLE_INHERIT = 0x80,
#define WSA_FLAG_NO_HANDLE_INHERIT WSA_FLAG_NO_HANDLE_INHERIT

  WSA_FLAG_FAKE_CREATION = 2147483649,
};

IOCP_DECL SOCKET WSASocket(_In_ int af, _In_ int type, _In_ int protocol, _In_ LPWSAPROTOCOL_INFO lpProtocolInfo,  _In_ SOCKET g, _In_ DWORD dwFlags);

#define WSASocketA WSASocket
#define WSASocketW WSASocket

#define socket(af, ty, protocal) WSASocket(af, ty, protocal, 0, 0, FILE_FLAG_OVERLAPPED)

IOCP_DECL SOCKET WSAAccept(
  _In_        SOCKET          s,
  _Out_       struct sockaddr        *addr,
  _Inout_     LPINT           addrlen,
  _In_        LPCONDITIONPROC lpfnCondition,
  _In_        DWORD_PTR       dwCallbackData
);

IOCP_DECL BOOL AcceptEx(_In_ SOCKET sListenSocket, _In_ SOCKET sAcceptSocket, _In_ PVOID lpOutputBuffer,
						_In_ DWORD dwReceiveDataLength, _In_ DWORD dwLocalAddressLength,
						_In_ DWORD dwRemoteAddressLength, _Out_ LPDWORD lpdwBytesReceived,
						_In_ LPOVERLAPPED lpOverlapped);

IOCP_DECL BOOL WSAConnectEx(
  _In_            SOCKET s,
  _In_            const struct sockaddr *name,
  _In_            int namelen,
  _In_opt_        PVOID lpSendBuffer,
  _In_            DWORD dwSendDataLength,
  _Out_           LPDWORD lpdwBytesSent,
  _In_            LPOVERLAPPED lpOverlapped
);

IOCP_DECL void GetAcceptExSockaddrs(
  _In_  PVOID      lpOutputBuffer,
  _In_  DWORD      dwReceiveDataLength,
  _In_  DWORD      dwLocalAddressLength,
  _In_  DWORD      dwRemoteAddressLength,
  _Out_ struct sockaddr** LocalSockaddr,
  _Out_ socklen_t* LocalSockaddrLength,
  _Out_ struct sockaddr** RemoteSockaddr,
  _Out_ socklen_t* RemoteSockaddrLength
);

IOCP_DECL int WSASend(_In_ SOCKET s, _In_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesSent, _In_ DWORD dwFlags, _In_ LPWSAOVERLAPPED lpOverlapped,
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

IOCP_DECL int WSARecv(_In_ SOCKET s, _Inout_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesRecvd, _Inout_ LPDWORD lpFlags,
					  _In_ LPWSAOVERLAPPED lpOverlapped,                          // must not NULL
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine // must be NULL
);

IOCP_DECL int WSASendTo(
  _In_    SOCKET                             s,
  _In_    LPWSABUF                           lpBuffers,
  _In_    DWORD                              dwBufferCount,
  _Out_   LPDWORD                            lpNumberOfBytesSent,
  _In_    DWORD                              dwFlags,
  _In_    const struct sockaddr                     *lpTo,
  _In_    int                                iTolen,
  _In_    LPWSAOVERLAPPED                    lpOverlapped,
  _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
);

IOCP_DECL int WSARecvFrom(
  _In_    SOCKET                             s,
  _In_    LPWSABUF                           lpBuffers,
  _In_    DWORD                              dwBufferCount,
  _Out_   LPDWORD                            lpNumberOfBytesRecvd,
  _In_    LPDWORD                            lpFlags,
  _Out_   struct sockaddr                   *lpFrom,
  _In_    LPINT                              lpFromlen,
  _In_    LPWSAOVERLAPPED                    lpOverlapped,
  _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
);

IOCP_DECL BOOL DisconnectEx(
  _In_ SOCKET       hSocket,
  _In_ LPOVERLAPPED lpOverlapped,
  _In_ DWORD        dwFlags,
  _In_ DWORD        reserved
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
  _In_ DWORD          cEvents,
  _In_ const WSAEVENT *lphEvents,
  _In_ BOOL           fWaitAll,
  _In_ DWORD          dwTimeout,
  _In_ BOOL           fAlertable
);

typedef struct _WSANETWORKEVENTS {
  long lNetworkEvents;
  int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

IOCP_DECL int WSAEnumNetworkEvents(
  _In_  SOCKET             s,
  _In_  WSAEVENT           hEventObject,
  _Out_ LPWSANETWORKEVENTS lpNetworkEvents
);

enum
{
	WSA_WAIT_FAILED = -2,
	WSA_WAIT_TIMEOUT = ETIMEDOUT,
	WSA_WAIT_EVENT_0 = 0,

  ERROR_OPERATION_ABORTED = ECANCELED,
	ERROR_HANDLE_EOF = 0x26,
  ERROR_NETNAME_DELETED = ECANCELED,

	ERROR_BUSY = 0xAA,
	ERROR_IO_PENDING = 997,
  ERROR_MORE_DATA = 998,

  WSAEINTR = EINTR,
  WSAEWOULDBLOCK = EWOULDBLOCK,
  WSAEMFILE = EMFILE,

  WSA_OPERATION_ABORTED = ECANCELED,
  WSA_IO_PENDING = ERROR_IO_PENDING,

  WSAEINPROGRESS = EINPROGRESS,
  WSAEALREADY = EALREADY,
  WSAEMSGSIZE = EMSGSIZE,
  WSAECANCELLED = ECANCELED,

  WSAENETDOWN = ENETDOWN,
  WSAENETUNREACH = ENETUNREACH,
  WSAENETRESET = ENETRESET,
  WSAECONNABORTED = ECONNABORTED,
  WSAECONNRESET = ECONNRESET,
#define ERROR_CONNRESET WSAECONNRESET
  WSAENOBUFS = ENOBUFS,
  WSAEOPNOTSUPP = EOPNOTSUPP,
  WSAEADDRINUSE = EADDRINUSE,
  WSAEADDRNOTAVAIL = EADDRNOTAVAIL,
  WSAESHUTDOWN = ESHUTDOWN,
  WSAETOOMANYREFS = ETOOMANYREFS,
  WSAETIMEDOUT = ETIMEDOUT,
  WSAECONNREFUSED = ECONNREFUSED,
  WSAENAMETOOLONG = ENAMETOOLONG,
  WSAEHOSTDOWN = EHOSTDOWN,
  WSAEHOSTUNREACH = EHOSTUNREACH,

  WSAHOST_NOT_FOUND = ENOENT,

  WSATRY_AGAIN = EAGAIN,
};


#define WSA_INFINITE 0xFFFFFFFF

IOCP_DECL int SOCKET_get_fd(SOCKET);

IOCP_DECL int closesocket(SOCKET);

#ifdef __cplusplus

int bind(SOCKET, SOCKADDR_IN*, int);
int bind(SOCKET, SOCKADDR*, int);

int listen(SOCKET, int);
int setsockopt(SOCKET, int __level, int __optname, const void *__optval, socklen_t __optlen);

#endif

#define ZeroMemory(a, b) memset(a, 0, b)

#define memcpy_s(a, b ,c ,d ) memcpy(a, c, d)

IOCP_DECL DWORD WSAGetLastError();
IOCP_DECL DWORD WSASetLastError(DWORD);

#define GetLastError() WSAGetLastError()
#define SetLastError(e) WSASetLastError(e)

IOCP_DECL WORD MAKEWORD(uint a, uint b);

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
  _In_            LPCSTR                lpFileName,
  _In_            DWORD                 dwDesiredAccess,
  _In_            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  _In_            DWORD                 dwCreationDisposition,
  _In_            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile
);

IOCP_DECL HANDLE CreateFileW(
  _In_            LPCWSTR               lpFileName,
  _In_            DWORD                 dwDesiredAccess,
  _In_            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  _In_            DWORD                 dwCreationDisposition,
  _In_            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile
);

#define CreateFile CreateFileA

IOCP_DECL DWORD GetFileSize(
  _In_            HANDLE  hFile,
  _Out_opt_ LPDWORD lpFileSizeHigh
);

#define FILE_BEGIN SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END SEEK_END

IOCP_DECL DWORD SetFilePointer(
  _In_                 HANDLE hFile,
  _In_                 LONG   lDistanceToMove,
  _Inout_              PLONG  lpDistanceToMoveHigh,
  _In_                 DWORD  dwMoveMethod
);

IOCP_DECL BOOL SetEndOfFile(
  _In_ HANDLE hFile
);

IOCP_DECL BOOL ReadFile(
  _In_                HANDLE       hFile,
  _Out_               LPVOID       lpBuffer,
  _In_                DWORD        nNumberOfBytesToRead,
  _Out_opt_	          LPDWORD      lpNumberOfBytesRead,
  _Inout_             LPOVERLAPPED lpOverlapped
);

IOCP_DECL BOOL ReadFileEx(
  _In_                HANDLE       hFile,
  _Out_               LPVOID       lpBuffer,
  _In_                DWORD        nNumberOfBytesToRead,
  _Inout_             LPOVERLAPPED lpOverlapped,
  _In_                LPOVERLAPPED_COMPLETION_ROUTINE callback
);


IOCP_DECL BOOL WriteFile(
  _In_                 HANDLE       hFile,
  _In_                 LPCVOID      lpBuffer,
  _In_                 DWORD        nNumberOfBytesToWrite,
  _Out_opt_            LPDWORD      lpNumberOfBytesWritten,
  _Inout_              LPOVERLAPPED lpOverlapped
);


#endif //__IOCP__H__
