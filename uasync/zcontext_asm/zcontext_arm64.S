
.align 16
.global zcontext_swap
// x0 = to save sp to
// x1 = to load sp from
// x2 = hook_function
// x3 = argument
zcontext_swap: // zcontext_swap(zcontext_t* from, zcontext_t* to, swap_hook_function_t hook_function, void* argument);
    sub  sp, sp, #0xa0

//  保存浮点上下文
    stp  d8,  d9,  [sp, #0x00]
    stp  d10, d11, [sp, #0x10]
    stp  d12, d13, [sp, #0x20]
    stp  d14, d15, [sp, #0x30]

//  保存非易失性寄存器
    stp  x19, x20, [sp, #0x40]
    stp  x21, x22, [sp, #0x50]
    stp  x23, x24, [sp, #0x60]
    stp  x25, x26, [sp, #0x70]
    stp  x27, x28, [sp, #0x80]
    stp  fp,  lr,  [sp, #0x90]

//  切换栈指针
    mov x10, sp
    str x10, [x0]

    ldr x10, [x1]
    mov sp, x10

//  调用 hook_function(argument)
    mov x0, x3
    cbz	x2, 1f
    sub sp, sp, #0x10
    blr x2
    add sp, sp, #0x10
1:
    //  恢复浮点上下文
    ldp  d8,  d9,  [sp, #0x00]
    ldp  d10, d11, [sp, #0x10]
    ldp  d12, d13, [sp, #0x20]
    ldp  d14, d15, [sp, #0x30]

    //  恢复非易失性寄存器
    ldp  x19, x20, [sp, #0x40]
    ldp  x21, x22, [sp, #0x50]
    ldp  x23, x24, [sp, #0x60]
    ldp  x25, x26, [sp, #0x70]
    ldp  x27, x28, [sp, #0x80]
    ldp  fp,  lr,  [sp, #0x90]

    // 调整栈.
    add  sp, sp, #0xa0

    // 返回
    ret

.align 16
.global zcontext_entry_point
zcontext_entry_point:
    mov x1, x0
    ldp x2, x0, [sp, #0x00]
    mov x20, x0
    mov x21, x1
    blr x2
    bl _exit

.align 16
.global zcontext_swap_preserve_none
// x20 = to save sp to
// x21 = to load sp from
// x22 = hook_function
// x23 = argument
zcontext_swap_preserve_none:
    sub  sp, sp, #0x10
//  保存浮点上下文
//  保存非易失性寄存器
    stp  fp,  lr,  [sp, #0x00]

//  切换栈指针
    mov x10, sp
    str x10, [x20]

    ldr x10, [x21]
    mov sp, x10

//  调用 hook_function(argument)
    mov x0, x23
    cbz	x22, 1f // 如无 hook ，跳过
    mov x20, x23
    blr x22
1:
    //  恢复非易失性寄存器
    ldp  fp,  lr,  [sp, #0x00]
    // 调整栈.
    add  sp, sp, #0x10
    // 返回
    ret
