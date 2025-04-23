#include "kr-malloc.h"

#define roundup(x,n) (((x)+((n)-1))&(~((n)-1)))

union align {
        double d;
        void *p;
        void (*fp)(void);
};

typedef union header { /* block header */
	struct {
    		union header *ptr; /* next block if on free list */
    		unsigned size; /* size of this block */
  	} s;
  	union align x; /* force alignment of blocks */
} Header;

static Header base;
static Header *freep = NULL;

#define NALLOC 1024

void *sbrk(long increment) {
    static int init_p;

    assert(increment > 0);
    if(!init_p) {
        kmalloc_init_set_start((void*)0x100000, 0x100000);
        init_p = 1;
    }
    return kmalloc(increment);
}

static Header *morecore(unsigned nu)
   {
       char *cp;
	   void *sbrk(long);
       Header *up;
       if (nu < NALLOC)
           nu = NALLOC;
       cp = sbrk(nu * sizeof(Header));
       if (cp == (char *) -1)   /* no space at all */
           return NULL;
       up = (Header *) cp;
       up->s.size = nu;
       kr_free((void *)(up+1));
       return freep;
   }


void *kr_malloc(unsigned nbytes) {
	Header *p, *prevp;
	Header *morecore(unsigned);
	unsigned nunits;

	nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) + 1;
       if ((prevp = freep) == NULL) {   /* no free list yet */ 
           base.s.ptr = freep = prevp = &base;
		   Header* prevptr = &base;
           base.s.size = 0;
       }
       for (p = prevp->s.ptr; ; prevp = p, p = p->s.ptr) {
           if (p->s.size >= nunits) {  /* big enough */
               if (p->s.size == nunits)  /* exactly */
                   prevp->s.ptr = p->s.ptr;
               else {              /* allocate tail end */
                   p->s.size -= nunits;
                   p += p->s.size;
                   p->s.size = nunits;
               }
               freep = prevp;
               return (void *)(p+1);
           }
           if (p == freep)  /* wrapped around free list */
               if ((p = morecore(nunits)) == NULL)
                   return NULL;    /* none left */
       }
}

void kr_free(void *ap)
   {
       Header *bp, *p;
       bp = (Header *)ap - 1;    /* point to  block header */
       for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
            if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
                break;  /* freed block at start or end of arena */
       if (bp + bp->s.size == p->s.ptr) {    /* join to upper nbr */
           bp->s.size += p->s.ptr->s.size;
           bp->s.ptr = p->s.ptr->s.ptr;
       } else
           bp->s.ptr = p->s.ptr;
       if (p + p->s.size == bp) {            /* join to lower nbr */
           p->s.size += bp->s.size;
           p->s.ptr = bp->s.ptr;
       } else
           p->s.ptr = bp;
       freep = p;
   }
