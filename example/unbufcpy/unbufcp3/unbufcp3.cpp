/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Copyright (C) 2024 - 2025.  microcai.  All rights reserved.

Module Name:

    unbufcp3.cpp

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
#define BUFFER_SIZE (1024*1024*16)

#include "universal_async.hpp"
#include "universal_fiber.hpp"

#include <chrono>

//
// Define the size of the buffers used to do the I/O.
// 64K is a nice number.
//

//
// The system's page size will always be a multiple of the
// sector size. Do all I/Os in page-size chunks.
//
DWORD PageSize;

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

	FiberOVERLAPPED read_ov, write_ov;
    read_ov.set_offset(0);
    write_ov.set_offset(0);

    char* pri_buf, *back_buf;
	std::array<char*, 2> buffer;

	pri_buf = buffer[0] = (char*) malloc(BUFFER_SIZE);
	back_buf = buffer[1] = (char*) malloc(BUFFER_SIZE);

	DWORD readLength = 0, back_readLength = 0, written = 0;
	auto ret = ReadFile(SourceFile, pri_buf, BUFFER_SIZE, &readLength, &read_ov);
    read_ov.last_error = GetLastError();
	if (!(!ret && read_ov.last_error != ERROR_IO_PENDING && read_ov.last_error != ERROR_MORE_DATA))
		readLength = get_overlapped_result(read_ov);

	while (readLength > 0)
	{
        read_ov.add_offset(readLength);
		ret = ReadFile(SourceFile, back_buf, BUFFER_SIZE, &back_readLength, &read_ov);
        read_ov.last_error = GetLastError();

		bool read_file_pending = !(!ret && read_ov.last_error != ERROR_IO_PENDING && read_ov.last_error != ERROR_MORE_DATA);

		auto result = WriteFile(DestFile, pri_buf, (readLength + PageSize - 1) & ~(PageSize - 1), &written, &write_ov);
        write_ov.last_error = WSAGetLastError();
		if (!(!result && write_ov.last_error != ERROR_IO_PENDING))
		{
            written = get_overlapped_result(write_ov);
		}
        write_ov.add_offset(written);

		if (write_ov.last_error)
		{
			if (read_file_pending)
			{
				// socket 发送错误，取消已经投递的文件读取请求
				CancelIoEx(SourceFile, &read_ov);
				// 无视取消是否成功，都等待文件读取请求。
				// 如果取消失败，（比如实际上文件已经读取成功）
				// 那么就当是无所谓了。
				// 如果取消成功，则 get_overlapped_result 会返回个错误
				// 但是不管 get_overlapped_result 返回的是啥，都已经无关紧要了
				// 这里还要调用 get_overlapped_result 仅仅是为了避免 退出本
				// 协程后，&file_ov 已经被系统 API给引用，防止野指针问题.
				back_readLength = get_overlapped_result(read_ov);
				// 现在，可以安全的执行 co_return -1 退出协程了.
			}

			exit(1);
		}

		if (read_file_pending)
		{
			back_readLength = get_overlapped_result(read_ov);
		}
		readLength = back_readLength;
		std::swap(pri_buf, back_buf);
	}
    //
    // All done. There is no need to call VirtualFree() to free CopyChunk
    // buffers here. The buffers will be freed when this process exits.
    //

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
