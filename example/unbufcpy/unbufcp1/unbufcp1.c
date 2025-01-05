/*++
THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
PARTICULAR PURPOSE.

Copyright (C) 1994 - 2000.  Microsoft Corporation.  All rights reserved.

Module Name:

    unbufcp1.c

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
#ifdef _IA64_
#pragma warning(disable:4100)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include "iocp.h"
#include <time.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef _WIN32

DWORD GetTickCount()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec* 1000 + ts.tv_nsec/1000000;
}

#endif

//
// File handles for the copy operation. All read operations are
// from SourceFile. All write operations are to DestFile.
//
HANDLE SourceFile = INVALID_HANDLE_VALUE;
HANDLE DestFile = INVALID_HANDLE_VALUE;

//
// I/O completion port. All read and writes to the files complete
// to this port.
//
HANDLE IoPort = INVALID_HANDLE_VALUE;

//
// Key values used to determine whether a read or a write
// has completed.
//
#define ReadKey 0
#define WriteKey 1

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
// Local function prototype
//
VOID
CopyLoop(
    ULARGE_INTEGER FileSize
    );

int main(
    int argc,
    char *argv[]
    )
{
    ULARGE_INTEGER FileSize;
    ULARGE_INTEGER InitialSize;
    BOOL Success;
    DWORD Status;
    DWORD StartTime, EndTime;
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
                            GENERIC_READ,
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
    InitialSize.QuadPart = (FileSize.QuadPart + PageSize - 1) & ~((DWORD_PTR)(PageSize-1));
    Status = SetFilePointer(DestFile,
                            InitialSize.LowPart,
                            (PLONG)&InitialSize.HighPart,
                            FILE_BEGIN);
    if ((Status == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR)) {
        fprintf(stderr, "initial SetFilePointer failed, error %d\n", GetLastError());
        exit(1);
    }
    Success = SetEndOfFile(DestFile);
    if (!Success) {
        fprintf(stderr, "SetEndOfFile failed, error %d\n", GetLastError());
        exit(1);
    }

    //
    //we are running on NT 3.51 or greater.
    //Create the I/O Completion Port
    //
    IoPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,//file handle to associate with I/O completion port
                                    NULL,                //optional handle to existing I/O completion port
                                    ReadKey,             //completion key
                                    1);                  //# of threads allowed to execute concurrently


    if (IoPort == NULL) {
        fprintf(stderr, "failed to create ReadPort, error %d\n", GetLastError());
        exit(1);
    }

    //
    //If we need to, aka we're running on NT 3.51, let's associate a file handle with the
    //completion port.
    //

    IoPort = CreateIoCompletionPort(SourceFile,
                                    IoPort,
                                    ReadKey,
                                    1);

    if (IoPort == NULL)
    {
        fprintf(stderr,
                "failed to create IoPort, error %d\n",
                GetLastError());

        exit(1);

    }

    //
    // Associate the destination file handle with the
    // I/O completion port.
    //

    IoPort = CreateIoCompletionPort(DestFile,
                                    IoPort,
                                    WriteKey,
                                    1);
    if (IoPort == NULL) {
        fprintf(stderr, "failed to create WritePort, error %d\n", GetLastError());
        exit(1);
    }



    StartTime = GetTickCount();

    //
    // Do the copy
    //
    CopyLoop(FileSize);

    EndTime = GetTickCount();

    CloseHandle(SourceFile);
    CloseHandle(DestFile);

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

    printf("%d bytes copied in %.3f seconds\n",
           FileSize.LowPart,
           (float)(EndTime-StartTime)/1000.0);
    printf("%.2f MB/sec\n",
           ((LONGLONG)FileSize.QuadPart/(1024.0*1024.0)) / (((float)(EndTime-StartTime)) / 1000.0));

    return(0);

}

void CopyLoop(ULARGE_INTEGER FileSize)
{
    ULARGE_INTEGER ReadPointer;
    BOOL Success;
    DWORD NumberBytes;
    LPOVERLAPPED CompletedOverlapped;
    DWORD_PTR Key;
    PCOPY_CHUNK Chunk;
    int PendingIO = 0;
    int i;

    //
    // Start reading the file. Kick off MAX_CONCURRENT_IO reads, then just
    // loop waiting for I/O to complete.
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
    // This simply waits until an I/O completes, then issues the next
    // I/O.  When a write completes, the next read is issued. When a
    // read completes, the corresponding write is issued.
    //
    while (PendingIO) {
        Success = GetQueuedCompletionStatus(IoPort,
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


        Chunk = (PCOPY_CHUNK)CompletedOverlapped;

        if (Key == ReadKey) {

            //
            // A read has completed, issue the corresponding write.
            //

            //
            // Round the number of bytes to write up to a sector boundary.
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


        } else if (Key == WriteKey) {

            //
            // A write has completed, issue the next read.
            //

            if (ReadPointer.QuadPart < FileSize.QuadPart) {
                Chunk->Overlapped.Offset = ReadPointer.LowPart;
                Chunk->Overlapped.OffsetHigh = ReadPointer.HighPart;
                ReadPointer.QuadPart += BUFFER_SIZE;
                Success = ReadFile(SourceFile,
                                   Chunk->Buffer,
                                   NumberBytes,
                                   &NumberBytes,
                                   &Chunk->Overlapped);
                if (!Success && (GetLastError() != ERROR_IO_PENDING)) {
                    fprintf(stderr,
                            "ReadFile at %lx failed, error %d\n",
                            Chunk->Overlapped.Offset,
                            GetLastError());
                    exit(1);
                }
            } else {
                //
                // There are no more reads left to issue, just
                // wait for the pending writes to drain.
                //
                --PendingIO;
            }
        }
    }
    //
    // All done. There is no need to call VirtualFree() to free CopyChunk
    // buffers here. The buffers will be freed when this process exits.
    //
}
