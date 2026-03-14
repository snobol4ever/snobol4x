/*======================================================================================
 * engine.h — SNOBOL4-tiny Byrd Box mtch engine
 *
 * Pure C.  No Python.  No external dependencies beyond the C standard library.
 *
 * This engine implements the Psi/Omega Byrd Box protocol:
 *   - PROCEED / SUCCEED / CONCEDE / RECEDE — the four signals
 *   - Psi  — continuation stack (where to return on success)
 *   - Omega — backtrack stack (each entry owns a deep-copied Psi snapshot)
 *   - Dispatch: (node_type << 2 | signal) — one switch, no indirect calls
 *
 * Usage:
 *   1. Build a Pattern tree with pattern_alloc() + set fields.
 *   2. Call engine_match(root, subject, subject_len).
 *   3. Call pattern_free_all() to release all nodes.
 *
 * Derived from SNOBOL4cython/snobol4c_module.c (Lon Cherryholmes, 2026).
 * Extracted: Python→C converter and CPython module removed entirely.
 *======================================================================================*/
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/*======================================================================================
 * PATTERN node types
 *======================================================================================*/
enum {
    T_ABORT   =  0,
    T_ANY     =  1,
    T_ARB     =  2,
    T_ARBNO   =  3,
    T_BAL     =  4,
    T_BREAK   =  5,
    T_BREAKX  =  6,
    T_FAIL    =  7,
    T_FENCE   =  8,
    T_LEN     =  9,
    T_MARB    = 10,
    T_MARBNO  = 11,
    T_NOTANY  = 12,
    T_POS     = 13,
    T_REM     = 14,
    T_RPOS    = 15,
    T_RTAB    = 16,
    T_SPAN    = 17,
    T_SUCCEED = 18,
    T_TAB     = 19,
    T_PI      = 29,     /* Π  alternation           */
    T_SIGMA   = 30,     /* Σ  sequence              */
    T_RHO     = 39,     /* ρ  conjunction           */
    T_pi      = 38,     /* π  optional              */
    T_EPSILON = 34,     /* ε  null mtch            */
    T_LITERAL = 40,     /* σ  literal string        */
    T_VARREF  = 41,     /* deferred variable pattern ref */
    T_ALPHA   = 32,     /* α  beginning of line     */
    T_OMEGA   = 42,     /* ω  end of line           */
    T_CAPTURE = 43,     /* capture: single child; on success fires cap_fn(cap_slot, start, end) */
    T_FUNC    = 44,     /* zero-width: call func(userdata) at mtch time; succeed if returns non-NULL */
};

/* The four Byrd Box signals */
#define PROCEED 0
#define SUCCEED 1
#define CONCEDE 2
#define RECEDE  3

/*======================================================================================
 * Pattern node — malloc'd, tracked via PatternList
 *======================================================================================*/
#define MAX_CHILDREN 30

typedef struct Pattern {
    int              type;
    int              n;           /* child count (Σ,Π) or integer arg (POS,LEN,…) */
    const char      *s;           /* string arg (σ literal)                         */
    int              s_len;       /* length of s                                    */
    const char      *chars;       /* char set (ANY, SPAN, BREAK, NOTANY)            */
    void           *(*func)(void *userdata);  /* T_FUNC callback                    */
    void            *func_data;   /* T_FUNC userdata                                */
    struct Pattern  *children[MAX_CHILDREN];
} Pattern;

/*--- Pattern allocation and cleanup ---*/
typedef struct {
    Pattern **list;
    int       count;
} PatternList;

static inline Pattern *pattern_alloc(PatternList *pl) {
    pl->list = realloc(pl->list, (pl->count + 1) * sizeof(Pattern *));
    Pattern *p = malloc(sizeof(Pattern));
    memset(p, 0, sizeof(Pattern));
    pl->list[pl->count++] = p;
    return p;
}

static inline void pattern_free_all(PatternList *pl) {
    for (int i = 0; i < pl->count; i++) free(pl->list[i]);
    free(pl->list);
    pl->list  = NULL;
    pl->count = 0;
}

/*======================================================================================
 * MatchResult — returned by engine_match()
 *======================================================================================*/
typedef struct {
    int matched;   /* 1 = success, 0 = failure */
    int start;     /* always 0 (anchored mtch) */
    int end;       /* cursor position at mtch end */
} MatchResult;

/* Capture callback: fired when a T_CAPTURE node succeeds.
 * cap_slot: opaque int (indx into the caller's capture table).
 * start, end: byte offsets within the subject passed to engine_match_ex. */
typedef void (*CaptureFn)(int cap_slot, int start, int end, void *userdata);

/* Variable-resolve callback: fired when a T_VARREF node is entered.
 * name: the variable name to look up.
 * Returns a materialised Pattern* (never NULL — return epsilon on failure). */
typedef Pattern *(*VarResolveFn)(const char *name, void *userdata);

typedef struct {
    CaptureFn     cap_fn;    /* NULL = no captures */
    void         *cap_data;  /* passed through to cap_fn unmodified */
    VarResolveFn  var_fn;    /* NULL = T_VARREF hits default (CONCEDE) */
    void         *var_data;  /* passed through to var_fn unmodified */
    int           scan_start; /* absolute start offset in original subject (for T_POS/T_TAB) */
} EngineOpts;

/*--- engine_match: run root against subject[0..subject_len) ---*/
MatchResult engine_match(Pattern *root, const char *subject, int subject_len);

/*--- engine_match_ex: same, with capture callback support ---*/
MatchResult engine_match_ex(Pattern *root, const char *subject, int subject_len,
                             const EngineOpts *opts);
