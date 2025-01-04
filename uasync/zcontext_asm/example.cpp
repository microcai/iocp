
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "zcontext.h"
using namespace zcontext;

zcontext_t main_thread;
zcontext_t coro_a;

void* free_stack(void* arg) ATTRIBUTE_PRESERVE_NONE
{
    munmap(arg, 40960);
    printf("stack %p freeed\n", arg);
    return arg;
}

void coro_a_function(void* arg) ATTRIBUTE_PRESERVE_NONE
{
    auto mem_stack = arg;
    printf("run in coro_a with stack = %p\n", mem_stack);

    auto str = zcontext_swap(&coro_a, &main_thread, 0, 0);

    printf("run in coro_a with %s\n", (char*) str);

    // 跳回主线程并释放栈.
    zcontext_swap(&coro_a, &main_thread, &free_stack, mem_stack);

    exit(1);
}

int main(int argc, char* argv[])
{
    // 分配一个栈。
    auto mem_stack = mmap(0, 40960, PROT_READ|PROT_WRITE, MAP_GROWSDOWN|MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);

    // 创建协程
    coro_a = zcontext_setup(mem_stack, 40960, &coro_a_function, mem_stack);

    // 第一次调度，打印 run in coro_a
    zcontext_swap(&main_thread, &coro_a, 0, 0);

    // 第2次调度，打印 run in coro_a with hello world
    zcontext_swap(&main_thread, &coro_a, 0, (void*) "hello world");

    // 此时 coro_a 如果再调度，就会运行到 exit(1) 去了，因此不能再调度了。

    return 0;
}
