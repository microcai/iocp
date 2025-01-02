
# 什么是 zcontext

zcontext 是一个上下文切换库。用于搭配 µasync 实现有栈协程功能。

目前支持的平台有

| 操作系统 \\ cpu架构 | x86_64 | arm64 | loongarch64 |  x86  | arm32 |
| :-----------------: | :----: | :---: | :---------: | :---: | :---: |
|       Windows       |   ✅    |   ❎   |      -      |   ❎   |   ❎   |
|        Linux        |   ✅    |   ✅   |      ❎      |   ❎   |   ❎   |
|        MacOS        |   -    |   ✅   |      -      |   -   |   -   |


# 使用

zcontext 只有 2 个 API。

```c
zcontext_t zcontext_setup(void* stack_mem, size_t stack_size, void (*func)(void*arg), void* argument);
void* zcontext_swap(zcontext_t* from, const zcontext_t* to, void* (*hook_function)(void*), void* argument);
```

## `zcontext_setup`

`zcontext_setup` 的作用是创建协程。但是协程此时是处于暂停状态。

stack_mem 是分配的栈内存的首字节地址。 stack_size 则为栈内存的大小。
新建协程的栈寄存器指针会初始化为 `stack_mem + stack_size`。

按平台的栈对齐要求，可能需要 stack_size 为 16的整数倍。并且最低要 数百字节起步。最低要求具体取决于平台上下文要保存的寄存器位宽和数量再加上协程运行过程中需要分配的的栈变量。
一般来说最少需要 1kb。一般直接分配页的整数倍比较合适。

新建的协程需要使用 `zcontext_swap` 进行切换。

## `zcontext_swap`

`zcontext_swap` 进行切换的时候，这个 from 参数是一个输出参数，只要使用一个未初始化的 zcontext_t 变量来承接输出即可。

hook_function 可以用来在 to 的栈上执行一段代码。主要用于在最后一次 zcontext_swap 的时候释放协程栈。

hook_function = null 的时候，argument 会变成 `to` 协程的 zcontext_swap 的返回值。
hook_function ！= null 的时候，argument 会变成  hook_function 的参数。而 hook_function 的返回值会变成`to` 协程的 zcontext_swap 的返回值。

所谓 ”`to` 协程的返回值“ 的意思，是说，to 协程上一次调用 zcontext_swap 将自己**挂起**。然后下一次他被恢复的时候，在它看来，他的 zcontext_swap 调用就返回了。

比如下面这个示例代码：

```c++

#include <stdio.h>
#include <stdlib.h>

#include "zcontext.h"
using namespace zcontext;

zcontext_t main_thread;
zcontext_t coro_a;

void coro_a_function(void* arg)
{
    auto mem_stack = arg;
    printf("run in coro_a\n");

    auto str = zcontext_swap(&coro_a, &main_thread, 0, 0);

    printf("run in coro_a with %s\n", (char*) str);

    // 跳回主线程并释放栈.
    zcontext_swap(&coro_a, &main_thread, (zcontext_swap_hook_function_t) &free, mem_stack);

    exit(1);
}

int main(int argc, char* argv[])
{
    // 分配一个栈。
    auto mem_stack = malloc(512);
    // 创建协程
    coro_a = zcontext_setup(mem_stack, 512, &coro_a_function, mem_stack);

    // 第一次调度，打印 run in coro_a
    zcontext_swap(&main_thread, &coro_a, 0, 0);

    // 第2次调度，打印 run in coro_a with hello world
    zcontext_swap(&main_thread, &coro_a, 0, (void*) "hello world");

    // 此时 coro_a 如果再调度，就会运行到 exit(1) 去了，因此不能再调度了。

    return 0;
}

```

在 coro_a_function 里，第一次调用 zcontext_swap 的时候，它写的是

```c
    auto str = zcontext_swap(&coro_a, &main_thread, 0, 0);
```

之后，cpu 跳回main运行。而第二次调度的时候，main 里调用 zcontext_swap 传了一个 "hello world" 字符串给协程。

于是，`auto str = zcontext_swap(&coro_a, &main_thread, 0, 0);` 返回，同时 str 的值就是 "hello world"。

最后，协程准备结束的时候，它给 zcontext_swap 的第三个参数传了 free 从而实现跳回 main 的同时自己释放自己的栈。

