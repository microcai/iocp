
;           Copyright Oliver Kowalke 2009.
;  Distributed under the Boost Software License, Version 1.0.
;     (See accompanying file LICENSE_1_0.txt or copy at
;           http://www.boost.org/LICENSE_1_0.txt)

;  ----------------------------------------------------------------------------------
;  |     0   |     1   |     2    |     3   |     4   |     5   |     6   |     7   |
;  ----------------------------------------------------------------------------------
;  |    0x0  |    0x4  |    0x8   |    0xc  |   0x10  |   0x14  |   0x18  |   0x1c  |
;  ----------------------------------------------------------------------------------
;  |                          SEE registers (XMM6-XMM15)                            |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |     8   |    9    |    10    |    11   |    12   |    13   |    14   |    15   |
;  ----------------------------------------------------------------------------------
;  |   0x20  |  0x24   |   0x28   |   0x2c  |   0x30  |   0x34  |   0x38  |   0x3c  |
;  ----------------------------------------------------------------------------------
;  |                          SEE registers (XMM6-XMM15)                            |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    16   |    17   |    18   |    19    |    20   |    21   |    22   |    23   |
;  ----------------------------------------------------------------------------------
;  |   0xe40  |   0x44 |   0x48  |   0x4c   |   0x50  |   0x54  |   0x58  |   0x5c  |
;  ----------------------------------------------------------------------------------
;  |                          SEE registers (XMM6-XMM15)                            |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    24   |   25    |    26    |   27    |    28   |    29   |    30   |    31   |
;  ----------------------------------------------------------------------------------
;  |   0x60  |   0x64  |   0x68   |   0x6c  |   0x70  |   0x74  |   0x78  |   0x7c  |
;  ----------------------------------------------------------------------------------
;  |                          SEE registers (XMM6-XMM15)                            |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    32   |   32    |    33    |   34    |    35   |    36   |    37   |    38   |
;  ----------------------------------------------------------------------------------
;  |   0x80  |   0x84  |   0x88   |   0x8c  |   0x90  |   0x94  |   0x98  |   0x9c  |
;  ----------------------------------------------------------------------------------
;  |                          SEE registers (XMM6-XMM15)                            |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    39   |   40    |    41    |   42    |    43   |    44   |    45   |    46   |
;  ----------------------------------------------------------------------------------
;  |   0xa0  |   0xa4  |   0xa8   |   0xac  |   0xb0  |   0xb4  |   0xb8  |   0xbc  |
;  ----------------------------------------------------------------------------------
;  | fc_mxcsr|fc_x87_cw|     <alignment>    |       fbr_strg    |      fc_dealloc   |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    47   |   48    |    49    |   50    |    51   |    52   |    53   |    54   |
;  ----------------------------------------------------------------------------------
;  |   0xc0  |   0xc4  |   0xc8   |   0xcc  |   0xd0  |   0xd4  |   0xd8  |   0xdc  |
;  ----------------------------------------------------------------------------------
;  |        limit      |         base       |         R12       |         R13       |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    55   |   56    |    57    |   58    |    59   |    60   |    61   |    62   |
;  ----------------------------------------------------------------------------------
;  |   0xe0  |   0xe4  |   0xe8   |   0xec  |   0xf0  |   0xf4  |   0xf8  |   0xfc  |
;  ----------------------------------------------------------------------------------
;  |        R14        |         R15        |         RDI       |        RSI        |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    63   |   64    |    65    |   66    |    67   |    68   |    69   |    70   |
;  ----------------------------------------------------------------------------------
;  |  0x100  |  0x104  |  0x108   |  0x10c  |  0x110  |  0x114  |  0x118  |  0x11c  |
;  ----------------------------------------------------------------------------------
;  |        RBX        |         RBP        |       hidden      |        RIP        |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    71   |   72    |    73    |   74    |    75   |    76   |    77   |    78   |
;  ----------------------------------------------------------------------------------
;  |  0x120  |  0x124  |  0x128   |  0x12c  |  0x130  |  0x134  |  0x138  |  0x13c  |
;  ----------------------------------------------------------------------------------
;  |                                   parameter area                               |
;  ----------------------------------------------------------------------------------
;  ----------------------------------------------------------------------------------
;  |    79   |   80    |    81    |   82    |    83   |    84   |    85   |    86   |
;  ----------------------------------------------------------------------------------
;  |  0x140  |  0x144  |  0x148   |  0x14c  |  0x150  |  0x154  |  0x158  |  0x15c  |
;  ----------------------------------------------------------------------------------
;  |       FCTX        |        DATA        |                                       |
;  ----------------------------------------------------------------------------------

; standard C library function
EXTERN  _exit:PROC
.code

; generate function table entry in .pdata and unwind information in
make_fcontext PROC  FRAME
    ; .xdata for a function's structured exception handling unwind behavior
    .endprolog

    ; first arg of make_fcontext() == top of context-stack
    mov  rax, rcx

    ; shift address in RAX to lower 16 byte boundary
    ; == pointer to fcontext_t and address of context stack
    and  rax, -16

    ; reserve space for context-data on context-stack
    ; on context-function entry: (RSP -0x8) % 16 == 0
    sub  rax, 0150h

    ; third arg of make_fcontext() == address of context-function
    ; stored in RBX
    mov  [rax+0100h], r8

    ; first arg of make_fcontext() == top of context-stack
    ; save top address of context stack as 'base'
    mov  [rax+0c8h], rcx
    ; second arg of make_fcontext() == size of context-stack
    ; negate stack size for LEA instruction (== substraction)
    neg  rdx
    ; compute bottom address of context stack (limit)
    lea  rcx, [rcx+rdx]
    ; save bottom address of context stack as 'limit'
    mov  [rax+0c0h], rcx
    ; save address of context stack limit as 'dealloction stack'
    mov  [rax+0b8h], rcx
	; set fiber-storage to zero
	xor  rcx, rcx
    mov  [rax+0b0h], rcx

	; save MMX control- and status-word
    stmxcsr  [rax+0a0h]
    ; save x87 control-word
    fnstcw  [rax+0a4h]

    ; compute address of transport_t
    lea rcx, [rax+0140h]
    ; store address of transport_t in hidden field
    mov [rax+0110h], rcx

    ; compute abs address of label trampoline
    lea  rcx, trampoline
    ; save address of trampoline as return-address for context-function
    ; will be entered after calling jump_fcontext() first time
    mov  [rax+0118h], rcx

    ; compute abs address of label finish
    lea  rcx, finish
    ; save address of finish as return-address for context-function in RBP
    ; will be entered after context-function returns 
    mov  [rax+0108h], rcx

    ret ; return pointer to context-data

trampoline:
    ; store return address on stack
    ; fix stack alignment
    push rbp
    ; jump to context-function
    jmp rbx

finish:
    ; exit code is zero
    xor  rcx, rcx
    ; exit application
    call  _exit
    hlt
make_fcontext ENDP
END