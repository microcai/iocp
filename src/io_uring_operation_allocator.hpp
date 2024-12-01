
#pragma once

#include "iocp.h"

class io_uring_operation_allocator
{
public:
	void deallocate(io_uring_operation_ptr p, std::size_t)
	{
		::free(p);
	}

	io_uring_operation_ptr allocate(std::size_t s)
	{
		auto ret = static_cast<io_uring_operation_ptr>(malloc(s));
		ret->size = s;
		return ret;
	}

	template<typename OP> //requires std::is_base_of_v<io_uring_operations, OP>
	OP* allocate()
	{
		return new (static_cast<OP*>(allocate(sizeof(OP)))) OP{};
	}
};
