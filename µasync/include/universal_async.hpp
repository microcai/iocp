
#pragma once

#include "awaitable.hpp"
#include <cstdint>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#else
#include "iocp.h"
#endif


struct awaitable_overlapped
{
    OVERLAPPED ovl;
    DWORD NumberOfBytes;
    DWORD last_error;
    std::coroutine_handle<> coro_handle;
    std::atomic_flag coro_handle_set;
    std::atomic_flag ready;
    std::atomic_flag pending;

    using out_standing_t = std::atomic_long;

    static out_standing_t out_standing;

    OVERLAPPED* operator & ()
    {
        ready.clear();
        return &ovl;
    }

    const void * addr () const {
        return this;
    }

    void reset()
    {
        ovl.Internal = ovl.InternalHigh = 0;
        ovl.hEvent = NULL;
        NumberOfBytes = 0;
        coro_handle_set.clear();
        coro_handle = nullptr;
    }

    void set_offset(uint64_t offset)
    {
        ovl.Offset = offset & 0xFFFFFFFF;
        ovl.OffsetHigh = (offset >> 32);
    }

    void add_offset(uint64_t offset)
    {
        uint64_t cur_offset = ovl.OffsetHigh;
        cur_offset <<= 32;
        cur_offset += ovl.Offset;
        cur_offset += offset;
        ovl.Offset = cur_offset & 0xFFFFFFFF;
        ovl.OffsetHigh = (cur_offset >> 32);
    }

    awaitable_overlapped()
    {
        reset();
        ovl.Offset = ovl.OffsetHigh = 0xFFFFFFFF;

    }

    ~awaitable_overlapped()
    {
        assert(pending.test() == false);
    }
};

inline typename awaitable_overlapped::out_standing_t awaitable_overlapped::out_standing = 0;

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
        return ov.ready.test();
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        ov.coro_handle = handle;
        ov.coro_handle_set.notify_all();
        if (ov.coro_handle_set.test_and_set())
        {
            return false;
        }
        ov.pending.test_and_set();
        return true;
    }

    void await_resume()
    {
        ov.pending.clear();
        assert(ov.ready.test());
    }
};

inline int pending_works()
{
    return awaitable_overlapped::out_standing;
}

// wait for overlapped to became complete. return NumberOfBytes
inline ucoro::awaitable<DWORD> get_overlapped_result(awaitable_overlapped& ov)
{
    ++ awaitable_overlapped::out_standing;
    co_await OverlappedAwaiter{ov};
    -- awaitable_overlapped::out_standing;

    auto R = ov.NumberOfBytes;
    WSASetLastError(ov.last_error);
    ov.reset();
    co_return R;
}

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov, DWORD NumberOfBytes, DWORD last_error)
{
    auto ov = reinterpret_cast<awaitable_overlapped*>(_ov);

    ov->last_error = last_error;
    ov->NumberOfBytes = NumberOfBytes;

    if (ov->coro_handle_set.test_and_set())
    {
        ov->coro_handle_set.wait(false);

        // make sure
        assert(ov->coro_handle);
        if (ov->pending.test())
        {
            // printf("resume on OVERLAPED = %p\n", _ov);
            ov->ready.test_and_set();
            ov->coro_handle.resume();
        }
    }
    else
    {
        ov->ready.test_and_set();
    }
}

#if _WIN32
inline void run_event_loop(HANDLE iocp_handle)
{
    bool quit_if_no_work = false;

    for (;;)
    {
        DWORD dwMilliseconds_to_wait = quit_if_no_work ? ( pending_works() ? 500 : 0 ) : INFINITE;

        DWORD NumberOfBytes = 0;
        ULONG_PTR ipCompletionKey = 0;
        LPOVERLAPPED ipOverlap = nullptr;

        // get IO status, no wait
        ::SetLastError(0);
        auto  ok = GetQueuedCompletionStatus(
                    iocp_handle,
                    &NumberOfBytes,
                    (PULONG_PTR)&ipCompletionKey,
                    &ipOverlap,
                    dwMilliseconds_to_wait);
        DWORD last_error = ::GetLastError();

        if (ipOverlap) [[likely]]
        {
            process_overlapped_event(ipOverlap, NumberOfBytes, last_error);
        }
        else if (ok && (ipCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
        {
            quit_if_no_work = true;
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

#else

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
            DWORD NumberOfBytes = 0;
            ULONG_PTR ipCompletionKey = 0;
            LPOVERLAPPED ipOverlap = nullptr;

            // get IO status, no wait
            ::SetLastError(0);
            auto  result = GetQueuedCompletionStatus(
                        iocp_handle,
                        &NumberOfBytes,
                        (PULONG_PTR)&ipCompletionKey,
                        &ipOverlap,
                        0);
            DWORD last_error = ::GetLastError();
            if (ipOverlap) [[likely]]
            {
                ops.emplace_back(ipOverlap, NumberOfBytes, last_error);
            }
            else if (result && (ipCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
            {
              quit_if_no_work = true;
            }
            else
            {
                if (result == FALSE && last_error == WSA_WAIT_TIMEOUT)
                {
                    break;
                }
            }
        }

        if (ops.empty())
        {
            DWORD NumberOfBytes = 0;
            ULONG_PTR ipCompletionKey = 0;
            LPOVERLAPPED ipOverlap = nullptr;

            // get IO status, no wait
            ::SetLastError(0);
            auto  ok = GetQueuedCompletionStatus(
                        iocp_handle,
                        &NumberOfBytes,
                        (PULONG_PTR)&ipCompletionKey,
                        &ipOverlap,
                        dwMilliseconds_to_wait);
            DWORD last_error = ::GetLastError();

            if (ipOverlap) [[likely]]
            {
                ops.emplace_back(ipOverlap, NumberOfBytes, last_error);
            }
            else if (ok && (ipCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
            {
                quit_if_no_work = true;
            }
            else
            {
                if (last_error == WSA_WAIT_TIMEOUT)
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

#endif
// 通知 loop 如果没有进行中的 IO 操作的时候，就退出循环。
inline void exit_event_loop_when_empty(HANDLE iocp_handle)
{
    PostQueuedCompletionStatus(iocp_handle, 0, (ULONG_PTR) iocp_handle, NULL);
}

// 执行这个，可以保证 协程被 IOCP 线程调度. 特别是 一个线程一个 IOCP 的模式下特有用
inline ucoro::awaitable<void> run_on_iocp_thread(HANDLE iocp_handle)
{
    awaitable_overlapped ov;
    struct SwitchIOCPAwaitable
    {
        HANDLE iocp_handle;

        awaitable_overlapped& ov;

        SwitchIOCPAwaitable(HANDLE iocp_handle, awaitable_overlapped& ov)
            : iocp_handle(iocp_handle)
            , ov(ov)
        {
        }

        constexpr bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            ov.coro_handle = handle;
            ov.coro_handle_set.test_and_set();
            PostQueuedCompletionStatus(iocp_handle, 0, 0, &ov);
        }

        void await_resume()
        {
            // coro->reset();
        }
    };

    co_await SwitchIOCPAwaitable{iocp_handle, ov};
}