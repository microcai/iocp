
#pragma once


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#ifndef SOCKET_get_fd
#define SOCKET_get_fd(x) (x)
#endif
#else
#include "iocp.h"
#endif

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
inline void process_overlapped_event(OVERLAPPED* _ov, DWORD NumberOfBytes, DWORD last_error)
{
	iocp_callback* ovl_res = (iocp_callback*)(_ov);

    (*ovl_res)(last_error, NumberOfBytes);
}

inline void run_event_loop(HANDLE iocp_handle)
{
	for (;;)
	{
		DWORD NumberOfBytes = 0;
		ULONG_PTR ipCompletionKey = 0;
		LPOVERLAPPED ipOverlap = NULL;

		// get IO status, no wait
		SetLastError(0);
		BOOL ok = GetQueuedCompletionStatus(iocp_handle, &NumberOfBytes, (PULONG_PTR)&ipCompletionKey, &ipOverlap, INFINITE);
		DWORD last_error = GetLastError();

		if (ipOverlap)
		{
			process_overlapped_event(ipOverlap, NumberOfBytes, last_error);
		}
	}
}
