
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

}
