/* runtime.h — snobol4x static runtime
 *
 * All MATCH_fn state is statically allocated. Zero allocation during matching.
 * CODE/EVAL dynamic patterns use heap (two-tier: static fast path + heap).
 */

#ifndef SNOBOL4_TINY_RUNTIME_H
#define SNOBOL4_TINY_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---------- string type ------------------------------------------- */

typedef struct {
    const char *ptr;
    int64_t     len;
} str_t;

#define STR_EMPTY  ((str_t){ "", 0 })
#define STR_LIT(s) ((str_t){ (s), (int64_t)(sizeof(s)-1) })

/* ---------- MATCH_fn state ------------------------------------------- */

typedef struct {
    const char *subject;
    int64_t     subject_len;
    int64_t     cursor;
} match_state_t;

/* ---------- output ------------------------------------------------- */

void output(str_t s);
void output_cstr(const char *s);

/* ---------- entry / exit frame ------------------------------------ */
/* Used by recursive patterns (test_s4_2.c calling convention)     */

void *ENTER_fn(void **frame_ptr, size_t frame_size);
void  EXIT_fn(void **frame_ptr);
void  arena_reset(void);   /* call between matches — resets arena to empty */

/* ---------- value stack ------------------------------------------- */
/* Used by evaluate() — patterns PUSH_fn computed integer values.       */
/* Max depth 256 covers any expression the worm generates.           */

#define VSTACK_SIZE 256

void    vpush(int64_t v);
int64_t vpop(void);
int64_t vpeek(void);
void    vreset(void);
int     vdepth(void);

#endif /* SNOBOL4_TINY_RUNTIME_H */
