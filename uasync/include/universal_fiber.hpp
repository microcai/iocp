
#ifndef ___UNIVERSAL_FIBER__H___
#define ___UNIVERSAL_FIBER__H___

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SOCKET_get_fd
#define SOCKET_get_fd(x) (x)
#endif
#else
#include "iocp.h"
#endif


#if defined(_WIN32)
inline LPFN_CONNECTEX WSAConnectEx = nullptr;
inline LPFN_DISCONNECTEX DisconnectEx = nullptr;
inline LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockaddrs = nullptr;
inline LPFN_ACCEPTEX _AcceptEx = nullptr;

#define AcceptEx _AcceptEx
#define GetAcceptExSockaddrs _GetAcceptExSockaddrs

inline void init_winsock_api_pointer()
{
	SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	GUID disconnectex = WSAID_DISCONNECTEX;
	GUID connect_ex_guid = WSAID_CONNECTEX;
	GUID acceptex = WSAID_ACCEPTEX;
    GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD BytesReturned;

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&disconnectex, sizeof(GUID), &DisconnectEx, sizeof(DisconnectEx),
		&BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&connect_ex_guid, sizeof(GUID), &WSAConnectEx, sizeof(WSAConnectEx), &BytesReturned, 0, 0);

	WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptex, sizeof(GUID), &_AcceptEx, sizeof(_AcceptEx),
		&BytesReturned, 0, 0);	

    WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &getacceptexsockaddrs, sizeof(GUID), &_GetAcceptExSockaddrs, sizeof(_GetAcceptExSockaddrs),
        &BytesReturned, 0, 0);
	closesocket(sock);
}

#endif // defined(_WIN32)

#include <cstdint>
#include <cstddef>

#if defined (USE_BOOST_CONTEXT)
#define USE_FCONTEXT
#elif defined (USE_ZCONTEXT)
#define USE_ZCONTEXT 1
#elif defined(_WIN32)
#define USE_WINFIBER
#elif defined(__APPLE__) && defined(__MACH__)
// #define USE_UCONTEXT
#define USE_SETJMP
#elif defined(DISABLE_UCONTEXT)
#define USE_SETJMP
#elif !defined(USE_ZCONTEXT)
#define USE_UCONTEXT
#endif


#if defined (USE_FCONTEXT)

typedef void*   fcontext_t;

struct transfer_t {
    fcontext_t  fctx;
    void    *   data;
};

extern "C" transfer_t jump_fcontext( fcontext_t const to, void * vp);
extern "C" fcontext_t make_fcontext( void * sp, std::size_t size, void (* fn)( transfer_t) );
#elif defined(USE_ZCONTEXT)
#include "zcontext.h"

#elif defined(USE_WINFIBER)

#elif defined(USE_UCONTEXT)

#if __APPLE__ && __MACH__
#define _XOPEN_SOURCE 600
	#include <sys/ucontext.h>
	#include <ucontext.h>
#else
	#include <ucontext.h>
#endif
#elif defined(USE_SETJMP)
#include <setjmp.h>
#endif

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <exception>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <tuple>

inline std::atomic_long out_standing_coroutines = 0;

typedef struct FiberOVERLAPPED
{
	OVERLAPPED ov;
#if defined (USE_FCONTEXT)
	fcontext_t resume_context;

#elif defined(USE_WINFIBER)
	LPVOID target_fiber;
#elif defined (USE_UCONTEXT)
	ucontext_t target;
#elif defined (USE_SETJMP)
	jmp_buf target_jmp;
#elif defined (USE_ZCONTEXT)
	zcontext_t target;
#endif

	DWORD byte_transfered;
	DWORD last_error;
	ULONG_PTR completekey;
	BOOL resultOk;
} FiberOVERLAPPED;

struct HelperStack
{
	alignas(64) char sp[448];
	alignas(64) char sp_top[1];
};

struct FiberContext
{
	unsigned long long sp[
		1024*8 - 64/sizeof(unsigned long long) - 64/sizeof(unsigned long long)
#if defined (USE_UCONTEXT)
		 - sizeof(ucontext_t) / sizeof(unsigned  long long)
#endif
	];

	alignas(64) unsigned long long sp_top[8];

	alignas(64) void* func_ptr; // NOTE： &func_ptr 相当于栈顶
#if defined (USE_UCONTEXT)
	ucontext_t ctx;
#elif defined (USE_ZCONTEXT)
	zcontext_t ctx;
#endif
};

struct FiberContextAlloctor
{
	FiberContext* allocate()
	{
#ifdef __linux__
		return (FiberContext*) mmap(0, sizeof(FiberContext), PROT_READ|PROT_WRITE, MAP_GROWSDOWN|MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
#else
		return (FiberContext*) malloc(sizeof(FiberContext));
#endif
	}

	void deallocate(FiberContext* ctx)
	{
#ifdef __linux__
		munmap(ctx, sizeof(FiberContext));
#else
		free(ctx);
#endif
	}
};

#if defined(USE_FCONTEXT)
inline void handle_fcontext_invoke(transfer_t res)
{
	if (reinterpret_cast<ULONG_PTR>(res.data) & 1)
	{
		FiberContextAlloctor{}.deallocate(reinterpret_cast<FiberContext*>(reinterpret_cast<ULONG_PTR>(res.data) ^ 1));
		-- out_standing_coroutines;
	}
	else
	{
		auto ov = reinterpret_cast<FiberOVERLAPPED*>(res.data);
		ov->resume_context = res.fctx;
	}
}
#endif

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// global thread_local data for coroutine usage
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
#if defined(USE_FCONTEXT)
inline thread_local fcontext_t __current_yield_fcontext;
#elif  defined (USE_ZCONTEXT)
inline thread_local zcontext_t* __current_yield_zctx = NULL;
#elif defined(USE_WINFIBER)
inline thread_local LPVOID __current_yield_fiber = NULL;
inline thread_local LPVOID __please_delete_me = NULL;
#elif defined (USE_UCONTEXT)
inline thread_local ucontext_t* __current_yield_ctx = NULL;
#elif defined (USE_SETJMP)
inline thread_local jmp_buf __current_jump_buf;
inline thread_local FiberContext* __please_delete_me;
#endif
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  diffrent impl for "DWORD get_overlapped_result(FiberOVERLAPPED* ov)"
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#if defined(USE_FCONTEXT)
// wait for overlapped to became complete. return NumberOfBytes
inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
	assert(__current_yield_fcontext && "get_overlapped_result should be called by a Fiber!");

	__current_yield_fcontext = jump_fcontext(__current_yield_fcontext, ov).fctx;
	WSASetLastError(ov->last_error);
	return ov->byte_transfered;
}

#elif defined(USE_WINFIBER)

inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
	assert(__current_yield_fiber && "get_overlapped_result should be called by a Fiber!");
	ov->target_fiber = GetCurrentFiber();
	SwitchToFiber(__current_yield_fiber);

	WSASetLastError(ov->last_error);
	return ov->byte_transfered;
}

#elif defined (USE_UCONTEXT)

inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
	assert(__current_yield_ctx && "get_overlapped_result should be called by a ucontext based coroutine!");
	swapcontext(&ov->target, __current_yield_ctx);

	WSASetLastError(ov->last_error);
	return ov->byte_transfered;
}

#elif defined (USE_SETJMP)

inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
	if (!setjmp(ov->target_jmp))
	{
		longjmp(__current_jump_buf, 1);
	}
	if (__please_delete_me)
	{
		FiberContextAlloctor{}.deallocate(__please_delete_me);
		__please_delete_me = NULL;
	}

	WSASetLastError(ov->last_error);
	return ov->byte_transfered;
}
#elif defined (USE_ZCONTEXT)

inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
	assert(__current_yield_zctx && "get_overlapped_result should be called by a ucontext based coroutine!");

	zcontext_swap(&ov->target, __current_yield_zctx, 0);

	WSASetLastError(ov->last_error);
	return ov->byte_transfered;
}

#endif

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  common api to implement uasync
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov,
			BOOL resultOk, DWORD NumberOfBytes, ULONG_PTR complete_key, DWORD last_error)
{
	FiberOVERLAPPED* ovl_res = (FiberOVERLAPPED*)(_ov);
	ovl_res->byte_transfered = NumberOfBytes;
	ovl_res->last_error = last_error;
	ovl_res->completekey = complete_key;
	ovl_res->resultOk = resultOk;
#ifdef USE_FCONTEXT
	auto old = __current_yield_fcontext;
	auto coro_invoke_resume = jump_fcontext(ovl_res->resume_context, 0);
	handle_fcontext_invoke(coro_invoke_resume);
	__current_yield_fcontext = old;
#elif defined(USE_WINFIBER)
	LPVOID old = __current_yield_fiber;
	__current_yield_fiber = GetCurrentFiber();
	SwitchToFiber(ovl_res->target_fiber);
	__current_yield_fiber = old;
	if (__please_delete_me)
	{
		DeleteFiber(__please_delete_me);
		__please_delete_me = NULL;
	}
#elif defined (USE_UCONTEXT)
	ucontext_t self;
	ucontext_t* old = __current_yield_ctx;
	__current_yield_ctx = &self;
	swapcontext(&self, &ovl_res->target);
	__current_yield_ctx = old;
#elif defined (USE_SETJMP)
	auto set_jmp_ret = setjmp(__current_jump_buf);
	if (set_jmp_ret == 0)
	{
		longjmp(ovl_res->target_jmp, 1);
	}
	if (__please_delete_me)
	{
		FiberContextAlloctor{}.deallocate(__please_delete_me);
		__please_delete_me = NULL;
	}
#elif defined (USE_ZCONTEXT)
	zcontext_t self;
	zcontext_t* old = __current_yield_zctx;
	__current_yield_zctx = &self;
	auto __please_delete_me = zcontext_swap(&self, &ovl_res->target, 0);
	__current_yield_zctx = old;
	if (__please_delete_me)
	{
		FiberContextAlloctor{}.deallocate((FiberContext*)__please_delete_me);
		__please_delete_me = NULL;
	}
#endif
}

inline int pending_works()
{
    return out_standing_coroutines;
}

inline void run_event_loop(HANDLE iocp_handle)
{
    bool quit_if_no_work = false;

	for (;;)
	{
		DWORD NumberOfBytes = 0;
		ULONG_PTR ipCompletionKey = 0;
		LPOVERLAPPED ipOverlap = NULL;

        DWORD dwMilliseconds_to_wait = quit_if_no_work ? ( pending_works() ? 500 : 0 ) : INFINITE;

		// get IO status, no wait
		SetLastError(0);
		BOOL ok = GetQueuedCompletionStatus(iocp_handle, &NumberOfBytes, (PULONG_PTR)&ipCompletionKey, &ipOverlap, dwMilliseconds_to_wait);
		DWORD last_error = GetLastError();

		if (ipOverlap)[[likely]]
		{
			process_overlapped_event(ipOverlap, ok, NumberOfBytes, ipCompletionKey, last_error);
		}
		else if (ipCompletionKey == (ULONG_PTR)iocp_handle) [[unlikely]]
		{
            quit_if_no_work = true;
		}

		if  ( quit_if_no_work) [[unlikely]]
        {
            // 检查还在投递中的 IO 操作.
            if (!pending_works())
            {
                break;
            }
        }
	}
}

inline void exit_event_loop_when_empty(HANDLE iocp_handle)
{
    PostQueuedCompletionStatus(iocp_handle, 0, (ULONG_PTR) iocp_handle, NULL);
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<



// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// different stackfull implementations
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#if defined (USE_SETJMP) || defined (USE_ZCONTEXT)
inline void execute_on_new_stack(void* new_sp, void (*jump_target)(void*), void * param)
{
#if defined (__x86_64__)
	__asm (
		"mov %0, %%rdi\n"
		"mov %1, %%rsp\n"
		"mov %1, %%rbp\n"
		"call *%2\n"
		:
		: "r" (param), "r" (new_sp), "r" (jump_target)
	);
#elif defined(__i386__)
	__asm (
		"mov %1, %%esp\n"
		"push %0\n"
		"call *%2\n"
		:
		: "r" (param), "r" (new_sp), "r" (jump_target)
	);
#elif defined(__aarch64__)

	__asm ("nop");

	__asm (
		"mov x0, %0\n"
		"mov sp, %1\n"
		"mov fp, %1\n"
		"mov x30, %1\n"
		"br %2\n"
		:
		: "r" (param), "r" (new_sp), "r" (jump_target)
	);
#else
	static_assert(false, "only x86_64 platform supported");

#endif
}
#endif

#if defined(USE_WINFIBER)

template<typename... Args>
struct FiberParamPack
{
	LPVOID caller_fiber;

	void (*func_ptr)(Args...);
	std::tuple<Args...> param;

	FiberParamPack(auto fp, Args... args)
		: func_ptr(fp)
		, param(std::forward<Args>(args)...)
	{}
};

template<typename... Args>
static inline void WINAPI __coroutine_entry_point(LPVOID param)
{
	auto converted_param = reinterpret_cast<FiberParamPack<Args...> *>(param);

	std::tuple<Args...> local_copyed_args(std::move(converted_param->param));
	auto func_ptr = converted_param->func_ptr;

	SwitchToFiber(converted_param->caller_fiber);

	std::apply(func_ptr, std::move(local_copyed_args));

	-- out_standing_coroutines;
	__please_delete_me = GetCurrentFiber();
	SwitchToFiber(__current_yield_fiber);
}
#endif // defined(USE_WINFIBER)


#if defined (USE_UCONTEXT) || defined (USE_SETJMP) || defined (USE_FCONTEXT) || defined (USE_ZCONTEXT)

template<typename... Args>
#if defined (USE_FCONTEXT)
inline void __coroutine_entry_point(transfer_t arg)
#else //if defined (USE_UCONTEXT) || defined (USE_SETJMP) || defined (USE_ZCONTEXT)
static inline void __coroutine_entry_point(FiberContext* ctx)
#endif
{

#ifdef USE_FCONTEXT
	__current_yield_fcontext = arg.fctx;
	FiberContext* ctx = reinterpret_cast<FiberContext*>(arg.data);
#endif

	using arg_type = std::tuple<Args...>;

	auto fiber_args = reinterpret_cast<arg_type*>(ctx->sp);

	{
		typedef void (* real_func_type)(Args...);
		auto  real_func_ptr = reinterpret_cast<real_func_type>(ctx->func_ptr);
		std::apply(real_func_ptr, std::move(*fiber_args));
	}

	-- out_standing_coroutines;

#if defined (USE_FCONTEXT)
	auto tagged_ptr = reinterpret_cast<ULONG_PTR>(ctx) | 1;

	jump_fcontext(__current_yield_fcontext, reinterpret_cast<void*>(tagged_ptr));
	// should never happens
	std::terminate();
#elif defined (USE_UCONTEXT)
	// 然后想办法 free(ctx);

	ucontext_t helper_ctx;

	getcontext(&helper_ctx);
	typedef void (*__func)(void);
	typedef void (*__func_arg1)(FiberContext*);

	alignas(64) static thread_local char helper_stack[256];

	helper_ctx.uc_stack.ss_sp = helper_stack;
	helper_ctx.uc_stack.ss_flags = 0;
	helper_ctx.uc_stack.ss_size = 256;
	helper_ctx.uc_link = __current_yield_ctx; // self_ctx->uc_link;

	makecontext(&helper_ctx, (__func) (__func_arg1) [](FiberContext* ctx){FiberContextAlloctor{}.deallocate(ctx); }, 1, ctx);
	setcontext(&helper_ctx);
#elif defined(USE_SETJMP)
	__please_delete_me = ctx;
	longjmp(__current_jump_buf, 1);
#elif defined (USE_ZCONTEXT)
	zcontext_t self;
	zcontext_swap(&self, __current_yield_zctx, ctx);
#endif

}
#endif

#if defined (USE_FCONTEXT) || defined (USE_UCONTEXT) || defined (USE_ZCONTEXT)

template<typename... Args>
inline void create_detached_coroutine(void (*func_ptr)(Args...), Args... args)
{
	using arg_tuple = std::tuple<Args...>;

	++ out_standing_coroutines;

	FiberContext* new_fiber_ctx = FiberContextAlloctor{}.allocate();
	new_fiber_ctx->func_ptr = reinterpret_cast<void*>(func_ptr);
	// placement new argument passed to function
	new (new_fiber_ctx->sp) arg_tuple{std::forward<Args>(args)...};

#	if defined(USE_FCONTEXT)
	auto new_fiber_resume_ctx = make_fcontext(&(new_fiber_ctx->func_ptr), sizeof(new_fiber_ctx->sp), __coroutine_entry_point<Args...>);
	auto old = __current_yield_fcontext;
	auto new_fiber_invoke_result = jump_fcontext(new_fiber_resume_ctx, new_fiber_ctx);
	handle_fcontext_invoke(new_fiber_invoke_result);
	__current_yield_fcontext = old;
#	elif defined (USE_UCONTEXT)

	ucontext_t self;
	ucontext_t* old = __current_yield_ctx;
	__current_yield_ctx = &self;

	ucontext_t* new_ctx = & new_fiber_ctx->ctx;

	auto stack_size = (new_fiber_ctx->sp_top - new_fiber_ctx->sp) * sizeof (unsigned long long);

	getcontext(new_ctx);
	new_ctx->uc_stack.ss_sp = new_fiber_ctx->sp;
	new_ctx->uc_stack.ss_flags = 0;
	new_ctx->uc_stack.ss_size = stack_size;
	new_ctx->uc_link = __current_yield_ctx;

	typedef void (*__func)(void);
	makecontext(new_ctx, (__func)__coroutine_entry_point<Args...>, 1, new_fiber_ctx);

	swapcontext(&self, new_ctx);
	__current_yield_ctx = old;
#elif defined (USE_ZCONTEXT)

	// setup a new stack, and jump to __coroutine_entry_point
	typedef void(*entry_point_type)(FiberContext*);

	entry_point_type entry_func = & __coroutine_entry_point<Args...>;
	unsigned long long * new_sp = new_fiber_ctx->sp_top;

	zcontext_t self;
	zcontext_t* old = __current_yield_zctx;
	__current_yield_zctx = &self;

	new_fiber_ctx->ctx.sp = new_sp;

	zcontext_setup(&new_fiber_ctx->ctx, reinterpret_cast<void (*)(void*)>(entry_func), new_fiber_ctx);
	auto __please_delete_me = zcontext_swap(&self, &new_fiber_ctx->ctx, 0);
	__current_yield_zctx = old;

	if (__please_delete_me)
	{
		FiberContextAlloctor{}.deallocate((FiberContext*) __please_delete_me);
		__please_delete_me = NULL;
	}
#	endif
}

#endif  //defined (USE_SETJMP) || defined (USE_ZCONTEXT) || defined (USE_ZCONTEXT)

#if defined (USE_SETJMP)

template<typename... Args>
inline void create_detached_coroutine(void (*func_ptr)(Args...), Args... args)
{
	using arg_tuple = std::tuple<Args...>;

	++ out_standing_coroutines;

	FiberContext* new_fiber_ctx = FiberContextAlloctor{}.allocate();
	new_fiber_ctx->func_ptr = reinterpret_cast<void*>(func_ptr);
	// placement new argument passed to function
	new (new_fiber_ctx->sp) arg_tuple{std::forward<Args>(args)...};

	// jmp_buf_fiber_entry(&fiber_param);
	if (!setjmp(__current_jump_buf))
	{
		// setup a new stack, and jump to __coroutine_entry_point
		typedef void(*entry_point_type)(FiberContext*);

		entry_point_type entry_func = & __coroutine_entry_point<Args...>;
		void * new_sp = new_fiber_ctx->sp_top;

		execute_on_new_stack(new_sp, reinterpret_cast<void (*)(void*)>(entry_func), new_fiber_ctx);
	}
}

#endif // defined (USE_SETJMP)

#if defined(USE_WINFIBER)

template<typename... Args>
inline void create_detached_coroutine(void (*func_ptr)(Args...), Args... args)
{
	++ out_standing_coroutines;

	FiberParamPack<Args...> fiber_param{func_ptr, args...};
	fiber_param.caller_fiber = GetCurrentFiber();

	LPVOID new_fiber = CreateFiber(0, __coroutine_entry_point<Args...>, &fiber_param);

	SwitchToFiber(new_fiber);
	LPVOID old = __current_yield_fiber;
	__current_yield_fiber = GetCurrentFiber();
	SwitchToFiber(new_fiber);
	__current_yield_fiber = old;
	if (__please_delete_me)
	{
		DeleteFiber(__please_delete_me);
		__please_delete_me = NULL;
	}
}

#endif // USE_WINFIBER

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


#endif // ___UNIVERSAL_FIBER__H___
