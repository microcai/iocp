
#pragma once

#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" namespace zcontext  {
#endif

#if defined (__x86_64__) && defined (__clang_major__)
	#if   __clang_major__ >= 19
		#define  HAS_CC_PRESERVE_NONE
                #define  ATTRIBUTE_PRESERVE_NONE [[clang::preserve_none]]
	#endif
#endif

#ifndef ATTRIBUTE_PRESERVE_NONE
#define ATTRIBUTE_PRESERVE_NONE
#endif

typedef struct _zcontext_t{
    void* sp;// pointer to active stack buttom
}zcontext_t;

typedef void* (*zcontext_swap_hook_function_t)(void*) ATTRIBUTE_PRESERVE_NONE;

// 使用本 API 进行协程切换。
// 在 to 协程栈上，会调用 hook_function(argument)
// 并且将 hook_function 的执行结果 返回给 to 协程

#if defined (HAS_CC_PRESERVE_NONE)

	__attribute__((preserve_none))
	void* zcontext_swap(zcontext_t* from, const zcontext_t* to, zcontext_swap_hook_function_t hook_function, void* argument) asm ("zcontext_swap_preserve_none");
#else

	void* zcontext_swap(zcontext_t* from, const zcontext_t* to, zcontext_swap_hook_function_t hook_function, void* argument); // from ASM code
#endif // defined(HAS_CC_PRESERVE_NONE)

// 内部使用. 新协程从此处开始运行.
void* zcontext_entry_point(); // from ASM code

// 创建一个新协程.
// 创建后，使用 zcontext_swap 切换.
inline zcontext_t zcontext_setup(void* stack_mem, size_t stack_size, void (*func)(void*arg), void* argument)
{
    struct startup_stack_structure
    {
#if defined(__aarch64__)
        void* generial_reg[18];
        void* reg_fp;
        void* ret_address;
        void* param1;
        void* param2;
        void* padding[2];
#elif defined (_M_X64) && defined (_WIN32)
#ifdef HAS_CC_PRESERVE_NONE
        void* padd;
#else
        void* fc_x87_cw;
        void* fc_mxcsr;
        void* mmx_reg[20];
        void* padd;
        void* general_reg[8];
#endif
        void* ret_address;
        void* param1;
        void* param2;
        void* padding[2];
#elif defined (__x86_64__)

        void* padd;
#ifndef HAS_CC_PRESERVE_NONE
        void* general_reg[8];
#endif
        void* ret_address;
        void* param1;
        void* param2;
#endif
    };

    zcontext_t target;

    char* sp = reinterpret_cast<char*>(stack_mem) + stack_size - sizeof(startup_stack_structure);;

    target.sp = sp;

    auto startup_stack = reinterpret_cast<startup_stack_structure*>(sp);

    memset(sp, 0, sizeof(startup_stack_structure));

    startup_stack->ret_address = (void*) zcontext_entry_point;
    startup_stack->param1 = (void*) func;
    startup_stack->param2 = argument;

    return target;
}

#ifdef __cplusplus
} // extern "C" namespace zcontext
#endif
