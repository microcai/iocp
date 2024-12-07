
#pragma once

#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <iostream>
#include <deque>
#include <boost/lockfree/queue.hpp>

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

	void unref();

	int native_handle() {return _socket_fd; }

};

namespace {


struct nullable_mutex
{
	std::optional<std::mutex> m;

	void lock()
	{
		if (m)
			m->lock();
	}

	void unlock()
	{
		if (m)
			m->unlock();
	}

	nullable_mutex(int thread_hint)
	{
		if (thread_hint != 1)
			m.emplace();
	}

};

template <typename T>
struct concurrent_pool
{
	concurrent_pool(T* pool, int size)
	{}

	T* take();
	T  giveback();

	std::deque<T> free_items;

};

struct wrapped_io_uring : public io_uring
{
	std::mutex mutex;
};

template<typename T>
concept prepare_has_two_arg = requires(T t, io_uring_sqe* sqe, wrapped_io_uring* ring)
{
	t(sqe, ring);
};

struct iocp_handle_emu_class final : public base_handle
{
	std::mutex mutex;
	std::deque<wrapped_io_uring> rings_;

	boost::lockfree::queue<wrapped_io_uring*> submit_queue;

	template<typename T>
	struct auto_push
	{
		boost::lockfree::queue<T*>& lock_free_queue;
		T* to_be_push;
		~auto_push()
		{
			while(!lock_free_queue.push(to_be_push))
				std::this_thread::yield();
		}

		auto_push(boost::lockfree::queue<T*>& lock_free_queue, T* to_be_push)
			: lock_free_queue(lock_free_queue)
			, to_be_push(to_be_push)
		{}
	};

	iocp_handle_emu_class(int NumberOfConcurrentThreads)
		: submit_queue(NumberOfConcurrentThreads)
	{
		submit_queue.push(create_new_ring());
	}

	~iocp_handle_emu_class()
	{
		for (auto & ring_ : rings_)
		{
			::io_uring_queue_exit(&ring_);
		}
	}

	wrapped_io_uring* create_new_ring()
	{
		std::scoped_lock<std::mutex> l(mutex);

		io_uring_params params = {0};
		params.flags = IORING_SETUP_CQSIZE|IORING_SETUP_SUBMIT_ALL|IORING_SETUP_TASKRUN_FLAG|IORING_SETUP_COOP_TASKRUN;

		params.cq_entries = 65536;
		params.sq_entries = 128;
		// params.features = IORING_FEAT_EXT_ARG;
		params.features = IORING_FEAT_NODROP | IORING_FEAT_EXT_ARG | IORING_FEAT_FAST_POLL | IORING_FEAT_RW_CUR_POS |IORING_FEAT_CUR_PERSONALITY;

		rings_.emplace_back();

		auto result = ::io_uring_queue_init_params(128, & rings_.back(), &params);
		if (result < 0)
		{
			rings_.pop_back();
			throw std::bad_alloc{};
		}

		return  & rings_.back();
	}

	template<typename PrepareOP> auto __submit_io(PrepareOP&& preparer, wrapped_io_uring* ring_)
	{
		std::scoped_lock<std::mutex> l(ring_->mutex);

		io_uring_sqe * sqe = io_uring_get_sqe(ring_);
		while (!sqe)
		{
			io_uring_submit(ring_);
			sqe = io_uring_get_sqe(ring_);
		}
		if constexpr (prepare_has_two_arg<PrepareOP>)
		{
			preparer(sqe, ring_);
		}
		else
			preparer(sqe);
		return io_uring_submit(ring_);
	}

	template<typename PrepareOP> auto submit_io(PrepareOP&& preparer, wrapped_io_uring* suggest_ring)
	{
		if (suggest_ring == nullptr)
		{
			// peak a ring, and then post IO there.
			wrapped_io_uring* ring_ = nullptr;
			while(!submit_queue.pop(ring_))
			{
				std::this_thread::yield();
			}

			auto_push<wrapped_io_uring> _auto_push(submit_queue, ring_);

			return __submit_io(std::forward<PrepareOP>(preparer), ring_);
		}
		else
		{
			return __submit_io(std::forward<PrepareOP>(preparer), suggest_ring);
		}
	}

	template<typename PrepareOP> auto submit_io(PrepareOP&& preparer)
	{
		// peak a ring, and then post IO there.
		wrapped_io_uring* ring_ = nullptr;
		while(!submit_queue.pop(ring_))
		{
			std::this_thread::yield();
		}

		auto_push<wrapped_io_uring> _auto_push(submit_queue, ring_);

		return __submit_io(std::forward<PrepareOP>(preparer), ring_);
	}
};

struct SOCKET_emu_class final : public base_handle
{
	iocp_handle_emu_class* _iocp;
	ULONG_PTR _completion_key;
	wrapped_io_uring* binded_ring = nullptr;

	SOCKET_emu_class(int fd, iocp_handle_emu_class* iocp = nullptr)
	 	: base_handle(fd)
		, _iocp(iocp)
		, _completion_key(0)
	{
	}

	virtual ~SOCKET_emu_class() override;
};

}