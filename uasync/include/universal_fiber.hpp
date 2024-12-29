
#ifndef ___UNIVERSAL_FIBER__H___
#define ___UNIVERSAL_FIBER__H___


#include "extensable_iocp.hpp"

using iocp::run_event_loop;
using iocp::exit_event_loop_when_empty;

#ifdef _WIN32
using iocp::WSAConnectEx;
using iocp::DisconnectEx;
using iocp::init_winsock_api_pointer;
#endif

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <thread>
#include <utility>

#if defined (USE_BOOST_CONTEXT)
#define USE_FCONTEXT
#elif defined (USE_ZCONTEXT)
#define USE_ZCONTEXT 1
#elif defined(_WIN32)
#define USE_WINFIBER
#elif defined(__APPLE__) && defined(__MACH__)
#if defined(DISABLE_UCONTEXT)
#error "no context switch method left to use"
#else
#define USE_UCONTEXT
#endif
#elif defined(DISABLE_UCONTEXT)
#error "no context switch method left to use"
#else
#define USE_UCONTEXT
#endif


#if defined (USE_FCONTEXT)

typedef void*   fcontext_t;

struct transfer_t {
    fcontext_t  fctx;
    void    *   data; // pass jump_info_t pointer
};

struct jump_info_t {
	void* (*func)(fcontext_t, void*);
	void* arguments;
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
#endif

#ifdef __linux__
#include <sys/mman.h>
#endif

#include <exception>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <tuple>
#include <functional>

typedef struct FiberOVERLAPPED
{
	OVERLAPPED ov;
#if defined (USE_FCONTEXT)
	fcontext_t target;
#elif defined(USE_WINFIBER)
	LPVOID target_fiber;
#elif defined (USE_UCONTEXT)
	ucontext_t target;
#elif defined (USE_ZCONTEXT)
	zcontext_t target;
#endif

	DWORD byte_transfered;
	DWORD last_error;

	std::atomic_flag ready;
	std::atomic_flag resume_flag;

	OVERLAPPED* operator & ()
	{
		 return & ov;
	}

    void reset()
    {
        ov.Internal = ov.InternalHigh = 0;
        ov.hEvent = NULL;
        byte_transfered = 0;
        ready.clear();
		resume_flag.clear();
    }

    void set_offset(uint64_t offset)
    {
        ov.Offset = offset & 0xFFFFFFFF;
        ov.OffsetHigh = (offset >> 32);
    }

    void add_offset(uint64_t offset)
    {
        uint64_t cur_offset = ov.OffsetHigh;
        cur_offset <<= 32;
        cur_offset += ov.Offset;
        cur_offset += offset;
        ov.Offset = cur_offset & 0xFFFFFFFF;
        ov.OffsetHigh = (cur_offset >> 32);
    }

	FiberOVERLAPPED()
	{
		set_offset(-1);
		reset();
	}

} FiberOVERLAPPED;

template<typename T>
inline static auto move_or_copy(T&& arg)
{
	if constexpr (std::is_move_constructible_v<T>)
	{
		return static_cast<T&&>(arg);
	}
	else
	{
		return static_cast<const T&>(arg);
	}
}

struct FiberContext
{
	unsigned long long sp[
		1024*8 - 8
#if defined (USE_UCONTEXT)
		 - sizeof(ucontext_t) / sizeof(unsigned  long long)
#elif defined (USE_ZCONTEXT)
		 - sizeof(zcontext_t) / sizeof(unsigned  long long)
#endif
	];

	alignas(64) unsigned long long sp_top[8]; // 栈顶
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
#elif defined (_WIN32)
		return (FiberContext*) VirtualAlloc(0, sizeof(FiberContext), MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
#else
		return (FiberContext*) malloc(sizeof(FiberContext));
#endif
	}

	void deallocate(FiberContext* ctx)
	{
#ifdef __linux__
		munmap(ctx, sizeof(FiberContext));
#elif defined (_WIN32)
		VirtualFree(ctx, 0, MEM_RELEASE);
#else
		free(ctx);
#endif
	}
};

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// global thread_local data for coroutine usage
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
#if defined(USE_FCONTEXT)
inline thread_local fcontext_t __current_yield_fcontext;
#elif  defined (USE_ZCONTEXT)
inline thread_local zcontext_t* __current_yield_zctx = NULL;
#elif defined(USE_WINFIBER)
inline thread_local LPVOID __current_yield_fiber = NULL;
inline thread_local std::function<void()> __current_yield_fiber_hook = NULL;
#elif defined (USE_UCONTEXT)
inline thread_local ucontext_t* __current_yield_ctx = NULL;
inline thread_local std::function<void()> __current_yield_ctx_hook = NULL;
#endif
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

#if defined(USE_FCONTEXT)
// run the "to" coro, and pass arg to it.
inline void fcontext_resume_coro(fcontext_t const to, void* arg = 0)
{
	auto old = __current_yield_fcontext;
	auto jmp_result = jump_fcontext(to, arg);
	__current_yield_fcontext = old;
	jump_info_t* res_info = (jump_info_t*) jmp_result.data;
	if (res_info && res_info->func)
	{
		res_info->func(jmp_result.fctx, res_info->arguments);
	}
}

// transfer control back to main thread. called by coro
inline void fcontext_suspend_coro(const jump_info_t& arg)
{
	auto jmp_result = jump_fcontext(__current_yield_fcontext, const_cast<jump_info_t*>(&arg));
	__current_yield_fcontext = jmp_result.fctx;
	jump_info_t* res_info = (jump_info_t*) jmp_result.data;
	if (res_info && res_info->func)
	{
		res_info->func(jmp_result.fctx, res_info->arguments);
	}
}
#elif  defined (USE_ZCONTEXT)

inline void zcontext_resume_coro(zcontext_t& target)
{
	zcontext_t* old = __current_yield_zctx;
	zcontext_t self;
	__current_yield_zctx = &self;
	zcontext_swap(&self, &target, 0, 0);
	__current_yield_zctx = old;
}

inline void zcontext_suspend_coro(FiberOVERLAPPED& ov)
{
	auto set_resume_flag = [](void* arg)
	{
		reinterpret_cast<FiberOVERLAPPED*>(arg)->resume_flag.test_and_set();
		return arg;
	};

	zcontext_swap(&ov.target, __current_yield_zctx, (zcontext_swap_hook_function_t) set_resume_flag, &ov);
}
#elif  defined (USE_UCONTEXT)

inline void ucontext_resume_coro(ucontext_t& target)
{
	ucontext_t self;
	ucontext_t* old = __current_yield_ctx;
	__current_yield_ctx = &self;
	swapcontext(&self, &target);
	__current_yield_ctx = old;

	if (__current_yield_ctx_hook)
	{
		__current_yield_ctx_hook();
		__current_yield_ctx_hook = nullptr;
	}
}

inline void zcontext_suspend_coro(FiberOVERLAPPED& ov)
{
	__current_yield_ctx_hook =[&ov]()
	{
		ov.resume_flag.test_and_set();
	};

	swapcontext(&ov.target, __current_yield_ctx);

	if (__current_yield_ctx_hook)
	{
		__current_yield_ctx_hook();
		__current_yield_ctx_hook = nullptr;
	}
}
#elif defined(USE_WINFIBER)

inline void winfiber_resume_coro(LPVOID target)
{
	LPVOID old = __current_yield_fiber;
	__current_yield_fiber = GetCurrentFiber();
	SwitchToFiber(target);
	__current_yield_fiber = old;
	if (__current_yield_fiber_hook)
	{
		__current_yield_fiber_hook();
		__current_yield_fiber_hook = nullptr;
	}
}

inline void winfiber_suspend_coro(FiberOVERLAPPED& ov)
{
	__current_yield_fiber_hook =[&ov]()
	{
		ov.resume_flag.test_and_set();
	};
	ov.target_fiber = GetCurrentFiber();
	SwitchToFiber(__current_yield_fiber);
	if (__current_yield_fiber_hook)
	{
		__current_yield_fiber_hook();
		__current_yield_fiber_hook = nullptr;
	}
}

#endif

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  diffrent impl for "DWORD get_overlapped_result(FiberOVERLAPPED& ov)"
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#if defined(USE_FCONTEXT)
// wait for overlapped to became complete. return NumberOfBytes
inline DWORD get_overlapped_result(FiberOVERLAPPED& ov)
{
	assert(__current_yield_fcontext && "get_overlapped_result should be called by a Fiber!");
	if (!ov.ready.test_and_set())
	{
		jump_info_t info = {
			.func = (void* (*)(fcontext_t ctx, void* arg)) [](fcontext_t ctx, void* arg) -> void* {
				auto ov = reinterpret_cast<FiberOVERLAPPED*>(arg);
				ov->target = ctx;
				ov->resume_flag.test_and_set();
				ov->resume_flag.notify_all();
				return arg;
			 },
			.arguments = &ov,
		};
		fcontext_suspend_coro(info);
	}

	ov.ready.clear();
	ov.resume_flag.clear();
	WSASetLastError(ov.last_error);
	return ov.byte_transfered;
}

#elif defined(USE_WINFIBER)

inline DWORD get_overlapped_result(FiberOVERLAPPED& ov)
{
	assert(__current_yield_fiber && "get_overlapped_result should be called by a Fiber!");
	if (!ov.ready.test_and_set())
	{
		winfiber_suspend_coro(ov);
	}

	ov.ready.clear();
	ov.resume_flag.clear();
	WSASetLastError(ov.last_error);
	return ov.byte_transfered;
}

#elif defined (USE_UCONTEXT)

inline DWORD get_overlapped_result(FiberOVERLAPPED& ov)
{
	assert(__current_yield_ctx && "get_overlapped_result should be called by a ucontext based coroutine!");
	if (!ov.ready.test_and_set())
	{
		zcontext_suspend_coro(ov);
	}

	ov.ready.clear();
	ov.resume_flag.clear();
	WSASetLastError(ov.last_error);
	return ov.byte_transfered;
}

#elif defined (USE_ZCONTEXT)

inline DWORD get_overlapped_result(FiberOVERLAPPED& ov)
{
	assert(__current_yield_zctx && "get_overlapped_result should be called by a ucontext based coroutine!");
	if (!ov.ready.test_and_set())
	{
		zcontext_suspend_coro(ov);
	}

	ov.ready.clear();
	ov.resume_flag.clear();
	WSASetLastError(ov.last_error);
	return ov.byte_transfered;
}

#endif

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  common api to implement uasync
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// call this after GetQueuedCompletionStatus.
inline void process_stack_full_overlapped_event(const OVERLAPPED_ENTRY* _ov, DWORD last_error)
{
	FiberOVERLAPPED* ovl_res = (FiberOVERLAPPED*)(_ov->lpOverlapped);
	ovl_res->byte_transfered = _ov->dwNumberOfBytesTransferred;
	ovl_res->last_error = last_error;

	if (ovl_res->ready.test_and_set()) [[likely]]
	{
		assert(ovl_res->ready.test());
		// need to resume
		while (!ovl_res->resume_flag.test()){}
		assert(ovl_res->ready.test());

#ifdef USE_FCONTEXT
		fcontext_resume_coro(ovl_res->target);
#elif defined (USE_ZCONTEXT)
		zcontext_resume_coro(ovl_res->target);
#elif defined (USE_UCONTEXT)
		ucontext_resume_coro(ovl_res->target);
#elif defined(USE_WINFIBER)

		winfiber_resume_coro(ovl_res->target_fiber);
#endif
	}
}

inline auto bind_stackfull_iocp(HANDLE file, HANDLE iocp_handle, DWORD = 0, DWORD = 0)
{
    return CreateIoCompletionPort(file, iocp_handle, (ULONG_PTR) (void*) &process_stack_full_overlapped_event, 0);
}

// 执行这个，可以保证 协程被 IOCP 线程调度. 特别是 一个线程一个 IOCP 的模式下特有用
inline void run_fiber_on_iocp_thread(HANDLE iocp_handle)
{
	FiberOVERLAPPED ov;

	auto switch_thread_handler = [](const OVERLAPPED_ENTRY* _ov, DWORD last_error) -> void
	{
#ifdef USE_WINFIBER
		if (!IsThreadAFiber())
		{
			ConvertThreadToFiber(0);
		}
#endif
		// make sure get_overlapped_result is invoked!
		auto ov = reinterpret_cast<FiberOVERLAPPED*>(_ov->lpOverlapped);

		while (!ov->ready.test()) {}

		process_stack_full_overlapped_event(_ov, 0);
	};
	PostQueuedCompletionStatus(iocp_handle, 2333, (ULONG_PTR) (void*) ( iocp::overlapped_proc_func ) switch_thread_handler, &ov);
	auto r =  get_overlapped_result(ov);
	assert(r == 2333);
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// different stackfull implementations
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
template<typename T>
inline consteval std::size_t stack_align_space()
{
	auto constexpr _T_size = sizeof(T);
	auto constexpr _T_align_or_stack_align = std::max((std::size_t)16, alignof(T));

	auto constexpr T_align_size_plus1 = (_T_size/_T_align_or_stack_align + 1)*_T_align_or_stack_align;
	auto constexpr T_align_size = (_T_size/_T_align_or_stack_align)*_T_align_or_stack_align;
	return T_align_size < _T_size ? T_align_size_plus1 : T_align_size;
}

template<typename Callable>
#if defined (USE_FCONTEXT)
inline void __coroutine_entry_point(transfer_t arg)
#elif defined (USE_UCONTEXT) || defined (USE_ZCONTEXT)
static inline void __coroutine_entry_point(FiberContext* ctx)
#elif defined (USE_WINFIBER)
static inline void __coroutine_entry_point(LPVOID param)
#endif
{

#ifdef USE_FCONTEXT
	__current_yield_fcontext = arg.fctx;
	FiberContext* ctx = reinterpret_cast<FiberContext*>(arg.data);
#endif

#if defined (USE_WINFIBER)
	auto callable_ptr_unstable = reinterpret_cast<Callable*>(param);

	alignas(Callable) char callable_storage[sizeof(Callable)];

	// copy Callable to callable_storage
	Callable* callable_ptr = new (callable_storage) Callable(std::move(*callable_ptr_unstable));
	// now, param is invalid. proceed without touching param.
#else

	auto stack_top = reinterpret_cast<char*>(ctx->sp_top);
	// reserve space for callable
	stack_top -= stack_align_space<Callable>();

	auto callable_ptr = reinterpret_cast<Callable*>(stack_top);
#endif

	{
		++ iocp::pending_works;
		// invoke callable
		// try
		// {
			(*callable_ptr)();
		// }
		// catch(...)
		// {
		// 	fprintf(stderr, "unhandled exception in coroutine code\n");
		// 	std::terminate();
		// }
		// deconstruct callable
		callable_ptr->~Callable();
		-- iocp::pending_works;
	}

#if defined (USE_FCONTEXT)
	jump_info_t info{
		.func = (void* (*)(fcontext_t, void* arg)) [](fcontext_t, void* arg){
			FiberContextAlloctor{}.deallocate(reinterpret_cast<FiberContext*>(arg));
			return arg;
		},
		.arguments = ctx
	};

	fcontext_suspend_coro(info);
#elif defined (USE_UCONTEXT)
	__current_yield_ctx_hook = [ctx]()
	{
		FiberContextAlloctor{}.deallocate(ctx);
	};
	setcontext(__current_yield_ctx);
#elif defined (USE_ZCONTEXT)
	auto stack_cleaner = [](void* __please_delete_me) -> void*
	{
		FiberContextAlloctor{}.deallocate((FiberContext*)__please_delete_me);
		return 0;
	};

	zcontext_swap(&ctx->ctx, __current_yield_zctx, (zcontext_swap_hook_function_t )stack_cleaner , ctx);
#elif defined (USE_WINFIBER)

	auto to_be_deleted = GetCurrentFiber();
	__current_yield_fiber_hook = [to_be_deleted]()
	{
		DeleteFiber(to_be_deleted);
	};
	SwitchToFiber(__current_yield_fiber);

#endif
	// should never happens
	std::terminate();
}

template<typename Callable>
inline void create_detached_coroutine(Callable callable)
{
	using NoRefCallableType = std::decay_t<Callable>;

#ifndef USE_WINFIBER
	FiberContext* new_fiber_ctx = FiberContextAlloctor{}.allocate();

	auto stack_top = reinterpret_cast<char*>(new_fiber_ctx->sp_top);
	// reserve space for callable
	stack_top -= stack_align_space<NoRefCallableType>();

	// placement new the callable into the stack
	// move/or copy construct
	new (stack_top) NoRefCallableType{move_or_copy(std::move(callable))};

	auto stack_size = (stack_top - reinterpret_cast<char*>(new_fiber_ctx->sp));

#endif

#	if defined(USE_FCONTEXT)

	auto new_fiber_resume_ctx = make_fcontext(stack_top, stack_size, __coroutine_entry_point<NoRefCallableType>);
	fcontext_resume_coro(new_fiber_resume_ctx, new_fiber_ctx);

#	elif defined (USE_UCONTEXT)

	ucontext_t* new_ctx = & new_fiber_ctx->ctx;

	getcontext(new_ctx);
	new_ctx->uc_stack.ss_sp = stack_top;
	new_ctx->uc_stack.ss_flags = 0;
	new_ctx->uc_stack.ss_size = stack_size;
	new_ctx->uc_link = nullptr;

	typedef void (*__func)(void);
	makecontext(new_ctx, (__func)__coroutine_entry_point<NoRefCallableType>, 1, new_fiber_ctx);
	ucontext_resume_coro(*new_ctx);

#elif defined (USE_ZCONTEXT)

	// setup a new stack, and jump to __coroutine_entry_point
	typedef void(*entry_point_type)(FiberContext*);

	entry_point_type entry_func = & __coroutine_entry_point<NoRefCallableType>;

	new_fiber_ctx->ctx.sp = stack_top;
	zcontext_setup(&new_fiber_ctx->ctx, reinterpret_cast<void (*)(void*)>(entry_func), new_fiber_ctx);
	zcontext_resume_coro(new_fiber_ctx->ctx);

#elif defined(USE_WINFIBER)

	if (!IsThreadAFiber())
	{
		ConvertThreadToFiber(0);
	}

	LPVOID old = __current_yield_fiber;
	__current_yield_fiber = GetCurrentFiber();
	LPVOID new_fiber = CreateFiber(0, __coroutine_entry_point<NoRefCallableType>, &callable);
	// switch to the new fiber
	SwitchToFiber(new_fiber);
	__current_yield_fiber = old;

	if (__current_yield_fiber_hook)
	{
		__current_yield_fiber_hook();
		__current_yield_fiber_hook = nullptr;
	}

#endif
}

template<typename... Args>
inline void create_detached_coroutine(void (*function_pointer)(Args...), Args... args)
{
	create_detached_coroutine([function_pointer, ...args=move_or_copy(std::move(args))]()
	{
		function_pointer(move_or_copy(std::move(args))...);
	});
}

template<typename Callable, typename... Args> requires std::is_invocable_v<Callable, Args...>
inline void create_detached_coroutine(Callable callable, Args... args)
{
	create_detached_coroutine([callable = std::move(callable), ...args=move_or_copy(std::move(args))]()
	{
		callable(move_or_copy(std::move(args))...);
	});
}

template<typename Class, typename... Args>
inline void create_detached_coroutine(void (Class::*mem_func_ptr)(Args...), Class* _this, Args... args)
{
	create_detached_coroutine([_this, mem_func_ptr, ...args=move_or_copy(std::move(args))]()
	{
		(_this->*mem_func_ptr)(move_or_copy(std::move(args))...);
	});
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


#endif // ___UNIVERSAL_FIBER__H___
