
#pragma once

#include "iocp.h"

#include <asio.hpp>

struct asio_operation
{
	OVERLAPPED* overlapped_ptr;
	ULONG_PTR CompletionKey;
	DWORD NumberOfBytes;
	DWORD last_error;
	LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine = nullptr;

	asio_operation()
		: overlapped_ptr(0)
		, CompletionKey(0)
		, NumberOfBytes(0)
		, last_error(0)
	{}

	asio_operation(OVERLAPPED* ov, ULONG_PTR k, DWORD b, DWORD e = 0)
		: overlapped_ptr(ov)
		, CompletionKey(k)
		, NumberOfBytes(b)
		, last_error(e)
	{}

	asio::cancellation_signal cancel_signal;
};

using asio_operation_ptr = std::unique_ptr<asio_operation>;
