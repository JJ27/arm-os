#include "rpi.h"

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
#include "cycle-util.h"

typedef void (*int_fp)(void);

static volatile unsigned cnt = 0;

// fake little "interrupt" handlers: useful just for measurement.
void int_0() { cnt++; }
void int_1() { cnt++; }
void int_2() { cnt++; }
void int_3() { cnt++; }
void int_4() { cnt++; }
void int_5() { cnt++; }
void int_6() { cnt++; }
void int_7() { cnt++; }

void generic_call_int(int_fp *intv, unsigned n) { 
    for(unsigned i = 0; i < n; i++)
        intv[i]();
}

static inline uint32_t armv6_push(int reg) {
    assert(reg<16);
    //printk("push: %x\n", 0xe52d0004 | (reg << 12));
    //todo("return the machine code to push{reg}\n");
    return 0xe52d0004 | (reg << 12);
}
static inline uint32_t armv6_pop(int reg) {
    assert(reg<16);
    //printk("pop: %x\n", 0xe49d0004 | (reg << 12));
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
    // printk("offset: %d\n", offset);
    // printk("Final bl: %x\n", 0xEB000000 | (offset & 0x00FFFFFF));
    return 0xEB000000 | ((offset >> 2) & 0x00FFFFFF);
}

static inline uint32_t armv6_b(uint32_t bl_pc, uint32_t target) {
    // Calculate the offset from PC+8 to target as a signed value
    int32_t offset = (int32_t)(target - (bl_pc + 8));
    
    // The bl instruction format:
    // 31:28 = condition (0xE for always)
    // printk("offset: %d\n", offset);
    // printk("Final bl: %x\n", 0xEA000000 | (offset & 0x00FFFFFF));
    return 0xEA000000 | ((offset >> 2) & 0x00FFFFFF);
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

enum {
        lr = 14,
        pc = 15,
        sp = 13,
        r0 = 0,
    };

// you will generate this dynamically.
void specialized_call_int(void) {
    int_0();
    int_1();
    int_2();
    int_3();
    int_4();
    int_5();
    int_6();
    int_7();
}

static void inline int_compile(int_fp *intv, unsigned n) {
    static unsigned code[20];
    code[0] = armv6_push(lr);
    for (unsigned i = 1; i < n; i++) {
        code[i] = armv6_bl((uint32_t)&code[i], (uint32_t)intv[i-1]);
    }
    code[n] = armv6_pop(lr);
    code[n+1] = armv6_b((uint32_t)&code[n+1], (uint32_t)intv[n-1]);
    //code[n+2] = armv6_bx(lr);
    
    void (*fp)(void) = (typeof(fp))code;
    fp();
}

void notmain(void) {
    int_fp intv[] = {
        int_0,
        int_1,
        int_2,
        int_3,
        int_4,
        int_5,
        int_6,
        int_7
    };

    cycle_cnt_init();

    unsigned n = NELEM(intv);

    // try with and without cache: but if you modify the routines to do 
    // jump-threadig, must either:
    //  1. generate code when cache is off.
    //  2. invalidate cache before use.
    // enable_cache();

    cnt = 0;
    TIME_CYC_PRINT10("cost of generic-int calling",  generic_call_int(intv,n));
    demand(cnt == n*10, "cnt=%d, expected=%d\n", cnt, n*10);

    // rewrite to generate specialized caller dynamically.
    cnt = 0;
    TIME_CYC_PRINT10("cost of specialized int calling", specialized_call_int() );
    demand(cnt == n*10, "cnt=%d, expected=%d\n", cnt, n*10);

    cnt = 0;
    TIME_CYC_PRINT10("cost of specialized int calling", int_compile(intv, n) );
    demand(cnt == n*10, "cnt=%d, expected=%d\n", cnt, n*10);

    clean_reboot();
}
