/*
 * bb_capture.c — CAPTURE Byrd Box for $ (immediate) and . (conditional) (M-DYN-4)
 *
 * Wraps a child box.  On every child γ, writes the matched substring to
 * a named variable.  Two modes:
 *
 *   XFNME  ($)  immediate=1: write on EVERY child γ, including during
 *               backtracking.  The variable always holds the most recent
 *               match of this sub-pattern.
 *
 *   XNME   (.)  immediate=0: buffer the match in `pending`; the statement
 *               executor (Phase 5) flushes pending captures only if the
 *               entire pattern succeeds.  On CAP_ω, pending is discarded.
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     CAP_α:              child α                        → CAP_γ_core / CAP_ω
 *     CAP_β:              child β                        → CAP_γ_core / CAP_ω
 *     CAP_γ_core:         if immediate: NV_SET(child_r)
 *                         else:         pending = child_r, has_pending=1
 *                                                        return child_r;
 *     CAP_ω:              has_pending = 0;               return spec_empty;
 *
 * State ζ: child fn/state, varname, immediate flag, pending capture.
 *
 * NV_SET_fn and DESCR_t are provided by stmt_exec.c.
 */

#include "bb_box.h"
#include <stdlib.h>
#include <string.h>

#ifndef BB_CAPTURE_STANDALONE
typedef struct { int v; union { int64_t i; const char *s; void *p; }; uint32_t slen; } DESCR_t;
#define DT_S 2
extern void  (*NV_SET_fn)(const char *name, DESCR_t val);
extern void  *GC_MALLOC(size_t);
#endif

typedef struct {
    bb_box_fn    child_fn;
    void        *child_ζ;
    const char  *varname;
    int          immediate;   /* 1=$ (XFNME), 0=. (XNME) */
    spec_t       pending;
    int          has_pending;
} capture_t;

spec_t bb_capture(capture_t **ζζ, int entry)
{
    capture_t *ζ = *ζζ;

    if (entry == α)                                     goto CAP_α;
    if (entry == β)                                     goto CAP_β;

    /*------------------------------------------------------------------------*/
    spec_t         child_r;

    CAP_α:        child_r = ζ->child_fn(&ζ->child_ζ, α);
                  if (spec_is_empty(child_r))                 goto CAP_ω;
                                                              goto CAP_γ_core;

    CAP_β:        child_r = ζ->child_fn(&ζ->child_ζ, β);
                  if (spec_is_empty(child_r))                 goto CAP_ω;
                                                              goto CAP_γ_core;

    CAP_γ_core:   if (ζ->varname && *ζ->varname) {
                      if (ζ->immediate) {
                          /* $ — write immediately on every γ */
                          char *s = (char *)GC_MALLOC(child_r.δ + 1);
                          memcpy(s, child_r.σ, (size_t)child_r.δ);
                          s[child_r.δ] = '\0';
                          DESCR_t val;
                          val.v    = DT_S;
                          val.slen = (uint32_t)child_r.δ;
                          val.s    = s;
                          NV_SET_fn(ζ->varname, val);
                      } else {
                          /* . — buffer; Phase 5 commits on overall success */
                          ζ->pending     = child_r;
                          ζ->has_pending = 1;
                      }
                  }
                                                              return child_r;

    CAP_ω:        ζ->has_pending = 0;                        return spec_empty;
}

capture_t *bb_capture_new(bb_box_fn child_fn, void *child_ζ,
                           const char *varname, int immediate)
{
    capture_t *ζ = calloc(1, sizeof(capture_t));
    ζ->child_fn  = child_fn;
    ζ->child_ζ   = child_ζ;
    ζ->varname   = varname;
    ζ->immediate = immediate;
                                                              return ζ;
}
