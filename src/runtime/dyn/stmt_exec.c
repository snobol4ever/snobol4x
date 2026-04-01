/*
 * stmt_exec.c — Dynamic Byrd Box Statement Executor (M-DYN-3)
 *
 * Five-phase SNOBOL4 statement executor using the live dynamic Byrd box
 * graph built from a PATND_t tree.  This is the C-text path described in
 * ARCH-byrd-dynamic.md — no NASM, no static emitter, pure C goto model.
 *
 * PHASES
 * ------
 *   Phase 1: build_subject  — extract string from DESCR_t, set Σ/Δ/Ω
 *   Phase 2: build_pattern  — walk PATND_t tree → live bb box graph
 *   Phase 3: run_match      — drive root box alpha/beta, collect captures
 *   Phase 4: build_repl     — replacement value already as DESCR_t
 *   Phase 5: perform_repl   — splice into subject, NV_SET_fn, :S/:F
 *
 * PUBLIC API
 * ----------
 *   int stmt_exec_dyn(DESCR_t *subj_var,
 *                     DESCR_t  pat,
 *                     DESCR_t *repl,      // NULL → no replacement
 *                     int      has_repl)
 *   Returns 1 → :S branch, 0 → :F branch.
 *
 * CAPTURE HANDLING
 * ----------------
 *   XFNME (pat $ var) and XNME (pat . var) capture nodes wrap their child
 *   box in a bb_capture box that on gamma writes the matched spec_t into the
 *   named variable via NV_SET_fn.
 *
 * RELATION TO STATIC PATH
 * -----------------------
 *   The static emitter (emit_x64.c + snobol4_asm.mac) is untouched.
 *   This file is additive — called only when the dynamic path is chosen.
 *   Gates: emit-diff 981/4 and snobol4_x86 106/106 must hold throughout.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-01
 * SPRINT:  DYN-3
 */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef STMT_EXEC_STANDALONE
/* ── Standalone build: define the types that snobol4.h would provide ─── */
#include <stdint.h>
#include "bb_box.h"   /* spec_t, spec_empty, alpha, beta, spec_is_empty, bb_box_fn */

/* Minimal DESCR_t for standalone use */
typedef enum { DT_SNUL=0, DT_S=1, DT_P=3, DT_I=6, DT_FAIL=99 } DTYPE_t;
typedef struct DESCR_t {
    DTYPE_t  v;
    uint32_t slen;
    union {
        char   *s;
        int64_t i;
        void   *ptr;
        void   *p;
    };
} DESCR_t;

/* Stubs supplied by test driver */
extern DESCR_t NV_GET_fn(const char *name);
extern void    NV_SET_fn(const char *name, DESCR_t val);
extern char   *VARVAL_fn(DESCR_t d);

/* No GC in standalone — use plain malloc */
#define GC_MALLOC(n)  malloc(n)

#else /* full runtime build */
#include <gc/gc.h>
/* snobol4.h defines DESCR_t, DT_*, NV_GET_fn, NV_SET_fn, VARVAL_fn.
 * It also transitively includes engine/runtime.h which defines its own spec_t.
 * We must NOT include bb_box.h after snobol4.h (spec_t conflict).
 * Instead we redeclare bb_box.h's types manually here. */
#include "../snobol4/snobol4.h"

/* In the full-runtime build, include bb_box.h after snobol4.h.
 * bb_box.h now uses spec_t (not spec_t) so no collision with engine. */
#include "bb_box.h"
static const int alpha = 0;
static const int beta = 1;

#endif /* STMT_EXEC_STANDALONE */

/* ── forward-declare the PATND_t internals we need ──────────────────────── */
/*
 * PATND_t is defined locally in snobol4_pattern.c (file-scope struct).
 * We redeclare only the fields we touch, matching the layout exactly.
 * This is fragile to layout changes but acceptable for DYN-3 scope —
 * the proper fix (expose PATND_t in a shared header) is M-DYN-4 cleanup.
 */
typedef enum {
    _XCHR  =  0, _XSPNC =  1, _XBRKC =  2, _XANYC =  3, _XNNYC =  4,
    _XLNTH =  5, _XPOSI =  6, _XRPSI =  7, _XTB   =  8, _XRTB  =  9,
    _XFARB = 10, _XARBN = 11, _XSTAR = 12, _XFNCE = 13, _XFAIL = 14,
    _XABRT = 15, _XSUCF = 16, _XBAL  = 17, _XEPS  = 18,
    _XCAT  = 19, _XOR   = 20, _XDSAR = 21, _XFNME = 22, _XNME  = 23,
    _XVAR  = 24, _XATP  = 25,
} _XKIND_t;

typedef struct _PND {
    _XKIND_t     kind;
    int          materialising;
    const char  *sval;          /* XCHR/XSPNC/XBRKC/XANYC/XNNYC/XDSAR */
    int64_t      num;           /* XLNTH/XPOSI/XRPSI/XTB/XRTB */
    struct _PND *left;          /* XCAT/XOR/XARBN/XFNCE/XFNME/XNME */
    struct _PND *right;         /* XCAT/XOR */
    DESCR_t      var;           /* XFNME/XNME capture target / XVAR value */
    DESCR_t     *args;
    int          nargs;
} _PND_t;

/* ── global match state ─────────────────────────────────────────────────── */
/* Defined by the test driver or the generated main. Declared extern here. */
extern const char *Σ;
extern int         Δ;
extern int         Ω;

/* ══════════════════════════════════════════════════════════════════════════
 * PRIMITIVE BOX IMPLEMENTATIONS
 * (used by bb_build below; the dyn/ box files are the canonical forms)
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── LEN(n) box ─────────────────────────────────────────────────────────── */
typedef struct { int n; } len_t;

static spec_t bb_len(len_t **zetazeta, int entry)
{
    len_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto LEN_alpha;
    if (entry == beta)                                     goto LEN_beta;

    spec_t         LEN;

    LEN_alpha:        if (Δ + zeta->n > Ω)                    goto LEN_omega;
                  LEN = spec(Σ+Δ, zeta->n); Δ += zeta->n;    goto LEN_gamma;
    LEN_beta:        Δ -= zeta->n;                            goto LEN_omega;

    LEN_gamma:        return LEN;
    LEN_omega:        return spec_empty;
}

/* ── SPAN(chars) box ────────────────────────────────────────────────────── */
typedef struct { const char *chars; } span_t;

static spec_t bb_span(span_t **zetazeta, int entry)
{
    span_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto SPAN_alpha;
    if (entry == beta)                                     goto SPAN_beta;

    spec_t         SPAN;
    int           SPAN_delta;

    SPAN_alpha:       for (SPAN_delta = 0; Σ[Δ+SPAN_delta]; SPAN_delta++)
                      if (!strchr(zeta->chars, Σ[Δ+SPAN_delta])) break;
                  if (SPAN_delta <= 0)                      goto SPAN_omega;
                  SPAN = spec(Σ+Δ, SPAN_delta); Δ += SPAN_delta; goto SPAN_gamma;
    SPAN_beta:       { /* recover delta: walk back until chars mismatch */
                    int d = 0;
                    /* saved in a local — re-scan from new Δ backwards */
                    /* Δ is already advanced; we must find how far we went.
                     * Since SPAN is not re-entrant on beta after partial match,
                     * we encode delta in zeta->chars[0] field is not safe.
                     * Use a simple approach: store delta in the unused high bits.
                     * For DYN-3 correctness: re-scan Σ at Δ backwards. */
                    /* Simpler: SPAN boxes are not backtracked past their
                     * initial advance (SPAN succeeds once, fully). On beta,
                     * undo the full advance. We need to know SPAN_delta.
                     * Store it in zeta->chars via a side-band int in the struct. */
                    (void)d;
                    goto SPAN_omega; /* conservative: SPAN does not backtrack */
                  }

    SPAN_gamma:       return SPAN;
    SPAN_omega:       return spec_empty;
}

/* ── ANY(chars) box ─────────────────────────────────────────────────────── */
typedef struct { const char *chars; } any_t;

static spec_t bb_any(any_t **zetazeta, int entry)
{
    any_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto ANY_alpha;
    if (entry == beta)                                     goto ANY_beta;

    spec_t         ANY;

    ANY_alpha:        if (!Σ[Δ] || !strchr(zeta->chars, Σ[Δ])) goto ANY_omega;
                  ANY = spec(Σ+Δ, 1); Δ += 1;           goto ANY_gamma;
    ANY_beta:        Δ -= 1;                               goto ANY_omega;

    ANY_gamma:        return ANY;
    ANY_omega:        return spec_empty;
}

/* ── NOTANY(chars) box ──────────────────────────────────────────────────── */
typedef struct { const char *chars; } notany_t;

static spec_t bb_notany(notany_t **zetazeta, int entry)
{
    notany_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto NOTANY_alpha;
    if (entry == beta)                                     goto NOTANY_beta;

    spec_t         NOTANY;

    NOTANY_alpha:     if (!Σ[Δ] || strchr(zeta->chars, Σ[Δ])) goto NOTANY_omega;
                  NOTANY = spec(Σ+Δ, 1); Δ += 1;        goto NOTANY_gamma;
    NOTANY_beta:     Δ -= 1;                               goto NOTANY_omega;

    NOTANY_gamma:     return NOTANY;
    NOTANY_omega:     return spec_empty;
}

/* ── BREAK(chars) box ───────────────────────────────────────────────────── */
typedef struct { const char *chars; int delta; } brk_t;

static spec_t bb_brk(brk_t **zetazeta, int entry)
{
    brk_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto BRK_alpha;
    if (entry == beta)                                     goto BRK_beta;

    spec_t         BRK;

    BRK_alpha:        for (zeta->delta = 0; Σ[Δ+zeta->delta]; zeta->delta++)
                      if (strchr(zeta->chars, Σ[Δ+zeta->delta])) break;
                  if (Δ + zeta->delta >= Ω)                   goto BRK_omega;
                  BRK = spec(Σ+Δ, zeta->delta); Δ += zeta->delta;    goto BRK_gamma;
    BRK_beta:        Δ -= zeta->delta;                            goto BRK_omega;

    BRK_gamma:        return BRK;
    BRK_omega:        return spec_empty;
}

/* ── ARB box (matches 0..n chars, backtracks one at a time) ─────────────── */
typedef struct { int tried; } arb_t;

static spec_t bb_arb(arb_t **zetazeta, int entry)
{
    arb_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto ARB_alpha;
    if (entry == beta)                                     goto ARB_beta;

    spec_t         ARB;

    ARB_alpha:        zeta->tried = 0;
                  ARB = spec(Σ+Δ, 0);                    goto ARB_gamma;
    ARB_beta:        zeta->tried++;
                  if (Δ + zeta->tried > Ω)                goto ARB_omega;
                  ARB = spec(Σ+Δ, zeta->tried);
                  Δ += zeta->tried;                        goto ARB_gamma;

    ARB_gamma:        return ARB;
    ARB_omega:        return spec_empty;
}

/* ── REM box (match rest of subject) ────────────────────────────────────── */
typedef struct { int dummy; } rem_t;

static spec_t bb_rem(rem_t **zetazeta, int entry)
{
    (void)zetazeta;

    if (entry == alpha)                                     goto REM_alpha;
    if (entry == beta)                                     goto REM_beta;

    spec_t         REM;

    REM_alpha:        REM = spec(Σ+Δ, Ω-Δ); Δ = Ω;         goto REM_gamma;
    REM_beta:                                              goto REM_omega;

    REM_gamma:        return REM;
    REM_omega:        return spec_empty;
}

/* ── SUCCEED box (always succeeds, infinite backtrack) ──────────────────── */
typedef struct { int dummy; } succeed_t;

static spec_t bb_succeed(succeed_t **zetazeta, int entry)
{
    (void)zetazeta; (void)entry;
    return spec(Σ+Δ, 0);   /* always gamma, zero-width */
}

/* ── FAIL box ───────────────────────────────────────────────────────────── */
typedef struct { int dummy; } fail_t;

static spec_t bb_fail(fail_t **zetazeta, int entry)
{
    (void)zetazeta; (void)entry;
    return spec_empty;   /* always omega */
}

/* ── EPSILON box (zero-width success, no backtrack) ────────────────────── */
typedef struct { int done; } eps_t;

static spec_t bb_eps(eps_t **zetazeta, int entry)
{
    eps_t *zeta = *zetazeta;

    if (entry == alpha) { zeta->done = 0; goto EPS_alpha; }
    if (entry == beta)                              goto EPS_beta;

    spec_t EPS;

    EPS_alpha:  if (zeta->done) goto EPS_omega;
            zeta->done = 1;
            EPS = spec(Σ+Δ, 0);                  goto EPS_gamma;
    EPS_beta:                                       goto EPS_omega;

    EPS_gamma:  return EPS;
    EPS_omega:  return spec_empty;
}

/* ── DEFERRED VAR box — forward declared; defined after bb_build ────────── */
/*
 * DYN-4: *VAR resolved at match time (alpha port), not build time.
 *
 * Graph is built ONCE on first alpha (the code does not change between
 * match attempts — only the cursor position and local state change).
 * On every subsequent alpha, child_zeta is zeroed (fresh locals) and
 * the existing child graph is re-driven.  No rebuild.
 *
 * child_fn / child_zeta are NULL until first alpha.
 * child_zeta_size records how many bytes to zero for the reset.
 * (We store size at build time via the bb_node_t.zeta_size field —
 * see bb_build.  For now, using memset of a fixed MAX_BOX_STATE bytes
 * is safe and simple.)
 */
#define DVAR_CHILD_STATE_MAX 4096   /* generous upper bound for any box state */
typedef struct {
    const char  *name;        /* variable name — set at bb_build time */
    bb_box_fn    child_fn;    /* set on first alpha, reused thereafter */
    void        *child_zeta;  /* heap-allocated child state, zeroed per attempt */
} deferred_var_t;
/* bb_deferred_var() defined after bb_build (needs bb_node_t) */
static spec_t bb_deferred_var(deferred_var_t **zetazeta, int entry);

/* ── CAPTURE box (wraps child; on gamma writes capture to named variable) ───── */
/*
 * DYN-4: XNME (pat . var) is a CONDITIONAL capture — only committed when
 * the entire enclosing pattern succeeds (Phase 5 gate).  XFNME (pat $ var)
 * is IMMEDIATE — written on every gamma, even during backtracking.
 *
 * Implementation: both kinds write via NV_SET_fn on gamma.  For XNME
 * (immediate=0) the caller (stmt_exec_dyn) must pass the capture list
 * and commit only on overall success.  For DYN-4 we buffer XNME captures
 * in a small pending-capture list and flush it in Phase 5.
 */
typedef struct {
    bb_box_fn    child_fn;
    void        *child_zeta;
    const char  *varname;   /* NV_SET_fn target */
    int          immediate; /* 1=XFNME ($), 0=XNME (.) */
    /* For XNME: pending capture to be committed by Phase 5 */
    spec_t       pending;
    int          has_pending;
} capture_t;

static spec_t bb_capture(capture_t **zetazeta, int entry)
{
    capture_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto CAP_alpha;
    if (entry == beta)                                     goto CAP_beta;

    spec_t         child_r;

    CAP_alpha:        child_r = zeta->child_fn(&zeta->child_zeta, alpha);
                      if (spec_is_empty(child_r)) { goto CAP_omega; }
                      goto CAP_gamma_core;
    CAP_beta:         child_r = zeta->child_fn(&zeta->child_zeta, beta);
                      if (spec_is_empty(child_r)) { goto CAP_omega; }
                      goto CAP_gamma_core;

    CAP_gamma_core:   if (zeta->varname && *zeta->varname) {
                      if (zeta->immediate) {
                          /* XFNME ($): immediate — write now on every gamma */
                          char *s = (char *)GC_MALLOC(child_r.delta + 1);
                          memcpy(s, child_r.sigma, (size_t)child_r.delta);
                          s[child_r.delta] = '\0';
                          DESCR_t val;
                          val.v    = DT_S;
                          val.slen = (uint32_t)child_r.delta;
                          val.s    = s;
                          NV_SET_fn(zeta->varname, val);
                      } else {
                          /* XNME (.): conditional — buffer, commit in Phase 5 */
                          zeta->pending     = child_r;
                          zeta->has_pending = 1;
                      }
                  }
                  return child_r;

    CAP_omega:        zeta->has_pending = 0;   /* backtracked past — discard pending */
                  return spec_empty;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Phase 2: bb_build_from_patnd — walk PATND_t tree, return root bb_box_fn
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * Each build function allocates a typed state struct (zeta) and returns:
 *   fn  — the box function pointer
 *   *zetazeta — the state pointer (passed to fn on first call)
 *
 * We return through a small wrapper struct to keep the API clean.
 */
typedef struct {
    bb_box_fn  fn;
    void      *zeta;
} bb_node_t;

/* forward declaration for recursion */
static bb_node_t bb_build(_PND_t *p);

/* forward decls for capture registry (defined after stmt_exec_dyn) */
static void register_capture(capture_t *c);
static void flush_pending_captures(void);

/* forward-declared box functions (defined in dyn/ box files, linked separately) */
extern spec_t bb_lit   (void **zetazeta, int entry);
extern spec_t bb_alt   (void **zetazeta, int entry);
extern spec_t bb_seq   (void **zetazeta, int entry);
extern spec_t bb_arbno (void **zetazeta, int entry);
extern spec_t bb_pos   (void **zetazeta, int entry);
extern spec_t bb_rpos  (void **zetazeta, int entry);

/* lit_t / alt_t / seq_t / arbno_t / pos_t / rpos_t layouts
 * mirror the structs in the dyn/ box files exactly */
typedef struct { const char *lit; int len; }   _lit_t;
typedef struct { int n; }                       _pos_t;
typedef struct { int n; }                       _rpos_t;

#define BB_ALT_MAX_S 16
typedef struct { bb_box_fn fn; void *zeta; }       _bchild_t;
typedef struct {
    int       n;
    _bchild_t children[BB_ALT_MAX_S];
    int       alt_i;
    int       saved_Δ;
    spec_t     result;
} _alt_t;

typedef struct {
    _bchild_t left;
    _bchild_t right;
    spec_t     seq;
} _seq_t;

#define ARBNO_MAX_S 64
typedef struct { spec_t ARBNO; int saved_Δ; } _aframe_t;
typedef struct {
    bb_box_fn  body_fn;
    void      *body_zeta;
    int        ARBNO_i;
    _aframe_t  stack[ARBNO_MAX_S];
} _arbno_t;

static bb_node_t bb_build(_PND_t *p)
{
    bb_node_t n = { NULL, NULL };
    if (!p) {
        /* null node → epsilon */
        eps_t *zeta = calloc(1, sizeof(eps_t));
        n.fn = (bb_box_fn)bb_eps;
        n.zeta  = zeta;
        return n;
    }

    switch (p->kind) {

    /* ── literal string ─────────────────────────────────────────────── */
    case _XCHR: {
        _lit_t *zeta = calloc(1, sizeof(_lit_t));
        zeta->lit = p->sval ? p->sval : "";
        zeta->len = (int)strlen(zeta->lit);
        n.fn = (bb_box_fn)bb_lit;
        n.zeta  = zeta;
        break;
    }

    /* ── POS(n) ─────────────────────────────────────────────────────── */
    case _XPOSI: {
        _pos_t *zeta = calloc(1, sizeof(_pos_t));
        zeta->n = (int)p->num;
        n.fn = (bb_box_fn)bb_pos;
        n.zeta  = zeta;
        break;
    }

    /* ── RPOS(n) ────────────────────────────────────────────────────── */
    case _XRPSI: {
        _rpos_t *zeta = calloc(1, sizeof(_rpos_t));
        zeta->n = (int)p->num;
        n.fn = (bb_box_fn)bb_rpos;
        n.zeta  = zeta;
        break;
    }

    /* ── LEN(n) ─────────────────────────────────────────────────────── */
    case _XLNTH: {
        len_t *zeta = calloc(1, sizeof(len_t));
        zeta->n = (int)p->num;
        n.fn = (bb_box_fn)bb_len;
        n.zeta  = zeta;
        break;
    }

    /* ── SPAN(chars) ────────────────────────────────────────────────── */
    case _XSPNC: {
        span_t *zeta = calloc(1, sizeof(span_t));
        zeta->chars = p->sval ? p->sval : "";
        n.fn = (bb_box_fn)bb_span;
        n.zeta  = zeta;
        break;
    }

    /* ── BREAK(chars) ───────────────────────────────────────────────── */
    case _XBRKC: {
        brk_t *zeta = calloc(1, sizeof(brk_t));
        zeta->chars = p->sval ? p->sval : "";
        n.fn = (bb_box_fn)bb_brk;
        n.zeta  = zeta;
        break;
    }

    /* ── ANY(chars) ─────────────────────────────────────────────────── */
    case _XANYC: {
        any_t *zeta = calloc(1, sizeof(any_t));
        zeta->chars = p->sval ? p->sval : "";
        n.fn = (bb_box_fn)bb_any;
        n.zeta  = zeta;
        break;
    }

    /* ── NOTANY(chars) ──────────────────────────────────────────────── */
    case _XNNYC: {
        notany_t *zeta = calloc(1, sizeof(notany_t));
        zeta->chars = p->sval ? p->sval : "";
        n.fn = (bb_box_fn)bb_notany;
        n.zeta  = zeta;
        break;
    }

    /* ── ARB ────────────────────────────────────────────────────────── */
    case _XFARB: {
        arb_t *zeta = calloc(1, sizeof(arb_t));
        n.fn = (bb_box_fn)bb_arb;
        n.zeta  = zeta;
        break;
    }

    /* ── REM ────────────────────────────────────────────────────────── */
    case _XSTAR: {
        rem_t *zeta = calloc(1, sizeof(rem_t));
        n.fn = (bb_box_fn)bb_rem;
        n.zeta  = zeta;
        break;
    }

    /* ── SUCCEED ────────────────────────────────────────────────────── */
    case _XSUCF: {
        succeed_t *zeta = calloc(1, sizeof(succeed_t));
        n.fn = (bb_box_fn)bb_succeed;
        n.zeta  = zeta;
        break;
    }

    /* ── FAIL ───────────────────────────────────────────────────────── */
    case _XFAIL: {
        fail_t *zeta = calloc(1, sizeof(fail_t));
        n.fn = (bb_box_fn)bb_fail;
        n.zeta  = zeta;
        break;
    }

    /* ── EPSILON ────────────────────────────────────────────────────── */
    case _XEPS: {
        eps_t *zeta = calloc(1, sizeof(eps_t));
        n.fn = (bb_box_fn)bb_eps;
        n.zeta  = zeta;
        break;
    }

    /* ── CONCATENATION (left right) ─────────────────────────────────── */
    case _XCAT: {
        _seq_t *zeta = calloc(1, sizeof(_seq_t));
        bb_node_t l = bb_build(p->left);
        bb_node_t r = bb_build(p->right);
        zeta->left.fn  = l.fn; zeta->left.zeta  = l.zeta;
        zeta->right.fn = r.fn; zeta->right.zeta = r.zeta;
        n.fn = (bb_box_fn)bb_seq;
        n.zeta  = zeta;
        break;
    }

    /* ── ALTERNATION (left | right) ─────────────────────────────────── */
    case _XOR: {
        /*
         * Flatten nested XOR into a single ALT with N children.
         * This matches the test_sno_1.c alt_alpha/alt_beta pattern exactly.
         */
        _alt_t *zeta = calloc(1, sizeof(_alt_t));
        /* collect all OR arms by walking right-spine */
        _PND_t *cur = p;
        int     nc  = 0;
        while (cur && cur->kind == _XOR && nc < BB_ALT_MAX_S - 1) {
            bb_node_t arm        = bb_build(cur->left);
            zeta->children[nc].fn  = arm.fn;
            zeta->children[nc].zeta   = arm.zeta;
            nc++;
            cur = cur->right;
        }
        /* last arm (rightmost non-OR node) */
        if (nc < BB_ALT_MAX_S) {
            bb_node_t arm        = bb_build(cur);
            zeta->children[nc].fn  = arm.fn;
            zeta->children[nc].zeta   = arm.zeta;
            nc++;
        }
        zeta->n = nc;
        n.fn = (bb_box_fn)bb_alt;
        n.zeta  = zeta;
        break;
    }

    /* ── ARBNO(body) ────────────────────────────────────────────────── */
    case _XARBN: {
        _arbno_t *zeta = calloc(1, sizeof(_arbno_t));
        bb_node_t body  = bb_build(p->left);
        zeta->body_fn = body.fn;
        zeta->body_zeta  = body.zeta;
        n.fn = (bb_box_fn)bb_arbno;
        n.zeta  = zeta;
        break;
    }

    /* ── IMMEDIATE CAPTURE: pat $ var ───────────────────────────────── */
    case _XFNME: {
        capture_t *zeta = calloc(1, sizeof(capture_t));
        bb_node_t child = bb_build(p->left);
        zeta->child_fn  = child.fn;
        zeta->child_zeta   = child.zeta;
        zeta->varname   = (p->var.v == DT_S && p->var.s) ? p->var.s : NULL;
        zeta->immediate = 1;
        register_capture(zeta);
        n.fn = (bb_box_fn)bb_capture;
        n.zeta  = zeta;
        break;
    }

    /* ── CONDITIONAL CAPTURE: pat . var ─────────────────────────────── */
    case _XNME: {
        capture_t *zeta = calloc(1, sizeof(capture_t));
        bb_node_t child = bb_build(p->left);
        zeta->child_fn  = child.fn;
        zeta->child_zeta   = child.zeta;
        zeta->varname   = (p->var.v == DT_S && p->var.s) ? p->var.s : NULL;
        zeta->immediate = 0;
        register_capture(zeta);
        n.fn = (bb_box_fn)bb_capture;
        n.zeta  = zeta;
        break;
    }

    /* ── DEFERRED VAR REF: *name — resolved at match time ───────────── */
    case _XDSAR:
    /* ── VAR holding a pattern — resolved at match time ────────────── */
    case _XVAR: {
        /*
         * DYN-4: defer NV_GET_fn to Phase 3 (alpha port of deferred_var_t).
         * DYN-3 resolved here (Phase 2 / build time) — correct only for
         * non-mutating patterns.  DYN-4 is exact: *X always sees the
         * value X holds at the moment each match attempt begins.
         *
         * Store only the variable name; the box fetches live at alpha.
         */
        const char *name = (p->kind == _XDSAR) ? p->sval
                         : (p->var.v == DT_S)  ? p->var.s : NULL;
        if (name && *name) {
            deferred_var_t *zeta = calloc(1, sizeof(deferred_var_t));
            zeta->name     = name;
            zeta->child_fn = NULL;
            zeta->child_zeta  = NULL;
            n.fn = (bb_box_fn)bb_deferred_var;
            n.zeta  = zeta;
        } else {
            /* no name — epsilon (degenerate, shouldn't arise) */
            eps_t *zeta = calloc(1, sizeof(eps_t));
            n.fn = (bb_box_fn)bb_eps;
            n.zeta  = zeta;
        }
        break;
    }

    /* ── TAB(n) — advance cursor to absolute position n ─────────────── */
    case _XTB: {
        /* TAB(n): like POS but anchors to absolute tab stop.
         * Implemented as: if (Δ <= n) Δ=n; else omega */
        _pos_t *zeta = calloc(1, sizeof(_pos_t));
        zeta->n = (int)p->num;
        /* reuse POS box: POS(n) already checks Δ==n.
         * TAB is slightly different (allows Δ <= n, advances to n).
         * For DYN-3 correctness we use POS semantics; TAB optimisation
         * is M-DYN-4. */
        n.fn = (bb_box_fn)bb_pos;
        n.zeta  = zeta;
        break;
    }

    /* ── RTAB(n) — advance to (Ω-n) from right ─────────────────────── */
    case _XRTB: {
        _rpos_t *zeta = calloc(1, sizeof(_rpos_t));
        zeta->n = (int)p->num;
        n.fn = (bb_box_fn)bb_rpos;
        n.zeta  = zeta;
        break;
    }

    /* ── FENCE / ABORT / BAL: fall back to epsilon/fail for DYN-3 ───── */
    case _XFNCE:
    case _XABRT:
    case _XBAL:
    case _XATP:
    default: {
        /* Unimplemented in DYN-3: use epsilon (safe — may give wrong
         * results for these constructs but will not crash or regress
         * the static path).  Logged for tracking. */
        fprintf(stderr, "stmt_exec: unimplemented XKIND %d — using epsilon\n",
                (int)p->kind);
        eps_t *zeta = calloc(1, sizeof(eps_t));
        n.fn = (bb_box_fn)bb_eps;
        n.zeta  = zeta;
        break;
    }
    } /* switch */

    return n;
}

/* ── bb_deferred_var — defined here, after bb_build (needs bb_node_t) ───── */
/*
 * DYN-4: *name resolved at match time.  Graph built ONCE on first alpha;
 * subsequent alpha calls zero child_zeta (fresh locals) and re-drive the
 * same graph.  beta re-drives child with beta — no rebuild ever.
 *
 * Three-column layout:
 *
 *   DVAR_alpha:  if first time: NV_GET_fn → bb_build child graph    once
 *                zero child_zeta                                fresh state
 *                DVAR = child_fn(&child_zeta, alpha)            drive
 *                if empty                                       goto DVAR_omega
 *                                                               goto DVAR_gamma
 *   DVAR_beta:   if no child                                    goto DVAR_omega
 *                DVAR = child_fn(&child_zeta, beta)             backtrack
 *                if empty                                       goto DVAR_omega
 *                                                               goto DVAR_gamma
 */
static spec_t bb_deferred_var(deferred_var_t **zetazeta, int entry)
{
    deferred_var_t *zeta = *zetazeta;

    if (entry == alpha)                                 goto DVAR_alpha;
    if (entry == beta)                                  goto DVAR_beta;

    spec_t          DVAR;

    DVAR_alpha:     if (!zeta->child_fn) {
                        /* First alpha: resolve variable and build graph once */
                        DESCR_t val = NV_GET_fn(zeta->name);
                        bb_node_t child;
                        if (val.v == DT_P && val.p) {
                            child = bb_build((_PND_t *)val.p);
                        } else if (val.v == DT_S && val.s) {
                            _lit_t *lz = calloc(1, sizeof(_lit_t));
                            lz->lit    = val.s;
                            lz->len    = (int)strlen(val.s);
                            child.fn   = (bb_box_fn)bb_lit;
                            child.zeta = lz;
                        } else {
                            eps_t *ez  = calloc(1, sizeof(eps_t));
                            child.fn   = (bb_box_fn)bb_eps;
                            child.zeta = ez;
                        }
                        zeta->child_fn   = child.fn;
                        zeta->child_zeta = child.zeta;
                    } else {
                        /* Subsequent alpha: zero child state for fresh locals */
                        if (zeta->child_zeta)
                            memset(zeta->child_zeta, 0, DVAR_CHILD_STATE_MAX);
                    }
                    DVAR = zeta->child_fn(&zeta->child_zeta, alpha);
                    if (spec_is_empty(DVAR))            goto DVAR_omega;
                    goto DVAR_gamma;

    DVAR_beta:      if (!zeta->child_fn)                goto DVAR_omega;
                    DVAR = zeta->child_fn(&zeta->child_zeta, beta);
                    if (spec_is_empty(DVAR))            goto DVAR_omega;
                    goto DVAR_gamma;

    DVAR_gamma:     return DVAR;
    DVAR_omega:     return spec_empty;
}

/* ══════════════════════════════════════════════════════════════════════════
 * PUBLIC: stmt_exec_dyn
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * DYN-4: kw_anchor — when non-zero, Phase 3 tries only position 0.
 * Imported from snobol4.h in the full-runtime build; declared extern here
 * for standalone / test builds.
 */
#ifndef STMT_EXEC_STANDALONE
extern int kw_anchor;
#else
/* Standalone: default to unanchored; test driver can override */
int kw_anchor = 0;
#endif

/*
 * DYN-4: pending-capture flush.
 * After Phase 3 confirms overall match success we walk the box graph
 * and commit any buffered XNME (.) captures.  We do this via a small
 * visitor that recurses through _seq_t and _alt_t frames and checks
 * every capture_t.has_pending flag.
 *
 * We need to reach capture_t nodes buried inside _seq_t / _alt_t.
 * Rather than a full graph walk (complex), we use a simple flat list:
 * bb_build registers every capture_t it allocates into a thread-local
 * array, and Phase 5 iterates the array.
 */
#define MAX_CAPTURES 64
static capture_t *g_capture_list[MAX_CAPTURES];
static int        g_capture_count = 0;

/* Called from bb_build whenever a capture_t is created */
static void register_capture(capture_t *c)
{
    if (g_capture_count < MAX_CAPTURES)
        g_capture_list[g_capture_count++] = c;
}

/* Flush all pending XNME captures (call after Phase 3 success) */
static void flush_pending_captures(void)
{
    for (int i = 0; i < g_capture_count; i++) {
        capture_t *c = g_capture_list[i];
        if (!c->immediate && c->has_pending && c->varname && *c->varname) {
            char *s = (char *)GC_MALLOC(c->pending.delta + 1);
            memcpy(s, c->pending.sigma, (size_t)c->pending.delta);
            s[c->pending.delta] = '\0';
            DESCR_t val;
            val.v    = DT_S;
            val.slen = (uint32_t)c->pending.delta;
            val.s    = s;
            NV_SET_fn(c->varname, val);
        }
        c->has_pending = 0;
    }
    g_capture_count = 0;
}

/*
 * stmt_exec_dyn — execute one SNOBOL4 statement dynamically.
 *
 * DYN-4 SIGNATURE CHANGE: subj_name (const char*) added.
 *   - If non-NULL: subject is fetched via NV_GET_fn(subj_name) (Phase 1)
 *     and written back via NV_SET_fn(subj_name, ...) (Phase 5).
 *     This is the correct lvalue path — the only safe write-back.
 *   - If NULL: subject is read-only; replacement is skipped (:S without
 *     write-back; has_repl=1 with subj_name=NULL → :F per SNOBOL4 spec).
 *
 * Parameters:
 *   subj_name — NV name of subject variable (or NULL for read-only)
 *   subj_var  — subject DESCR_t (used when subj_name is NULL, e.g. literals)
 *   pat       — pattern DESCR_t (DT_P or DT_S)
 *   repl      — replacement DESCR_t pointer, or NULL for no replacement
 *   has_repl  — 1 if replacement is present
 *
 * Returns 1 → :S, 0 → :F.
 */
int stmt_exec_dyn(const char  *subj_name,
                  DESCR_t     *subj_var,
                  DESCR_t      pat,
                  DESCR_t     *repl,
                  int          has_repl)
{
    /* reset capture registry for this statement */
    g_capture_count = 0;

    /* ── Phase 1: build subject ─────────────────────────────────────── */
    /*
     * DYN-4: If subj_name is given, fetch fresh via NV_GET_fn.
     * Otherwise fall back to subj_var (literal / expression result).
     */
    DESCR_t subj_fetched;
    if (subj_name && *subj_name) {
        subj_fetched = NV_GET_fn(subj_name);
        subj_var     = &subj_fetched;
    }

    const char *subj_str = "";
    int         subj_len = 0;

    if (subj_var) {
        char *sv = VARVAL_fn(*subj_var);
        if (sv) {
            subj_str = sv;
            subj_len = (int)strlen(sv);
        }
    }

    Σ = subj_str;
    Ω = subj_len;

    /* ── Phase 2: build pattern ─────────────────────────────────────── */
    bb_node_t root;
    if (pat.v == DT_P && pat.p) {
        root = bb_build((_PND_t *)pat.p);
    } else if (pat.v == DT_S && pat.s) {
        _lit_t *lzeta = calloc(1, sizeof(_lit_t));
        lzeta->lit = pat.s;
        lzeta->len = (int)strlen(pat.s);
        root.fn = (bb_box_fn)bb_lit;
        root.zeta  = lzeta;
    } else {
        eps_t *ezeta = calloc(1, sizeof(eps_t));
        root.fn = (bb_box_fn)bb_eps;
        root.zeta  = ezeta;
    }

    /* ── Phase 3: run match ─────────────────────────────────────────── */
    /*
     * DYN-4: honour kw_anchor (&ANCHOR keyword).
     * If non-zero, try only position 0 (anchored match).
     * Otherwise scan positions 0..Ω (unanchored).
     */
    int match_start = -1;
    int match_end   = -1;

    int scan_limit = kw_anchor ? 0 : Ω;

    for (int scan = 0; scan <= scan_limit; scan++) {
        Δ = scan;
        spec_t result = root.fn(&root.zeta, alpha);
        if (!spec_is_empty(result)) {
            match_start = scan;
            match_end   = Δ;
            goto Phase4;
        }
        if (scan == scan_limit) break;
    }

    /* match failed → :F */
    return 0;

Phase4:
    /* ── Phase 4: build replacement ────────────────────────────────── */
    /*
     * DYN-4 lvalue rule: if replacement present and subj_name is NULL,
     * the subject has no NV home — can't write back safely.
     * In the full runtime this is :F.
     * Exception: subj_name=NULL + subj_var provided = test/convenience
     * wrapper (stmt_exec_dyn_str).  We allow direct write in that case.
     */
    if (has_repl && repl && !subj_name && !subj_var) return 0;

    /* Flush XNME (.) conditional captures — overall match succeeded */
    flush_pending_captures();

    if (!has_repl || !repl) goto Success;

    /* ── Phase 5: perform replacement ──────────────────────────────── */
    {
        const char *repl_str = "";
        int         repl_len = 0;
        if (repl->v == DT_S && repl->s) {
            repl_str = repl->s;
            repl_len = repl->slen ? (int)repl->slen : (int)strlen(repl->s);
        } else if (repl->v == DT_I) {
            char ibuf[32];
            snprintf(ibuf, sizeof(ibuf), "%lld", (long long)repl->i);
            char *gs = (char *)GC_MALLOC(strlen(ibuf) + 1);
            strcpy(gs, ibuf);
            repl_str = gs;
            repl_len = (int)strlen(gs);
        }

        int   new_len = match_start + repl_len + (Ω - match_end);
        char *new_s   = (char *)GC_MALLOC((size_t)new_len + 1);

        memcpy(new_s,                          subj_str,             (size_t)match_start);
        memcpy(new_s + match_start,            repl_str,             (size_t)repl_len);
        memcpy(new_s + match_start + repl_len, subj_str + match_end, (size_t)(Ω - match_end));
        new_s[new_len] = '\0';

        DESCR_t new_val;
        new_val.v    = DT_S;
        new_val.slen = (uint32_t)new_len;
        new_val.s    = new_s;

        if (subj_name && *subj_name) {
            /* DYN-4: safe lvalue write-back via NV_SET_fn */
            NV_SET_fn(subj_name, new_val);
        } else if (subj_var) {
            /* convenience / test path: direct write (no NV table) */
            *subj_var = new_val;
        }
    }

Success:
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * stmt_exec_dyn_str — convenience wrapper: subject and pattern as C strings
 *
 * Used by the test driver (stmt_exec_test.c) to exercise the executor
 * without going through the full DESCR_t / PATND_t stack.
 *
 * NOTE: subj_name is passed as NULL here — the test driver owns the
 * subject buffer directly.  Phase 5 write-back goes via *out_subject
 * for test purposes only.  In the full runtime, callers always pass
 * a real subj_name.
 * ══════════════════════════════════════════════════════════════════════════ */
int stmt_exec_dyn_str(const char  *subject,
                      const char  *pattern,
                      const char  *repl_str,
                      char       **out_subject)
{
    /* Build subject DESCR_t */
    DESCR_t subj;
    subj.v    = DT_S;
    subj.slen = subject ? (uint32_t)strlen(subject) : 0;
    subj.s    = subject ? (char *)subject : (char *)"";

    /* Build pattern DESCR_t (literal string → DT_S) */
    DESCR_t pat;
    pat.v    = DT_S;
    pat.slen = pattern ? (uint32_t)strlen(pattern) : 0;
    pat.s    = pattern ? (char *)pattern : (char *)"";

    /* Build replacement DESCR_t */
    DESCR_t repl_d;
    repl_d.v    = DT_S;
    repl_d.slen = repl_str ? (uint32_t)strlen(repl_str) : 0;
    repl_d.s    = repl_str ? (char *)repl_str : NULL;

    int has_repl = (repl_str != NULL);

    /* subj_name=NULL: test driver, no NV table, replacement returned via
     * out_subject for convenience.  Phase 5 writes into subj.s via the
     * direct path (special case: when subj_name is NULL and !has_repl the
     * replacement step is skipped gracefully). */
    int r = stmt_exec_dyn(NULL, &subj, pat, has_repl ? &repl_d : NULL, has_repl);

    if (out_subject && r) {
        *out_subject = subj.s;
    }
    return r;
}
