#include "rpi-asm.h"

@ store all registers into the the array passed in <r0>
@ and return.
MK_FN(dump_regs)
    stmia r0!, {r0-r12, lr}
    bx lr

@ trampoline used to:
@   - save all callee saved registers to stack.
@   - call ck_gc_fn(sp)
@   - return
@
@  note: also have to save/restore lr
MK_FN(ck_gc)
    push {r4-r11, lr}  @ Save callee-saved registers and lr
    mov r0, sp         @ Pass stack pointer as first argument
    bl ck_gc_fn        @ Call GC function
    pop {r4-r11, lr}   @ Restore registers
    bx lr              @ Return

@ similar trampoline as above, but will call
@ <ck_find_leaks_fn>.  note: sp is in a different
@ register
MK_FN(ck_find_leaks)
    push {r4-r11, lr}  @ Save callee-saved registers and lr
    mov r1, sp         @ Pass stack pointer as second argument
    bl ck_find_leaks_fn @ Call leak detection function
    pop {r4-r11, lr}   @ Restore registers
    bx lr              @ Return
