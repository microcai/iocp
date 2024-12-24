
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

#include "internal_iocp_struct.hpp"

#include "operation_allocator.hpp"

#define IORING_CQE_F_USER 32


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

IOCP_DECL WORD MAKEWORD(uint a, uint b)
{
	return (a << 8) + b;
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

IOCP_DECL BOOL WINAPI CloseHandle(_In_ HANDLE h)
{
	h->unref();
	return true;
}

IOCP_DECL HANDLE WINAPI CreateIoCompletionPort(_In_ HANDLE FileHandle, _In_ HANDLE ExistingCompletionPort,
											   _In_ ULONG_PTR CompletionKey, _In_ DWORD NumberOfConcurrentThreads)
{
	auto iocphandle = dynamic_cast<iocp_handle_emu_class*>(ExistingCompletionPort);
	if (iocphandle == nullptr)
	{
		iocp_handle_emu_class* ret = new iocp_handle_emu_class{};
		iocphandle = ret;
	}

	if (FileHandle != INVALID_HANDLE_VALUE)
	{
		auto _impl = dynamic_cast<SOCKET_emu_class*>(FileHandle);
		_impl->_completion_key = CompletionKey;

		if (_impl->_iocp != iocphandle)
		{
			_impl->change_io_(iocphandle);
		}
	}

	return iocphandle;
}

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatus(
	_In_ HANDLE CompletionPort, _Out_ LPDWORD lpNumberOfBytes, _Out_ PULONG_PTR lpCompletionKey,
	_Out_ LPOVERLAPPED* lpOverlapped, _In_ DWORD dwMilliseconds)
{
	struct iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);
	*lpOverlapped = nullptr;

	std::unique_ptr<asio_operation> complete_result;

	if ( dwMilliseconds == 0)
	{
		iocp->io_.poll();
		std::scoped_lock<std::mutex> l(iocp->result_mutex);

		if (iocp->results_.empty())
		{
			SetLastError(WSA_WAIT_TIMEOUT);
			return false;
		}
		else
		{
			complete_result = std::move(iocp->results_.front());
			iocp->results_.pop_front();
		}
	}
	else
	{
		std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		while (iocp->results_.empty())
		{
			iocp->result_mutex.unlock();
			if (dwMilliseconds == INFINITE)
				iocp->io_.run_one();
			else
				iocp->io_.run_one_until(until);
			iocp->result_mutex.lock();
		}
		complete_result = std::move(iocp->results_.front());
		iocp->results_.pop_front();
	}

	*lpNumberOfBytes = complete_result->NumberOfBytes;
	*lpOverlapped = complete_result->overlapped_ptr;
	*lpCompletionKey = complete_result->CompletionKey;
	SetLastError(complete_result->last_error);
	return true;
}

IOCP_DECL BOOL WINAPI GetQueuedCompletionStatusEx(
  _In_  HANDLE             CompletionPort,
  _Out_ LPOVERLAPPED_ENTRY lpCompletionPortEntries,
  _In_  ULONG              ulCount,
  _Out_ PULONG             ulNumEntriesRemoved,
  _In_  DWORD              dwMilliseconds,
  _In_  BOOL               fAlertable)
{
	struct iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);
	*ulNumEntriesRemoved = 0;

	std::vector<asio_operation_ptr> complete_result;

	if ( dwMilliseconds == 0)
	{
		iocp->io_.poll();
		std::scoped_lock<std::mutex> l(iocp->result_mutex);

		if (iocp->results_.empty())
		{
			SetLastError(WSA_WAIT_TIMEOUT);
			return false;
		}
		else
		{
			while(!iocp->results_.empty() && complete_result.size() < ulCount)
			{
				complete_result.push_back(std::move(iocp->results_.front()));
				iocp->results_.pop_front();
			}
		}
	}
	else
	{
		std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		while (iocp->results_.empty())
		{
			iocp->result_mutex.unlock();
			if (dwMilliseconds == INFINITE)
				iocp->io_.run_one();
			else
				iocp->io_.run_one_until(until);

			while(iocp->io_.poll()){}

			iocp->result_mutex.lock();
		}

		while(!iocp->results_.empty() && complete_result.size() < ulCount)
		{
			complete_result.push_back(std::move(iocp->results_.front()));
			iocp->results_.pop_front();
		}
	}

	int number_of_results = 0;

	for(asio_operation_ptr& res : complete_result)
	{
		lpCompletionPortEntries[number_of_results].lpCompletionKey = res->CompletionKey;
		lpCompletionPortEntries[number_of_results].lpOverlapped = res->overlapped_ptr;
		lpCompletionPortEntries[number_of_results].dwNumberOfBytesTransferred = res->NumberOfBytes;
		SetLastError(res->last_error);
		number_of_results ++;
	}

	*ulNumEntriesRemoved = number_of_results;
	return true;
}

IOCP_DECL BOOL WINAPI PostQueuedCompletionStatus(
  _In_     HANDLE       CompletionPort,
  _In_     DWORD        dwNumberOfBytesTransferred,
  _In_     ULONG_PTR    dwCompletionKey,
  _In_opt_ LPOVERLAPPED lpOverlapped)
{
	iocp_handle_emu_class* iocp = dynamic_cast<iocp_handle_emu_class*>(CompletionPort);

	{
		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(new asio_operation{lpOverlapped, dwCompletionKey, dwNumberOfBytesTransferred});
	}

	if (!iocp->io_.get_executor().running_in_this_thread())
	{
		asio::post(iocp->io_, [](){});
	}

	return true;
}

IOCP_DECL BOOL WINAPI CancelIo(_In_ HANDLE hFile)
{
	SOCKET_emu_class* s = dynamic_cast<SOCKET_emu_class*>(hFile);

	iocp_handle_emu_class* iocp = s->_iocp;

	return s->cancel_all();
}

IOCP_DECL BOOL WINAPI CancelIoEx(_In_ HANDLE  hFile, _In_opt_ LPOVERLAPPED lpOverlapped)
{
	reinterpret_cast<asio_operation*>(lpOverlapped->Internal)->cancel_signal.emit(asio::cancellation_type::partial);

	return TRUE;
}

/** ********************************************************************************** **/
// WSA* for use with IOCP
/** ********************************************************************************** **/

IOCP_DECL SOCKET WSASocket(_In_ int af, _In_ int type, _In_ int protocol, _In_ LPWSAPROTOCOL_INFO lpProtocolInfo,
						   _In_ SOCKET g, _In_ DWORD dwFlags)
{
	assert(lpProtocolInfo == 0);
	if (dwFlags == WSA_FLAG_FAKE_CREATION)
		return new SOCKET_emu_class{-1};
	return new SOCKET_emu_class{af, type};
}

IOCP_DECL SOCKET WSAAccept(
  _In_        SOCKET          s,
  _Out_       sockaddr        *addr,
  _Inout_     LPINT           addrlen,
  _In_        LPCONDITIONPROC lpfnCondition,
  _In_        DWORD_PTR       dwCallbackData)
{
	int new_sock = accept(s->native_handle(), addr, (socklen_t*) addrlen);
	if (new_sock < 0)
		return INVALID_SOCKET;
	return new SOCKET_emu_class{ SOCKET_emu_class::tcp_sock{SOCKET_emu_class::internal_fake_io_context,
		addr->sa_family == AF_INET6 ? asio::ip::tcp::v6() : asio::ip::tcp::v4(),
	 	new_sock} };
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

	assert(lpOverlapped);

	struct accept_op : asio_operation
	{
		SOCKET_emu_class* accept_into;

		PVOID lpOutputBuffer;
	};

	accept_op* op = new accept_op{};
	op->overlapped_ptr = lpOverlapped;
	op->CompletionKey = listen_sock->_completion_key;

	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);

	op->accept_into = dynamic_cast<SOCKET_emu_class*>(sAcceptSocket);
	op->CompletionKey = listen_sock->_completion_key;
	op->lpOutputBuffer = lpOutputBuffer;

	op->accept_into->_iocp = iocp;
	op->accept_into->construct_tcp_socket();

	listen_sock->async_accept(op->accept_into, asio::bind_cancellation_slot(op->cancel_signal.slot(), [iocp, op](asio::error_code ec)
	{
		op->last_error = ec.value();

		if (op->lpOutputBuffer)
		{
			auto& new_sock = op->accept_into->tcp_socket();

			auto local_addr = new_sock.local_endpoint();
			auto remote_addr =  new_sock.remote_endpoint();

			uint8_t local_addr_len = local_addr.size();
			uint8_t remote_addr_len = remote_addr.size();

			memcpy(op->lpOutputBuffer, &local_addr_len, 1);

			memcpy(reinterpret_cast<char*>(op->lpOutputBuffer) + 1, local_addr.data(), local_addr_len);

			memcpy(reinterpret_cast<char*>(op->lpOutputBuffer) + 1 + local_addr_len, &remote_addr_len, 1);
			memcpy(reinterpret_cast<char*>(op->lpOutputBuffer) + 2 + local_addr_len, remote_addr.data(), remote_addr_len);

			// *lpNumberOfBytes = remote_addr_len + local_addr_len + 2;			// TODO, encode local and remote address to outputbuffer
		}

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

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

template <typename Protocal>
static asio::ip::basic_endpoint<Protocal> from_sockaddr(const sockaddr* name)
{
	asio::ip::address addr;
	asio::ip::port_type port;

	if ( name->sa_family == AF_INET)
	{
		sockaddr_in * v4addr = (sockaddr_in*) name;
		asio::ip::address_v4::bytes_type native_sin_addr;
		memcpy(&native_sin_addr, &(v4addr->sin_addr), 4);
		addr = asio::ip::address_v4{native_sin_addr};
		port = ntohs(v4addr->sin_port);
	}
	else if ( name->sa_family == AF_INET6)
	{
		sockaddr_in6 * v6addr = (sockaddr_in6*) name;
		asio::ip::address_v6::bytes_type native_sin_addr;
		memcpy(&native_sin_addr, &(v6addr->sin6_addr), 4);
		addr = asio::ip::address_v6{native_sin_addr, v6addr->sin6_scope_id};
		port = ntohs(v6addr->sin6_port);
	}

	return asio::ip::basic_endpoint<Protocal>{addr, port};
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

	if (s->type == SOCK_STREAM)
		s->construct_tcp_socket();

	struct connect_op : asio_operation
	{
		PVOID lpSendBuffer;
		DWORD dwSendDataLength;
		SOCKET_emu_class* sock;
	};

	// now, enter IOCP emul logic
	connect_op* op = new connect_op{};
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;
	op->lpSendBuffer = lpSendBuffer;
	op->dwSendDataLength = dwSendDataLength;
	op->sock = s;

	asio::ip::tcp::endpoint endpoint = from_sockaddr<asio::ip::tcp>(name);

	s->async_connect(endpoint, asio::bind_cancellation_slot(op->cancel_signal.slot(), [op, iocp](asio::error_code ec)
	{
		op->last_error = ec.value();

		if (op->lpSendBuffer && op->dwSendDataLength)
		{
			// TODO
			// s->async_send();
			// WSASend()
		}

		op->NumberOfBytes = 0;

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

	WSASetLastError(ERROR_IO_PENDING);
	return false;
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

	struct write_op : asio_operation
	{
		std::vector<asio::const_buffer> buffers;
	};

	// now, enter IOCP emul logic
	write_op* op = new write_op;
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	op->buffers.resize(dwBufferCount);
	int64_t total_send_bytes = 0;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->buffers[i] = asio::buffer(lpBuffers[i].buf, lpBuffers[i].len);
		total_send_bytes += lpBuffers[i].len;
	}

	s->async_send(op->buffers, asio::bind_cancellation_slot(op->cancel_signal.slot(), [iocp, op](asio::error_code ec, std::size_t bytes_transfered)
	{
		op->last_error = ec.value();
		op->NumberOfBytes = bytes_transfered;

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

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

	struct read_op : asio_operation
	{
		std::vector<asio::mutable_buffer> buffers;
	};

	// now, enter IOCP emul logic
	read_op* op = new read_op{};

	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	op->buffers.resize(dwBufferCount);
	int64_t total_send_bytes = 0;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->buffers[i] = asio::buffer(lpBuffers[i].buf, lpBuffers[i].len);
		total_send_bytes += lpBuffers[i].len;
	}

	s->async_receive(op->buffers, asio::bind_cancellation_slot(op->cancel_signal.slot(), [iocp, op](asio::error_code ec, std::size_t bytes_transfered)
	{
		op->last_error = ec.value();
		op->NumberOfBytes = bytes_transfered;

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

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
		WSASetLastError(EOPNOTSUPP);
		return SOCKET_ERROR;
	}

	iocp_handle_emu_class* iocp = s->_iocp;

	assert(lpOverlapped);

	lpOverlapped->InternalHigh = (ULONG_PTR) __builtin_extract_return_addr (__builtin_return_address (0));

	if (lpNumberOfBytesSent) [[likely]]
		*lpNumberOfBytesSent = 0;

	struct write_op : asio_operation
	{
		std::vector<asio::const_buffer> buffers;
	};

	// now, enter IOCP emul logic
	write_op* op = new write_op;
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	op->buffers.resize(dwBufferCount);
	int64_t total_send_bytes = 0;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->buffers[i] = asio::buffer(lpBuffers[i].buf, lpBuffers[i].len);
		total_send_bytes += lpBuffers[i].len;
	}

	asio::ip::udp::endpoint dest = from_sockaddr<asio::ip::udp>(lpTo);


	s->async_sendto(op->buffers, dest, asio::bind_cancellation_slot(op->cancel_signal.slot(), [iocp, op](asio::error_code ec, std::size_t bytes_transfered)
	{
		op->last_error = ec.value();
		op->NumberOfBytes = bytes_transfered;

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

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

	struct read_op : asio_operation
	{
		sockaddr* out_remote_addr;
		asio::ip::udp::endpoint remote_addr;
		std::vector<asio::mutable_buffer> buffers;
	};

	// now, enter IOCP emul logic
	read_op* op = new read_op{};

	op->out_remote_addr = lpFrom;
	op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;

	op->buffers.resize(dwBufferCount);
	int64_t total_send_bytes = 0;
	for (int i = 0; i < dwBufferCount; i++)
	{
		op->buffers[i] = asio::buffer(lpBuffers[i].buf, lpBuffers[i].len);
		total_send_bytes += lpBuffers[i].len;
	}

	s->async_receive_from(op->buffers, op->remote_addr, asio::bind_cancellation_slot(op->cancel_signal.slot(), [iocp, op](asio::error_code ec, std::size_t bytes_transfered)
	{
		op->last_error = ec.value();
		op->NumberOfBytes = bytes_transfered;

		memcpy(op->out_remote_addr, op->remote_addr.data(), op->remote_addr.size());

		std::scoped_lock<std::mutex> l(iocp->result_mutex);
		iocp->results_.emplace_back(op);
	}));

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

	// asio does not support async shutdown, do it sync
	shutdown(s->native_handle(), SHUT_RDWR);


	asio_operation* op = new asio_operation { lpOverlapped, s->_completion_key, 0, 0};
	// op->lpCompletionRoutine = lpCompletionRoutine;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);

	std::scoped_lock<std::mutex> l(iocp->result_mutex);
	iocp->results_.emplace_back(op);

	WSASetLastError(0);
	return TRUE;
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

	int readed = read(s->native_handle(), lpBuffer, nNumberOfBytesToRead);
	if (lpNumberOfBytesRead)
		*lpNumberOfBytesRead = readed;

	asio_operation* op = new asio_operation;
	// op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;
	op->last_error = readed == 0 ? ERROR_HANDLE_EOF : 0;
	op->NumberOfBytes = readed;

	std::scoped_lock<std::mutex> l(iocp->result_mutex);
	iocp->results_.emplace_back(op);

	SetLastError(ERROR_IO_PENDING);

	return readed > 0;
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

	auto write_ret = write(s->native_handle(), lpBuffer, nNumberOfBytesToWrite);

	if (lpNumberOfBytesWritten)
		*lpNumberOfBytesWritten = write_ret;


	// now, enter IOCP emul logic
	asio_operation* op = new asio_operation;
	// op->lpCompletionRoutine = lpCompletionRoutine;
	op->overlapped_ptr = lpOverlapped;
	lpOverlapped->Internal = reinterpret_cast<ULONG_PTR>(op);
	op->CompletionKey = s->_completion_key;
	op->last_error = errno;
	op->NumberOfBytes = write_ret;

	std::scoped_lock<std::mutex> l(iocp->result_mutex);
	iocp->results_.emplace_back(op);

	SetLastError(ERROR_IO_PENDING);

	return write_ret > 0;
}

asio::io_context SOCKET_emu_class::internal_fake_io_context;
