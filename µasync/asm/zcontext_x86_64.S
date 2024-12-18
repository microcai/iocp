
.align 16
.global zcontext_swap
// RDI = to save sp to
// RSI = to load sp from
// RDX = argument
zcontext_swap: // void* zcontext_swap(zcontext_t* from, zcontext_t* to, void* argument)

// 保存非易失性寄存器
    push %rbp
    push %rbx
    push %r10
    push %r12
    push %r13
    push %r14
    push %r15
//  保存浮点上下文
    sub $16, %rsp
    fnstcw  0(%rsp)
    stmxcsr 8(%rsp)

//  切换栈指针
    mov %rsp, (%rdi)
    mov (%rsi), %rsp

//  恢复浮点上下文
    fldcw   (%rsp)
    ldmxcsr 8(%rsp)
    add $16, %rsp
//  恢复非易失性寄存器
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r10
    pop %rbx
    pop %rbp

    // 返回 argument
    mov %rdx, %rax
    mov (%rsp), %rcx
    retq
.align 16


.align 16
zcontext_entry_point:
    mov %r15, %rdi
    call *%r14
    hlt

.align 16
.global zcontext_setup
// RDI = target
// RSI = func
// RDX = argument
zcontext_setup: // void zcontext_setup(zcontext_t* target, void (*func)(void*arg), void* argument)
    movq    (%rdi), %rax
    movq    %rdx, -64(%rax)
    subq    $80, %rax
    movq    %rsi, 24(%rax)
    movq    zcontext_entry_point@GOTPCREL(%rip), %rcx;
    movq    %rcx, 72(%rax)
    movq    %rax, (%rdi)
    ret