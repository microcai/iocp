
#pragma once

#include "iocp.h"

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
