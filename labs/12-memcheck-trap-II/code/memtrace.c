#include "rpi.h"
#include "memtrace.h"
#include "sbrk-trap.h"
#include <stdint.h>

#include "watchpoint.h"

#include "mmu.h"
// 140e exception handling support
#include "full-except.h"
// 140e helpers for getting exception reason.
#include "armv6-except.h"
// 140e code for full context switching
// (caller,callee and cpsr).
#include "switchto.h"

// Global state for the memory tracing system
static struct {
    void *data;              
    memtrace_fn_t pre;      
    memtrace_fn_t post;      
    unsigned trap_dom;      
} memtrace_state;

// 1 = we expect a domain fault.
// 0 = we expect a or a watchpoint fault.
// used to catch some mistakes.
static int expect_domain_fault_p = 1;

// right now we only allow a single checker.  wrap this
// up for multiple checkers.
static memtrace_fn_t pre;
static memtrace_fn_t post;
static void *data;

static int quiet_p = 1;
void memtrace_yap_off(void) { quiet_p = 1; }
void memtrace_yap_on(void)  { quiet_p = 0; }

// pre-computed domain register values.
static uint32_t trap_access;
static uint32_t no_trap_access;

static int trap_is_on_p(void) {
    return domain_access_ctrl_get() == trap_access;
}

static void trap_on(void) {
    domain_access_ctrl_set(trap_access);
}

static void trap_off(void) {
    domain_access_ctrl_set(no_trap_access);
}

// turn memtracing on: wrapper with extra error checking.
void memtrace_trap_enable(void) {
    // need at least one handler!
    assert(pre || post);
    // if not true, didn't init
    assert(trap_access && no_trap_access);
    assert(!trap_is_on_p());
    trap_on();
}

// turn memtracing off: wrapper with extra error checking.
void memtrace_trap_disable(void) {
    // if not true, didn't init
    assert(trap_access && no_trap_access);
    assert(trap_is_on_p());
    trap_off();
}

// XXX: a good extension: change this so you look at the
// actual instruction and get the actual bytes.
static inline unsigned inst_nbytes(uint32_t inst) {
    return 4;
}

static void data_fault(regs_t *r) {
    // sanity check that we still at SUPER
    //   - should make it so we can run at user level.
    if(mode_get(r->regs[16]) != SUPER_MODE)
        panic("got a fault not at SUPER level?\n");

    if (trap_is_on_p()) {
        uint32_t addr = data_abort_addr();
        memtrace_trap_disable();
        uint32_t pc = r->regs[15];

        watchpt_on_ptr((uint32_t *)addr);

        fault_ctx_t ctx = fault_ctx_mk(r, addr, inst_nbytes(0), 0);

        if (pre) {
            if (!quiet_p)
                output("memtrace: pre-handler: pc=%x, addr=%x\n", pc, addr);
            pre(data, &ctx);
        }
    } else {
        uint32_t addr = watchpt_fault_addr();
        watchpt_off_ptr((uint32_t *)addr);
        int load_p = watchpt_load_fault_p();
        uint32_t pc = r->regs[15];
        fault_ctx_t ctx = fault_ctx_mk(r, addr, inst_nbytes(0), load_p);

        if (post) {
            if (!quiet_p)
                output("memtrace: post-handler: pc=%x, addr=%x\n", pc, addr);
            post(data, &ctx);
        }

        memtrace_trap_enable();
    }

    // drain printk to avoid the "can tx" race in UART.
    while(!uart_can_put8())
        ;

    switchto(r);
}

// initialize memtrace system.
void memtrace_init(
    void *data_h,
    memtrace_fn_t pre_h,
    memtrace_fn_t post_h,
    unsigned trap_dom) {

    // setting up VM does not belong here, but we do it to keep things
    // simple for today's lab.
    assert(!mmu_is_enabled());
    sbrk_init();
    assert(mmu_is_enabled());

    pre = pre_h;
    post = post_h;
    if(!pre && !post)
        panic("must supply one handler: pre=%x, post=%x\n", pre,post);
    data = data_h;
    assert(trap_dom < 16);

    trap_access = 4;
    no_trap_access = 4 | (1 << (trap_dom*2));

    // XXX: what's the right way to handle SS exceptions at the same time?
    full_except_install(0);
    full_except_set_data_abort(data_fault);
}
