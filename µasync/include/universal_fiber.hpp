
#ifndef ___UNIVERSAL_FIBER__H___
#define ___UNIVERSAL_FIBER__H___

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#ifndef SOCKET_get_fd
#define SOCKET_get_fd(x) (x)
#endif
#else
#include "iocp.h"

#include <ucontext.h>

#endif

#ifdef USE_BOOST_CONTEXT
#include <boost/context/detail/fcontext.hpp>
#endif

#include <exception>
#include <atomic>
#include <assert.h>
#include <tuple>

inline std::atomic_long out_standing_coroutines = 0;

typedef struct
{
	OVERLAPPED ov;
#ifdef USE_BOOST_CONTEXT
	boost::context::detail::fcontext_t resume_context;

#elif defined(_WIN32)
	LPVOID target_fiber;
#else
	ucontext_t target;
#endif
	DWORD byte_transfered;
	DWORD last_error;
	ULONG_PTR completekey;
	BOOL resultOk;
} FiberOVERLAPPED;

#ifdef USE_BOOST_CONTEXT

inline thread_local boost::context::detail::fcontext_t __current_yield_fcontext;

struct FiberContext
{
	unsigned long sp[1024*8];

	unsigned long arg[128];

	void* func_ptr;
};

inline void handle_fcontext_invoke(boost::context::detail::transfer_t res)
{
	if (reinterpret_cast<ULONG_PTR>(res.data) & 1)
	{
		delete reinterpret_cast<FiberContext*>(reinterpret_cast<ULONG_PTR>(res.data) ^ 1);
		-- out_standing_coroutines;
	}
	else
	{
		auto ov = reinterpret_cast<FiberOVERLAPPED*>(res.data);
		ov->resume_context = res.fctx;
	}
}

#elif defined(_WIN32)
inline thread_local LPVOID __current_yield_fiber = NULL;
inline thread_local LPVOID __please_delete_me = NULL;
#else
inline thread_local ucontext_t* __current_yield_ctx = NULL;
#endif

// wait for overlapped to became complete. return NumberOfBytes
inline DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{

#ifdef USE_BOOST_CONTEXT
	assert(__current_yield_fcontext && "get_overlapped_result should be called by a Fiber!");

	__current_yield_fcontext = boost::context::detail::jump_fcontext(__current_yield_fcontext, ov).fctx;
	return ov->byte_transfered;

#elif defined(_WIN32)
	assert(__current_yield_fiber && "get_overlapped_result should be called by a Fiber!");
	ov->target_fiber = GetCurrentFiber();
	SwitchToFiber(__current_yield_fiber);
#else
	assert(__current_yield_ctx && "get_overlapped_result should be called by a ucontext based coroutine!");
	swapcontext(&ov->target, __current_yield_ctx);
#endif
	DWORD R = ov->byte_transfered;
	WSASetLastError(ov->last_error);
	return R;
}

// call this after GetQueuedCompletionStatus.
inline void process_overlapped_event(OVERLAPPED* _ov,
			BOOL resultOk, DWORD NumberOfBytes, ULONG_PTR complete_key, DWORD last_error)
{
	FiberOVERLAPPED* ovl_res = (FiberOVERLAPPED*)(_ov);
	ovl_res->byte_transfered = NumberOfBytes;
	ovl_res->last_error = last_error;
	ovl_res->completekey = complete_key;
	ovl_res->resultOk = resultOk;
#ifdef USE_BOOST_CONTEXT
	auto coro_invoke_resume = boost::context::detail::jump_fcontext(ovl_res->resume_context, 0);
	handle_fcontext_invoke(coro_invoke_resume);

#elif defined(_WIN32)
	LPVOID old = __current_yield_fiber;
	__current_yield_fiber = GetCurrentFiber();
	SwitchToFiber(ovl_res->target_fiber);
	__current_yield_fiber = old;
	if (__please_delete_me)
	{
		DeleteFiber(__please_delete_me);
		__please_delete_me = NULL;
	}
#else
	ucontext_t self;
	ucontext_t* old = __current_yield_ctx;
	__current_yield_ctx = &self;
	swapcontext(&self, &ovl_res->target);
	__current_yield_ctx = old;
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

#ifdef USE_BOOST_CONTEXT

namespace  {

template<typename... Args>
inline void _fcontext_fn_entry(boost::context::detail::transfer_t arg)
{
	__current_yield_fcontext = arg.fctx;

	FiberContext* ctx = reinterpret_cast<FiberContext*>(arg.data);

	using arg_type = std::tuple<Args...>;

	static_assert(sizeof(arg_type) < sizeof(ctx->arg), "argument too large, does not fit into fiber argument pack");

	auto fiber_args = reinterpret_cast<arg_type*>(ctx->arg);

	{
		typedef void (* real_func_type)(Args...);
		auto  real_func_ptr = reinterpret_cast<real_func_type>(ctx->func_ptr);

		std::apply(real_func_ptr, std::move(* fiber_args));
	}

	auto tagged_ptr = reinterpret_cast<ULONG_PTR>(ctx) | 1;

	boost::context::detail::jump_fcontext(__current_yield_fcontext, reinterpret_cast<void*>(tagged_ptr));
	// should never happens
	std::terminate();
}

}

#elif defined(_WIN32)

typedef  struct{
	LPVOID caller_fiber;
	void (*func_ptr)(void* param);
	void* param;
} FiberParamPack;

static inline void WINAPI FiberEntryPoint(LPVOID param)
{
	FiberParamPack saved_param;
	memcpy(&saved_param, param, sizeof(FiberParamPack));
	SwitchToFiber(saved_param.caller_fiber);

	saved_param.func_ptr(saved_param.param);

	-- out_standing_coroutines;
	__please_delete_me = GetCurrentFiber();
	SwitchToFiber(__current_yield_fiber);
}

#else

template<typename... Args>
static inline void __coroutine_entry_point(void (*func_ptr)(Args...), std::tuple<Args...>* arg_pack, ucontext_t* self_ctx,
										   void* stack_pointer)
{
	std::apply(func_ptr, *arg_pack);
	-- out_standing_coroutines;
	delete arg_pack;

	// 然后想办法 free(stack_pointer);

	ucontext_t helper_ctx;
	_Thread_local static char helper_stack[256];

	getcontext(&helper_ctx);
	typedef void (*__func)(void);

	helper_ctx.uc_stack.ss_sp = helper_stack;
	helper_ctx.uc_stack.ss_flags = 0;
	helper_ctx.uc_stack.ss_size = 256;
	helper_ctx.uc_link = __current_yield_ctx; // self_ctx->uc_link;

	free(self_ctx);

	makecontext(&helper_ctx, (__func)free, 1, stack_pointer);
	setcontext(&helper_ctx);
}
#endif // _WIN32

template<typename... Args>
inline void create_detached_coroutine(void (*func_ptr)(Args...), Args... args)
{
	++ out_standing_coroutines;

#ifdef USE_BOOST_CONTEXT

	FiberContext* new_fiber_ctx = (FiberContext*) malloc(sizeof (FiberContext));

	new_fiber_ctx->func_ptr = reinterpret_cast<void*>(func_ptr);

	using arg_tuple = std::tuple<Args...>;

	// placement new argument passed to function
	new (new_fiber_ctx->arg) arg_tuple{args...};

	auto new_fiber_resume_ctx = boost::context::detail::make_fcontext(
			new_fiber_ctx->arg, sizeof(new_fiber_ctx->sp), _fcontext_fn_entry<Args...>);
	auto new_fiber_invoke_result = boost::context::detail::jump_fcontext(new_fiber_resume_ctx, new_fiber_ctx);
	handle_fcontext_invoke(new_fiber_invoke_result);

#elif defined(_WIN32)

	FiberParamPack fiber_param;
	fiber_param.func_ptr = func_ptr;
	fiber_param.param = param;
	fiber_param.caller_fiber = GetCurrentFiber();

	LPVOID new_fiber = CreateFiber(0, FiberEntryPoint, &fiber_param);

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


#else  // _WIN32
	ucontext_t* new_ctx = (ucontext_t*)calloc(1, sizeof(ucontext_t));
	void* new_stack = malloc(8192);

	typedef void (*__func)(void);

	ucontext_t self;
	ucontext_t* old = __current_yield_ctx;
	__current_yield_ctx = &self;

	getcontext(new_ctx);
	new_ctx->uc_stack.ss_sp = new_stack;
	new_ctx->uc_stack.ss_flags = 0;
	new_ctx->uc_stack.ss_size = 8000;
	new_ctx->uc_link = __current_yield_ctx;
	auto arg_pack_ptr = new std::tuple<Args...>(args...);
	makecontext(new_ctx, (__func)__coroutine_entry_point<Args...>, 4, func_ptr, arg_pack_ptr, new_ctx, new_stack);

	swapcontext(&self, new_ctx);
	__current_yield_ctx = old;
#endif // _WIN32
}

inline void exit_event_loop_when_empty(HANDLE iocp_handle)
{
    PostQueuedCompletionStatus(iocp_handle, 0, (ULONG_PTR) iocp_handle, NULL);
}

#endif // ___UNIVERSAL_FIBER__H___
