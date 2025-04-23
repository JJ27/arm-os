/*************************************************************************
 * engler, cs240lx: Purify/Boehm style leak checker/gc starter code.
 *
 * We've made a bunch of simplifications.  Given lab constraints:
 * people talk about trading space for time or vs. but the real trick 
 * is how to trade space and time for less IQ needed to see something 
 * is correct. (I haven't really done a good job of that here, but
 * it's a goal.)
 * 
 * XXX: note: I don't think the gcc_mb stuff is needed?  Remove and see if get
 * errors.
 * 
 * need to wrap up in regions: the gc should be given a list of places to scan,
 * and there should be one place that knows all the places we use.
 * i think just set this up in cstart?    it has to know where stuff is so it
 * can init.  you can then add a region later.
 */
#include "rpi.h"
#include "rpi-constants.h"
#include "ckalloc.h"
#include "kr-malloc.h"
#include "libc/helper-macros.h"
#include "memmap.h"
#include <stdint.h>  // For uint32_t


// implement these five routines below.

// these four are at the end of this file.
static void mark(const char *where, uint32_t *p, uint32_t *e);
static void mark_all(uint32_t *sp);
static unsigned sweep_leak(int warn_no_start_ref_p);
static unsigned sweep_free(void);

// this routine is in <gc-asm.S>
//
// write the assembly to dump all registers.
// need this to be in a seperate assembly file since gcc 
// seems to be too smart for its own good.
void dump_regs(uint32_t v[16]);

/**********************************************************************
 * starter code: don't have to modify other than for extensions.
 */

// currently assume allocated memory is contiguous.  otherwise
// we need a list of allocated regions.
static void * heap_start;
static void * heap_end;

void *sbrk(long increment) {
    static int init_p;

    assert(increment > 0);
    if(init_p) 
        panic("not handling\n");
    else {
        unsigned onemb = 0x100000;
        heap_start = (void*)onemb;
        heap_end = (char*)heap_start + onemb;
        kmalloc_init_set_start((void*)onemb, onemb);
        init_p = 1;
    }
    return kmalloc(increment);
}

// quick check that the pointer is between the start of
// the heap and the last allocated heap pointer.  saves us 
// walk through all heap blocks.
//
// MAYBE: warn if the pointer is within number of bytes of
// start or end to detect simple overruns.
static int in_heap(void *p) {
    // should be the last allocated byte(?)
    if(p < heap_start || p >= heap_end)
        return 0;
    // output("ptr %p is in heap!\n", p);
    return 1;
}

// given potential address <addr>, returns:
//  - 0 if <addr> does not correspond to an address range of 
//    any allocated block.
//  - the associated header otherwise (even if freed: caller should
//    check and decide what to do in that case).
//
// XXX: you'd want to abstract this some so that you can use it with
// other allocators.  our leak/gc isn't really allocator specific.
static hdr_t *is_ptr(uint32_t addr) {
    void *p = (void*)addr;
    
    if(!in_heap(p))
        return 0;
    return ck_ptr_is_alloced(p);
}

// return number of bytes allocated?  freed?  leaked?
// how do we check people?
unsigned ck_find_leaks_fn(int warn_no_start_ref_p, uint32_t *sp) {
    mark_all(sp);
    return sweep_leak(warn_no_start_ref_p);
}

// used for tests.  just keep it here.
void check_no_leak(void) {
    // when in the original tests, it seemed gcc was messing 
    // around with these checks since it didn't see that 
    // the pointer could escape.
    gcc_mb();
    if(ck_find_leaks(1))
        panic("GC: should have no leaks!\n");
    else
        trace("GC: SUCCESS: no leaks!\n");
    gcc_mb();
}

// used for tests.  just keep it here.
unsigned check_should_leak(void) {
    // when in the original tests, it seemed gcc was messing 
    // around with these checks since it didn't see that 
    // the pointer could escape.
    gcc_mb();
    unsigned nleaks = ck_find_leaks(1);
    if(!nleaks)
        panic("GC: should have leaks!\n");
    else
        trace("GC: SUCCESS: found %d leaks!\n", nleaks);
    gcc_mb();
    return nleaks;
}

/***********************************************************************
 * implement the routines below.
 */


// mark phase:
//  - iterate over the words in the range [p,e], marking any block 
//    potentially referenced.
//  - if we mark a block for the first time, recurse over its memory
//    as well.
//
// EXTENSION: if we have lots of words, could be faster with shadow memory 
// or a lookup table.  however, given our small sizes, this stupid 
// search may well be faster :)
//
// If you switch: measure speedup!
//
static void mark(const char *where, uint32_t *p, uint32_t *e) {
    assert(p<e);
    assert(aligned(p,4));
    assert(aligned(e,4));

    // sweep through each integer in [p,e] and mark all allocated
    // blocks the integer could point to (start, or internal)
    for(; p < e; p++) {
        hdr_t *h = is_ptr(*p);
        if(!h)
            continue;

        // Check if pointer points to start of block
        void *data_start = ck_data_start(h);
        if((void*)*p == data_start) {
            h->refs_start++;
        } else {
            h->refs_middle++;
        }

        // If block wasn't marked before, mark it and scan its contents
        if(!h->mark) {
            h->mark = 1;
            // Recursively scan the block's data region
            mark(where, (uint32_t*)data_start, (uint32_t*)ck_data_end(h));
        }
    }
}

// do a sweep, warning about any leaks.
static unsigned sweep_leak(int warn_no_start_ref_p) {
    unsigned nblocks = 0, errors = 0, maybe_errors = 0;
    output("---------------------------------------------------------\n");
    output("checking for leaks:\n");

    // sweep through all the allocated blocks.  
    for(hdr_t *h = ck_first_alloc(); h; h = ck_next_hdr(h), nblocks++) {
        if(h->state != ALLOCED)
            continue;

        // If block wasn't marked, it's definitely leaked
        if(!h->mark) {
            void *ptr = ck_data_start(h);
            ck_error(h, "GC:DEFINITE LEAK of block=%u [addr=%p]\n",
                    h->block_id, ptr);
            errors++;
        }
        // If block only has middle refs and warning is enabled, it's a maybe leak
        else if(warn_no_start_ref_p && h->refs_start == 0 && h->refs_middle > 0) {
            void *ptr = ck_data_start(h);
            ck_error(h, "GC:MAYBE LEAK of block %u [addr=%p] (no pointer to the start)\n", 
                    h->block_id, ptr);
            maybe_errors++;
        }
    }

    trace("\tGC:Checked %d blocks.\n", nblocks);
    if(!errors && !maybe_errors)
        trace("\t\tGC:SUCCESS: No leaks found!\n");
    else
        trace("\t\tGC:ERRORS: %d errors, %d maybe_errors\n", 
                errors, maybe_errors);
    output("----------------------------------------------------------\n");
    return errors + maybe_errors;
}


// a very slow leak checker.
static void mark_all(uint32_t *sp) {
    // Initialize mark and ref counts for all blocks
    for(hdr_t *h = ck_first_alloc(); h; h = ck_next_hdr(h)) {
        h->mark = h->refs_start = h->refs_middle = 0;
    }

    // the start of the stack (see libpi/staff-start.S)
    uint32_t *stack_top = (void*)STACK_ADDR;
    if(ck_verbose_p)
        debug("stack has %d words\n", stack_top - sp);

    // Scan stack
    mark("stack", sp, stack_top);

    // Scan bss segment
    mark("bss", (uint32_t*)__bss_start__, (uint32_t*)__bss_end__);

    // Scan data segment
    mark("data", (uint32_t*)__data_start__, (uint32_t*)__data_end__);
}


// similar to sweep_leak: go through and <ckfree> any ALLOCED
// block that has no references all all (nothing to start, 
// nothing to middle).
static unsigned sweep_free(void) {
    unsigned nblocks = 0, nfreed = 0, nbytes_freed = 0;
    output("---------------------------------------------------------\n");
    output("compacting:\n");

    // sweep through allocated list: free any block that has no pointers
    hdr_t *h = ck_first_alloc();
    while(h) {
        nblocks++;
        hdr_t *next = ck_next_hdr(h);  // Get next before potentially freeing h
        
        if(h->state == ALLOCED && !h->mark) {
            void *ptr = ck_data_start(h);
            trace("GC:FREEing block id=%u [addr=%p]\n", h->block_id, ptr);
            nfreed++;
            nbytes_freed += h->nbytes_alloc;
            ckfree(ptr);
        }
        
        h = next;
    }

    trace("\tGC:Checked %d blocks, freed %d, %d bytes\n", nblocks, nfreed, nbytes_freed);
    return nbytes_freed;
}

unsigned ck_gc_fn(uint32_t *sp) {
    mark_all(sp);
    unsigned nbytes = sweep_free();

    // perhaps coalesce these and give back to heap.  will have to modify last.

    return nbytes;
}

