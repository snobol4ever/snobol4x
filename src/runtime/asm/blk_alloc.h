/*
 * blk_alloc.h — per-invocation DATA block allocation and memory protection
 *
 * Each user-defined function invocation gets its own DATA block:
 *   blk_alloc(size)          — allocate sz bytes of anonymous RW memory
 *   blk_free(ptr, size)      — release memory
 *   blk_mprotect_rx(p, sz)   — RW → RX (ready to execute)
 *   blk_mprotect_rw(p, sz)   — RX → RW (for patching)
 */
#ifndef BLK_ALLOC_H
#define BLK_ALLOC_H
#include <stddef.h>
void *blk_alloc(size_t sz);
void  blk_free(void *p, size_t sz);
int   blk_mprotect_rx(void *p, size_t sz);
int   blk_mprotect_rw(void *p, size_t sz);
#endif /* BLK_ALLOC_H */
