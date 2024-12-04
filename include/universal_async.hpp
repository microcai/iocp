
#pragma once

#include "awaitable.hpp"


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

struct awaitable_overlapped : public OVERLAPPED
{
    std::mutex m;
    DWORD NumberOfBytes;
    std::coroutine_handle<> coro_handle;
    bool completed;

    awaitable_overlapped()
    {
        memset(static_cast<OVERLAPPED*>(this), 0, sizeof (OVERLAPPED));
        NumberOfBytes = 0;
        coro_handle = nullptr;
        completed = false;
    }
};

struct OverlappedAwaiter
{
    OverlappedAwaiter(const OverlappedAwaiter&) = delete;
    OverlappedAwaiter& operator = (const OverlappedAwaiter&) = delete;
    awaitable_overlapped& ov;
public:
    OverlappedAwaiter(OverlappedAwaiter&&) = default;

    explicit OverlappedAwaiter(awaitable_overlapped& ov)
        : ov(ov)
    {
    }

    bool await_ready() noexcept
    {
        std::scoped_lock<std::mutex> l(ov.m);
        return ov.completed;
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        std::scoped_lock<std::mutex> l(ov.m);
        ov.coro_handle = handle;
    }

    DWORD await_resume()
    {
        std::scoped_lock<std::mutex> l(ov.m);
        ov.completed = false;
        ov.coro_handle = nullptr;
        return ov.NumberOfBytes;
    }
};

// wait for overlapped to became complete. return NumberOfBytes
inline ucoro::awaitable<DWORD> wait_overlapped(awaitable_overlapped& ov)
{
    co_return co_await OverlappedAwaiter{ov};
}

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov, DWORD NumberOfBytes)
{
    auto ov = reinterpret_cast<awaitable_overlapped*>(_ov);

    ov->m.lock();

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
    for (;;)
    {
        DWORD NumberOfBytes;
        ULONG_PTR ipCompletionKey;
        LPOVERLAPPED ipOverlap = nullptr;
        // get IO status
        auto  result = GetQueuedCompletionStatus(
                    iocp_handle,
                    &NumberOfBytes,
                    (PULONG_PTR)&ipCompletionKey,
                    &ipOverlap,
                    INFINITE);
        if (result == 0)
        {
            auto err = GetLastError();
            continue;
        }

        if (ipOverlap)
            process_overlapped_event(ipOverlap, NumberOfBytes);
    }
}
