﻿
#pragma once

#include "extensable_iocp.hpp"

#include "awaitable.hpp"
#include <cstdint>
#include <mutex>
#include <array>

#if defined(_WIN32)
inline LPFN_CONNECTEX WSAConnectEx = nullptr;
inline LPFN_DISCONNECTEX DisconnectEx = nullptr;
inline LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockaddrs = nullptr;
inline LPFN_ACCEPTEX _AcceptEx = nullptr;

#define AcceptEx _AcceptEx
#define GetAcceptExSockaddrs _GetAcceptExSockaddrs

inline void init_winsock_api_pointer()
{
	SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	GUID disconnectex = WSAID_DISCONNECTEX;
	GUID connect_ex_guid = WSAID_CONNECTEX;
	GUID acceptex = WSAID_ACCEPTEX;
    GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD BytesReturned;

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&disconnectex, sizeof(GUID), &DisconnectEx, sizeof(DisconnectEx),
		&BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&connect_ex_guid, sizeof(GUID), &WSAConnectEx, sizeof(WSAConnectEx), &BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptex, sizeof(GUID), &_AcceptEx, sizeof(_AcceptEx),
		&BytesReturned, 0, 0);

    WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &getacceptexsockaddrs, sizeof(GUID), &_GetAcceptExSockaddrs, sizeof(_GetAcceptExSockaddrs),
        &BytesReturned, 0, 0);
	closesocket(sock);
}

#endif // defined(_WIN32)

struct awaitable_overlapped
{
    OVERLAPPED ovl;
    DWORD byte_transfered;
    DWORD last_error;
    std::coroutine_handle<> coro_handle;
    std::atomic_flag coro_handle_set;
    std::atomic_flag ready;
    std::atomic_flag pending;

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
        byte_transfered = 0;
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

// wait for overlapped to became complete. return NumberOfBytes
inline ucoro::awaitable<DWORD> get_overlapped_result(awaitable_overlapped& ov)
{
    ++ iocp::pending_works;
    co_await OverlappedAwaiter{ov};
    -- iocp::pending_works;

    auto R = ov.byte_transfered;
    WSASetLastError(ov.last_error);
    ov.reset();
    co_return R;
}

// call this after GetQueuedCompletionStatus.
inline void process_awaitable_overlapped_event(const OVERLAPPED_ENTRY& ov_entry, DWORD last_error)
{
    auto ov = reinterpret_cast<awaitable_overlapped*>(ov_entry.lpOverlapped);

    ov->last_error = last_error;
    ov->byte_transfered = ov_entry.dwNumberOfBytesTransferred;

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

inline auto bind_stackless_iocp(HANDLE file, HANDLE iocp_handle, DWORD = 0, DWORD = 0)
{
    return CreateIoCompletionPort(file, iocp_handle, (ULONG_PTR) (void*) &process_awaitable_overlapped_event, 0);
}

inline void run_event_loop(HANDLE iocp_handle)
{
    return iocp::run_event_loop(iocp_handle);
}

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