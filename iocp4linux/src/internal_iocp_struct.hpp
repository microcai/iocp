
#pragma once

#include <mutex>
#include <optional>
#include <thread>
#include <iostream>

#include <liburing.h>

#include "iocp.h"

struct base_handle
{
	int _socket_fd;
	int ref_count;

	base_handle()
		: ref_count(1)
		, _socket_fd(-1)
	{}

	base_handle(int fd)
		: ref_count(1)
		, _socket_fd(fd)
	{}

	virtual ~base_handle() {}

	void ref(){ ref_count++;}

	void unref()
	{
		if (--ref_count == 0)
		{
			delete this;
		}
	}

	int native_handle() {return _socket_fd; }

};

namespace {


struct nullable_mutex
{
	std::optional<std::mutex> m;

	void lock()
	{
		// if (m)
			m->lock();
	}

	void unlock()
	{
		// if (m)
			m->unlock();
	}

	nullable_mutex(int thread_hint)
	{
		// if (thread_hint != 1)
			m.emplace();
	}

};

struct iocp_handle_emu_class final : public base_handle
{
	::io_uring ring_ = { 0 };
	mutable std::mutex submit_mutex;
	mutable nullable_mutex wait_mutex;

	iocp_handle_emu_class(int NumberOfConcurrentThreads)
		: wait_mutex(NumberOfConcurrentThreads)
	{
		ring_.ring_fd = -1;
	}

	~iocp_handle_emu_class()
	{
		if (ring_.ring_fd != -1)
			io_uring_queue_exit(&ring_);
	}

	template<typename PrepareOP> auto submit_io(PrepareOP&& preparer)
	{
		std::scoped_lock<std::mutex> l(submit_mutex);
		io_uring_sqe * sqe = io_uring_get_sqe(&ring_);
		while (!sqe)
		{
			io_uring_submit(&ring_);
			sqe = io_uring_get_sqe(&ring_);
		}
		preparer(sqe);
		// return io_uring_submit(&ring_);
	}

	template<typename PrepareOP> auto submit_io_immediatly(PrepareOP&& preparer)
	{
		std::scoped_lock<std::mutex> l(submit_mutex);
		io_uring_sqe * sqe = io_uring_get_sqe(&ring_);
		while (!sqe)
		{
			io_uring_submit(&ring_);
			sqe = io_uring_get_sqe(&ring_);
		}
		preparer(sqe);
		return io_uring_submit(&ring_);
	}
};

struct SOCKET_emu_class final : public base_handle
{
	iocp_handle_emu_class* _iocp;
	ULONG_PTR _completion_key;

	SOCKET_emu_class(int fd, iocp_handle_emu_class* iocp = nullptr)
	 	: base_handle(fd)
		, _iocp(iocp)
		, _completion_key(0)
	{
	}

	virtual ~SOCKET_emu_class() override
	{
		if (_iocp)
		{
			_iocp->submit_io_immediatly([this](io_uring_sqe* sqe)
			{
				io_uring_prep_close(sqe, _socket_fd);
				io_uring_sqe_set_data(sqe, nullptr);
			});
		}
		else
		{
			close(_socket_fd);
		}
	}
};

}