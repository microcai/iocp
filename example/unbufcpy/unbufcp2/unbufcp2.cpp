/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Copyright (C) 1994 - 2000.  Microsoft Corporation.  All rights reserved.


Module Name:

    unbufcp2.c

Abstract:

    Intended to demonstrate how to complete I/O in a different thread
    asynchronously than the I/O was started from.  This is useful for
    people who want a more powerful mechanism of asynchronous completion
    callbacks than Win32 provides (i.e. VMS developers who are used to ASTs).

    Two threads are used. The first thread posts overlapped reads from the
    source file. These reads complete to an I/O completion port the second
    thread is waiting on. The second thread sees the I/O completion and
    posts an overlapped write to the destination file. The write completes
    to another I/O completion port that the first thread is waiting on.
    The first thread sees the I/O completion and posts another overlapped
    read.

    Thread 1                                        Thread 2
       |                                               |
       |                                               |
    kick off a few                           -->GetQueuedCompletionStatus(ReadPort)
    overlapped reads                         |         |
       |                                     |         |
       |                                     |  read has completed,
  ->GetQueuedCompletionStatus(WritePort)     |  kick off the corresponding
  |    |                                     |  write.
  | write has completed,                     |         |
  | kick off another                         |_________|
  | read
  |    |
  |____|

--*/

#ifdef _IA64_
#pragma warning(disable:4100 4127)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include "iocp.h"
#endif
#include <chrono>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

//
// File handles for the copy operation. All read operations are
// from SourceFile. All write operations are to DestFile.
//
HANDLE SourceFile;
HANDLE DestFile;

//
// I/O completion ports. All reads from the source file complete
// to ReadPort. All writes to the destination file complete to
// WritePort.
//
HANDLE ReadPort;
HANDLE WritePort;

//
// Structure used to track each outstanding I/O. The maximum
// number of I/Os that will be outstanding at any time is
// controllable by the MAX_CONCURRENT_IO definition.
//

#define MAX_CONCURRENT_IO 20

typedef struct _COPY_CHUNK {
    OVERLAPPED Overlapped;
    LPVOID Buffer;
} COPY_CHUNK, *PCOPY_CHUNK;

COPY_CHUNK CopyChunk[MAX_CONCURRENT_IO];

//
// Define the size of the buffers used to do the I/O.
// 64K is a nice number.
//
#define BUFFER_SIZE (64*1024)

//
// The system's page size will always be a multiple of the
// sector size. Do all I/Os in page-size chunks.
//
DWORD PageSize;


//
// Local function prototypes
//
int
WriteLoop(
    ULARGE_INTEGER FileSize
    );

void ReadLoop(
    ULARGE_INTEGER FileSize
    );

int main(
    int argc,
    char *argv[]
    )
{
    std::thread WritingThread;
    ULARGE_INTEGER FileSize;
    ULARGE_INTEGER InitialFileSize;
    BOOL Success;
    DWORD Status;
    HANDLE BufferedHandle;

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
    // Open the source file and create the destination file.
    // Use FILE_FLAG_NO_BUFFERING to avoid polluting the
    // system cache with two copies of the same data.
    //

    SourceFile = CreateFile(argv[1],
                            GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ,
                            NULL,
                            OPEN_EXISTING,
                            FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
                            NULL);
    if (SourceFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "failed to open %s, error %d\n", argv[1], GetLastError());
        exit(1);
    }
    FileSize.LowPart = GetFileSize(SourceFile, &FileSize.HighPart);
    if ((FileSize.LowPart == 0xffffffff) && (GetLastError() != NO_ERROR)) {
        fprintf(stderr, "GetFileSize failed, error %d\n", GetLastError());
        exit(1);
    }

    DestFile = CreateFile(argv[2],
                          GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          CREATE_ALWAYS,
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
                          SourceFile);
    if (DestFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "failed to open %s, error %d\n", argv[2], GetLastError());
        exit(1);
    }

    //
    // Extend the destination file so that the filesystem does not
    // turn our asynchronous writes into synchronous ones.
    //
    InitialFileSize.QuadPart = (FileSize.QuadPart + PageSize - 1) & ~((DWORD_PTR)(PageSize - 1));
    Status = SetFilePointer(DestFile,
                            InitialFileSize.LowPart,
                            (PLONG)&InitialFileSize.HighPart,
                            FILE_BEGIN);
    if ((Status == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)) {
        fprintf(stderr, "SetFilePointer failed, error %d\n", GetLastError());
        exit(1);
    }
    Success = SetEndOfFile(DestFile);
    if (!Success) {
        fprintf(stderr, "SetEndOfFile failed, error %d\n", GetLastError());
        exit(1);
    }


    {
        ReadPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, //file handle to associate with I/O completion port
                                        NULL,                 //optional handle to existing I/O completion port
                                        (DWORD_PTR)SourceFile,    //completion key
                                        1);                   //# of threads allowed to execute concurrently




        //
        //If we need to, aka we're running on NT 3.51, let's associate a file handle with the
        //completion port.
        //
        ReadPort = CreateIoCompletionPort(SourceFile,
                                        ReadPort,
                                        (DWORD_PTR)SourceFile,  //should be the previously specified key.
                                        1);

        if (ReadPort == NULL)
        {
        fprintf(stderr,
                "failed to create ReadPort, error %d\n",
                GetLastError());

        exit(1);

        }
    }

    WritePort = CreateIoCompletionPort(DestFile,
                                       NULL,
                                       (DWORD_PTR)DestFile,
                                       1);
    if (WritePort == NULL) {
        fprintf(stderr, "failed to create WritePort, error %d\n", GetLastError());
        exit(1);
    }

    //
    // Start the writing thread
    //
    WritingThread = std::thread(&WriteLoop, FileSize);

    WritingThread.detach();

    auto StartTime = std::chrono::steady_clock::now();

    //
    // Start the reads
    //
    ReadLoop(FileSize);

    auto EndTime = std::chrono::steady_clock::now();

    //
    // We need another handle to the destination file that is
    // opened without FILE_FLAG_NO_BUFFERING. This allows us to set
    // the end-of-file marker to a position that is not sector-aligned.
    //
    BufferedHandle = CreateFile(argv[2],
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);
    if (BufferedHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr,
                "failed to open buffered handle to %s, error %d\n",
                argv[2],
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
    CloseHandle(SourceFile);
    CloseHandle(DestFile);

    auto dur_ms =std::chrono::duration_cast<std::chrono::milliseconds> (EndTime-StartTime);

    float dur_sec = dur_ms.count() / 1000.0f;

    printf("\n\n%d bytes copied in %.3f seconds\n",
           FileSize.LowPart, dur_sec);
    printf("%.2f MB/sec\n",
           ((LONGLONG)FileSize.QuadPart/(1024.0*1024.0)) / dur_sec);

    return(0);

}

void ReadLoop(ULARGE_INTEGER FileSize)
{
    ULARGE_INTEGER ReadPointer;
    BOOL Success;
    DWORD NumberBytes;
    LPOVERLAPPED CompletedOverlapped;
    ULONG_PTR Key;
    PCOPY_CHUNK Chunk;
    int PendingIO = 0;
    int i;

    //
    // Start reading the file. Kick off MAX_CONCURRENT_IO reads, then just
    // loop waiting for writes to complete.
    //
    ReadPointer.QuadPart = 0;

    for (i=0; i < MAX_CONCURRENT_IO; i++) {
        if (ReadPointer.QuadPart >= FileSize.QuadPart) {
            break;
        }
        //
        // Use VirtualAlloc so we get a page-aligned buffer suitable
        // for unbuffered I/O.
        //
        CopyChunk[i].Buffer = malloc(BUFFER_SIZE);

        if (CopyChunk[i].Buffer == NULL) {
            fprintf(stderr, "VirtualAlloc %d failed, error %d\n",i, GetLastError());
            exit(1);
        }
        CopyChunk[i].Overlapped.Offset = ReadPointer.LowPart;
        CopyChunk[i].Overlapped.OffsetHigh = ReadPointer.HighPart;
        CopyChunk[i].Overlapped.hEvent = NULL;     // not needed

        Success = ReadFile(SourceFile,
                           CopyChunk[i].Buffer,
                           BUFFER_SIZE,
                           &NumberBytes,
                           &CopyChunk[i].Overlapped);

        if (!Success && (GetLastError() != ERROR_IO_PENDING)) {
            fprintf(stderr,
                    "ReadFile at %lx failed, error %d\n",
                    ReadPointer.LowPart,
                    GetLastError());
            exit(1);
        } else {
            ReadPointer.QuadPart += BUFFER_SIZE;
            ++PendingIO;

        }
    }

    //
    // We have started the initial async. reads, enter the main loop.
    // This simply waits until a write completes, then issues the next
    // read.
    //
    while (PendingIO) {
        Success = GetQueuedCompletionStatus(WritePort,
                                            &NumberBytes,
                                            &Key,
                                            &CompletedOverlapped,
                                            INFINITE);
        if (!Success) {
            //
            // Either the function failed to dequeue a completion packet
            // (CompletedOverlapped is not NULL) or it dequeued a completion
            // packet of a failed I/O operation (CompletedOverlapped is NULL).  
            //
            fprintf(stderr,
                    "GetQueuedCompletionStatus on the IoPort failed, error %d\n",
                    GetLastError());
            exit(1);
        }
        //
        // Issue the next read using the buffer that has just completed.
        //
        if (ReadPointer.QuadPart < FileSize.QuadPart) {
            Chunk = (PCOPY_CHUNK)CompletedOverlapped;
            Chunk->Overlapped.Offset = ReadPointer.LowPart;
            Chunk->Overlapped.OffsetHigh = ReadPointer.HighPart;
            ReadPointer.QuadPart += BUFFER_SIZE;
            Success = ReadFile(SourceFile,
                               Chunk->Buffer,
                               BUFFER_SIZE,
                               &NumberBytes,
                               &Chunk->Overlapped);

            if (!Success && (GetLastError() != ERROR_IO_PENDING)) {
                fprintf(stderr,
                        "ReadFile at %lx failed, error %d\n",
                        Chunk->Overlapped.Offset,
                        GetLastError());
                exit(1);
            }
        }
        else {
            //
            // There are no more reads left to issue, just wait
            // for the pending writes to drain.
            //
            --PendingIO;

        }
    }
    //
    // All done. There is no need to call VirtualFree() to free CopyChunk 
    // buffers here. The buffers will be freed when this process exits.
    //
}

int WriteLoop(
    ULARGE_INTEGER FileSize
    )
{
    BOOL Success;
    ULONG_PTR Key;
    LPOVERLAPPED CompletedOverlapped;
    PCOPY_CHUNK Chunk;
    DWORD NumberBytes;
    ULARGE_INTEGER TotalBytesWritten;

    TotalBytesWritten.QuadPart = 0;

    for (;;) {
        Success = GetQueuedCompletionStatus(ReadPort,
                                            &NumberBytes,
                                            &Key,
                                            &CompletedOverlapped,
                                            INFINITE);

        if (!Success) {
            //
            // Either the function failed to dequeue a completion packet
            // (CompletedOverlapped is not NULL) or it dequeued a completion
            // packet of a failed I/O operation (CompletedOverlapped is NULL).  
            //
            fprintf(stderr,
                    "GetQueuedCompletionStatus on the IoPort failed, error %d\n",
                    GetLastError());
            exit(1);
        }

        //
        // Update the total number of bytes written.
        //
        TotalBytesWritten.QuadPart += NumberBytes;

        //
        // Issue the next write using the buffer that has just been read into.
        //
        Chunk = (PCOPY_CHUNK)CompletedOverlapped;

        //
        // Round the number of bytes to write up to a sector boundary
        //
        NumberBytes = (NumberBytes + PageSize - 1) & ~(PageSize-1);

        Success = WriteFile(DestFile,
                            Chunk->Buffer,
                            NumberBytes,
                            &NumberBytes,
                            &Chunk->Overlapped);

        if (!Success && (GetLastError() != ERROR_IO_PENDING)) {
            fprintf(stderr,
                    "WriteFile at %lx failed, error %d\n",
                    Chunk->Overlapped.Offset,
                    GetLastError());
            exit(1);
        }

        //
        //Check to see if we've copied the complete file, if so return
        //
        if (TotalBytesWritten.QuadPart >= FileSize.QuadPart)
            return 1;
    }
    return 0;
}
