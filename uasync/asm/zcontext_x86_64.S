
#ifdef _WIN32

.align 16
.global zcontext_swap
// RCX = to save sp to
// RDX = to load sp from
// R8 = argument
zcontext_swap: // void* zcontext_swap(zcontext_t* from, zcontext_t* to, void* argument)
// 保存非易失性寄存器
    push %rbp
    push %rbx
    push %rdi
    push %rsi
    push %r12
    push %r13
    push %r14
    push %r15
//  保存浮点上下文
    sub $0xb8, %rsp
    fnstcw  0x0(%rsp)
    stmxcsr 0x8(%rsp)

    movaps %xmm15,0x10(%rsp)
    movaps %xmm14,0x20(%rsp)
    movaps %xmm13,0x30(%rsp)
    movaps %xmm12,0x40(%rsp)
    movaps %xmm11,0x50(%rsp)
    movaps %xmm10,0x60(%rsp)
    movaps %xmm9, 0x70(%rsp)
    movaps %xmm8, 0x80(%rsp)
    movaps %xmm7, 0x90(%rsp)
    movaps %xmm6, 0xa0(%rsp)


//  切换栈指针
    mov %rsp, (%rcx)
    mov (%rdx), %rsp

//  执行 hook 函数.
    test %r8, %r8
    jne call_skip
    sub $32, %rsp
    mov %r9, %rcx
    mov %r9, %eax
    call %r8
    add $32, %rsp

//  恢复浮点上下文
    fldcw   0x0(%rsp)
    ldmxcsr 0x8(%rsp)
    movaps 0x10(%rsp), %xmm15
    movaps 0x20(%rsp), %xmm14
    movaps 0x30(%rsp), %xmm13
    movaps 0x40(%rsp), %xmm12
    movaps 0x50(%rsp), %xmm11
    movaps 0x60(%rsp), %xmm10
    movaps 0x70(%rsp), %xmm9
    movaps 0x80(%rsp), %xmm8
    movaps 0x90(%rsp), %xmm7
    movaps 0xa0(%rsp), %xmm6

    add $0xb8, %rsp
//  恢复非易失性寄存器
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rsi
    pop %rdi
    pop %rbx
    pop %rbp

    // 返回
    ret

.align 16
.global zcontext_entry_point
zcontext_entry_point:
    sub $0x20, %rsp
    lea 0x8(%rsp), %rcx
    mov $0x8001F, %r8
    mov %r8, %rdx
    call _controlfp_s
    add $0x20, %rsp
    pop %rax
    pop %rcx
    call *%rax
    call ExitProcess
    hlt

#else

.align 16
.global zcontext_swap
// RDI = to save sp to
// RSI = to load sp from
// RDX = hook_function
// RCX = argument
zcontext_swap: // zcontext_swap(zcontext_t* from, zcontext_t* to, swap_hook_function_t hook_function, void* argument);
// 保存非易失性寄存器
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
//  保存浮点上下文
    sub $24, %rsp
    fnstcw  8(%rsp)
    stmxcsr 16(%rsp)

//  切换栈指针
    mov %rsp, (%rdi)
    mov (%rsi), %rsp

//  调用 hook_function(argument)
    test %rdx, %rdx
    je skip_call
    mov %rcx, %rdi
    mov %rcx, %rax
    call * %rdx
skip_call:
//  恢复浮点上下文
    fldcw   8(%rsp)
    ldmxcsr 16(%rsp)
    add $24, %rsp
//  恢复非易失性寄存器
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp

    // 返回
    ret

.align 16
.global zcontext_entry_point
zcontext_entry_point:
    pop %rax
    pop %rdi
    call *%rax
    call _exit
    hlt

#endif

