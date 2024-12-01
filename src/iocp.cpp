
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>

#include <iostream>
#include <vector>
#include "iocp.h"

#include <liburing.h>

#include "io_uring_operation_allocator.hpp"

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(
	__in	HANDLE FileHandle,
	__in	HANDLE ExistingCompletionPort,
	__in	ULONG_PTR CompletionKey,
	__in	DWORD NumberOfConcurrentThreads)
{
	HANDLE	iocphandle = ExistingCompletionPort;
	if (iocphandle == nullptr)
	{
		::io_uring* ring_ = new ::io_uring;
		iocphandle = reinterpret_cast<HANDLE>(ring_);
		auto ret = io_uring_queue_init(16384, ring_, 0);
		if (ret < 0)
		{
			delete ring_;
			WSASetLastError(-ret);
			return INVALID_HANDLE_VALUE;
		}
	}

	if (FileHandle != INVALID_HANDLE_VALUE)
	{
		reinterpret_cast<SOCKET>(FileHandle)->_completion_key = CompletionKey;
		reinterpret_cast<SOCKET>(FileHandle)->_iocp = iocphandle;
	}

	return iocphandle;
}

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(
  __in          HANDLE CompletionPort,
  __out         LPDWORD lpNumberOfBytes,
  __out         PULONG_PTR lpCompletionKey,
  __out         LPOVERLAPPED* lpOverlapped,
  __in          DWORD dwMilliseconds)
{
	struct io_uring* ring = reinterpret_cast<io_uring*>(CompletionPort);
	struct io_uring_cqe* cqe = nullptr;
	struct __kernel_timespec ts = {
		.tv_sec = dwMilliseconds / 1000,
		.tv_nsec = dwMilliseconds % 1000 * 1000000
	};

	auto io_uring_ret = io_uring_wait_cqe_timeout(ring, &cqe, &ts);

	if (io_uring_ret < 0)
	{
		// timeout
		WSASetLastError(-io_uring_ret);
		return false;
	}

	// get LPOVERLAPPED from cqe
	io_uring_operation_ptr op = reinterpret_cast<io_uring_operation_ptr>(io_uring_cqe_get_data(cqe));
	if (!op)
	{
		io_uring_cqe_seen(ring, cqe);
		WSASetLastError(ERROR_IO_PENDING);
		return false;
	}

	if (lpOverlapped)
		*lpOverlapped = op->overlapped_ptr;
	if (lpCompletionKey)
		*lpCompletionKey = op->CompletionKey;
	if (lpNumberOfBytes)
		*lpNumberOfBytes = cqe->res;

	if (cqe->flags & IORING_CQE_F_MORE)
	{
		io_uring_cqe_seen(ring, cqe);
		return false;
	}

	op->do_complete(cqe->res);
	io_uring_cqe_seen(ring, cqe);
	io_uring_operation_allocator{}.deallocate(op, op->size);

	return true;
}

/** ********************************************************************************** **/
// WSA* for use with IOCP
/** ********************************************************************************** **/

IOCP_DECL SOCKET WSASocket(
  _In_  int af,
  _In_  int type,
  _In_  int protocol,
  _In_  LPWSAPROTOCOL_INFO lpProtocolInfo,
  _In_  GROUP g,
  _In_  DWORD dwFlags)
{
	assert(lpProtocolInfo == 0 );
	assert(dwFlags & WSA_FLAG_OVERLAPPED);

	if(dwFlags & WSA_FLAG_NO_HANDLE_INHERIT)
		type |= SOCK_CLOEXEC;
	auto ret = new SOCKET_emu_class {
		::socket(af, type, protocol)
	};
	return ret;
}

IOCP_DECL BOOL AcceptEx(
  _In_  SOCKET       sListenSocket,
  _In_  SOCKET       sAcceptSocket,
  _In_  PVOID        lpOutputBuffer,
  _In_  DWORD        dwReceiveDataLength,
  _In_  DWORD        dwLocalAddressLength,
  _In_  DWORD        dwRemoteAddressLength,
  _Out_ LPDWORD      lpdwBytesReceived,
  _In_  LPOVERLAPPED lpOverlapped)
{
	SOCKET listen_sock = reinterpret_cast<SOCKET>(sListenSocket);
	io_uring* ring_ = reinterpret_cast<io_uring*>(listen_sock->_iocp);

	if (ring_ == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	if (lpOverlapped == nullptr)
	{
		struct sockaddr addr;
		socklen_t addr_len = sizeof(addr);
		int accepted = accept(listen_sock->_socket_fd, &addr, &addr_len);
		return new SOCKET_emu_class{accepted, listen_sock->_iocp};
	}


	struct io_uring_accept_op : io_uring_operations
	{
		struct sockaddr addr;
		socklen_t addr_len = sizeof(addr);

		SOCKET accept_into;

		virtual void do_complete(int res) override
		{
			accept_into->_socket_fd = res;
		}
	};

	// now, enter IOCP emul logic
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring_);

	io_uring_accept_op* op = io_uring_operation_allocator{}.allocate<io_uring_accept_op>();
	io_uring_sqe_set_data(sqe, op);
	op->overlapped_ptr = lpOverlapped;
	op->accept_into = sAcceptSocket;
	op->CompletionKey = sListenSocket->_completion_key;
	close(sAcceptSocket->_socket_fd);

	io_uring_prep_accept(sqe, sListenSocket->_socket_fd, &(op->addr), &(op->addr_len), 0);
	io_uring_submit(ring_);
	return true;
}


IOCP_DECL int WSASend(
  _In_   SOCKET s,
  _In_   LPWSABUF lpBuffers,
  _In_   DWORD dwBufferCount,
  _Out_  LPDWORD lpNumberOfBytesSent,
  _In_   DWORD dwFlags,
  _In_   LPWSAOVERLAPPED lpOverlapped,
  _In_   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	io_uring* ring_ = reinterpret_cast<io_uring*>(s->_iocp);

	assert(lpCompletionRoutine == 0);
	assert(lpOverlapped);
	if (lpNumberOfBytesSent)
		*lpNumberOfBytesSent = 0;

	struct io_uring_write_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(int res) override
		{
			std::cerr << "write op \n";
		}
	};

	// now, enter IOCP emul logic
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring_);

	io_uring_write_op* op = io_uring_operation_allocator{}.allocate<io_uring_write_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();

	for (int i =0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}


	io_uring_prep_sendmsg_zc(sqe, s->_socket_fd, &op->msg, 0);
	io_uring_sqe_set_data(sqe, op);
	io_uring_submit(ring_);
	return true;
}

IOCP_DECL int WSARecv(
  _In_     SOCKET s,
  _Inout_  LPWSABUF lpBuffers,
  _In_     DWORD dwBufferCount,
  _Out_    LPDWORD lpNumberOfBytesRecvd,
  _Inout_  LPDWORD lpFlags,
  _In_     LPWSAOVERLAPPED lpOverlapped, //must not NULL
  _In_     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	io_uring* ring_ = reinterpret_cast<io_uring*>(s->_iocp);

	*lpNumberOfBytesRecvd = 0;
	assert(lpCompletionRoutine == 0);
	assert(lpOverlapped);

	struct io_uring_read_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(int res) override
		{
		}
	};

	// now, enter IOCP emul logic
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring_);

	io_uring_read_op* op = io_uring_operation_allocator{}.allocate<io_uring_read_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();

	for (int i =0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}

	io_uring_prep_recvmsg(sqe, s->_socket_fd, &op->msg, 0);
	io_uring_sqe_set_data(sqe, op);
	io_uring_submit(ring_);
	return true;
}

void SOCKET_emu_class::unref()
{
	if (--ref_count == 0)
	{
		::io_uring* ring = reinterpret_cast<io_uring*>(_iocp);
		::io_uring_sqe* sqe = io_uring_get_sqe(ring);
		io_uring_prep_close(sqe, _socket_fd);
		io_uring_sqe_set_data(sqe, nullptr);
		io_uring_submit(ring);
		delete this;
	}
}

IOCP_DECL int closesocket(SOCKET s)
{
	s->unref();
	return 0;
}

/** ********************************************************************************** **/
// junk stub for stupid windows API
/** ********************************************************************************** **/
IOCP_DECL int WSAStartup(
  _In_   WORD wVersionRequested,
  _Out_  LPWSADATA lpWSAData)
{
	return 0;
}


IOCP_DECL DWORD WSASetLastError(DWORD e)
{
	errno = e;
	return e;
}

IOCP_DECL DWORD WSAGetLastError()
{
	return errno;
}