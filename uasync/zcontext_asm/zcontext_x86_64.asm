
EXTERN  ExitProcess:PROC
EXTERN  _controlfp_s:PROC

.code

; RCX = to save sp to
; RDX = to load sp from
; R8 = hook_function
; R9 = argument
; zcontext_swap(zcontext_t* from, zcontext_t* to, swap_hook_function_t hook_function, void* argument);
zcontext_swap PROC  FRAME
    .endprolog

; 保存非易失性寄存器
    push rbp
    push rbx
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
;  保存浮点上下文
    sub rsp, 0b8h
    fnstcw  [rsp]
    stmxcsr [rsp + 8]

    movaps [rsp + 010h] , xmm15
    movaps [rsp + 020h] , xmm14
    movaps [rsp + 030h] , xmm13
    movaps [rsp + 040h] , xmm12
    movaps [rsp + 050h] , xmm11
    movaps [rsp + 060h] , xmm10
    movaps [rsp + 070h] , xmm9
    movaps [rsp + 080h] , xmm8
    movaps [rsp + 090h] , xmm7
    movaps [rsp + 0a0h] , xmm6

;  切换栈指针
    mov [rcx], rsp
    mov rsp, [rdx]

;  执行 hook 函数.
    mov rcx, r9
    mov rax, r9
    test r8, r8
    je call_skip
    sub rsp, 32
    call r8
    add rsp, 32
call_skip:
;  恢复浮点上下文
    fldcw   [rsp]
    ldmxcsr [rsp + 8]
    movaps xmm15, [ rsp + 010h ]
    movaps xmm14, [ rsp + 020h ]
    movaps xmm13, [ rsp + 030h ]
    movaps xmm12, [ rsp + 040h ]
    movaps xmm11, [ rsp + 050h ]
    movaps xmm10, [ rsp + 060h ]
    movaps xmm9,  [ rsp + 070h ]
    movaps xmm8,  [ rsp + 080h ]
    movaps xmm7,  [ rsp + 090h ]
    movaps xmm6,  [ rsp + 0a0h ]

    add rsp, 0b8h

    ; 恢复非易失性寄存器
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    pop rbp

    ; 返回 argument
    ret
zcontext_swap ENDP


zcontext_entry_point PROC  FRAME
.endprolog
    mov r12, rax
    sub rsp, 020h
    lea rcx, [rsp + 8]
    mov r8, 08001Fh
    mov rdx, r8
    call _controlfp_s
    add rsp, 020h
    pop rbx
    pop rcx
    mov rdx, r12
    call rbx
    call ExitProcess
    hlt
zcontext_entry_point ENDP

END
