/*
 * bb_dvar.c — DVAR (deferred/indirect pattern variable) Byrd Box (M-DYN-4)
 *
 * Handles two XKIND cases that share identical runtime behaviour:
 *
 *   _XDSAR  (*VAR)  — indirect pattern: VAR holds a pattern node pointer.
 *                     Dereferences VAR on every α and uses the live value.
 *
 *   _XVAR   (VAR)   — pattern variable: a named variable whose current value
 *                     is a DT_P pattern node or DT_S string, resolved live.
 *
 * Both cases store only the variable name at build time.  On every α the
 * box calls NV_GET_fn(name), builds (or reuses) a child graph for the
 * current value, and delegates α to that child.  β delegates to the same
 * child if one was built.
 *
 * Re-resolve rule (DYN-4): the child graph is rebuilt whenever the value
 * pointer changes.  If the same pattern node pointer is seen again, the
 * existing child graph is reused (but its ζ is memset to 0 for a clean α).
 *
 * DYN-12 exception: bb_lit child nodes must NOT be memset — their lit/len
 * fields are configuration, not mutable match state.  memset would zero
 * len, causing bb_lit to match everywhere with δ=0.
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     DVAR_α:             NV_GET(name) → build/reuse child
 *                         reset child ζ if reused
 *                         child α                        → DVAR_γ / DVAR_ω
 *     DVAR_β:             child β (if built)             → DVAR_γ / DVAR_ω
 *     DVAR_γ:                                            return DVAR;
 *     DVAR_ω:                                            return spec_empty;
 *
 * State ζ: name, child_fn, child_ζ, child_ζ_size.
 */

#include "bb_box.h"
#include <stdlib.h>
#include <string.h>

#ifndef BB_DVAR_STANDALONE
typedef struct { int v; union { int64_t i; const char *s; void *p; }; uint32_t slen; } DESCR_t;
#define DT_P 3
#define DT_S 2
extern DESCR_t (*NV_GET_fn)(const char *name);
/* bb_build and bb_lit are provided by stmt_exec.c at link time */
typedef struct { bb_box_fn fn; void *ζ; size_t ζ_size; } bb_node_t;
extern bb_node_t bb_build(void *p);
extern spec_t bb_lit(void **ζζ, int entry);
typedef struct { const char *lit; int len; } _lit_t;
#endif

typedef struct {
    const char  *name;
    bb_box_fn    child_fn;
    void        *child_ζ;
    size_t       child_ζ_size;
} dvar_t;

spec_t bb_deferred_var(dvar_t **ζζ, int entry)
{
    dvar_t *ζ = *ζζ;

    if (entry == α)                                     goto DVAR_α;
    if (entry == β)                                     goto DVAR_β;

    /*------------------------------------------------------------------------*/
    spec_t          DVAR;

    DVAR_α:         {
                        DESCR_t val = NV_GET_fn(ζ->name);
                        int rebuilt = 0;

                        if (val.v == DT_P && val.p) {
                            if (val.p != ζ->child_ζ || !ζ->child_fn) {
                                bb_node_t child = bb_build(val.p);
                                ζ->child_fn     = child.fn;
                                ζ->child_ζ      = child.ζ;
                                ζ->child_ζ_size = child.ζ_size;
                                rebuilt = 1;
                            }
                        } else if (val.v == DT_S && val.s) {
                            _lit_t *lz = (_lit_t *)ζ->child_ζ;
                            if (!lz || lz->lit != val.s) {
                                lz = calloc(1, sizeof(_lit_t));
                                lz->lit = val.s;
                                lz->len = (int)strlen(val.s);
                                ζ->child_fn     = (bb_box_fn)bb_lit;
                                ζ->child_ζ      = lz;
                                ζ->child_ζ_size = sizeof(_lit_t);
                                rebuilt = 1;
                            }
                        } else {
                            if (!ζ->child_fn) {
                                /* unset variable — epsilon */
                                eps_t *ez = calloc(1, sizeof(eps_t));
                                ζ->child_fn     = (bb_box_fn)bb_eps;
                                ζ->child_ζ      = ez;
                                ζ->child_ζ_size = sizeof(eps_t);
                                rebuilt = 1;
                            }
                        }

                        /* Reset child state for clean α.  Never memset bb_lit
                         * nodes — lit/len are config, not match state (DYN-12). */
                        if (!rebuilt && ζ->child_ζ && ζ->child_ζ_size
                                && ζ->child_fn != (bb_box_fn)bb_lit)
                            memset(ζ->child_ζ, 0, ζ->child_ζ_size);
                    }
                    if (!ζ->child_fn)                         goto DVAR_ω;
                    DVAR = ζ->child_fn(&ζ->child_ζ, α);
                    if (spec_is_empty(DVAR))                  goto DVAR_ω;
                                                              goto DVAR_γ;

    DVAR_β:         if (!ζ->child_fn)                         goto DVAR_ω;
                    DVAR = ζ->child_fn(&ζ->child_ζ, β);
                    if (spec_is_empty(DVAR))                  goto DVAR_ω;
                                                              goto DVAR_γ;

    /*------------------------------------------------------------------------*/
    DVAR_γ:                                                   return DVAR;
    DVAR_ω:                                                   return spec_empty;
}

dvar_t *bb_dvar_new(const char *name)
{
    dvar_t *ζ = calloc(1, sizeof(dvar_t));
    ζ->name = name;
                                                              return ζ;
}
