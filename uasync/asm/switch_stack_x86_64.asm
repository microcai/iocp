
EXTERN  ExitProcess:PROC
EXTERN  _controlfp_s:PROC

.code

; RCX = argument
; RDX = jump_target
; R8 = new_sp
; void execute_on_new_stack(void * argument, void (*jump_target)(void*), void* new_sp);
execute_on_new_stack PROC  FRAME
    .endprolog
    lea rsp, [r8 - 16]
    mov rbp, rsp
    call rdx

execute_on_new_stack ENDP

END
