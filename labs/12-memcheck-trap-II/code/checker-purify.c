// engler: cs240lx: a sort-of purify checker: gives an error
// message if a load/store to a heap addres is:
//   - not within a legal block.
//   - to freed memory.
//
// uses the checking allocator (ckalloc).  for the moment
// just reboot on an error.
// 
// limits:
//   - does not check that its within the *correct*
//     legal block (can do this w/ replay).
//   - does not track anything about global or stack memory.
#include "memtrace.h"
#include "ckalloc.h"
#include "purify.h"
#include "sbrk-trap.h"
#include "memmap-default.h"
#include "rpi.h"

static int purify_quiet_p = 0;
void purify_yap_off(void) {
    purify_quiet_p = 1;
    memtrace_yap_off();
}
void purify_yap_on(void) {
    purify_quiet_p = 0;
    memtrace_yap_off();
}

void purify_error(fault_ctx_t *ctx){
    hdr_t *h = ck_get_containing_blk((void *)ctx->addr);
    if (!h) {
        ck_error(h, " illegal access to address 0x%x (not in any block)\n", (uint32_t *)ctx->addr);
    }

    int offset = ck_illegal_offset(h, (void *)ctx->addr);
    if (h->state == FREED) {
        if (offset == 0) {
            ck_error(h, " use after free at  within freed block\n");
        } else {
            if (offset < 0) {
                ck_error(h, " illegal store to to FREED block at  is %d bytes before legal mem (block size=%d)\n",
                offset, h->nbytes_alloc);
            } else {
                ck_error(h, " illegal store to to FREED block at  is %d bytes after legal mem (block size=%d)\n",
                offset, h->nbytes_alloc);
            }
        }
    } else if (offset < 0) {
        ck_error(h, " illegal %s to to allocated block at  is %d bytes before legal mem (block size=%d)\n",
              ctx->load_p ? "load" : "store", offset, h->nbytes_alloc);
    } else {
        ck_error(h, " illegal %s to to allocated block at  is %d bytes after legal mem (block size=%d)\n",
              ctx->load_p ? "load" : "store", offset, h->nbytes_alloc);
    }
}

// Pre-handler for memory tracing
static int purify_handler(void *data, fault_ctx_t *ctx) {
    if (!purify_quiet_p)
        trace(": %s to address %x\n", 
                ctx->load_p ? "load" : "store", ctx->addr);

    // Check if the address is in an allocated block
    hdr_t *h = ck_ptr_is_alloced((void *)ctx->addr);
    if (h) {
        // Address is in an allocated block, access is legal
        return MEMTRACE_OK;
    }

    purify_error(ctx);
    clean_reboot();
}

void purify_init(void) {
    memtrace_init(0, purify_handler, 0, dom_trap);
    memtrace_trap_enable();
    memtrace_yap_off();
}

void *purify_alloc_raw(unsigned n, src_loc_t loc) {
    memtrace_trap_disable();
    unsigned *p =  (ckalloc)(n, loc);
    memtrace_trap_enable();
    return p;
}

void purify_free_raw(void *p, src_loc_t loc) {
    memtrace_trap_disable();
    (ckfree)(p, loc);
    memtrace_trap_enable();
}

int purify_heap_errors(void) {
    return ck_heap_errors();
}
