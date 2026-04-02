/*
 * bb_box.h — Dynamic Byrd Box Runtime Types (M-DYN-2)
 *
 * THE CANONICAL FORM — one C function per box, three-column layout:
 *
 *     LABEL:          ACTION                          GOTO
 *     ─────────────────────────────────────────────────────
 *     BIRD_α:         if (Σ[Δ+0] != 'B')             goto BIRD_ω;
 *                     BIRD = str(Σ+Δ, 4); Δ += 4;    goto BIRD_γ;
 *     BIRD_β:         Δ -= 4;                         goto BIRD_ω;
 *     BIRD_γ:         return BIRD;
 *     BIRD_ω:         return empty;
 *
 * Reference implementations: .github/test_sno_*.c, .github/test_icon.c
 *
 * Every box is:
 *   str_t BoxName(boxname_t **ζζ, int entry);
 *
 * entry == α (0): fresh entry (allocate state ζ, go to α port)
 * entry == β (1): re-entry for backtracking (go to β port)
 * return is_empty(): ω fired (failure)
 * return non-empty: γ fired (success, value = matched substring)
 *
 * Global match state (shared, like SNOBOL4's implicit subject):
 *   Σ  — subject string pointer
 *   Δ  — cursor (current match position)
 *   Ω  — subject length
 */

#ifndef BB_BOX_H
#define BB_BOX_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── str_t — the universal value type ──────────────────────────────────── */
/*
 * str_t represents a substring of the subject (or the empty/fail sentinel).
 * σ = pointer into subject string, δ = length.
 * empty (σ == NULL) signals failure (ω port fired).
 * This matches test_sno_*.c exactly.
 */
typedef struct { const char *σ; int δ; } spec_t;

/* The failure sentinel */
static const spec_t spec_empty = { (const char *)0, 0 };

/* Construct a str_t from pointer and length */
static inline spec_t spec(const char *σ, int δ) { return (spec_t){ σ, δ }; }

/* Concatenate two substrings (they must be contiguous in the subject) */
static inline spec_t spec_cat(spec_t x, spec_t y)   { return (spec_t){ x.σ, x.δ + y.δ }; }

/* Test for failure sentinel */
static inline bool spec_is_empty(spec_t x)           { return x.σ == (const char *)0; }

/* ── entry ports ────────────────────────────────────────────────────────── */
static const int α = 0;   /* fresh entry */
static const int β = 1;   /* backtrack re-entry */
#define BB_ALPHA_DEFINED 1

/* ── global match state ─────────────────────────────────────────────────── */
/*
 * These are set by the statement executor (Phase 1: build subject) and
 * shared across all boxes during a single match.  Cursor Δ is mutated
 * by each box as it matches forward; restored on backtrack.
 */
/* Σ/Δ/Ω are defined (non-static) in stmt_exec.c.
 * All bb_*.c files declare them extern here to resolve at link time. */
extern const char *Σ;   /* subject string */
extern int         Δ;   /* cursor */
extern int         Ω;   /* subject length */

/* ── state allocator (from test_sno_3.c §enter) ─────────────────────────── */
/*
 * Each box that needs per-invocation state (cursor saves, loop vars)
 * gets a typed struct ζ allocated on first α entry and reused on β.
 * This matches the enter() pattern from test_sno_3.c exactly.
 */
static inline void *bb_enter(void **ζζ, size_t size) {
    void *ζ = *ζζ;
    if (size) {
        if (ζ) memset(ζ, 0, size);
        else   ζ = *ζζ = calloc(1, size);
    }
    return ζ;
}
#define BB_ENTER(ref, T)  ((T *)bb_enter((void **)(ref), sizeof(T)))

/* ── box function signature ──────────────────────────────────────────────── */
/*
 * Every box is called as:
 *   spec_t result = BoxName(&ζ, α);   // fresh
 *   spec_t result = BoxName(&ζ, β);   // backtrack
 *
 * Caller checks is_empty(result) to know if γ or ω fired.
 * The λ dispatch idiom (from test_sno_3.c):
 *
 *   BoxName_α:  BoxName = Box(&ζ->Box_ζ, α);     goto BoxName_λ;
 *   BoxName_β:  BoxName = Box(&ζ->Box_ζ, β);     goto BoxName_λ;
 *   BoxName_λ:  if (is_empty(BoxName))            goto BoxName_ω;
 *               else                              goto BoxName_γ;
 */
typedef spec_t (*bb_box_fn)(void *zeta, int entry);

/* ── box state typedefs — ONE definition, used by bb_*.c, stmt_exec.c, bb_*.s ─
 * Each box's private state struct lives here.  bb_build() in stmt_exec.c
 * allocates these; the box functions cast zeta to the appropriate type.
 * Named with _t suffix; the .s files use field offsets matching these layouts. */
typedef struct { const char *lit; int len; }          lit_t;
typedef struct { int n; }                              len_t;
typedef struct { const char *chars; int δ; }          span_t;
typedef struct { const char *chars; }                  any_t;
typedef struct { const char *chars; }                  notany_t;
typedef struct { const char *chars; int δ; }          brk_t;
typedef struct { const char *chars; int δ; }          brkx_t;
typedef struct { int count; int start; }              arb_t;
typedef struct { int dummy; }                          rem_t;
typedef struct { int dummy; }                          succeed_t;
typedef struct { int dummy; }                          fail_t;
typedef struct { int done; }                           eps_t;
typedef struct { int n; }                              pos_t;
typedef struct { int n; }                              rpos_t;
typedef struct { int n; int advance; }                tab_t;
typedef struct { int n; int advance; }                rtab_t;
typedef struct { int fired; }                          fence_t;
typedef struct { int dummy; }                          abort_t;
typedef struct { int done; const char *varname; }     atp_t;
/* deferred_var_t needs bb_node_t (defined in stmt_exec.c) — declared there */

#endif /* BB_BOX_H */
