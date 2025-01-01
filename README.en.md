[![actions workflow](https://github.com/microcai/iocp/actions/workflows/ci.yml/badge.svg)](https://github.com/microcai/iocp/actions)

For Chinese, visit [this link](README.md)

# iocp4linux & µasync

This repository consists of two projects, µasync and iocp4linux

µasync is a lightweight coroutine library built on top of IOCP.
In order to allow µasync to be used on Linux platforms, there is also a library called iocp4linux that provide IOCP interface on Linux system.
So that the code written based on µasync can be cross platform.

### The role of each subfolder:

- example

Example code. For the description of the example code itself, please refer to [example/README.md](example/README.md)

- iocp4linux

Linux version of iocp implementation. Not required on Windows.

- iocp_asio

IOCP interface implemented using asio as the backend. Not required on Windows.
This means that all systems supported by asio can provide iocp.
Unless liburing.so is found, µasync will automatically use this as the backend.

- uasync

A library for coroutine encapsulation of IOCP. The official name is µasync, but because we encountered some difficulties with symbol µ in folder name, so we used u instead.

- cmake

Some cmake auxiliary scripts needed for this project

- doc

Documents, pictures

## Preface

iocp4linux, as the name suggests, implements windows equivalent IO completion ports on Linux. Its purpose is exactly the opposite of WEPOLL.

As for why we have to do the opposite of wepoll, tt is because the reactor is actually a broken model.
please read [proactor is the most ideal model](https://microcai.org/2024/11/19/proactor-is-better-than-reactor.html) for futher investagation.

## Usage

For projects managed by cmake, use git submodule or directly download the source code and place this repository in the third_party/uasync directory.

Then use

```
add_subdirectory(third_party/uasync)
link_libraries(uasync)
```

to use it.

When using, use the header file universal_async.hpp

##

## Compatibale

Lists of implemented APIs:

| IOCP 接口                   | io_uring backend | asio(epoll) backend|
| --------------------------- | ----------------------- | ---------------------------- |
| CreateIoCompletionPort      | ✅                       | ✅                            |
| GetQueuedCompletionStatus   | ✅                       | ✅                            |
| GetQueuedCompletionStatusEx | ✅                       | ✅                            |
| PostQueuedCompletionStatus  | ✅                       | ✅                            |
| WSASend                     | ✅                       | ✅                            |
| WSARecv                     | ✅                       | ✅                            |
| WSASendTo                   | ✅                       | ✅                            |
| WSARecvFrom                 | ✅                       | ✅                            |
| AcceptEx                    | ✅                       | ✅                            |
| WSAConnectEx                | ✅                       | ✅                            |
| DisconnectEx                | ✅                       | ✅                            |
| CreateFileA                 | ✅                       | ✅                            |
| CreateFileW                 | ✅                       | ✅                            |
| ReadFile                    | ✅                       | API exist, but  blocking      |
| WriteFile                   | ✅                       | API exist, but  blocking      |
| CloseHandle                 | ✅                       | ✅                            |
| CancelIo                    | ✅                       | ✅                            |
| CancelIoEx                  | ✅                       | ✅                            |



# Performance

The following results are measured by running the sample program ./example/web_server/server.cpp:

![img](doc/img/test_with_wrk.png)

When requesting a 404 path, the pure network service capability is tested. It can reach more than 110,000 QPS.

From the source code, we can see that this routine does not use the connection:keep-alive capability of the HTTP protocol.
So it handles over 110,000 AcceptEx pre second.

The second test is to download a 1MiB file.
The server.cpp uses a small buffer of 1kb for file transfer.
That is, only 1kb bytes are read from the file system each time, and then 1kb bytes are sent to the socket.

The speed reaches 1.5GB/s. That is, it can fully saturate a 10G network card by small package.

If a larger buffer is used for data transfer, then saturating a 100G network is a piece of cake.

The reason why a small buffer is used during the test is to verify the number of `WSASend` that this library can perform per second.

It turns out that the performance is good enough to saturate a 10G network with smallest packet. And a 100G network can be fully utilized with a large packet.

# About µasync

If you look at the examples in the example folder, you will find that they include the `universal_async.hpp` header file, and then use IOCP in a coroutine way. What kind of library is this?

When writing iocp4linux, I need to write some test code.

At first, I found an echo test based on IOCP and a simple web server from random internet.

Second, make sure that the examples can be compiled on Windows.

Then modify the ifdef _WIN32 guard and use the iocp.h header file on Linux platform.
Then they were all easily ported to Linux.

However, I was not satisfied with these C-style demos. Because I couldn't change their code at all.
So, I thought, why not write a super lightweight async IO library?
This library should be cross-platform - that is, compatible with the original windows.h and iocp.h at the same time. Then the code should be as short as possible and easy to use.
Finally, I made a super lightweight coroutine library.
Without further ado, let's take a look at a piece of asynchronous accept code snipte:
![img](doc/img/awaitable_overlapped.png)
In the red box, the asynchronous AcceptEx on Windows is used.
When AcceptEx is called, an ov object is passed in.
This ov object is of type awaitable_overlapped .
awaitable_overlapped can be implicitly converted to `WSAOVERLAPPED*` and accepted by AcceptEx.

Because it is asynchronous IO, the code will continue to run downward. In non-coroutine code,
at this time, the function should be returned, and put the subsequent logic of accpet into
the event loop that calls `GetQueuedCompletionStatus`. But in coroutine code. we only need to
wait until the delivered overlapped object becomes "completed".

This is completed by a single line of code : `co_await get_overlapped_result(ov)`.

After that, client_socket represent the new connection that has just been accepted.

Then call CreateIoCompletionPort to bind it to the completion port.

Finally, create a new coroutine. echo_sever_client_session to handle this connection.

This is also a very useful aspect of this coroutine library. Any ucoro::awaitable coroutine can either co_await it asynchronously or call .detach() to make it continue to run in the "background".

Since this accpet logic is to continuously accept connections. Therefore, it needs to delegate the processing logic to an independent coroutine.
That's why  you cannot use `co_await echo_sever_client_session(client_socket)` here, but instead use
`echo_sever_client_session(client_socket).detach()` to put the coroutine into the background to continue running.

The logic of this asynchronous accept is extremely **clear** and **obvious**. It seems that it is not using an extremely complex async API at all.
It is just like using the traditional synchronous accept. And it uses the native, non wrapped Windows API.

This is also a huge difference between this library and other network libraries: try to use native APIs. It just helps users manage overlapping IO logic.

So how do you make this library manage overlapping IO? Let's look at the main function:

![img](doc/img/awaitable_overlapped3.png)

It turns out that accept_coro().detach() creates multiple accept coroutines and performs accept concurrently.
Then use run_event_loop(iocp_handle) to runs the entire coroutine operation logic.

You don't even need to define an io_service object!

In addition, this awaitable_overlapped is not something that can only be used once.

Look at this code:

![img](doc/img/awaitable_overlapped2.png)

This coroutine has only one awaitable_overlapped object, but reused by WSARecv and WSASend.

By the way, if used in file IO, you need to constantly adjust the file offset. This is implemented with add_offset. As shown below:

![img](doc/img/awaitable_overlapped4.png)

You need to use `set_offset(0)` to move the read and write pointers to the beginning of the file.
Then after each read, update the returned read result through add_offset.

At this time, since the overlapped needs to carry the offset status, WSASend does not share the same awaitable_overlapped object.
After all, WSASend does not support passing in offsets.

This library is so easy to use, in fact, there is only one header file. It is in the iocp4linux repository. It is called `universal_async.hpp`

Just include this header file and you can enjoy it.

## What are the other header files of µasync?

univerasl_fiber.hpp is a stacked coroutine library that uses ucontext/windows Fiber/boost.context for context switching.

For specific usage, please refer to [echo_client_stackfull.cpp](example/echo_client/echo_client_stackfull.cpp)

easy_iocp.hpp is a code that demonstrates how to insert a callback function into an OVERLAPPED* structure and call the callback function in a world loop.
For specific usage, please refer to [echo_server_callback.cpp](example/echo_server/echo_server_callback.cpp)

univerasl_fiber.h is a stacked coroutine library that uses ucontext/windows Fiber for context switching and supports use in C language.
For specific usage, please refer to [echo_server_stackfull.c](example/echo_server/echo_server_stackfull.c)

awaitable.hpp is a c++20 coroutine library from the µcoro project. It is dependent on universal_async.hpp.

## About awaitable.hpp

This file comes from [µcoro](https://github.com/avplayer/ucoro). It is this small code of several hundred lines that helps uasync implement c++20 coroutine support.

# Communication

If you have any questions, in addition to using github issues, you can also join the [iocp4linux Telegram group](https://t.me/iocp4linux)