/*
 * blk_alloc.c — per-invocation DATA block allocator
 *
 * Each user-defined function call allocates a fresh DATA block via mmap so
 * that simultaneous invocations (recursive calls) each have private locals.
 * CODE is shared (read-execute); DATA is per-invocation (read-write).
 *
 * Usage pattern at every call site:
 *   void *data = blk_alloc(box_FOO_data_size);
 *   memcpy(data, box_FOO_data_template, box_FOO_data_size);
 *   // invoke with r12 = data
 *   // on return:
 *   blk_free(data, box_FOO_data_size);
 */
#include <sys/mman.h>
#include <stddef.h>
#include "blk_alloc.h"

/*
 * blk_alloc — allocate sz bytes of anonymous RW memory.
 * Returns MAP_FAILED on error (caller should check).
 */
void *blk_alloc(size_t sz) {
    void *p = mmap(NULL, sz,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

/*
 * blk_free — release memory previously obtained from blk_alloc.
 * sz must match the value passed to blk_alloc.
 */
void blk_free(void *p, size_t sz) {
    if (p) munmap(p, sz);
}

/* NOTE: blk_mprotect_rx/rw are provided for future CODE-block use.
 * Current DATA blocks stay RW throughout their lifetime.
 * On x86-64 the i-cache and d-cache are coherent so no explicit
 * flush is needed after writing code to a DATA block before
 * calling blk_mprotect_rx().
 */

/*
 * blk_mprotect_rx — make a region read+execute (no write).
 */
int blk_mprotect_rx(void *p, size_t sz) {
    return mprotect(p, sz, PROT_READ | PROT_EXEC);
}

/*
 * blk_mprotect_rw — make a region read+write (no execute).
 */
int blk_mprotect_rw(void *p, size_t sz) {
    return mprotect(p, sz, PROT_READ | PROT_WRITE);
}
