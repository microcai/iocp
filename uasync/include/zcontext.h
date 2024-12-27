
#pragma once

#include <cstdint>

extern "C" {
struct zcontext_t
{
    void* sp;// pointer to active stack buttom
};

typedef void* (*zcontext_swap_hook_function_t)(void*) ;

// 内部使用. 新协程从此处开始运行.
void* zcontext_entry_point(); // from ASM code

// 使用本 API 进行协程切换。
// 在 to 协程栈上，会调用 hook_function(argument)
// 并且将 hook_function 的执行结果 返回给 to 协程
void* zcontext_swap(zcontext_t* from, zcontext_t* to, zcontext_swap_hook_function_t hook_function, void* argument); // from ASM code

// 创建一个新协程.
// 创建后，使用 zcontext_swap 切换.
inline void zcontext_setup(zcontext_t* target, void (*func)(void*arg), void* argument)
{
    struct startup_stack_structure
    {
#ifdef _WIN32
        void* fc_x87_cw;
        void* fc_mxcsr;
        void* mmx_reg[20];
        void* pad;
        void* general_reg[8];
#else
        void* padding;
        void* fc_x87_cw;
        void* fc_mxcsr;
        void* general_reg[6];
#endif
        void* ret_address;
        void* param1;
        void* param2;

#ifdef _WIN32
        void* padding[2];
#endif
    };

    char* sp = reinterpret_cast<char*>(target->sp);

    sp -= sizeof(startup_stack_structure);

    target->sp = sp;

    auto startup_stack = reinterpret_cast<startup_stack_structure*>(sp);

    startup_stack->ret_address = (void*) zcontext_entry_point;
    startup_stack->param1 = (void*) func;
    startup_stack->param2 = argument;
    startup_stack->fc_mxcsr = startup_stack->fc_x87_cw = 0;
}

} // extern "C"
