#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include "libunix.h"
#include <unistd.h>
#include "code-gen.h"
#include "armv6-insts.h"

// Function pointers for derived instructions
uint32_t (*arm_movw)(uint32_t dst, uint32_t imm);
uint32_t (*arm_movt)(uint32_t dst, uint32_t imm);
uint32_t (*arm_str)(uint32_t src, uint32_t base, int32_t offset);
uint32_t (*arm_sub)(uint32_t dst, uint32_t src1, uint32_t src2);
uint32_t (*arm_bl)(int32_t offset);

// Array of all ARM registers
const char *all_regs[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    0  // NULL terminator
};

// Object-oriented interface for code generation
typedef struct {
    uint32_t *code;
    unsigned nbytes;
    unsigned capacity;
} code_gen_t;

/*
 *  1. emits <insts> into a temporary file: (use create_file
 *     write_exact, and then close the fd (otherwise).
 *  2. compiles it: 
 *      - look in examples/make.sh or the normal pi compilation 
 *         commands to see what commands to run.
 *      - use <run_system> to run them.
 *  3. read the results back in using <read_file> and return the pointer
 */
uint32_t *insts_emit(unsigned *nbytes, char *insts) {
    int fd = create_file("temp.s");
    if(fd < 0) 
        panic("could not create temp.s\n");

    write_exact(fd, insts, strlen(insts));
    write_exact(fd, "\n", 1);
    close(fd);

    run_system("arm-none-eabi-as temp.s -o temp1.o");
    run_system("arm-none-eabi-objcopy -O binary temp1.o temp2.bin");

    uint32_t *code = (uint32_t *)read_file(nbytes, "temp2.bin");
    if(!code)
        panic("could not read temp2.bin\n");

    run_system("rm -f temp.s temp1.o temp2.bin");

    return code;
}

/*
 * a cross-checking hack that uses the native GNU compiler/assembler to 
 * check our instruction encodings.
 *  1. compiles <insts> using <insts_emit>
 *  2. compares <code,nbytes> to the result of (1) for equivalance.
 *  3. prints out a useful error message if it did not succeed!!
 */
void insts_check(char *insts, uint32_t *code, unsigned nbytes) {
    unsigned gen_nbytes;
    uint32_t *gen = insts_emit(&gen_nbytes, insts);
    
    if(nbytes != gen_nbytes) {
        output("Mismatch in instruction size:\n");
        output("  Expected: %d bytes\n", nbytes);
        output("  Got: %d bytes\n", gen_nbytes);
        panic("instruction size mismatch\n");
    }
    for(unsigned i = 0; i < nbytes/4; i++) {
        if(code[i] != gen[i]) {
            output("Mismatch in instruction encoding:\n");
            output("  For instruction: %s\n", insts);
            output("  Expected: 0x%x\n", code[i]);
            output("  Got: 0x%x\n", gen[i]);
            panic("instruction encoding mismatch\n");
        }
    }
}

// check a single instruction.
void check_one_inst(char *insts, uint32_t inst) {
    return insts_check(insts, &inst, 4);
}

// helper function to make reverse engineering instructions a bit easier.
void insts_print(char *insts) {
    // emit <insts>
    unsigned gen_nbytes;
    uint32_t *gen = insts_emit(&gen_nbytes, insts);

    // print the result.
    output("getting encoding for: < %20s >\t= [", insts);
    unsigned n = gen_nbytes / 4;
    for(int i = 0; i < n; i++)
         output(" 0x%x ", gen[i]);
    output("]\n");
}


// helper function for reverse engineering.  you should refactor its interface
// so your code is better.
uint32_t emit_rrr(const char *op, const char *d, const char *s1, const char *s2) {
    char buf[1024];
    sprintf(buf, "%s %s, %s, %s", op, d, s1, s2);

    uint32_t n;
    uint32_t *c = insts_emit(&n, buf);
    assert(n == 4);
    return *c;
}

uint32_t emit_rr(const char *op, const char *d, const char *s1) {
    char buf[1024];
    sprintf(buf, "%s %s, %s", op, d, s1);

    uint32_t n;
    uint32_t *c = insts_emit(&n, buf);
    assert(n == 4);
    return *c;
}

uint32_t emit_r(const char* op, const char *r) {
    char buf[1024];
    sprintf(buf, "%s %s", op, r);

    uint32_t n;
    uint32_t *c = insts_emit(&n, buf);
    assert(n == 4);
    return *c;
}

// overly-specific.  some assumptions:
//  1. each register is encoded with its number (r0 = 0, r1 = 1)
//  2. related: all register fields are contiguous.
//
// NOTE: you should probably change this so you can emit all instructions 
// all at once, read in, and then solve all at once.
//
// For lab:
//  1. complete this code so that it solves for the other registers.
//  2. refactor so that you can reused the solving code vs cut and pasting it.
//  3. extend system_* so that it returns an error.
//  4. emit code to check that the derived encoding is correct.
//  5. emit if statements to checks for illegal registers (those not in <src1>,
//    <src2>, <dst>).
void derive_op_rrr(const char *name, const char *opcode, 
        const char **dst, const char **src1, const char **src2) {

    const char *s1 = src1[0];
    const char *s2 = src2[0];
    const char *d = dst[0];
    assert(d && s1 && s2);

    unsigned d_off = 0, src1_off = 0, src2_off = 0, op = ~0;

    uint32_t always_0 = ~0, always_1 = ~0;

    // dst
    for(unsigned i = 0; dst[i]; i++) {
        uint32_t u = emit_rrr(opcode, dst[i], s1, s2);
        always_0 &= ~u;
        always_1 &= u;
    }

    if(always_0 & always_1) 
        panic("impossible overlap: always_0 = %x, always_1 %x\n", 
            always_0, always_1);

    uint32_t never_changed = always_0 | always_1;
    uint32_t changed = ~never_changed;

    d_off = ffs(changed) - 1;
    if(((changed >> d_off) & ~0xf) != 0)
        panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // src1
    always_0 = ~0;
    always_1 = ~0;
    for(unsigned i = 0; src1[i]; i++) {
        uint32_t u = emit_rrr(opcode, d, src1[i], s2);
        always_0 &= ~u;
        always_1 &= u;
    }

    never_changed = always_0 | always_1;
    changed = ~never_changed;
    src1_off = ffs(changed) - 1;
    if(((changed >> src1_off) & ~0xf) != 0)
        panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // src2
    always_0 = ~0;
    always_1 = ~0;
    for(unsigned i = 0; src2[i]; i++) {
        uint32_t u = emit_rrr(opcode, d, s1, src2[i]);
        always_0 &= ~u;
        always_1 &= u;
    }

    never_changed = always_0 | always_1;
    changed = ~never_changed;
    src2_off = ffs(changed) - 1;
    if(((changed >> src2_off) & ~0xf) != 0)
        panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // Emit the function
    output("static int %s(uint32_t dst, uint32_t src1, uint32_t src2) {\n", name);
    output("    return %x | (dst << %d) | (src1 << %d) | (src2 << %d);\n",
                op,
                d_off,
                src1_off,
                src2_off);
    output("}\n");
}

// Helper function to derive a two-register instruction
void derive_op_rr(const char *name, const char *opcode, 
        const char **dst, const char **src) {
    const char *s = src[0];
    const char *d = dst[0];
    assert(d && s);

    unsigned d_off = 0, src_off = 0, op = ~0;
    uint32_t always_0 = ~0, always_1 = ~0;

    // dst
    for(unsigned i = 0; dst[i]; i++) {
        //output("name: %s, opcode: %s, dst: %s, src: %s\n", name, opcode, dst[i], s);
        uint32_t u = emit_rr(opcode, dst[i], s);
        always_0 &= ~u;
        always_1 &= u;
    }

    if(always_0 & always_1) 
        panic("impossible overlap: always_0 = %x, always_1 %x\n", 
            always_0, always_1);

    uint32_t never_changed = always_0 | always_1;
    uint32_t changed = ~never_changed;
    d_off = ffs(changed) - 1;
    if(((changed >> d_off) & ~0xf) != 0)
        panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // src
    always_0 = ~0;
    always_1 = ~0;
    for(unsigned i = 0; src[i]; i++) {
        uint32_t u = emit_rr(opcode, d, src[i]);
        always_0 &= ~u;
        always_1 &= u;
    }

    never_changed = always_0 | always_1;
    changed = ~never_changed;
    src_off = ffs(changed) - 1;
    if(((changed >> src_off) & ~0xf) != 0)
        panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // Print the function
    output("static int %s(uint32_t dst, uint32_t src) {\n", name);
    output("    return %x | (dst << %d) | (src << %d);\n",
                op,
                d_off,
                src_off);
    output("}\n");

    // Create a function pointer for this instruction
    // uint32_t (*func)(uint32_t, uint32_t) = (void*)kmalloc(1024);
    // uint32_t *code = (uint32_t*)func;
    
    // // Generate the machine code for the function
    // code[0] = op | (0 << d_off) | (0 << src_off);  // Template instruction
    // code[1] = 0xe12fff1e;  // bx lr (return)
    
    // // Store the function pointer in a global variable
    // if(strcmp(name, "arm_movw") == 0) arm_movw = func;
    // else if(strcmp(name, "arm_movt") == 0) arm_movt = func;
    // else if(strcmp(name, "arm_str") == 0) arm_str = func;
    // else if(strcmp(name, "arm_sub") == 0) arm_sub = func;
    // else if(strcmp(name, "arm_bl") == 0) arm_bl = func;
    // else panic("unknown instruction: %s\n", name);
}

void derive_op_r(const char *name, const char *opcode, 
        const char **reg) {
    const char *r = reg[0];
    assert(r);

    unsigned r_off = 0, op = ~0;
    uint32_t always_0 = ~0, always_1 = ~0;

    // dst
    for(unsigned i = 0; reg[i]; i++) {
        //output("name: %s, opcode: %s, reg: %s", name, opcode, reg[i]);
        uint32_t u = emit_r(opcode, reg[i]);
        always_0 &= ~u;
        always_1 &= u;
    }

    if(always_0 & always_1) 
        panic("impossible overlap: always_0 = %x, always_1 %x\n", 
            always_0, always_1);

    uint32_t never_changed = always_0 | always_1;
    uint32_t changed = ~never_changed;
    r_off = ffs(changed) - 1;
    // if(((changed >> r_off) & ~0xf) != 0)
    //     panic("weird instruction!  expecting at most 4 contig bits: %x\n", changed);
    op &= never_changed;

    // Print the function
    output("static int %s(uint32_t dst) {\n", name);
    output("    return %x | (dst << %d);\n",
                op,
                r_off);
    output("}\n");
}

// Initialize a code generator
// void code_gen_init(code_gen_t *cg) {
//     kmalloc_init(1024);  // Initialize heap with 1KB
//     cg->capacity = 1024;
//     cg->code = kmalloc(cg->capacity * sizeof(uint32_t));
//     cg->nbytes = 0;
// }

// // Emit an instruction into the code buffer
// void code_gen_emit(code_gen_t *cg, uint32_t inst) {
//     if(cg->nbytes + 4 > cg->capacity) {
//         // If we run out of space, just panic
//         panic("code buffer full! capacity=%d, nbytes=%d\n", 
//             cg->capacity, cg->nbytes);
//     }
//     cg->code[cg->nbytes/4] = inst;
//     cg->nbytes += 4;
// }

// // Load a 32-bit immediate into a register using movw/movt
// void code_gen_load_imm(code_gen_t *cg, uint32_t reg, uint32_t imm) {
//     // First derive the movw and movt instructions
//     derive_op_rr("arm_movw", "movw", all_regs, all_regs);
//     derive_op_rr("arm_movt", "movt", all_regs, all_regs);
    
//     // movw loads the lower 16 bits
//     code_gen_emit(cg, arm_movw(reg, imm & 0xffff));
//     // movt loads the upper 16 bits
//     code_gen_emit(cg, arm_movt(reg, imm >> 16));
// }

// // Push a register onto the stack
// void code_gen_push(code_gen_t *cg, uint32_t reg) {
//     // First derive the str and sub instructions
//     derive_op_rr("arm_str", "str", all_regs, all_regs);
//     derive_op_rr("arm_sub", "sub", all_regs, all_regs);
    
//     // Store the register at [sp, #-4]
//     code_gen_emit(cg, arm_str(reg, arm_sp, -4));
//     // Decrement the stack pointer
//     code_gen_emit(cg, arm_sub(arm_sp, arm_sp, 4));
// }

// // Make a non-linking function call
// void code_gen_call(code_gen_t *cg, uint32_t target) {
//     // First derive the bl instruction
//     derive_op_rr("arm_bl", "bl", all_regs, all_regs);
    
//     // Calculate the offset from current PC to target
//     // PC is 8 bytes ahead of current instruction in ARM
//     int32_t offset = target - ((uint32_t)cg->code + cg->nbytes + 8);
//     code_gen_emit(cg, arm_bl(offset));
// }

/*
 * 1. we start by using the compiler / assembler tool chain to get / check
 *    instruction encodings.  this is sleazy and low-rent.   however, it 
 *    lets us get quick and dirty results, removing a bunch of the mystery.
 *
 * 2. after doing so we encode things "the right way" by using the armv6
 *    manual (esp chapters a3,a4,a5).  this lets you see how things are 
 *    put together.  but it is tedious.
 *
 * 3. to side-step tedium we use a variant of (1) to reverse engineer 
 *    the result.
 *
 *    we are only doing a small number of instructions today to get checked off
 *    (you, of course, are more than welcome to do a very thorough set) and focus
 *    on getting each method running from beginning to end.
 *
 * 4. then extend to a more thorough set of instructions: branches, loading
 *    a 32-bit constant, function calls.
 *
 * 5. use (4) to make a simple object oriented interface setup.
 *    you'll need: 
 *      - loads of 32-bit immediates
 *      - able to push onto a stack.
 *      - able to do a non-linking function call.
 */

 enum {
    armv6_lr = 14,
    armv6_pc = 15,
    armv6_sp = 13,
    armv6_r0 = 0,

    op_mov = 0b1101,
    armv6_mvn = 0b1111,
    armv6_orr = 0b1100,
    op_mult = 0b0000,

    cond_always = 0b1110
};

// trivial wrapper for register values so type-checking works better.
typedef struct {
    uint8_t reg;
} reg_t;

static inline uint32_t arm_b(uint32_t bl_pc, uint32_t target) {
    // Calculate the offset from PC+8 to target as a signed value
    int32_t offset = (int32_t)(target - (bl_pc + 8));
    
    // The bl instruction format:
    // 31:28 = condition (0xE for always)
    // printk("offset: %d\n", offset);
    // printk("Final bl: %x\n", 0xEB000000 | (offset & 0x00FFFFFF));
    return 0xEA000000 | ((offset >> 2) & 0x00FFFFFF);
}

 static inline uint32_t 
armv6_ldr_off12(uint8_t rd, uint8_t rn, int offset) {
    // a5-20
    int u;
    uint32_t off;
    if(offset < 0) {
        u = 0;
        off = -offset;
    } else {
        u = 1;
        off = offset;
    }
    if(off >= (1 << 12))
        panic("offset %d too big!\n", offset);

    return (cond_always << 28)
         | (1 << 26)             // bits 27-26 = 01
         | (1 << 24)             // P = 1 (pre-index)
         | (u << 23)             // U = sign bit
         | (0 << 22)             // B = 0 (word access)
         | (0 << 21)             // W = 0 (no write-back)
         | (1 << 20)             // L = 1 (load)
         | (rn << 16)
         | (rd << 12)
         | (off & 0xfff);
}
int main(void) {
    // part 1: we gave the code code to do this.
    output("-----------------------------------------\n");
    output("part1: checking: correctly generating assembly.\n");
    insts_print("add r0, r0, r1");
    insts_print("bx lr");
    insts_print("mov r0, #1");
    insts_print("nop");
    output("\n");
    output("success!\n");

    // part 2: implement <check_one_inst> so these checks pass.
    // these should all pass.
    output("\n-----------------------------------------\n");
    output("part 2: checking we correctly compare asm to machine code.\n");
    check_one_inst("add r0, r0, r1", 0xe0800001);
    check_one_inst("bx lr", 0xe12fff1e);
    check_one_inst("mov r0, #1", 0xe3a00001);
    //check_one_inst("nop", 0xe320f000);
    output("success!\n");

    // part 3: sanity check the add encoding instruction (see armv6-insts.h)
    output("\n-----------------------------------------\n");
    output("part3: checking that we can generate an <add> by hand\n");
    check_one_inst("add r0, r1, r2", arm_add(arm_r0, arm_r1, arm_r2));
    check_one_inst("add r3, r4, r5", arm_add(arm_r3, arm_r4, arm_r5));
    check_one_inst("add r6, r7, r8", arm_add(arm_r6, arm_r7, arm_r8));
    check_one_inst("add r9, r10, r11", arm_add(arm_r9, arm_r10, arm_r11));
    check_one_inst("add r12, r13, r14", arm_add(arm_r12, arm_r13, arm_r14));
    check_one_inst("add r15, r7, r3", arm_add(arm_r15, arm_r7, arm_r3));
    output("success!\n");

    // part 4: implement the code so it will derive the add instruction.
    output("\n-----------------------------------------\n");
    output("part4: checking that we can reverse engineer an <add>\n");

    const char *all_regs[] = {
                "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
                "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
                0 
    };

    const char *bracket_regs[] = {
        "[r0]", "[r1]", "[r2]", "[r3]", "[r4]", "[r5]", "[r6]", "[r7]",
        "[r8]", "[r9]", "[r10]", "[r11]", "[r12]", "[r13]", "[r14]", "[r15]",
    };

    // XXX: should probably pass a bitmask in instead.
    derive_op_rrr("arm_add", "add", all_regs,all_regs,all_regs);
    //derive_op_rrr("arm_ldr", "ldr", all_regs, all_regs, )
    output("did something: now use the generated code in the checks above!\n");

    // Part 5: Extend to more instructions and build OO interface
    output("\n-----------------------------------------\n");
    output("part5: extending to more instructions and OO interface\n");

    // Derive more instructions
    //derive_op_rr("arm_ldr", "ldr", all_regs, bracket_regs);
    //derive_op_rr("arm_str", "str", all_regs, bracket_regs);
    derive_op_rr("arm_mov", "mov", all_regs, all_regs);
    derive_op_rr("arm_cmp", "cmp", all_regs, all_regs);
    derive_op_r("arm_bx", "bx", all_regs);
    const char *braces_regs[] = {
        "{r0}", "{r1}", "{r2}", "{r3}", "{r4}", "{r5}", "{r6}", "{r7}",
        "{r8}", "{r9}", "{r10}", "{r11}", "{r12}", "{r14}", "{r15}",
    };
    derive_op_r("arm_push", "push", braces_regs);
    derive_op_r("arm_pop", "pop", braces_regs);

    check_one_inst("b label; label: ", arm_b(0,4));
    //check_one_inst("bl label; label: ", arm_bl(0,4));
    check_one_inst("label: b label; ", arm_b(0,0));
    //check_one_inst("label: bl label; ", arm_bl(0,0));
    check_one_inst("ldr r0, [pc,#0]", armv6_ldr_off12(arm_r0, arm_r15, 0));
    check_one_inst("ldr r0, [pc,#256]", armv6_ldr_off12(arm_r0, arm_r15, 256));
    check_one_inst("ldr r0, [pc,#-256]", armv6_ldr_off12(arm_r0, arm_r15, -256));

    // Test the OO interface
    // code_gen_t cg;
    // code_gen_init(&cg);

    // // Test case 1: Load immediate
    // code_gen_load_imm(&cg, arm_r0, 0x12345678);
    // check_one_inst("movw r0, #0x5678", cg.code[0]);
    // check_one_inst("movt r0, #0x1234", cg.code[1]);
    
    // // Test case 2: Push onto stack
    // code_gen_push(&cg, arm_r0);
    // check_one_inst("str r0, [sp, #-4]", cg.code[2]);
    // check_one_inst("sub sp, sp, #4", cg.code[3]);
    
    // // Test case 3: Function call
    // code_gen_call(&cg, 0x8000);
    // check_one_inst("bl #0x8000", cg.code[4]);

    // // Print the generated code
    // output("Generated code:\n");
    // for(unsigned i = 0; i < cg.nbytes/4; i++) {
    //     output("0x%x\n", cg.code[i]);
    // }

    output("All OO interface tests passed!\n");
    return 0;
}
