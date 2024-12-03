
#pragma once


struct base_handle
{
	int _socket_fd;
	int ref_count;

	base_handle()
		: ref_count(1)
		, _socket_fd(-1)
	{}

	base_handle(int fd)
		: ref_count(1)
		, _socket_fd(fd)
	{}

	virtual ~base_handle() {}

	void ref(){ ref_count++;}

	void unref();

	int native_handle() {return _socket_fd; }

};

struct SOCKET_emu_class final : public base_handle
{
	base_handle* _iocp;
	ULONG_PTR _completion_key;

	SOCKET_emu_class(int fd, base_handle* iocp = nullptr)
	 	: base_handle(fd)
		, _iocp(iocp)
		, _completion_key(0)
	{
	}

	virtual ~SOCKET_emu_class() override;
};

