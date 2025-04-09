@ put any assembly you want to see machine code for
@ in here
#include "rpi-asm.h"

@ can disassemble the machine code to see what these are
.word 0xe1a0100e
.word 0xea000008

@mla r12, r1, r2, r12
@ldr r0, [r0,#0]
@mul r0, r0, r1

@mov r0, r1
@ ldr <dst>, [<src>+#<off>]
@b 0x8024

str r1, [r0]