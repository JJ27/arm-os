// dynamically generate code to call a routine.  we
// do this in two steps to make it easier to debug.
//  - first, generate a call to a predefined functions 
//    that take no arguments (hello_before and hello_after).
//  - when that works, generate a call to a routine that takes
//    a 32-bit argument.  the general way that arm handles this:
//       - write the 32-bit value in the code array
//         *after* all the code you generate
//       - emit an ldr instruction using the pc to load
//         it.
#include "rpi.h"
#include "rpi-interrupts.h"
#include <stdint.h>

// we use this to catch if you jump off by one
static void guard1(void) { asm volatile ("bkpt"); }

static void hello1(void) { 
    printk("hello world 1\n");
}

// we use this to catch if you jump off by one
static void guard2(void) { asm volatile ("bkpt"); }

void hello2(void) { 
    printk("hello world 2\n");
}

// we use this to catch if you jump off by one
static void guard3(void) { asm volatile ("bkpt"); }

// the routines to implement.
static inline uint32_t armv6_push(int reg) {
    assert(reg<16);
    printk("push: %x\n", 0xe52d0004 | (reg << 12));
    //todo("return the machine code to push{reg}\n");
    return 0xe52d0004 | (reg << 12);
}
static inline uint32_t armv6_pop(int reg) {
    assert(reg<16);
    printk("pop: %x\n", 0xe49d0004 | (reg << 12));
    //todo("return the machine code to pop{reg}\n");
    return 0xe49d0004 | (reg << 12);
}

// pc = where the instruction will be put.  this is 
// needed so that you can compute the offset from <pc>
// to <addr> which is what gets put in <bl>
static inline uint32_t armv6_bl(uint32_t bl_pc, uint32_t target) {
    // Calculate the offset from PC+8 to target as a signed value
    int32_t offset = (int32_t)(target - (bl_pc + 8));
    
    // The bl instruction format:
    // 31:28 = condition (0xE for always)
    printk("offset: %d\n", offset);
    printk("Final bl: %x\n", 0xEB000000 | (offset & 0x00FFFFFF));
    return 0xEB000000 | ((offset >> 2) & 0x00FFFFFF);
}
static inline uint32_t armv6_bx(uint32_t reg) {
    assert(reg<16);
    //todo("return the machine code to bx <reg>\n");
    return 0xe12fff10 | reg;
}

static inline uint32_t 
armv6_ldr(uint32_t dst_reg, uint32_t src_reg, uint32_t off) {
    assert(dst_reg<16);
    assert(src_reg<16);
    //todo("return the machine code to: ldr <dst>, [<src>+#<off>]\n");
    return 0xe5900000 | (dst_reg << 12) | (src_reg << 16) | (off & 0xFFF);
}

uint32_t pc_val_get(void) {
    uint32_t pc;
    asm volatile ("mov %0, pc" : "=r" (pc));
    return pc;
}

uint32_t reg1_val_get(void) {
    uint32_t val;
    //asm volatile ("mov %0, pc" : "=r" (pc));
    // concatenate "r" and reg to form the register name
    //asm volatile ("mov %0, %1" : "=r" (val) : "r" (reg));
    asm volatile ("mov %0, r1" : "=r" (val));
    return val;
}

// generate a dynamic call to hello() 
void jit_hello(void *fn, void * arg) {
    static uint32_t code[8];
    

    // a few of the registers
    enum {
        lr = 14,
        pc = 15,
        sp = 13,
        r0 = 0,
    };

    uint32_t addr =(uint32_t)fn;
    uint32_t n = 0;

    // generate a trampoline to call <fn>.
    //   1. we need to save and restore <lr> since the 
    //      call (bl) will trash it.
    //   2. need to make sure you sign extend <addr> in bl
    //      correctly so that it works with a negative offset!
    code[n++] = armv6_push(lr);

    // to see what value the pc register has when you read it
    // you can look at <prelab-code-pi/4-derive-pc-reg.c>
    // or also read the manual :)
    if(arg) {
        //todo("extend this code to handle a 32-bit argument!\n");
        //printk("arg: %s\n", (char *)arg);
        code[n++] = armv6_ldr(0, pc, 8);
    }

    uint32_t src = (uint32_t)&code[n];
    code[n++] = armv6_bl(src, addr);
    code[n++] = armv6_pop(lr);
    code[n++] = armv6_bx(lr);
    code[n++] = (uint32_t)arg;

    printk("emitted code at %x to call routine (%x):\n", code, addr);
    for(int i = 0; i < 4; i++) 
        printk("code[%d]=0x%x\n", i, code[i]);

    void (*fp)(void) = (typeof(fp))code;
    printk("about to call: %x\n", fp);
    printk("--------------------------------------\n");
    fp();
    printk("--------------------------------------\n");
}

void notmain(void) {
    // so we can catch some exceptions.
    interrupt_init();

    //uint32_t pc = pc_val_get();

    printk("arm push: %x\n", armv6_bl(0x8008, 0x8040));

    //step 1: generate calls that take no arguments.
    jit_hello(hello1, 0);
    jit_hello(hello2, 0);
    // step 2: generate calls that take a single argument
    jit_hello(printk, "hello world\n");
    jit_hello(printk, "hello world2\n");
}
