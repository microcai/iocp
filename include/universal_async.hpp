
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
#else
#include "iocp.h"
#endif


struct awaitable_overlapped : public OVERLAPPED
{
    DWORD NumberOfBytes;
    DWORD last_error;
    std::coroutine_handle<> coro_handle;
    std::atomic_flag completed;
    std::mutex m;

    using out_standing_t = std::atomic_long;

    static out_standing_t out_standing;

    void reset()
    {
        this->Internal = this->InternalHigh = 0;
        this->hEvent = NULL;
        NumberOfBytes = 0;
        coro_handle = nullptr;
        completed.clear();
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

    awaitable_overlapped()
    {
        Offset = OffsetHigh = 0xFFFFFFFF;
        reset();
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
        this->ov.m.lock();
        if (ov.completed.test_and_set())
        {
            return true;
        }
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        ov.coro_handle = handle;
        this->ov.m.unlock();
        ++ awaitable_overlapped::out_standing;
    }

    DWORD await_resume()
    {
        auto NumberOfBytes = ov.NumberOfBytes;
        WSASetLastError(ov.last_error);
        ov.reset();
        this->ov.m.unlock();
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
    co_return co_await OverlappedAwaiter{ov};
}

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov, DWORD NumberOfBytes, DWORD last_error)
{
    auto ov = reinterpret_cast<awaitable_overlapped*>(_ov);

    ov->last_error = last_error;
    ov->NumberOfBytes = NumberOfBytes;

    ov->m.lock();
    if (ov->completed.test_and_set())
    {
        ov->coro_handle.resume();
        printf("resume continued here\n");
    }
    else
    {
        ov->m.unlock();
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
            DWORD NumberOfBytes;
            ULONG_PTR ipCompletionKey;
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