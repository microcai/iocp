
#pragma once

#include "awaitable.hpp"


#ifdef _WIN32
#include <windows.h>

#else

#include "iocp.h"

#endif


#include <mutex>

struct awaitable_overlapped : public OVERLAPPED
{
    std::mutex m;
    DWORD NumberOfBytes = 0;
    std::coroutine_handle<> coro_handle = nullptr;
    bool completed = false;
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

    if ( ov->coro_handle )
    {
        ov->m.unlock();
        ov->coro_handle.resume();
    }
    else
    {
        ov->m.unlock();
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
            continue;

        if (ipOverlap)
            process_overlapped_event(ipOverlap, NumberOfBytes);
    }
}
