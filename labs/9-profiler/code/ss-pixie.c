// a simple single step tracing example.
//
// lab: 
//   1. change this code so it is an instruction profiler 
//      (similar go gprof in 140e)
//   2. add PMU counters to make things more precise.
//      (you will have to change the <prefetch_abort_vector>
//      signature)
#include <stdint.h>
#include "rpi.h"
#include "ss-pixie.h"
#include "memmap.h"
#include "kr-malloc.h"

#include "breakpoint.h"
#include "cpsr-util.h"
// defines <cp_asm_set> macro.
#include "asm-helpers.h"

// counter of the number of instructions.
static volatile unsigned n_inst = 0;

// array to store instruction counts
static unsigned *inst_counts = 0;

// array to store cycle counts
static unsigned *cycle_counts = 0;

// correction factor for handler overhead
static unsigned handler_overhead = 0;
static int calibration_done = 0;

static int first_initialization = 1;

// Functions to read from thread ID registers
cp_asm(read_thread_id_reg0, p15, 0, c13, c0, 2);
cp_asm(read_thread_id_reg1, p15, 0, c13, c0, 3);

// use a swi instruction to invoke system
//  see: <ss-pixie-asm.S>
int pixie_invoke_syscall(int syscall, ...);

// switch to SUPER mode and load <regs>
//  see: <ss-pixie-asm.S>
void pixie_switchto_super_asm(uint32_t regs[16]);

// switch to USER mode.
//  see: <ss-pixie-asm.S>
void pixie_switchto_user_asm(void);

// control output a bit: if non-zero print, otherwise don't.
#define pixout(args...) \
    do { if(pixie_verbose_p) output(args); } while(0)

static int pixie_verbose_p = 1;
void pixie_verbose(int verbose_p) {
    pixie_verbose_p = verbose_p;
}

// called on each single-step exception. see
// <ss-pixie-asm.S>
void prefetch_abort_vector(uint32_t pc) {
    if(!brkpt_fault_p())
        panic("have a non-breakpoint fault\n");

    n_inst++;

    // Get cycle counts from thread ID registers
    uint32_t end_cycles = read_thread_id_reg0_get();
    uint32_t start_cycles = read_thread_id_reg1_get();
    
    // Calculate cycle count for this instruction
    uint32_t cycles = end_cycles - start_cycles;
    
    // Subtract handler overhead after calibration is done
    // if (calibration_done && cycles > handler_overhead) {
    //     cycles -= handler_overhead;
    // }

    pixout("%d: ss fault: pc=%x, cycles=%d\n", n_inst, pc, cycles);

    // Calculate index into instruction count array
    // Each instruction is 4 bytes, so divide by 4
    // Subtract code start address to get relative offset
    unsigned idx = (pc - (uint32_t)__code_start__) / 4;
    if(idx < ((uint32_t)__code_end__ - (uint32_t)__code_start__) / 4) {
        inst_counts[idx]++;
        cycle_counts[idx] += cycles;
    }

    // set a mismatch on the fault pc so that we can run it.
    brkpt_mismatch_set(pc);

    // if you don't print: you don't have to do this.
    // if you do print, there is a race condition if we 
    // are stepping through the UART code --- note: we could
    // just check the pc and the address range of
    // uart.o
    while(!uart_can_put8())
        ;
}

// called before dying.  we make the symbol weak so
// that if the test case has, will override.
// this lets test cases do thier own internal
// checking.
void WEAK(pixie_die_handler)(uint32_t regs[16]) {
    output("done: dying!\n");
}

// very limited system calls.  we only need these
// b/c SS has to run at USER level so can't switch
// back.
void pixie_syscall(int sysno, uint32_t *regs) {
    // check the saved spsr and make sure is USER level.
    if(mode_get(spsr_get()) != USER_MODE)
        panic("not at USER level?\n");

    switch(sysno) {
    case PIXIE_SYS_DIE:
        pixie_die_handler(regs);
        clean_reboot();
    case PIXIE_SYS_STOP:
        pixout("done: pc=%x\n", regs[15]);
        // change to SUPER
        pixie_switchto_super_asm(regs);
        not_reached();
    default:
        panic("invalid syscall: %d\n", sysno);
    }
    not_reached();
}

// 3-121 --- set exception jump table location.
cp_asm_set(vector_base_asm, p15, 0, c12, c0, 0)

static void calibrate_handler_overhead(void) {
    unsigned total_cycles = 0;
    const unsigned num_samples = 100;
    
    for(unsigned i = 0; i < num_samples + 1; i++) {
        asm volatile("nop");
        uint32_t start = read_thread_id_reg1_get();
        uint32_t end = read_thread_id_reg0_get();

        total_cycles += (end - start);
        output("total_cycles: %d\n", total_cycles);
    }
    
    handler_overhead = total_cycles / num_samples;
    calibration_done = 1;
    
    output("Handler overhead calibrated: %d cycles\n", handler_overhead);
}

// start single step mismatching.
//  1. sets up exception vectors (idempotent).
//  2. enables single stepping (idempotent)
//  3. switches to USER mode.
//
// NOTE:
//   if you want to ignore instructions until 
//   return back outside of <pixie_start>
//   1. get the return address before switching
//      to USER mode
//         ignore_until_pc = __builtin_return_address(0);
//   2. in the single-step handler: ignore all PC values 
//      until <pc> equals <ignore_until_pc>
void pixie_start(void) {
    // Initialize kmalloc with the correct heap location
    if (first_initialization) {
        kmalloc_init(1024);
        first_initialization = 0;
    }

    // Allocate instruction count array
    unsigned code_size = (uint32_t)__code_end__ - (uint32_t)__code_start__;
    inst_counts = kmalloc(code_size / 4 * sizeof(unsigned));
    cycle_counts = kmalloc(code_size / 4 * sizeof(unsigned));
    memset(inst_counts, 0, code_size / 4 * sizeof(unsigned));
    memset(cycle_counts, 0, code_size / 4 * sizeof(unsigned));


    // vector of exception handlers: see <ss-pixie-asm.S>
    // check alignment and then set the vector base to it.
    extern uint32_t pixie_exception_vec[];
    uint32_t vec = (uint32_t)pixie_exception_vec;
    unsigned rem = vec % 32;
    if(rem != 0)
        panic("interrupt vec not aligned to 32 bytes!\n", rem);
    vector_base_asm_set(vec);

    // enable mismatching.  note: we are currently
    // at privileged mode so won't start til we 
    // switch to to user mode.
    //
    // this routine is from 140e.  will: 
    //  1. enable the debug co-processor cp14 
    //  2. set a mismatch on address <0>.  once we are 
    //    at user-level, this will cause any pc != 0 (all 
    //    of them) to immediately mismatch
    brkpt_mismatch_start();

    // switch from SUPER to USER mode: not just a matter of
    // switching the cpsr since the sp,lr are different.

    // 1. sanity check that we are at SUPER_MODE
    if(mode_get(cpsr_get()) != SUPER_MODE)
        panic("not at SUPER level?\n");

    // 2. switch to USER.  will immediately
    // immediately start mismatching when
    // the mode switch occurs in routine. 

    pixie_switchto_user_asm();
    //output("test\n");


    // 3. sanity check that at USER. 
    //
    // NOTE: this check adds extra instructions to the 
    // trace if you don't filter them (see note above)
    if(mode_get(cpsr_get()) != USER_MODE)
        panic("not at user level?\n");

    // Calibrate handler overhead
    //calibrate_handler_overhead();
}

// turn off single step matching. 
//
// we need to do a system call b/c we are running 
// at user level and thus can't write to the debug 
// co-processor
//
// alernative: catch the undefined instruction and 
// do it.
unsigned pixie_stop(void) {
    pixie_invoke_syscall(PIXIE_SYS_STOP);
    pixout("pixie: stopped!\n");
    return n_inst;
}

// Print out instructions with counts >= N
void pixie_dump(unsigned N) {
    unsigned code_size = (uint32_t)__code_end__ - (uint32_t)__code_start__;
    unsigned n_instructions = code_size / 4;

    // First pass: count how many instructions have count >= N
    unsigned n_above = 0;
    for(unsigned i = 0; i < n_instructions; i++) {
        if(inst_counts[i] >= N) {
            n_above++;
        }
    }

    // Allocate array to store indices of instructions with count >= N
    unsigned *indices = kmalloc(n_above * sizeof(unsigned));
    unsigned idx = 0;
    for(unsigned i = 0; i < n_instructions; i++) {
        if(inst_counts[i] >= N) {
            indices[idx++] = i;
        }
    }

    // Sort indices by count (descending)
    for(unsigned i = 0; i < n_above - 1; i++) {
        for(unsigned j = 0; j < n_above - i - 1; j++) {
            if(inst_counts[indices[j]] < inst_counts[indices[j + 1]]) {
                unsigned temp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = temp;
            }
        }
    }

    // Print results with both instruction counts and average cycles
    output("Instructions with count >= %d:\n", N);
    for(unsigned i = 0; i < n_above; i++) {
        unsigned pc = (uint32_t)__code_start__ + indices[i] * 4;
        unsigned avg_cycles = cycle_counts[indices[i]] / inst_counts[indices[i]];
        output("pc=%x: count=%d, avg_cycles=%d\n", pc, inst_counts[indices[i]], avg_cycles);
    }

    //kfree(indices);
}
