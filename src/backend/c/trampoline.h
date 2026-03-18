#ifndef TRAMPOLINE_H
#define TRAMPOLINE_H
/*
 * trampoline.h — block_fn_t + trampoline execution model
 *
 * Every SNOBOL4 statement compiles to a C function:
 *
 *   static block_fn_t stmt_N(block_locals_t *z);
 *
 * It returns the address of the next block to execute:
 *   - S-label block on success
 *   - F-label block on failure
 *   - NULL to terminate the program
 *
 * Statements are grouped into block functions by label reachability:
 *   static block_fn_t block_L(void);
 *
 * The trampoline IS the engine:
 *   block_fn_t pc = block_START;
 *   while (pc) pc = pc();
 *
 * Architecture decided: Lon Cherryholmes + Claude Sonnet 4.6, 2026-03-14
 * Sprint: trampoline (1/9 toward M-BEAUTY-FULL)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/* -----------------------------------------------------------------------
 * Core type: a block function returns the next block to execute.
 * NULL = program end (normal termination).
 * ----------------------------------------------------------------------- */
typedef void *(*block_fn_t)(void);

/* -----------------------------------------------------------------------
 * &STCOUNT / &STLIMIT — declared in snobol4.h; referenced here.
 * trampoline_stno(lineno) called at TOP_fn of every emitted stmt.
 * kw_stlimit < 0 = unlimited (default).
 * ----------------------------------------------------------------------- */
#ifndef SNOBOL4_H   /* avoid redeclaration if snobol4.h already included */
extern int64_t kw_stlimit;
extern int64_t kw_stcount;
#endif
static inline void trampoline_stno(int n) {
    extern int64_t kw_stlimit;
    extern int64_t kw_stcount;
    ++kw_stcount;
    if (getenv("SNO_TRACE")) fprintf(stderr, "STNO %lld line=%d\n", (long long)kw_stcount, n);
    if (kw_stlimit >= 0 && kw_stcount > kw_stlimit) {
        fprintf(stderr,
            "\n** &STLIMIT exceeded at statement %d"
            " (&STCOUNT=%lld &STLIMIT=%lld)\n",
            n, (long long)kw_stcount, (long long)kw_stlimit);
        exit(1);
    }
}

static inline void trampoline_run(block_fn_t start) {
    block_fn_t pc = start;
    while (pc) {
        pc = (block_fn_t)pc();
    }
}

/* -----------------------------------------------------------------------
 * Convenience macro for declaring a block function.
 * ----------------------------------------------------------------------- */
#define BLOCK_FN(name)   static void *name(void)
#define STMT_FN(name)    static void *name(void)

/* -----------------------------------------------------------------------
 * ABORT handler support (cold path only).
 * Normal S/F routing uses pure gotos — zero overhead.
 * ABORT/FENCE bare/runtime errors use longjmp to nearest handler.
 * ----------------------------------------------------------------------- */
typedef struct abort_frame {
    jmp_buf             jmp;
    struct abort_frame *prev;
} abort_frame_t;

extern abort_frame_t *_sno_abort_top;

#define ABORT_GUARD_PUSH(frame)  \
    do { (frame).prev = _sno_abort_top; _sno_abort_top = &(frame); } while(0)

#define ABORT_GUARD_POP()        \
    do { _sno_abort_top = _sno_abort_top->prev; } while(0)

#define ABORT_GUARD_THROW()      \
    do { if (_sno_abort_top) longjmp(_sno_abort_top->jmp, 1); } while(0)

#endif /* TRAMPOLINE_H */

/* sno_computed_goto — resolve a runtime label string to a block function pointer.
 * Called by $'literal' and $(expr) goto targets.
 * The block_label_table is emitted by sno2c at the end of each program. */
typedef struct { const char *name; void *(*fn)(void); } _BlockEntry_t;
extern _BlockEntry_t _block_label_table[];
extern int           _block_label_count;

static inline void *sno_computed_goto(const char *lbl) {
    if (!lbl) return NULL;
    for (int i = 0; i < _block_label_count; i++)
        if (strcasecmp(_block_label_table[i].name, lbl) == 0)
            return (void*)_block_label_table[i].fn;
    return NULL;  /* label not found — fall through */
}
