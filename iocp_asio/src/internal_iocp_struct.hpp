
#pragma once

#include <mutex>
#include <deque>
#include <map>

#include "asio.hpp"

#include "iocp.h"
#include "operation_allocator.hpp"

struct base_handle
{
	int ref_count;

	base_handle()
		: ref_count(1)
	{}

	base_handle(int fd)
		: ref_count(1)
	{}

	virtual ~base_handle() {}
	virtual int native_handle() {return  -1;}

	void ref(){ ref_count++;}

	void unref()
	{
		if (--ref_count == 0)
		{
			delete this;
		}
	}

};

namespace {

struct iocp_handle_emu_class final : public base_handle
{
	asio::io_context io_;

	std::mutex result_mutex;
	std::deque<asio_operation_ptr> results_;
	asio::executor_work_guard<asio::any_io_executor> work_guard;

	// std::map<OVERLAPPED*, asio_operation_ptr> pending_io_;

	iocp_handle_emu_class()
		: work_guard(io_.get_executor())
	{
	}

	~iocp_handle_emu_class()
	{
	}
};

struct SOCKET_emu_class final : public base_handle
{
	iocp_handle_emu_class* _iocp;
	ULONG_PTR _completion_key;

	using normal_file = int;
	using tcp_sock = asio::ip::tcp::socket;
	using udp_sock = asio::ip::udp::socket;
	using acceptor = asio::ip::tcp::acceptor;

	using variant_sock = std::variant<normal_file, tcp_sock, udp_sock, acceptor>;

	static asio::io_context internal_fake_io_context;

	variant_sock sock_;

	int af_family;
	int type;

	SOCKET_emu_class(int af_family, int type, iocp_handle_emu_class* iocp = nullptr)
	 	:  _iocp(iocp)
		, _completion_key(0)
		, af_family(af_family)
		, type(type)
		, sock_(std::in_place_type_t<acceptor>{}, internal_fake_io_context)
	{
		accept_socket().open(af_family == AF_INET6 ? asio::ip::tcp::v6() : asio::ip::tcp::v4());
		// 一个空 socket, 但是其实真正创建的地方，是在
		// ConnectEx/AcceptEx 两个地方。
	}

	SOCKET_emu_class(int fd, iocp_handle_emu_class* iocp = nullptr)
	 	:  _iocp(iocp)
		, _completion_key(0)
		, af_family(AF_UNSPEC)
		, type(0)
		, sock_(std::in_place_type_t<normal_file>{}, fd)
	{
		// 一个空 socket, 但是其实真正创建的地方，是在
		// ConnectEx/AcceptEx 两个地方。
	}


	void construct_tcp_socket()
	{
		sock_.emplace<tcp_sock>(_iocp->io_);
	}

	void construct_udp_socket()
	{
		sock_.emplace<udp_sock>(_iocp->io_);
	}

	void change_io_(iocp_handle_emu_class* iocp)
	{
		this->_iocp = iocp;
		int fd;

		// re construct socket
		switch (sock_.index())
		{
			case 1:
				fd = release_native_fd();
				sock_.emplace<tcp_sock>( iocp->io_);
				tcp_socket().assign(af_family == AF_INET6 ? asio::ip::tcp::v6() : asio::ip::tcp::v4() , fd);
				break;
			case 2:
				fd = release_native_fd();
				sock_.emplace<udp_sock>( iocp->io_);
				udp_socket().assign(af_family == AF_INET6 ? asio::ip::udp::v6() : asio::ip::udp::v4() , fd);
				break;
			case 3:
				fd = release_native_fd();
				sock_.emplace<acceptor>( iocp->io_);
				accept_socket().assign(af_family == AF_INET6 ? asio::ip::tcp::v6() : asio::ip::tcp::v4() , fd);
				break;
			// case 4:
			// 	sock_.emplace<4>( iocp, fd );
			// 	break;
		}

	}

	virtual ~SOCKET_emu_class() override
	{
		if (std::holds_alternative<normal_file>(sock_))
		{
			auto fd = std::get<normal_file>(sock_);
			if (fd >= 0)
				close(fd);
		}
	}

	virtual int native_handle() override
	{
		return std::visit([](auto& s){
			if constexpr (std::is_same_v<decltype(s), normal_file&>)
			{
				return s;
			}
			else
			{
				return s.native_handle();
			}
		}, sock_);
	}

	int release_native_fd()
	{
		return std::visit([](auto& s){
			if constexpr (std::is_same_v<decltype(s), normal_file&>)
			{
				return s;
			}
			else
			{
				if (s.is_open())
					return s.release();
				return -1;
			}
		}, sock_);
	}

	tcp_sock& tcp_socket() { return std::get<tcp_sock>(sock_); };
	udp_sock& udp_socket() { return std::get<udp_sock>(sock_); };
	acceptor& accept_socket() { return std::get<acceptor>(sock_); };

	template<typename Handler>
	void async_accept(SOCKET_emu_class* into, Handler&& handler)
	{
		acceptor& accept_sock = std::get<acceptor>(sock_);

		accept_sock.async_accept(into->tcp_socket(), std::forward<Handler>(handler));
	}

	template<typename Handler>
	void async_connect(asio::ip::address addr, asio::ip::port_type port, Handler&& handler)
	{
		assert(_iocp);
		if (std::holds_alternative<normal_file>(sock_))
		{
			int old_fd = std::get<normal_file>(sock_);
			if (old_fd >=0) close(old_fd);

			// construct a socket!
			if (type == SOCK_STREAM)
			{
				construct_tcp_socket();
			}
			else if(type == SOCK_DGRAM)
			{
				construct_udp_socket();
			}
		}

		if (type == SOCK_STREAM)
		{
			tcp_socket().async_connect(asio::ip::tcp::endpoint{addr, port}, std::forward<Handler>(handler));
		}
		else if (type == SOCK_DGRAM)
		{
			udp_socket().async_connect(asio::ip::udp::endpoint{addr, port}, std::forward<Handler>(handler));
		}
	}

	template<typename Buffer, typename Handler>
	void async_send(Buffer&& buf, Handler&& handler)
	{
		if (type == SOCK_STREAM)
		{
			tcp_socket().async_send(buf, std::forward<Handler>(handler));
		}
		else if (type == SOCK_DGRAM)
		{
			udp_socket().async_send(buf, std::forward<Handler>(handler));
		}
	}

	template<typename Buffer, typename Handler>
	void async_sendto(Buffer&& buf, asio::ip::udp::endpoint dest, Handler&& handler)
	{
		if (type == SOCK_DGRAM)
		{
			udp_socket().async_send_to(buf, dest, std::forward<Handler>(handler));
		}
	}

	template<typename Buffer, typename Handler>
	void async_receive(Buffer&& buf, Handler&& handler)
	{
		if (type == SOCK_STREAM)
		{
			tcp_socket().async_receive(buf, std::forward<Handler>(handler));
		}
		else if (type == SOCK_DGRAM)
		{
			udp_socket().async_receive(buf, std::forward<Handler>(handler));
		}
	}

};

}