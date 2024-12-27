
#pragma once

/*

可扩展 IOCP 库，是一个统一的 IOCP 循环，为不同种类的 IOCP 用法提供了统一的 IOCP 事件循环.

而不同种类的用法，可以和谐共处在同一个 iocp 循环里。

*/


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SOCKET_get_fd
#define SOCKET_get_fd(x) (x)
#endif
#else
#include "iocp.h"
#endif

#include <array>
#include <atomic>

namespace iocp
{
    inline std::atomic_long pending_works = 0;

    typedef void (*overlapped_proc_func)(const OVERLAPPED_ENTRY*, DWORD last_error);

    inline void run_event_loop(HANDLE iocp_handle)
    {
        bool quit_if_no_work = false;

        // batch size of 128
        std::array<OVERLAPPED_ENTRY, 128> ops;

        for (;;)
        {
            DWORD dwMilliseconds_to_wait = quit_if_no_work ? ( pending_works ? 500 : 0 ) : INFINITE;

            ULONG Entries = ops.size();
            // get IO status, no wait
            ::SetLastError(0);
            auto  result = GetQueuedCompletionStatusEx(iocp_handle,
                ops.data(), ops.size(), &Entries, dwMilliseconds_to_wait, TRUE);
            DWORD last_error = ::GetLastError();
            if (result == FALSE && last_error == WSA_WAIT_TIMEOUT)
            {
                if (!quit_if_no_work)
                    continue;
            }

            for (auto i = 0; i < Entries; i++)
            {
                auto& op = ops[i];
                if (result && (op.lpCompletionKey == (ULONG_PTR) iocp_handle)) [[unlikely]]
                {
                    quit_if_no_work = true;
                }
                else if (op.lpOverlapped && op.lpCompletionKey) [[likely]]
                {
                    reinterpret_cast<overlapped_proc_func>(op.lpCompletionKey)(&op, last_error);
                }
            }

            if  ( quit_if_no_work) [[unlikely]]
            {
                // 检查还在投递中的 IO 操作.
                if (!pending_works)
                {
                    break;
                }
            }
        }
    }

#if defined(_WIN32)
    inline LPFN_CONNECTEX WSAConnectEx = nullptr;
    inline LPFN_DISCONNECTEX DisconnectEx = nullptr;
    inline LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockaddrs = nullptr;
    inline LPFN_ACCEPTEX _AcceptEx = nullptr;

    #define AcceptEx iocp::_AcceptEx
    #define GetAcceptExSockaddrs iocp::_GetAcceptExSockaddrs

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

    // 通知 loop 如果没有进行中的 IO 操作的时候，就退出循环。
    inline void exit_event_loop_when_empty(HANDLE iocp_handle)
    {
        PostQueuedCompletionStatus(iocp_handle, 0, (ULONG_PTR) iocp_handle, NULL);
    }

}
