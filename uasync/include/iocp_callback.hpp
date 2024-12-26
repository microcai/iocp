
#pragma once

#include "extensable_iocp.hpp"

#include <assert.h>
#include <tuple>
#include <functional>

struct iocp_callback : OVERLAPPED
{
	using callback_type = std::function<void(DWORD last_error, DWORD byte_transfered)>;

	callback_type cb;

	iocp_callback()
	{
		this->Offset = this->OffsetHigh = this->Internal = this->InternalHigh = 0;
		this->hEvent = 0;
	}

	template<typename Callback>
	iocp_callback(Callback&& cb)
		: cb (std::forward<Callback>(cb))
	{
		this->Offset = this->OffsetHigh = this->Internal = this->InternalHigh = 0;
		this->hEvent = 0;
	}

	void operator()(DWORD last_error, DWORD byte_transfered)
	{
		cb(last_error, byte_transfered);
		callback_type tmp;
		std::swap(tmp, cb);
	}
};


// call this after GetQueuedCompletionStatus.
inline void process_callback_overlapped_event(const OVERLAPPED_ENTRY* _ov, DWORD last_error)
{
	iocp_callback* ovl_res = (iocp_callback*)(_ov->lpOverlapped);

    (*ovl_res)(last_error, _ov->dwNumberOfBytesTransferred);
}

inline void bind_callback_iocp(HANDLE file, HANDLE iocp_handle, DWORD = 0, DWORD = 0)
{
    CreateIoCompletionPort(file, iocp_handle, (ULONG_PTR) (void*) &process_callback_overlapped_event, 0);
}

inline void run_event_loop(HANDLE iocp_handle)
{
	return iocp::run_event_loop(iocp_handle);
}

#if defined(_WIN32)
inline LPFN_CONNECTEX WSAConnectEx = nullptr;
inline LPFN_DISCONNECTEX DisconnectEx = nullptr;
inline LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockaddrs = nullptr;
inline LPFN_ACCEPTEX _AcceptEx = nullptr;

#define AcceptEx _AcceptEx
#define GetAcceptExSockaddrs _GetAcceptExSockaddrs

inline void init_winsock_api_pointer()
{
	SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	GUID disconnectex = WSAID_DISCONNECTEX;
	GUID connect_ex_guid = WSAID_CONNECTEX;
	GUID acceptex = WSAID_ACCEPTEX;
    GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD BytesReturned;

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&disconnectex, sizeof(GUID), &DisconnectEx, sizeof(DisconnectEx),
		&BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&connect_ex_guid, sizeof(GUID), &WSAConnectEx, sizeof(WSAConnectEx), &BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptex, sizeof(GUID), &_AcceptEx, sizeof(_AcceptEx),
		&BytesReturned, 0, 0);

    WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &getacceptexsockaddrs, sizeof(GUID), &_GetAcceptExSockaddrs, sizeof(_GetAcceptExSockaddrs),
        &BytesReturned, 0, 0);
	closesocket(sock);
}

#endif // defined(_WIN32)
