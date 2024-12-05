
# iocp4linux

## 序

iocp4linux 如名字所言，它在 Linux 上实现IO完成端口。它的目的和 WEPOLL 正好相反。

iocp4linux as name suggest, provide windows equivalent IOCP API for linux. This is the reverse
of what wepoll does.


## Usage

将 iocp.cpp 和 iocp.h 俩文件带入你的项目即可。

just add iocp.cpp and iocp.h to your project and compile with the rest codes.

## Compatibale

已经实现的接口有

| IOCP 接口    | 状态 |
| -------- | ------- |
| CreateIoCompletionPort | ✅    |
| GetQueuedCompletionStatus  | ✅ |
| PostQueuedCompletionStatus   |  ✅  |
| WSASend    |  ✅  |
| WSARecv    |  ✅  |
| WSASendTo    |  ✅  |
| WSARecvFrom    |  ✅  |
| AcceptEx   |  ✅  |
| WSAConnectEx |✅ |
| CreateFileA |✅ |
| CreateFileW |✅ |
| ReadFile |✅ |
| WriteFile | ✅ |
| CloseHandle |✅ |



