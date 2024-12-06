
#pragma once

#include "iocp.h"
#include <liburing.h>

typedef struct io_uring_operations
{
	LPOVERLAPPED overlapped_ptr = 0;
	ULONG_PTR CompletionKey = 0;
	std::size_t size = 0;
	LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine = nullptr;

	virtual void do_complete(io_uring_cqe* cqe, DWORD*) {};
	virtual ~io_uring_operations(){}

}* io_uring_operation_ptr;

class io_uring_operation_allocator
{
public:
	void deallocate(io_uring_operation_ptr p, std::size_t)
	{
		delete p;
	}

	template<typename OP> //requires std::is_base_of_v<io_uring_operations, OP>
	OP* allocate()
	{
		auto ret = new OP{};
		ret->size = sizeof (OP);
		return ret;
	}
};
