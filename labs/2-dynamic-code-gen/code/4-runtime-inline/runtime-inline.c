// test framework to check and time runtime inlining of GET32 and PUT32.
//
// at runtime we do binary rewriting to replace every call to 
//  - GET32_inline with an equivalent load instruction
//  - and PUT32_inline with an equiv store instruction.
//
// we do it in this kind of roundabout way to make debugging easier.
// after you get this working, you can do something more aggressive
// as an extension!
#include "rpi.h"
#include "cycle-count.h"

// prototypes: these are identical to get32/put32.

// *(unsigned *)addr
unsigned GET32_inline(unsigned addr);
unsigned get32_inline(const volatile void *addr);

// *(unsigned *)addr = v;
void PUT32_inline(unsigned addr, unsigned v);
void put32_inline(volatile void *addr, unsigned v);

static int inline_on_p = 0, inline_cnt = 0;

// turn off inlining if we are dying.
#define die(msg...) do { inline_on_p = 0; panic("DYING:" msg); } while(0)

// inline helper: called from runtime-inline-asm.S:GET32_inline
// <lr> holds the value of the lr register, which will be 4 
// bytes past the call instruction to GET32_inline
uint32_t GET32_inline_helper(uint32_t addr, uint32_t lr) {
    if(inline_on_p) {
        //todo("smash the call instruction to just do: ldr r0, [r0]\n");
        uint32_t pc = lr - 4;

        inline_cnt++;

        // turn inlining off while we print to make debugging easier.
        inline_on_p = 0;
        output("GET: rewriting address=%x, inline count=%d\n", pc, inline_cnt);
        *((uint32_t *)pc) = 0xe5900000;
        inline_on_p = 1;
    }

    // manually do the load and return the first time.
    return *(volatile uint32_t*)addr;
}

// go through and rewrite similar to GET32_inline -- you have to 
// modify <runtime-inline-asm.S> as well.
void PUT32_inline_helper(uint32_t addr, uint32_t val, uint32_t lr) {
    //printk("PUT32_inline_helper, addr: %x, val: %x, lr: %x\n", addr, val, lr);
    if (inline_on_p) {
    uint32_t pc = lr - 4;  // address of the call instruction
    inline_cnt++;

        inline_on_p = 0;
        output("PUT: rewriting address=%x, inline count=%d\n", pc, inline_cnt);
        // Replace the call instruction with the store:
        // str r1, [r0] is encoded as 0xe5801000.
        *(volatile uint32_t *)pc = 0xe5801000;
        inline_on_p = 1;
    }
    // Do the store manually on the first call.
    *(volatile uint32_t *)addr = val;
}

/********************************************************************
 * simple tests that check results.
 */

// this should get rerwitten
void test_get32_inline(unsigned n) {
    for(unsigned i = 0; i < n; i++) {
        uint32_t got = GET32_inline((uint32_t)&i);
        if(got != i) {
            inline_on_p = 0;
            panic("got %d, expected %d\n", got, i);
        }
    }
}

// this should not get rewritten initially.
void test_get32(unsigned n) {
    for(unsigned i = 0; i < n; i++) {
        uint32_t got = GET32((uint32_t)&i);
        if(got != i)
            panic("got %d, expected %d\n", got, i);
    }
}


// test using our runtime inline version.
void test_put32_inline(unsigned n) {
    uint32_t x;

    for(unsigned i = 0; i < n; i++) {
        PUT32_inline((uint32_t)&x, i);
        if(x != i)
            panic("got %d, expected %d\n", x, i);
    }
}

// test using regular put32: this won't get rewritten initially.
void test_put32(unsigned n) {
    uint32_t x;

    for(unsigned i = 0; i < n; i++) {
        PUT32((uint32_t)&x, i);
        if(x != i)
            panic("got %d, expected %d\n", x, i);
    }
}

/*************************************************************
 * versions without loops or checking: more sensitive to speedup
 */

// use our inline GET32: all of these calls should get inlined.
// a good check as an extension: after running this onece, check
// in GET32_inline that we never get called with an <lr> in this
// routine.
void test_get32_inline_10(void) {
    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);

    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);
    GET32_inline(0);
}

void test_put32_inline_10(void) {
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);

    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
    PUT32_inline(0, 0);
}

// use the raw GET32: these won't get inlined, which
// gives us a comparison point.
void test_get32_10(void) {
    GET32(0);
    GET32(0);
    GET32(0);
    GET32(0);
    GET32(0);

    GET32(0);
    GET32(0);
    GET32(0);
    GET32(0);
    GET32(0);
}

void test_put32_10(void) {
    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);

    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);
    PUT32(0, 0);
}


static inline uint32_t armv6_bl(uint32_t bl_pc, uint32_t target) {
    // Calculate the offset from PC+8 to target as a signed value
    int32_t offset = (int32_t)(target - (bl_pc + 8));
    
    // The bl instruction format:
    // 31:28 = condition (0xE for always)
    // printk("offset: %d\n", offset);
    // printk("Final bl: %x\n", 0xEB000000 | (offset & 0x00FFFFFF));
    return 0xEB000000 | ((offset >> 2) & 0x00FFFFFF);
}

// we time things a bunch of different ways.
void notmain(void) {
    assert(!inline_cnt);

    output("about to test get32 inlining\n");
    inline_on_p = 1;
    uint32_t t_inline = TIME_CYC(test_get32_inline(1));
    uint32_t t_inline_run10 = TIME_CYC(test_get32_inline(100));
    uint32_t t_run10 = TIME_CYC(test_get32(100));
    inline_on_p = 0;

    output("time to run w/ inlining overhead:   %d\n", t_inline);
    output("time to run 10 times inlined:       %d\n", t_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_run10);
    output("total inline count=%d\n", inline_cnt);


    output("about to test get32 inlining without loop or checking\n");

    // test without a loop
    inline_on_p = 1;
    t_inline       = TIME_CYC(test_get32_inline_10());
    t_inline_run10 = TIME_CYC(test_get32_inline_10());
    t_run10        = TIME_CYC(test_get32_10());
    inline_on_p = 0;

    output("time to run w/ inlining overhead:   %d\n", t_inline);
    output("time to run 10 times inlined:       %d\n", t_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_run10);
    output("total inline count=%d\n", inline_cnt);

    //todo("smash the original get32: should get same speedup\n");
    
    // smash the GET32 code to call the GET32_inline_helper, identically
    // how GET32_inline does.  you can' just copy the code since the branch
    // is relative to the destination.
    // 
    //      note: 0xe1a0100e  =  mov r1, lr
    uint32_t *get_pc = (void *)GET32;
    uint32_t orig_val0 = get_pc[0];
    uint32_t orig_val1 = get_pc[1];

    // 1. rewrite the GET32 code to call GET32_inline.
    // 2. this will result in all caller sites to GET32 getting
    //    inlined as well.
    // 3. thus: when it runs below, should have the same speedup
    //todo("assign the new instructions to get_pc[0] and get_pc[1]\n");
    // uint32_t target = (uint32_t)GET32_inline_helper;
    // uint32_t pc_addr = (uint32_t)get_pc;  // address of first instruction in GET32
    // uint32_t offset = (target - (pc_addr + 8)) >> 2;
    // // BL opcode is 0xEB000000.
    // get_pc[0] = 0xEB000000 | (offset & 0x00FFFFFF);
    // // Second instruction: move lr into r1: encoded as 0xe1a0100e.
    // get_pc[1] = 0xe1a0100e;

    output("branching to GET32_inline\n");

    uint32_t bl_to_get32_inline = armv6_bl((uint32_t)get_pc, (uint32_t)GET32_inline) & ~(1 << 24);

    output("bl_to_get32_inline: %x\n", bl_to_get32_inline);

    get_pc[0] = bl_to_get32_inline;
    //get_pc[0] = 0xe1a0100e;

    output("after rewriting get32!\n");
    output("    inst[0] = %x\n", get_pc[0]);
    output("    inst[1] = %x\n", get_pc[1]);
    inline_on_p = 1;

    // this test should now have inline overhead.
    t_inline       = TIME_CYC(test_get32_10());
    // this is our original: should have the same speedup.
    t_inline_run10 = TIME_CYC(test_get32_inline_10());
    // after inlining this should be same as the GET32_inline overhead.
    t_run10        = TIME_CYC(test_get32_10());
    inline_on_p = 0;

    output("time to run w/ inlining overhead:   %d\n", t_inline);
    output("time to run 10 times inlined:       %d\n", t_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_run10);
    output("total inline count=%d\n", inline_cnt);

    output("--------------------------------\n");

    output("about to test put32 inlining\n");
    inline_on_p = 1;
    uint32_t t_put_inline = TIME_CYC(test_put32_inline(1));
    uint32_t t_put_inline_run10 = TIME_CYC(test_put32_inline(100));
    uint32_t t_put = TIME_CYC(test_put32(100));
    inline_on_p = 0;
    
    output("time to run w/ inlining overhead:   %d\n", t_put_inline);
    output("time to run 10 times inlined:       %d\n", t_put_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_put);
    output("total inline count=%d\n", inline_cnt);

    output("about to test put32 inlining without loop or checking\n");

    inline_on_p = 1;
    t_put_inline       = TIME_CYC(test_put32_inline_10());
    t_put_inline_run10 = TIME_CYC(test_put32_inline_10());
    t_put = TIME_CYC(test_put32_10());
    inline_on_p = 0;
    
    output("time to run w/ inlining overhead:   %d\n", t_put_inline);
    output("time to run 10 times inlined:       %d\n", t_put_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_put);
    output("total inline count=%d\n", inline_cnt);

    uint32_t *put_pc = (void *)PUT32;
    // uint32_t orig_val0 = put_pc[0];
    // uint32_t orig_val1 = put_pc[1];

    output("branching to PUT32_inline\n");
    
    uint32_t bl_to_put32_inline = armv6_bl((uint32_t)put_pc, (uint32_t)PUT32_inline) & ~(1 << 24);

    output("bl_to_put32_inline: %x\n", bl_to_put32_inline);

    put_pc[0] = bl_to_put32_inline;
    //put_pc[0] = 0xe1a0100e;

    output("after rewriting put32!\n");
    output("    inst[0] = %x\n", put_pc[0]);
    output("    inst[1] = %x\n", put_pc[1]);
    inline_on_p = 1;

    t_put_inline = TIME_CYC(test_put32_inline(1));
    t_put_inline_run10 = TIME_CYC(test_put32_inline(100));
    t_put = TIME_CYC(test_put32(100));
    inline_on_p = 0;

    output("time to run w/ inlining overhead:   %d\n", t_put_inline);
    output("time to run 10 times inlined:       %d\n", t_put_inline_run10);
    output("time to run 10 times non-inlined:   %d\n", t_put);
    output("total inline count=%d\n", inline_cnt);

    output("--------------------------------\n");
}
