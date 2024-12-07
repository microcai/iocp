﻿
#pragma once

#include "awaitable.hpp"
#include <cstdint>
#include <memory_resource>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include "iocp.h"
#endif

#include <mutex>
#include <optional>

struct dummy_mutex
{
	constexpr void lock() noexcept
	{
	}

	constexpr void unlock() noexcept
	{
	}
};

template <typename MUTEX>
struct counter_trait
{
    using type = std::atomic_int;
};

template <>
struct counter_trait<dummy_mutex>
{
    using type = long;
};

template <typename MUTEX>
struct basic_awaitable_overlapped : public OVERLAPPED
{
    MUTEX m;
    DWORD NumberOfBytes;
    DWORD last_error;
    std::coroutine_handle<> coro_handle;
    bool completed;

    static typename counter_trait<MUTEX>::type out_standing;

    void reset()
    {
        this->Internal = this->InternalHigh = 0;
        this->hEvent = NULL;
        NumberOfBytes = 0;
        coro_handle = nullptr;
        completed = false;
    }

    void set_offset(uint64_t offset)
    {
        this->Offset = offset & 0xFFFFFFFF;
        this->OffsetHigh = (offset >> 32);
    }

    void add_offset(uint64_t offset)
    {
        uint64_t cur_offset = this->OffsetHigh;
        cur_offset <<= 32;
        cur_offset += this->Offset;
        cur_offset += offset;
        Offset = cur_offset & 0xFFFFFFFF;
        OffsetHigh = (cur_offset >> 32);
    }

    basic_awaitable_overlapped()
    {
        Offset = OffsetHigh = 0xFFFFFFFF;
        reset();
    }
};

#ifdef DISABLE_THREADS
using awaitable_overlapped = basic_awaitable_overlapped<dummy_mutex>;
#else
using awaitable_overlapped = basic_awaitable_overlapped<std::mutex>;
#endif

template <typename MUTEX>
inline typename counter_trait<MUTEX>::type basic_awaitable_overlapped<MUTEX>::out_standing = 0;

template <typename MUTEX>
struct BasicOverlappedAwaiter
{
    BasicOverlappedAwaiter(const BasicOverlappedAwaiter&) = delete;
    BasicOverlappedAwaiter& operator = (const BasicOverlappedAwaiter&) = delete;
    basic_awaitable_overlapped<MUTEX>& ov;
public:
    BasicOverlappedAwaiter(BasicOverlappedAwaiter&&) = default;

    explicit BasicOverlappedAwaiter(basic_awaitable_overlapped<MUTEX>& ov)
        : ov(ov)
    {
    }

    bool await_ready() noexcept
    {
        std::scoped_lock<MUTEX> l(ov.m);
        return ov.completed;
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        std::scoped_lock<MUTEX> l(ov.m);
        ov.coro_handle = handle;
        ++ awaitable_overlapped::out_standing;
    }

    DWORD await_resume()
    {
        std::scoped_lock<MUTEX> l(ov.m);
        auto NumberOfBytes = ov.NumberOfBytes;
        WSASetLastError(ov.last_error);
        ov.reset();
        -- awaitable_overlapped::out_standing;
        return NumberOfBytes;
    }
};

inline int pending_works()
{
    return awaitable_overlapped::out_standing;
}

// wait for overlapped to became complete. return NumberOfBytes
inline ucoro::awaitable<DWORD> get_overlapped_result(awaitable_overlapped& ov)
{
    co_return co_await BasicOverlappedAwaiter{ov};
}

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov, DWORD NumberOfBytes, DWORD last_error)
{
    auto ov = reinterpret_cast<awaitable_overlapped*>(_ov);

    ov->m.lock();

    ov->last_error = last_error;
    ov->NumberOfBytes = NumberOfBytes;
    ov->completed = true;

    if ( ov->coro_handle == nullptr)
    {
        ov->m.unlock();
    }
    else
    {
        ov->m.unlock();
        ov->coro_handle.resume();
    }
}

inline void run_event_loop(HANDLE iocp_handle)
{
    bool quit_if_no_work = false;

    struct OVERLAPPEDRESULT
    {
        LPOVERLAPPED op;
        DWORD NumberOfBytes;
        DWORD last_error;

        OVERLAPPEDRESULT(OVERLAPPED* ovl, DWORD b, DWORD e)
            : op(ovl)
            , NumberOfBytes(b)
            , last_error(e)
        {}
    };

    // batch size of 128
    std::vector<OVERLAPPEDRESULT> ops;
    ops.reserve(128);

    for (;;)
    {
        DWORD dwMilliseconds_to_wait = quit_if_no_work ? ( pending_works() ? 500 : 0 ) : INFINITE;

        while (ops.size() < 128)
        {
            DWORD NumberOfBytes;
            ULONG_PTR ipCompletionKey;
            LPOVERLAPPED ipOverlap = nullptr;

            // get IO status, no wait
            auto  result = GetQueuedCompletionStatus(
                        iocp_handle,
                        &NumberOfBytes,
                        (PULONG_PTR)&ipCompletionKey,
                        &ipOverlap,
                        0);
            if (ipOverlap) [[likely]]
            {
                ops.emplace_back(ipOverlap, NumberOfBytes, GetLastError());
            }
            else if (result && (ipCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
            {
              quit_if_no_work = true;
            }
            else
            {
                if (result == FALSE && GetLastError() == WSA_WAIT_TIMEOUT)
                    break;
            }
        }

        if (ops.empty())
        {
            DWORD NumberOfBytes;
            ULONG_PTR ipCompletionKey;
            LPOVERLAPPED ipOverlap = nullptr;

            // get IO status, no wait
            auto  result = GetQueuedCompletionStatus(
                        iocp_handle,
                        &NumberOfBytes,
                        (PULONG_PTR)&ipCompletionKey,
                        &ipOverlap,
                        dwMilliseconds_to_wait);

            if (ipOverlap) [[likely]]
            {
                ops.emplace_back(ipOverlap, NumberOfBytes, GetLastError());
            }
            else if (result && (ipCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
            {
                quit_if_no_work = true;
            }
            else
            {
                if (GetLastError() == WSA_WAIT_TIMEOUT)
                    break;
            }
        }
        else
        {
            for (OVERLAPPEDRESULT& op : ops)
            {
                process_overlapped_event(op.op, op.NumberOfBytes, op.last_error);
            }
            ops.clear();
        }

        if  ( quit_if_no_work) [[unlikely]]
        {
            // 检查还在投递中的 IO 操作.
            if (!pending_works())
            {
                break;
            }
        }

    }
}

// 通知 loop 如果没有进行中的 IO 操作的时候，就退出循环。
inline void exit_event_loop_when_empty(HANDLE iocp_handle)
{
    PostQueuedCompletionStatus(iocp_handle, 0, (ULONG_PTR) iocp_handle, NULL);
}

// 执行这个，可以保证 协程被 IOCP 线程调度. 特别是 一个线程一个 IOCP 的模式下特有用
inline ucoro::awaitable<void> run_on_iocp_thread(HANDLE iocp_handle)
{
    struct SwitchIOCPAwaitable : public OVERLAPPED
    {
        HANDLE iocp_handle;

        std::unique_ptr<awaitable_overlapped> coro;

        SwitchIOCPAwaitable(HANDLE iocp_handle) : iocp_handle(iocp_handle) {
            coro.reset(new awaitable_overlapped{});
        }

        constexpr bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            coro->coro_handle = handle;
            PostQueuedCompletionStatus(iocp_handle, 0, 0, coro.get());
        }

        void await_resume()
        {
        }
    };


    co_await SwitchIOCPAwaitable{iocp_handle};
}