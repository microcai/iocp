
#ifdef _WIN32

#define ARG1_REG %rcx
#define ARG2_REG %rdx
#define ARG3_REG %r8
#define ARG4_REG %r9

#else

#define ARG1_REG %rdi
#define ARG2_REG %rsi
#define ARG3_REG %rdx
#define ARG4_REG %rcx

#endif

#define NONE_PRESERVE_ARG1_REG %r12
#define NONE_PRESERVE_ARG2_REG %r13
#define NONE_PRESERVE_ARG3_REG %r14
#define NONE_PRESERVE_ARG4_REG %r15

#ifdef _WIN32

.align 16
.text
.p2align 4,,15
.global zcontext_swap
.section .text.zcontext_swap, "x"
// RCX = to save sp to
// RDX = to load sp from
// R8 = hook_function
// R9 = argument
zcontext_swap: // zcontext_swap(zcontext_t* from, zcontext_t* to, swap_hook_function_t hook_function, void* argument);
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
    mov ARG4_REG, %rax
    test ARG3_REG, ARG3_REG
    je 1f
    sub $32, %rsp
    mov ARG4_REG, ARG1_REG
    mov ARG4_REG, %r12 // 同时兼容 preserve_none 调用约定 的 hook
    call * ARG3_REG
    add $32, %rsp

//  恢复浮点上下文
1:
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
.text
.p2align 4,,15
.global zcontext_entry_point
.section .text.zcontext_entry_point, "x"
zcontext_entry_point:
    mov %rax, NONE_PRESERVE_ARG2_REG
    sub $0x20, %rsp
    lea 0x8(%rsp), ARG1_REG
    mov $0x8001F, %rax
    mov %rax, ARG2_REG
    mov %rax, ARG3_REG
    call _controlfp_s
    mov 0x20(%rsp),%rbx
    mov NONE_PRESERVE_ARG2_REG, ARG2_REG
    mov 0x28(%rsp),ARG1_REG
    mov ARG1_REG, NONE_PRESERVE_ARG1_REG
    call *%rbx
    call ExitProcess
    hlt

#else // _WIN32

.align 16
.global zcontext_swap
.section .text.zcontext_swap, "x"

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
    mov %rsp, (ARG1_REG)
    mov (ARG2_REG), %rsp

//  调用 hook_function(argument)
    mov ARG4_REG, ARG1_REG
    mov ARG4_REG, %rax
    test ARG3_REG, ARG3_REG
    je 1f
    call * ARG3_REG
1:
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
.section .text.zcontext_entry_point, "x"
zcontext_entry_point:
    pop %rbx
    pop ARG1_REG
    mov %rax, ARG2_REG
    mov %rax, NONE_PRESERVE_ARG2_REG
    mov ARG1_REG, NONE_PRESERVE_ARG1_REG
    call *%rbx
    call _exit
    hlt

#endif


.align 16
.text
.p2align 4,,15
.global zcontext_swap_preserve_none
.section .text.zcontext_swap_preserve_none, "x"
// r12 = to save sp to
// r13 = to load sp from
// r14 = hook_function
// r15 = argument
zcontext_swap_preserve_none: // zcontext_swap2(zcontext_t* from, zcontext_t* to, swap_hook_function_t hook_function, void* argument);
    // 保存frame寄存器
    push %rbp

    // 切换栈指针
    mov %rsp, (NONE_PRESERVE_ARG1_REG)
    mov (NONE_PRESERVE_ARG2_REG), %rsp

    //  恢复 frame 寄存器
    pop %rbp

    // if(hook_function) hook_function(argument);
    mov NONE_PRESERVE_ARG4_REG, %rax
    mov NONE_PRESERVE_ARG4_REG, ARG1_REG
    mov NONE_PRESERVE_ARG4_REG, NONE_PRESERVE_ARG1_REG // 同时兼容 preserve_none 调用约定 的 hook
    test NONE_PRESERVE_ARG3_REG, NONE_PRESERVE_ARG3_REG
    jne 1f
    // 返回 argument
    pop NONE_PRESERVE_ARG3_REG ; // pop+jump = ret !
1:
    jmp * NONE_PRESERVE_ARG3_REG // tail call optimization
