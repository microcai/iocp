
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
#undef socket

#include <liburing.h>

#include "internal_iocp_struct.hpp"

#include "io_uring_operation_allocator.hpp"

#define IORING_CQE_F_USER 32

IOCP_DECL BOOL WINAPI CloseHandle(_In_ HANDLE h)
{
	h->unref();
	return true;
}

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(_In_ HANDLE FileHandle, _In_ HANDLE ExistingCompletionPort,
											   _In_ ULONG_PTR CompletionKey, _In_ DWORD NumberOfConcurrentThreads)
{
	HANDLE iocphandle = ExistingCompletionPort;
	if (iocphandle == nullptr)
	{
		iocp_handle_emu_class* ret = new iocp_handle_emu_class{static_cast<int>(NumberOfConcurrentThreads)};
		iocphandle = ret;
		io_uring_params params = {0};
		params.flags = IORING_SETUP_CQSIZE|IORING_SETUP_SUBMIT_ALL|IORING_SETUP_TASKRUN_FLAG|IORING_SETUP_COOP_TASKRUN;

		params.cq_entries = 65536;
		params.sq_entries = 128;
		// params.features = IORING_FEAT_EXT_ARG;
		params.features = IORING_FEAT_NODROP | IORING_FEAT_EXT_ARG | IORING_FEAT_FAST_POLL | IORING_FEAT_RW_CUR_POS |IORING_FEAT_CUR_PERSONALITY;
		auto result = io_uring_queue_init_params(128, &ret->ring_, &params);
		// auto result = io_uring_queue_init(16384, &ret->ring_, IORING_SETUP_SQPOLL|IORING_SETUP_CLAMP);
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
		dynamic_cast<SOCKET_emu_class*>(FileHandle)->_iocp = dynamic_cast<iocp_handle_emu_class*>(iocphandle);
	}

	return iocphandle;
}

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(
	_In_ HANDLE CompletionPort, _Out_ LPDWORD lpNumberOfBytes, _Out_ PULONG_PTR lpCompletionKey,
	_Out_ LPOVERLAPPED* lpOverlapped, _In_ DWORD dwMilliseconds)
{
	struct iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);
	struct io_uring_cqe* cqe = nullptr;
	struct __kernel_timespec ts = {.tv_sec = dwMilliseconds / 1000, .tv_nsec = dwMilliseconds % 1000 * 1000000};
	*lpOverlapped = nullptr;

	while (1)
	{
		io_uring_operation_ptr op = nullptr;
		{
			std::scoped_lock<nullable_mutex> l(iocp->wait_mutex);
			int io_uring_ret = 0;
			if (dwMilliseconds == 0)
			{
				io_uring_ret = io_uring_peek_cqe(&iocp->ring_, &cqe);
				static struct __kernel_timespec min_ts = {.tv_sec = 0, .tv_nsec = 2000 };
				if (io_uring_ret == -EAGAIN)
				{
					std::scoped_lock<std::mutex> ll(iocp->submit_mutex);
					io_uring_submit(&iocp->ring_);
					WSASetLastError(WSA_WAIT_TIMEOUT);
					return false;
				}
			}
			else
			{
				io_uring_ret = io_uring_peek_cqe(&iocp->ring_, &cqe);
				if (io_uring_ret == - EAGAIN)
				{
					{
						std::scoped_lock<std::mutex> ll(iocp->submit_mutex);
						if (io_uring_sq_ready(&iocp->ring_) > 0)
							io_uring_submit(&iocp->ring_);
					}
					io_uring_ret = io_uring_wait_cqe_timeout(&iocp->ring_, &cqe, dwMilliseconds == UINT32_MAX ? nullptr : &ts);
				}
			}

			if (io_uring_ret < 0) [[unlikely]]
			{
				if (io_uring_ret == -EAGAIN && dwMilliseconds ==0) [[likely]]
				{
					WSASetLastError(WSA_WAIT_TIMEOUT);
					return false;
				}
				else if (io_uring_ret == -EINTR) [[unlikely]]
				{
					continue;
				}
				else if (dwMilliseconds == INFINITE && io_uring_ret == -EAGAIN)
				{
					continue;
				}
				// timeout
				WSASetLastError(WSA_WAIT_TIMEOUT);
				return false;
			}

			if (io_uring_cq_has_overflow(&(iocp->ring_))) [[unlikely]]
			{
				std::cerr << "uring completion queue overflow!" << std::endl;
				std::terminate();
			}

			// get LPOVERLAPPED from cqe
			op = reinterpret_cast<io_uring_operation_ptr>(io_uring_cqe_get_data(cqe));
			if (!op) [[unlikely]]
			{
				io_uring_cqe_seen(&iocp->ring_, cqe);
				if (dwMilliseconds == UINT32_MAX)
				{
					continue;
				}
				WSASetLastError(ERROR_BUSY);
				return false;
			}

			if (lpNumberOfBytes) [[likely]]
			{
				*lpNumberOfBytes = cqe->res;
			}

			if (cqe->flags & IORING_CQE_F_MORE) [[unlikely]]
			{
				op->do_complete(cqe, lpNumberOfBytes);
				io_uring_cqe_seen(&iocp->ring_, cqe);
				continue;
			}
			else if (uring_unlikely(cqe->flags & IORING_CQE_F_NOTIF)) [[unlikely]]
			{
			}
			else if (uring_unlikely(cqe->flags == IORING_CQE_F_USER)) [[unlikely]]
			{
				*lpCompletionKey = op->CompletionKey;
				*lpOverlapped = op->overlapped_ptr;
				op->do_complete(cqe, lpNumberOfBytes);
				op->overlapped_ptr = 0;
				op->CompletionKey = 0;
				io_uring_cqe_seen(&iocp->ring_, cqe);
				return true;
			}
			else [[likely]]
			{
				op->do_complete(cqe, lpNumberOfBytes);
			}

			io_uring_cqe_seen(&iocp->ring_, cqe);
		}

		*lpCompletionKey = op->CompletionKey;
		*lpOverlapped = op->overlapped_ptr;
		if (op->lpCompletionRoutine) [[unlikely]]
		{
			op->lpCompletionRoutine(cqe->res < 0 ? -cqe->res : 0, cqe->res < 0 ? 0 :cqe->res, op->overlapped_ptr);
		}

		io_uring_operation_allocator{}.deallocate(op, op->size);

		if (*lpOverlapped == NULL) [[unlikely]]
		{
			if (dwMilliseconds == UINT32_MAX) [[likely]]
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
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			*lpNumberOfBytes = cqe->res;
			overlapped_ptr = nullptr;
			CompletionKey = 0;
		}
	};

	io_uring_nop_op* op = io_uring_operation_allocator{}.allocate<io_uring_nop_op>();
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = dwCompletionKey;

	iocp->submit_io_immediatly([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_msg_ring_cqe_flags(sqe, iocp->ring_.ring_fd, dwNumberOfBytesTransferred, (__u64) op, 0, IORING_CQE_F_USER);
		io_uring_sqe_set_data(sqe, op);
	});

	return true;
}

IOCP_DECL BOOL WINAPI CancelIo(_In_ HANDLE hFile)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	iocp_handle_emu_class* iocp = s->_iocp;

	struct io_uring_cancel_op : io_uring_operations
	{
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_cancel_op* op = io_uring_operation_allocator{}.allocate<io_uring_cancel_op>();
	op->CompletionKey = 0;
	op->lpCompletionRoutine = 0;
	op->overlapped_ptr = 0;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_cancel_fd(sqe, s->_socket_fd, IORING_ASYNC_CANCEL_ALL);
		io_uring_sqe_set_data(sqe, op);
	});

	return TRUE;
}

IOCP_DECL BOOL WINAPI CancelIoEx(_In_ HANDLE  hFile, _In_opt_ LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	iocp_handle_emu_class* iocp = s->_iocp;

	struct io_uring_cancel_op : io_uring_operations
	{
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
		}
	};

	// now, enter IOCP emul logic
	io_uring_cancel_op* op = io_uring_operation_allocator{}.allocate<io_uring_cancel_op>();
	void* user_data = (void*) lpOverlapped->Internal;
	op->CompletionKey = 0;
	op->lpCompletionRoutine = 0;
	op->overlapped_ptr = 0;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_cancel(sqe, user_data, IORING_ASYNC_CANCEL_ALL);
		io_uring_sqe_set_data(sqe, op);
	});

	return TRUE;
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
	auto ret = new SOCKET_emu_class{ (dwFlags == WSA_FLAG_FAKE_CREATION )  ? -1 : ::socket(af, type, protocol) };
	return ret;
}

IOCP_DECL BOOL AcceptEx(_In_ SOCKET sListenSocket, _In_ SOCKET sAcceptSocket, _In_ PVOID lpOutputBuffer,
						_In_ DWORD dwReceiveDataLength, _In_ DWORD dwLocalAddressLength,
						_In_ DWORD dwRemoteAddressLength, _Out_ LPDWORD lpdwBytesReceived,
						_In_ LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* listen_sock = dynamic_cast<SOCKET_emu_class*>(sListenSocket);

	if (listen_sock->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = listen_sock->_iocp;

	if (lpOverlapped == nullptr) [[unlikely]]
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

		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res <= 0) [[unlikely]]
			{
				WSASetLastError(-cqe->res);
				*lpNumberOfBytes = 0;
				return;
			}

			WSASetLastError(0);
			accept_into->_socket_fd = cqe->res;

			// 向 lpOutputBuffer 写入数据，以便 GetAcceptExSockaddrs 解析
			if (lpOutputBuffer) [[likely]]
			{
				getsockname(accept_into->_socket_fd, &local_addr, &local_addr_len);
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

	if (sAcceptSocket->_socket_fd != -1)
		close(sAcceptSocket->native_handle());

	io_uring_accept_op* op = io_uring_operation_allocator{}.allocate<io_uring_accept_op>();
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);

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
  _In_            SOCKET socket_,
  _In_            const sockaddr *name,
  _In_            int namelen,
  _In_opt_        PVOID lpSendBuffer,
  _In_            DWORD dwSendDataLength,
  _Out_           LPDWORD lpdwBytesSent,
  _In_            LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	assert(lpOverlapped);

	struct io_uring_connect_op : io_uring_operations
	{
		PVOID lpSendBuffer;
		DWORD dwSendDataLength;
		SOCKET s;

		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
			else if (lpSendBuffer && dwSendDataLength) [[unlikely]]
			{
				auto send_bytes = send(s->native_handle(), lpSendBuffer, dwSendDataLength, MSG_NOSIGNAL|MSG_DONTWAIT);
				if (lpNumberOfBytes && send_bytes > 0)
					* lpNumberOfBytes = send_bytes;
			}
			WSASetLastError(0);
		}
	};

	// now, enter IOCP emul logic
	io_uring_connect_op* op = io_uring_operation_allocator{}.allocate<io_uring_connect_op>();
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
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

IOCP_DECL void GetAcceptExSockaddrs(_In_ PVOID lpOutputBuffer, _In_ DWORD dwReceiveDataLength,
									_In_ DWORD dwLocalAddressLength, _In_ DWORD dwRemoteAddressLength,
									_Out_ sockaddr** LocalSockaddr, _Out_ socklen_t* LocalSockaddrLength,
									_Out_ sockaddr** RemoteSockaddr, _Out_ socklen_t* RemoteSockaddrLength)
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

	if (s->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	assert(lpOverlapped);

	lpOverlapped->InternalHigh = (ULONG_PTR) __builtin_extract_return_addr (__builtin_return_address (0));

	if (lpNumberOfBytesSent) [[likely]]
		*lpNumberOfBytesSent = 0;

	struct io_uring_write_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
			else [[likely]]
				WSASetLastError(0);
		}
	};

	// now, enter IOCP emul logic
	io_uring_write_op* op = io_uring_operation_allocator{}.allocate<io_uring_write_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;
	op->msg_iov.resize(dwBufferCount);
	op->msg.msg_iovlen = dwBufferCount;
	op->msg.msg_iov = op->msg_iov.data();
	int64_t total_send_bytes = 0;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->msg_iov[i].iov_base = lpBuffers[i].buf;
		op->msg_iov[i].iov_len = lpBuffers[i].len;
		total_send_bytes += lpBuffers[i].len;
	}

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		if (total_send_bytes > 3000) [[unlikely]]
			io_uring_prep_sendmsg_zc(sqe, s->_socket_fd, &op->msg, 0);
		else [[likely]]
			io_uring_prep_sendmsg(sqe, s->_socket_fd, &op->msg, 0);
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

	iocp_handle_emu_class* iocp = s->_iocp;

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_read_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
			else if (cqe->res == 0) [[unlikely]]
				WSASetLastError(ERROR_HANDLE_EOF);
			else [[likely]]
				WSASetLastError(0);
		}
	};

	// now, enter IOCP emul logic
	io_uring_read_op* op = io_uring_operation_allocator{}.allocate<io_uring_read_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
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
    _In_    SOCKET                             socket_,
    _In_    LPWSABUF                           lpBuffers,
    _In_    DWORD                              dwBufferCount,
    _Out_   LPDWORD                            lpNumberOfBytesSent,
    _In_    DWORD                              dwFlags,
    _In_    const sockaddr                     *lpTo,
    _In_    int                                iTolen,
    _In_    LPWSAOVERLAPPED                    lpOverlapped,
    _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(WSAEOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	assert(lpOverlapped);

	*lpNumberOfBytesSent = 0;

	struct io_uring_write_op : io_uring_operations
	{
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0)
				WSASetLastError(-cqe->res);
		}
	};

	// now, enter IOCP emul logic
	io_uring_write_op* op = io_uring_operation_allocator{}.allocate<io_uring_write_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
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
    _In_    SOCKET                             socket_,
    _In_    LPWSABUF                           lpBuffers,
    _In_    DWORD                              dwBufferCount,
    _Out_   LPDWORD                            lpNumberOfBytesRecvd,
    _In_    LPDWORD                            lpFlags,
    _Out_   sockaddr                           *lpFrom,
    _In_    LPINT                              lpFromlen,
    _In_    LPWSAOVERLAPPED                    lpOverlapped,
    _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(socket_);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_read_op : io_uring_operations
	{
		LPINT lpFromlen;
		std::vector<iovec> msg_iov;
		msghdr msg = {};
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
			else if (cqe->res == 0) [[unlikely]]
				WSASetLastError(ERROR_HANDLE_EOF);
			else [[likely]]
				* lpFromlen = msg.msg_namelen;
		}
	};

	// now, enter IOCP emul logic
	io_uring_read_op* op = io_uring_operation_allocator{}.allocate<io_uring_read_op>();
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
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

IOCP_DECL BOOL DisconnectEx(
  _In_ SOCKET       hSocket,
  _In_ LPOVERLAPPED lpOverlapped,
  _In_ DWORD        dwFlags,
  _In_ DWORD        reserved)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hSocket);

	if (s->_iocp == nullptr && lpOverlapped)
	{
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	// now, enter IOCP emul logic
	io_uring_operations* op = io_uring_operation_allocator{}.allocate<io_uring_operations>();
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_shutdown(sqe, s->_socket_fd, SHUT_RDWR);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return FALSE;
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
  _In_            LPCSTR                lpFileName,
  _In_            DWORD                 dwDesiredAccess,
  _In_            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  _In_            DWORD                 dwCreationDisposition,
  _In_            DWORD                 dwFlagsAndAttributes,
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
			break;
	}

	if (fd >= 0)
		return new SOCKET_emu_class(fd);
	return INVALID_HANDLE_VALUE;
}

IOCP_DECL HANDLE CreateFileW(
  _In_            LPCWSTR               lpFileName,
  _In_            DWORD                 dwDesiredAccess,
  _In_            DWORD                 dwShareMode,
  _In_opt_        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  _In_            DWORD                 dwCreationDisposition,
  _In_            DWORD                 dwFlagsAndAttributes,
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
  _In_                HANDLE       hFile,
  _Out_               LPVOID       lpBuffer,
  _In_                DWORD        nNumberOfBytesToRead,
  _Out_opt_	          LPDWORD      lpNumberOfBytesRead,
  _Inout_             LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	if (s->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_readfile_op : io_uring_operations
	{
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
			else if (cqe->res == 0) [[unlikely]]
				WSASetLastError(ERROR_HANDLE_EOF);
		}
	};

	// now, enter IOCP emul logic
	io_uring_readfile_op* op = io_uring_operation_allocator{}.allocate<io_uring_readfile_op>();
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;
	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_read(sqe, s->native_handle(), lpBuffer, nNumberOfBytesToRead, lpOverlapped->offset_64);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

IOCP_DECL BOOL WriteFile(
  _In_                 HANDLE       hFile,
  _In_                 LPCVOID      lpBuffer,
  _In_                 DWORD        nNumberOfBytesToWrite,
  _Out_opt_            LPDWORD      lpNumberOfBytesWritten,
  _Inout_              LPOVERLAPPED lpOverlapped)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	if (s->_iocp == nullptr && lpOverlapped) [[unlikely]]
	{
		WSASetLastError(EOPNOTSUPP);
		return false;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	// *lpNumberOfBytesRecvd = 0;
	assert(lpOverlapped);

	struct io_uring_writefile_op : io_uring_operations
	{
		virtual void do_complete(io_uring_cqe* cqe, DWORD* lpNumberOfBytes) override
		{
			if (cqe->res < 0) [[unlikely]]
				WSASetLastError(-cqe->res);
		}
	};

	// now, enter IOCP emul logic
	io_uring_writefile_op* op = io_uring_operation_allocator{}.allocate<io_uring_writefile_op>();
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	iocp->submit_io([&](struct io_uring_sqe* sqe)
	{
		io_uring_prep_write(sqe, s->native_handle(), lpBuffer, nNumberOfBytesToWrite, lpOverlapped->offset_64);
		io_uring_sqe_set_data(sqe, op);
	});

	WSASetLastError(ERROR_IO_PENDING);
	return false;
}

