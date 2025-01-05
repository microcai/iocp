/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Copyright (C) 2024 - 2025.  microcai.  All rights reserved.

Module Name:

    unbufcp4.cpp

Abstract:

    This single-threaded version shows how to multiplex I/O to a number of files with
    a single thread. This is the most efficient mechanism if you do not need the
    asynchronous completion model that the dual-threaded version offers.

    Only one thread and one I/O completion port is used. The file handles for the
    source and destination file are both associated with the same port.  The
    thread starts off by posting a number of overlapped
    reads from the source file.  It then waits on the I/O completion port.
    Whenever a read completes, it immediately turns it around into a write to
    the destination file. Whenever a write completes, it immediately posts the
    next read from the source file.

    Thread 1
       |
       |
    kick off a few
    overlapped reads
       |
       |
  ->GetQueuedCompletionStatus(WritePort) <-----------
  |    |                                            |
  |    |-------------------------------             |
  |    |                              |             |
  | write has completed,      read has completed,   |
  | kick off another          kick off the write.   |
  | read                              |             |
  |    |                              |             |
  |____|                              |_____________|

--*/
#define BUFFER_SIZE (4*1024*1024)

#define PENDING_IO 20

#include "universal_async.hpp"
#include "universal_fiber.hpp"

#include <chrono>
#include <deque>

//
// Define the size of the buffers used to do the I/O.
// 64K is a nice number.
//

//
// The system's page size will always be a multiple of the
// sector size. Do all I/Os in page-size chunks.
//
DWORD PageSize;

struct IOBuffer
{
	void* buf_ptr;
	operator char* () { return (char*) buf_ptr; }
	operator void* () { return (void*) buf_ptr; }

	IOBuffer()
	{
#ifdef _WIN32
		buf_ptr = VirtualAlloc(0, BUFFER_SIZE, MEM_COMMIT|MEM_64K_PAGES, PAGE_READWRITE);
#else
		buf_ptr = malloc(BUFFER_SIZE);
#endif
	}
	~IOBuffer()
	{
#ifdef _WIN32
		VirtualFree(buf_ptr, 0, MEM_RELEASE);
#else
		free(buf_ptr);
#endif
	}
};

template<typename T>
class FiberChannel
{
	void wake_up_one_pusher()
	{
		if (m_push_awaiting.empty())
			return;
		auto top_waiter = m_push_awaiting.front();
		// wake up .
		m_push_awaiting.pop_front();
		PostQueuedCompletionStatus(m_iocp, 0, 
			(ULONG_PTR)(iocp::overlapped_proc_func) & process_stack_full_overlapped_event
			, top_waiter);
	}
	void wake_up_one_poper()
	{
		if (m_pop_awaiting.empty())
			return;
		auto top_waiter = m_pop_awaiting.front();
		// wake up .
		m_pop_awaiting.pop_front();
		PostQueuedCompletionStatus(m_iocp, 0,
			(ULONG_PTR)(iocp::overlapped_proc_func)&process_stack_full_overlapped_event
			, top_waiter);
	}

public:
	T pop()
	{
		if (m_queue.empty())
		{
			FiberOVERLAPPED ov;
			// yield
			m_pop_awaiting.push_back(&ov);
			get_overlapped_result(ov);
		}
		T r = m_queue.front();
		m_queue.pop_front();

		if (m_queue.size() < m_max_pending)
		{
			wake_up_one_pusher();
		}

		return r;
	}

	void push(T t)
	{
		m_queue.push_back(t);
		wake_up_one_poper();
		if (m_queue.size() > m_max_pending)
		{
			// sleep until wakeup.
			FiberOVERLAPPED ov;
			// yield
			m_push_awaiting.push_back(&ov);
			get_overlapped_result(ov);
		}
	}

	FiberChannel(HANDLE iocp, long max_pending)
		: m_max_pending(max_pending)
		, m_iocp(iocp)
	{
	}

	long m_max_pending = 1;
	std::deque<T> m_queue;

	HANDLE m_iocp;
	std::deque<FiberOVERLAPPED*> m_pop_awaiting;
	std::deque<FiberOVERLAPPED*> m_push_awaiting;
};

static void write_loop(FiberChannel<WSABUF>& channel, FiberChannel<uint64_t>& channel2, HANDLE destFile)
{
	DWORD written;
	uint64_t all_written = 0;
	FiberOVERLAPPED write_ov;
	write_ov.set_offset(0);

	while (1)
	{
		auto buf = channel.pop();
		if (buf.len == 0)
		{
			channel2.push(all_written);
			return;
		}
		auto result = WriteFile(destFile, buf.buf, buf.len, &written, &write_ov);

		write_ov.last_error = WSAGetLastError();
		if (!(!result && write_ov.last_error != ERROR_IO_PENDING))
		{
			written = get_overlapped_result(write_ov);
		}
		all_written += written;
		write_ov.add_offset(written);
	}
}

static void read_loop(FiberChannel<WSABUF>& channel, FiberChannel<uint64_t>& channel2, HANDLE srcFile)
{
	DWORD readbytes = 1;

	std::array<IOBuffer, PENDING_IO> buffers;

	FiberOVERLAPPED read_ov;
	read_ov.set_offset(0);
	for (int buffer_index = 0; readbytes; buffer_index = (buffer_index +1) % PENDING_IO )
	{
		auto ret = ReadFile(srcFile, buffers[buffer_index], BUFFER_SIZE, &readbytes, &read_ov);
		read_ov.last_error = GetLastError();
		if (!(!ret && read_ov.last_error != ERROR_IO_PENDING))
			readbytes = get_overlapped_result(read_ov);
		if (readbytes)
		{
			read_ov.add_offset(readbytes);
			channel.push(WSABUF{ .len = (readbytes + PageSize - 1) & ~(PageSize - 1) , .buf = buffers[buffer_index] });
		}
	}
	channel.push(WSABUF{ .len = 0, .buf = 0 });

	// wait until write complete, so buffers can be safe to be deconstructed.
	channel2.pop();
}


static void copy_coroutine(HANDLE IoPort, std::string sourcefilename, std::string destfilename)
{
	ULARGE_INTEGER FileSize;
	ULARGE_INTEGER InitialSize;

    auto StartTime = std::chrono::steady_clock::now();

	//
	// Open the source file and create the destination file.
	// Use FILE_FLAG_NO_BUFFERING to avoid polluting the
	// system cache with two copies of the same data.
	//

	HANDLE SourceFile = CreateFileA(sourcefilename.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
		NULL);
	if (SourceFile == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "failed to open %s, error %d\n", sourcefilename.c_str(), GetLastError());
		exit(1);
	}
	FileSize.LowPart = GetFileSize(SourceFile, &FileSize.HighPart);
	if ((FileSize.LowPart == 0xffffffff) && (GetLastError() != NO_ERROR)) {
		fprintf(stderr, "GetFileSize failed, error %d\n", GetLastError());
		exit(1);
	}

	HANDLE DestFile = CreateFileA(destfilename.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_ALWAYS,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
		SourceFile);
	if (DestFile == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "failed to open %s, error %d\n", destfilename.c_str(), GetLastError());
		exit(1);
	}

	//
	// Extend the destination file so that the filesystem does not
	// turn our asynchronous writes into synchronous ones.
	//
	InitialSize.QuadPart = (FileSize.QuadPart + PageSize - 1) & ~((DWORD_PTR)(PageSize - 1));
	auto Status = SetFilePointer(DestFile,
		InitialSize.LowPart,
		(PLONG)&InitialSize.HighPart,
		FILE_BEGIN);
	if ((Status == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)) {
		fprintf(stderr, "initial SetFilePointer failed, error %d\n", GetLastError());
		exit(1);
	}
	auto Success = SetEndOfFile(DestFile);
	if (!Success) {
		fprintf(stderr, "SetEndOfFile failed, error %d\n", GetLastError());
		exit(1);
	}

	SetFilePointer(DestFile, 0, 0, FILE_BEGIN);

	bind_stackfull_iocp(SourceFile, IoPort);
	bind_stackfull_iocp(DestFile, IoPort);

	FiberChannel<WSABUF> channel(IoPort, PENDING_IO);
	FiberChannel<uint64_t> channel2(IoPort, 1);

	// spawn a write coro
	create_detached_coroutine(std::bind(&write_loop, std::ref(channel), std::ref(channel2), DestFile));

	// read and maintain at most PENDING_IO concurrent buffers
	read_loop(channel, channel2, SourceFile);

    CloseHandle(SourceFile);
    CloseHandle(DestFile);

    //
    // We need another handle to the destination file that is
    // opened without FILE_FLAG_NO_BUFFERING. This allows us to set
    // the end-of-file marker to a position that is not sector-aligned.
    //
    HANDLE BufferedHandle = CreateFileA(destfilename.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);
    if (BufferedHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr,
                "failed to open buffered handle to %s, error %d\n",
                destfilename.c_str(),
                GetLastError());
        exit(1);
    }


    //
    // Set the destination's file size to the size of the
    // source file, in case the size of the source file was
    // not a multiple of the page size.
    //
    Status = SetFilePointer(BufferedHandle,
                            FileSize.LowPart,
                            (PLONG)&FileSize.HighPart,
                            FILE_BEGIN);
    if ((Status == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)) {
        fprintf(stderr, "final SetFilePointer failed, error %d\n", GetLastError());
        exit(1);
    }
    Success = SetEndOfFile(BufferedHandle);
    if (!Success) {
        fprintf(stderr, "SetEndOfFile failed, error %d\n", GetLastError());
        exit(1);
    }
    CloseHandle(BufferedHandle);

    auto EndTime = std::chrono::steady_clock::now();

    auto duration = EndTime - StartTime;
    auto dur_sec = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() / 1000.0f;

    printf("%llu bytes copied in %.3f seconds\n", FileSize.QuadPart, dur_sec);
    printf("%.2f MB/sec\n", ((LONGLONG)FileSize.QuadPart/(1024.0*1024.0)) / dur_sec);


	create_detached_coroutine(std::bind(&copy_coroutine, IoPort, sourcefilename, destfilename));
	//exit(0);
}


int main(int argc, char* argv[])
{

	//
	// I/O completion port. All read and writes to the files complete
	// to this port.
	//
	HANDLE IoPort;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s SourceFile DestinationFile\n", argv[0]);
		exit(1);
	}

#ifdef _WIN32
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	PageSize = SystemInfo.dwPageSize;
#else
	PageSize = 4096;
#endif

	//
	//we are running on NT 3.51 or greater.
	//Create the I/O Completion Port
	//
	IoPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,//file handle to associate with I/O completion port
		NULL,                //optional handle to existing I/O completion port
		0,                            //completion key
		1);               //# of threads allowed to execute concurrently


	if (IoPort == NULL) {
		fprintf(stderr, "failed to create ReadPort, error %d\n", GetLastError());
		exit(1);
	}

	//
	// Do the copy
	//
	create_detached_coroutine(std::bind(&copy_coroutine, IoPort, std::string(argv[1]), std::string(argv[2])));

	iocp::run_event_loop(IoPort);
}
