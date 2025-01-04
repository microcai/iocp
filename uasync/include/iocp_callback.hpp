
#pragma once

#include "extensable_iocp.hpp"

using iocp::run_event_loop;
using iocp::exit_event_loop_when_empty;

#ifdef _WIN32
using iocp::WSAConnectEx;
using iocp::DisconnectEx;
using iocp::init_winsock_api_pointer;
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
inline void process_callback_overlapped_event(const OVERLAPPED_ENTRY* _ov, DWORD last_error)
{
	iocp_callback* ovl_res = (iocp_callback*)(_ov->lpOverlapped);

    (*ovl_res)(last_error, _ov->dwNumberOfBytesTransferred);
}

inline void bind_callback_iocp(HANDLE file, HANDLE iocp_handle, DWORD = 0, DWORD = 0)
{
    CreateIoCompletionPort(file, iocp_handle, (ULONG_PTR) (iocp::overlapped_proc_func) &process_callback_overlapped_event, 0);
}
