#include <vector>

#include "iocp.h"

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/poll.h>

#include "internal_iocp_struct.hpp"


struct wsa_event_emu_class : public base_handle
{
    ~wsa_event_emu_class()
    {
        close(this->_socket_fd);
    }

    wsa_event_emu_class()
    {
        _socket_fd = epoll_create1(EPOLL_CLOEXEC);
    }


};


IOCP_DECL WSAEVENT WSACreateEvent()
{
    return new wsa_event_emu_class{};
}

IOCP_DECL int WSAEventSelect(
  _In_ SOCKET   s,
  _In_ WSAEVENT hEventObject,
  _In_ long     lNetworkEvents)
{
    struct epoll_event ev = {0};

    if (lNetworkEvents & FD_ACCEPT)
        ev.events = EPOLLIN;

    epoll_ctl(hEventObject->native_handle(), EPOLL_CTL_ADD, s->native_handle(), &ev);
    // connect s with hEventObject
    // so that
    return 0;
}

IOCP_DECL DWORD WSAWaitForMultipleEvents(
  __in DWORD          cEvents,
  __in const WSAEVENT *lphEvents,
  __in BOOL           fWaitAll,
  __in DWORD          dwTimeout,
  __in BOOL           fAlertable)
{
    // std::vector<pollfd> fds{cEvents};

     pollfd fds[1];

    for (int i = 0; i < cEvents; i ++)
    {
        fds[i].fd = lphEvents[i]->native_handle();
        fds[i].events = POLLIN;
    }

    int ready_ = poll(fds, cEvents, dwTimeout);
    if (ready_ > 0)
    {
        for (pollfd& fd : fds)
        {
            if (fd.revents & POLLIN)
            {
                epoll_event ev;
                epoll_wait(fd.fd, &ev, 1, dwTimeout);
                return WSA_WAIT_EVENT_0;
            }

        }




    }

    return WSA_WAIT_TIMEOUT;
}

IOCP_DECL int WSAEnumNetworkEvents(
  __in  SOCKET             s,
  __in  WSAEVENT           hEventObject,
  __out LPWSANETWORKEVENTS lpNetworkEvents)
{
    epoll_event ev;
    epoll_wait(hEventObject->native_handle(), &ev, 1, -1);

    if (ev.events == POLLIN)
    {
        lpNetworkEvents->lNetworkEvents = FD_ACCEPT|FD_READ;
    }
    return 0;
}
