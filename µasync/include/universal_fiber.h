
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

#define UASYNC_API inline

#else
#include "iocp.h"

#if __APPLE__ && __MACH__
#define _XOPEN_SOURCE 600
	#include <sys/ucontext.h>
	#include <ucontext.h>
#else
	#include <ucontext.h>
#endif

#define UASYNC_API static

#endif

#include <assert.h>

typedef struct
{
	OVERLAPPED ov;
#ifdef _WIN32
	LPVOID target_fiber;
#else
	ucontext_t target;
#endif
	DWORD byte_transfered;
	DWORD last_error;
	ULONG_PTR completekey;
	BOOL resultOk;
} FiberOVERLAPPED;

#ifdef _WIN32
extern __declspec(thread) LPVOID __current_yield_fiber = NULL;
extern __declspec(thread) LPVOID __please_delete_me = NULL;

#else
extern _Thread_local ucontext_t* __current_yield_ctx = NULL;
#endif

// wait for overlapped to became complete. return NumberOfBytes
UASYNC_API DWORD get_overlapped_result(FiberOVERLAPPED* ov)
{
#ifdef _WIN32
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
UASYNC_API void process_overlapped_event(OVERLAPPED* _ov,
			BOOL resultOk, DWORD NumberOfBytes, ULONG_PTR complete_key, DWORD last_error)
{
	FiberOVERLAPPED* ovl_res = (FiberOVERLAPPED*)(_ov);
	ovl_res->byte_transfered = NumberOfBytes;
	ovl_res->last_error = last_error;
	ovl_res->completekey = complete_key;
	ovl_res->resultOk = resultOk;

#ifdef _WIN32
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

UASYNC_API void run_event_loop(HANDLE iocp_handle)
{
	for (;;)
	{
		DWORD NumberOfBytes = 0;
		ULONG_PTR ipCompletionKey = 0;
		LPOVERLAPPED ipOverlap = NULL;

		// get IO status, no wait
		SetLastError(0);
		BOOL ok = GetQueuedCompletionStatus(iocp_handle, &NumberOfBytes, (PULONG_PTR)&ipCompletionKey, &ipOverlap, INFINITE);
		DWORD last_error = GetLastError();

		if (ipOverlap)
		{
			process_overlapped_event(ipOverlap, ok, NumberOfBytes, ipCompletionKey, last_error);
		}
	}
}

#ifdef _WIN32

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

   __please_delete_me = GetCurrentFiber();
   SwitchToFiber(__current_yield_fiber);
}

#else

UASYNC_API void __coroutine_entry_point(void (*func_ptr)(void* param), void* param, ucontext_t* self_ctx,
										   void* stack_pointer)
{
	func_ptr(param);

	// 然后想办法 free(stack_pointer);

	static _Thread_local char helper_stack[256];

	ucontext_t helper_ctx;

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

UASYNC_API void create_detached_coroutine(void (*func_ptr)(void* param), void* param)
{
#ifdef _WIN32

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
	makecontext(new_ctx, (__func)__coroutine_entry_point, 4, func_ptr, param, new_ctx, new_stack);

	swapcontext(&self, new_ctx);
	__current_yield_ctx = old;
#endif // _WIN32
}

#endif // ___UNIVERSAL_FIBER__H___
