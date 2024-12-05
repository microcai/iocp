
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <optional>

#include <stdlib.h>

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>

#include "iocp.h"

#include <liburing.h>

#include "internal_iocp_struct.hpp"

#include "io_uring_operation_allocator.hpp"

#define IORING_CQE_F_USER 32

namespace {

struct nullable_mutex
{
	std::optional<std::mutex> m;

	void lock()
	{
		if (m)
			m->lock();
	}

	void unlock()
	{
		if (m)
			m->unlock();
	}

	nullable_mutex(int thread_hint)
	{
		if (thread_hint != 1)
			m.emplace();
	}

};
}

struct iocp_handle_emu_class final : public base_handle
{
	io_uring ring_;
	mutable nullable_mutex submit_mutex;
	mutable nullable_mutex wait_mutex;

	iocp_handle_emu_class(int NumberOfConcurrentThreads)
		: submit_mutex(NumberOfConcurrentThreads)
		, wait_mutex(NumberOfConcurrentThreads)
	{
		io_uring_queue_exit(&ring_);
	}

	~iocp_handle_emu_class(){}

	template<typename PrepareOP> auto submit_io(PrepareOP&& preparer)
	{
		std::scoped_lock<nullable_mutex> l(submit_mutex);
		io_uring_sqe * sqe = io_uring_get_sqe(&ring_);
		while (!sqe)
		{
			io_uring_submit(&ring_);
			sqe = io_uring_get_sqe(&ring_);
		}
		preparer(sqe);
		return io_uring_submit(&ring_);
	}
};

SOCKET_emu_class::~SOCKET_emu_class()
{
	auto* iocp = reinterpret_cast<iocp_handle_emu_class*>(_iocp);
	iocp->submit_io([this](io_uring_sqe* sqe)
	{
		io_uring_prep_close(sqe, _socket_fd);
		io_uring_sqe_set_data(sqe, nullptr);
	});
}

IOCP_DECL BOOL WINAPI CloseHandle(__in HANDLE h)
{
	h->unref();
	return true;
}

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(__in HANDLE FileHandle, __in HANDLE ExistingCompletionPort,
											   __in ULONG_PTR CompletionKey, __in DWORD NumberOfConcurrentThreads)
{
	HANDLE iocphandle = ExistingCompletionPort;
	if (iocphandle == nullptr)
	{
		iocp_handle_emu_class* ret = new iocp_handle_emu_class{static_cast<int>(NumberOfConcurrentThreads)};
		iocphandle = ret;
		io_uring_params params = {0};
		params.flags = IORING_SETUP_CQSIZE;
		params.cq_entries = 65536;
		params.sq_entries = 128;
		// params.features = IORING_FEAT_EXT_ARG;
		params.features = IORING_FEAT_NODROP | IORING_FEAT_EXT_ARG | IORING_FEAT_FAST_POLL | IORING_FEAT_RW_CUR_POS;
		auto result = io_uring_queue_init_params(128, &ret->ring_, &params);
		// auto result = io_uring_queue_init(16384, &ret->ring_, 0);
		ret->_socket_fd = ret->ring_.ring_fd;

		if (result < 0)
		{
			delete ret;
			WSASetLastError(-result);
			return INVALID_HANDLE_VALUE;
		}
	}

	if (FileHandle != INVALID_HANDLE_VALUE)
	{
		dynamic_cast<SOCKET_emu_class*>(FileHandle)->_completion_key = CompletionKey;
		dynamic_cast<SOCKET_emu_class*>(FileHandle)->_iocp = iocphandle;
	}

	return iocphandle;
}

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(__in HANDLE CompletionPort, __out LPDWORD lpNumberOfBytes,
												__out PULONG_PTR lpCompletionKey, __out LPOVERLAPPED* lpOverlapped,
												__in DWORD dwMilliseconds)
{
	struct iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);
	struct io_uring_cqe* cqe = nullptr;
	struct __kernel_timespec ts = {.tv_sec = dwMilliseconds / 1000, .tv_nsec = dwMilliseconds % 1000 * 1000000};

	while (1)
	{
		io_uring_operation_ptr op = nullptr;
		{
			std::scoped_lock<nullable_mutex> l(iocp->wait_mutex);
			auto io_uring_ret = io_uring_wait_cqe_timeout(&iocp->ring_, &cqe, dwMilliseconds == UINT32_MAX ? nullptr : &ts);

			// auto io_uring_ret =
			// 	io_uring_submit_and_wait_timeout(&iocp->ring_, &cqe, 1,  dwMilliseconds == UINT32_MAX ? nullptr : &ts, nullptr);

			if (io_uring_ret < 0)
			{
				if (io_uring_ret == -EINTR)
				{
					continue;
				}
				// timeout
				WSASetLastError(ERROR_WAIT_TIMEOUT);
				return false;
			}

			if (io_uring_cq_has_overflow(&(iocp->ring_)))
			{
				std::cerr << "uring completion queue overflow!" << std::endl;
				std::terminate();
			}

			// get LPOVERLAPPED from cqe
			op = reinterpret_cast<io_uring_operation_ptr>(io_uring_cqe_get_data(cqe));
			if (!op)
			{
				io_uring_cqe_seen(&iocp->ring_, cqe);
				if (dwMilliseconds == UINT32_MAX)
				{
					continue;
				}
				WSASetLastError(ERROR_BUSY);
				return false;
			}

			if (lpOverlapped)
			{
				*lpOverlapped = op->overlapped_ptr;
			}
			if (lpCompletionKey)
			{
				*lpCompletionKey = op->CompletionKey;
			}
			if (lpNumberOfBytes)
			{
				*lpNumberOfBytes = cqe->res;
			}

			if (uring_unlikely(cqe->flags & IORING_CQE_F_MORE))
			{
				if(cqe->res < 0)
				{
					if (cqe->res == -EOF)
					{
						WSASetLastError(ERROR_HANDLE_EOF);
					}
				}
				else
				{
					op->do_complete(lpNumberOfBytes);
				}
				io_uring_cqe_seen(&iocp->ring_, cqe);
				continue;
			}
			else if (uring_unlikely(cqe->flags & IORING_CQE_F_NOTIF))
			{
			}
			else if (uring_unlikely(cqe->flags == IORING_CQE_F_USER))
			{
				op->do_complete(0);
				io_uring_cqe_seen(&iocp->ring_, cqe);
				return true;
			}
			else
			{
				op->do_complete(lpNumberOfBytes);
			}

			io_uring_cqe_seen(&iocp->ring_, cqe);
		}

		if (op->lpCompletionRoutine)
		{
			op->lpCompletionRoutine(cqe->res < 0 ? -cqe->res : 0, cqe->res < 0 ? 0 :cqe->res, op->overlapped_ptr);
		}

		io_uring_operation_allocator{}.deallocate(op, op->size);

		if (lpOverlapped == NULL)
		{
			if (dwMilliseconds == UINT32_MAX)
			{
				continue;
			}
			WSASetLastError(ERROR_BUSY);
			return false;
		}

		return true;
	}
}

IOCP_DECL BOOL WINAPI PostQueuedCompletionStatus(
  _In_     HANDLE       CompletionPort,
  _In_     DWORD        dwNumberOfBytesTransferred,
  _In_     ULONG_PTR    dwCompletionKey,
  _In_opt_ LPOVERLAPPED lpOverlapped)
{
	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);

	struct io_uring_nop_op : io_uring_operations
	{
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
			overlapped_ptr = nullptr;
			CompletionKey = 0;
		}
	};

	io_uring_nop_op* op = io_uring_operation_allocator{}.allocate<io_uring_nop_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = dwCompletionKey;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_msg_ring_cqe_flags(sqe, iocp->ring_.ring_fd, dwNumberOfBytesTransferred, (__u64) op, 0, IORING_CQE_F_USER);
		io_uring_sqe_set_data(sqe, op);
	});

	return true;
}

/** ********************************************************************************** **/
// WSA* for use with IOCP
/** ********************************************************************************** **/

IOCP_DECL SOCKET WSASocket(_In_ int af, _In_ int type, _In_ int protocol, _In_ LPWSAPROTOCOL_INFO lpProtocolInfo,
						   _In_ SOCKET g, _In_ DWORD dwFlags)
{
	assert(lpProtocolInfo == 0);
	// assert(dwFlags & WSA_FLAG_OVERLAPPED);

	if (dwFlags & WSA_FLAG_NO_HANDLE_INHERIT)
	{
		type |= SOCK_CLOEXEC;
	}
	auto ret = new SOCKET_emu_class{::socket(af, type, protocol)};
	return ret;
}

IOCP_DECL BOOL AcceptEx(_In_ SOCKET sListenSocket, _In_ SOCKET sAcceptSocket, _In_ PVOID lpOutputBuffer,
						_In_ DWORD dwReceiveDataLength, _In_ DWORD dwLocalAddressLength,
						_In_ DWORD dwRemoteAddressLength, _Out_ LPDWORD lpdwBytesReceived,
						_In_ LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* listen_sock = dynamic_cast<SOCKET_emu_class*>(sListenSocket);

	if (listen_sock->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(listen_sock->_iocp);

	if (lpOverlapped == nullptr)
	{
		struct sockaddr addr;
		socklen_t addr_len = sizeof(addr);
		int accepted = accept(listen_sock->_socket_fd, &addr, &addr_len);
		return new SOCKET_emu_class{accepted, listen_sock->_iocp};
	}

	struct io_uring_accept_op : io_uring_operations
	{
		struct sockaddr local_addr;
		struct sockaddr remote_addr;
		socklen_t local_addr_len = sizeof(local_addr);
		socklen_t remote_addr_len = sizeof(remote_addr);

		SOCKET_emu_class* accept_into;

		PVOID lpOutputBuffer;

		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
			accept_into->_socket_fd = *lpNumberOfBytes;

			getsockname(accept_into->_socket_fd, &local_addr, &local_addr_len);

			// 向 lpOutputBuffer 写入数据，以便 GetAcceptExSockaddrs 解析
			if (lpOutputBuffer)
			{
				// lpOutputBuffer 的结构是
				// [local_addr_length][local_addr][remote_addr_len][remote_addr]
				memcpy(lpOutputBuffer, &local_addr_len, 1);
				memcpy(reinterpret_cast<char*>(lpOutputBuffer) + 1, &local_addr, local_addr_len);

				memcpy(reinterpret_cast<char*>(lpOutputBuffer) + 1 + local_addr_len, &remote_addr_len, 1);
				memcpy(reinterpret_cast<char*>(lpOutputBuffer) + 2 + local_addr_len, &remote_addr, remote_addr_len);

				*lpNumberOfBytes = remote_addr_len + local_addr_len + 2;
			}
		}
	};

	close(sAcceptSocket->native_handle());

	io_uring_accept_op* op = io_uring_operation_allocator{}.allocate<io_uring_accept_op>();
	op->overlapped_ptr = lpOverlapped;
	op->accept_into = dynamic_cast<SOCKET_emu_class*>(sAcceptSocket);
	op->CompletionKey = listen_sock->_completion_key;
	op->lpOutputBuffer = lpOutputBuffer;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_accept(sqe, listen_sock->_socket_fd, &(op->remote_addr), &(op->remote_addr_len), 0);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

IOCP_DECL BOOL WSAConnectEx(
  __in            SOCKET socket_,
  __in            const sockaddr *name,
  __in            int namelen,
  _In_opt_        PVOID lpSendBuffer,
  __in            DWORD dwSendDataLength,
  __out           LPDWORD lpdwBytesSent,
  __in            LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	assert(lpOverlapped);

	struct io_uring_connect_op : io_uring_operations
	{
		PVOID lpSendBuffer;
		DWORD dwSendDataLength;
		SOCKET s;

		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
			if (lpSendBuffer && dwSendDataLength)
			{
				auto send_bytes = send(s->native_handle(), lpSendBuffer, dwSendDataLength, MSG_NOSIGNAL|MSG_DONTWAIT);
				if (lpNumberOfBytes && send_bytes > 0)
					* lpNumberOfBytes = send_bytes;
			}
		}
	};

	// now, enter IOCP emul logic
	io_uring_connect_op* op = io_uring_operation_allocator{}.allocate<io_uring_connect_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->lpSendBuffer = lpSendBuffer;
	op->dwSendDataLength = dwSendDataLength;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_connect(sqe, s->_socket_fd, name, namelen);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

IOCP_DECL void GetAcceptExSockaddrs(__in PVOID lpOutputBuffer, __in DWORD dwReceiveDataLength,
									__in DWORD dwLocalAddressLength, __in DWORD dwRemoteAddressLength,
									__out sockaddr** LocalSockaddr, __out LPINT LocalSockaddrLength,
									__out sockaddr** RemoteSockaddr, __out LPINT RemoteSockaddrLength)
{
	// lpOutputBuffer 的结构是
	// [local_addr_length][local_addr][remote_addr_len][remote_addr]
	unsigned char local_addr_length = *reinterpret_cast<unsigned char*>(lpOutputBuffer);
	*LocalSockaddrLength = local_addr_length;
	*LocalSockaddr = reinterpret_cast<sockaddr*>(reinterpret_cast<char*>(lpOutputBuffer) + 1);

	unsigned char remote_addr_length = *(reinterpret_cast<unsigned char*>(lpOutputBuffer) + local_addr_length + 1);
	*RemoteSockaddrLength = remote_addr_length;
	*RemoteSockaddr = reinterpret_cast<sockaddr*>(reinterpret_cast<char*>(lpOutputBuffer) + local_addr_length + 2);
}

IOCP_DECL int WSASend(_In_ SOCKET socket_, _In_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesSent, _In_ DWORD dwFlags, _In_ LPWSAOVERLAPPED lpOverlapped,
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	assert(lpOverlapped);
	if (lpNumberOfBytesSent)
	{
		*lpNumberOfBytesSent = 0;
	}

	struct io_uring_write_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_write_op* op = io_uring_operation_allocator{}.allocate<io_uring_write_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_sendmsg_zc(sqe, s->_socket_fd, &op->msg, 0);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return SOCKET_ERROR;
}

IOCP_DECL int WSARecv(_In_ SOCKET socket_, _Inout_ LPWSABUF lpBuffers, _In_ DWORD dwBufferCount,
					  _Out_ LPDWORD lpNumberOfBytesRecvd, _Inout_ LPDWORD lpFlags,
					  _In_ LPWSAOVERLAPPED lpOverlapped, // must not NULL
					  _In_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_read_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_read_op* op = io_uring_operation_allocator{}.allocate<io_uring_read_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();

	for (int i = 0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_recvmsg(sqe, s->_socket_fd, &(op->msg), 0);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return SOCKET_ERROR;
}


IOCP_DECL int WSASendTo(
    __in    SOCKET                             socket_,
    __in    LPWSABUF                           lpBuffers,
    __in    DWORD                              dwBufferCount,
    __out   LPDWORD                            lpNumberOfBytesSent,
    __in    DWORD                              dwFlags,
    __in    const sockaddr                     *lpTo,
    __in    int                                iTolen,
    __in    LPWSAOVERLAPPED                    lpOverlapped,
    __in    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	assert(lpOverlapped);
	if (lpNumberOfBytesSent)
	{
		*lpNumberOfBytesSent = 0;
	}

	struct io_uring_write_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_write_op* op = io_uring_operation_allocator{}.allocate<io_uring_write_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();
	op->msg.msg_name = (void*) lpTo;
	op->msg.msg_namelen = iTolen;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_sendmsg_zc(sqe, s->_socket_fd, &op->msg, 0);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return SOCKET_ERROR;
}

IOCP_DECL int WSARecvFrom(
    __in    SOCKET                             socket_,
    __in    LPWSABUF                           lpBuffers,
    __in    DWORD                              dwBufferCount,
    __out   LPDWORD                            lpNumberOfBytesRecvd,
    __in    LPDWORD                            lpFlags,
    __out   sockaddr                           *lpFrom,
    __in    LPINT                              lpFromlen,
    __in    LPWSAOVERLAPPED                    lpOverlapped,
    __in    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_read_op : io_uring_operations
	{
		LPINT lpFromlen;
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
			* lpFromlen = msg.msg_namelen;
		}
	};

	// now, enter IOCP emul logic
	io_uring_read_op* op = io_uring_operation_allocator{}.allocate<io_uring_read_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();
	op->msg.msg_name = lpFrom;
	op->msg.msg_namelen = *lpFromlen;
	op->lpFromlen = lpFromlen;

	for (int i = 0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
	}

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_recvmsg(sqe, s->_socket_fd, &(op->msg), 0);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return SOCKET_ERROR;
}

void base_handle::unref()
{
	if (--ref_count == 0)
	{
		delete this;
	}
}

IOCP_DECL int closesocket(SOCKET s)
{
	s->unref();
	return 0;
}

IOCP_DECL int SOCKET_get_fd(SOCKET s)
{
	return s->native_handle();
}

int bind(SOCKET s, SOCKADDR_IN* a, int l)
{
	return ::bind(s->native_handle(), (sockaddr*)a, l);
}

int bind(SOCKET s, SOCKADDR* a, int l)
{
	return ::bind(s->native_handle(), (sockaddr*)a, l);
}

int listen(SOCKET s, int l)
{
	return ::listen(s->native_handle(), l);
}

int setsockopt(SOCKET __fd, int __level, int __optname, const void *__optval, socklen_t __optlen)
{
	return ::setsockopt(__fd->native_handle(), __level, __optname, __optval, __optlen);
}

/** ********************************************************************************** **/
// junk stub for stupid windows API
/** ********************************************************************************** **/
IOCP_DECL int WSAStartup(_In_ WORD wVersionRequested, _Out_ LPWSADATA lpWSAData)
{
	return 0;
}

IOCP_DECL int WSACleanup()
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

/***********************************************************************************
* Overlapped File IO
************************************************************************************/

IOCP_DECL HANDLE CreateFileA(
  __in            LPCSTR                lpFileName,
  __in            DWORD                 dwDesiredAccess,
  __in            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in            DWORD                 dwCreationDisposition,
  __in            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile
)
{
	assert(dwFlagsAndAttributes & FILE_FLAG_OVERLAPPED);

	int oflag = 0;
	int mode = 0644;
	if (dwDesiredAccess & GENERIC_READ)
	{
		oflag |= O_RDONLY;
	}
	if (dwDesiredAccess & GENERIC_WRITE)
	{
		oflag |= O_WRONLY;
	}

	if (dwDesiredAccess & GENERIC_EXECUTE)
	{
		mode = 0755;
	}

	int fd = - 1;

	switch (dwCreationDisposition)
	{
		case CREATE_NEW:
			oflag |= O_EXCL;
		case OPEN_ALWAYS:
			oflag |= O_CREAT;
		case OPEN_EXISTING:
			fd = open(lpFileName, oflag, mode );
			break;
		case TRUNCATE_EXISTING:
			oflag |= O_TRUNC;
		case CREATE_ALWAYS:
			oflag |= O_CREAT;
			fd = open(lpFileName, oflag, mode );
			if (fd >= 0)
				ftruncate(fd, 0);
			break;
	}

	if (fd >= 0)
		return new SOCKET_emu_class(fd);
	return INVALID_HANDLE_VALUE;
}

IOCP_DECL HANDLE CreateFileW(
  __in            LPCWSTR               lpFileName,
  __in            DWORD                 dwDesiredAccess,
  __in            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  __in            DWORD                 dwCreationDisposition,
  __in            DWORD                 dwFlagsAndAttributes,
  _In_opt_        HANDLE                hTemplateFile)
{
	// convert wstring back to string
	auto filename_length = wcslen(lpFileName);
	std::string utf8_str;
	utf8_str.resize(filename_length*3);
	auto filename_utf8_len = wcstombs(utf8_str.data(), lpFileName, filename_length);
	utf8_str.resize(filename_utf8_len);

	return CreateFileA(utf8_str.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

IOCP_DECL BOOL ReadFile(
  __in                HANDLE       hFile,
  __out               LPVOID       lpBuffer,
  __in                DWORD        nNumberOfBytesToRead,
  _Out_opt_	          LPDWORD      lpNumberOfBytesRead,
  _Inout_             LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_readfile_op : io_uring_operations
	{
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_readfile_op* op = io_uring_operation_allocator{}.allocate<io_uring_readfile_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;
	__u64 offset = lpOverlapped->Offset + ( static_cast<__u64>(lpOverlapped->OffsetHigh) << 32 );

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_read(sqe, s->native_handle(), lpBuffer, nNumberOfBytesToRead, offset);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

IOCP_DECL BOOL WriteFile(
  __in                 HANDLE       hFile,
  __in                 LPCVOID      lpBuffer,
  __in                 DWORD        nNumberOfBytesToWrite,
  _Out_opt_            LPDWORD      lpNumberOfBytesWritten,
  _Inout_              LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(s->_iocp);

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_writefile_op : io_uring_operations
	{
		virtual void do_complete(DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_writefile_op* op = io_uring_operation_allocator{}.allocate<io_uring_writefile_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = s->_completion_key;

	__u64 offset = lpOverlapped->Offset + ( static_cast<__u64>(lpOverlapped->OffsetHigh) << 32 );

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_write(sqe, s->native_handle(), lpBuffer, nNumberOfBytesToWrite, offset);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

