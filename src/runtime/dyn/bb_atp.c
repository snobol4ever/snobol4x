/*
 * bb_atp.c — ATP (@var cursor capture) Byrd Box (M-DYN-4)
 *
 * On α: writes the current cursor position Δ as DT_I into the named
 * variable via NV_SET_fn.  Succeeds with a zero-width match.
 * On β: always ω — cursor-position capture has no meaningful backtrack
 * semantics (we don't un-write the variable).
 *
 * Pattern:  @CURSOR
 * SNOBOL4:  @VAR  (captures cursor position into VAR)
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     ATP_α:              NV_SET(varname, DT_I(Δ));
 *                         ATP = spec(Σ+Δ, 0);             → ATP_γ
 *     ATP_β:                                             → ATP_ω
 *     ATP_γ:                                             return ATP;
 *     ATP_ω:                                             return spec_empty;
 *
 * State ζ: varname + done flag (done prevents double-write on
 * hypothetical re-entry, though β always ω's so it can't happen
 * in normal use).
 *
 * NV_SET_fn and DESCR_t are provided by the runtime environment
 * (snobol4_vars.h / stmt_exec.c).  In standalone builds these must
 * be stubbed out.
 */

#include "bb_box.h"
#include <stdlib.h>

/* Runtime hooks — provided by stmt_exec.c in full builds.
 * Forward-declared here; the linker resolves them. */
#ifndef BB_ATP_STANDALONE
typedef struct { int v; union { int64_t i; const char *s; void *p; }; uint32_t slen; } DESCR_t;
#define DT_I 1
extern void (*NV_SET_fn)(const char *name, DESCR_t val);
#endif

typedef struct { int done; const char *varname; } atp_t;

spec_t bb_atp(atp_t **ζζ, int entry)
{
    atp_t *ζ = *ζζ;

    if (entry == α)                                 goto ATP_α;
    if (entry == β)                                 goto ATP_β;

    /*------------------------------------------------------------------------*/
    spec_t         ATP;

    ATP_α:        ζ->done = 1;
                  if (ζ->varname && ζ->varname[0]) {
                      DESCR_t val;
                      val.v = DT_I;
                      val.i = (int64_t)Δ;
                      NV_SET_fn(ζ->varname, val);
                  }
                  ATP = spec(Σ+Δ, 0);                         goto ATP_γ;

    ATP_β:                                                    goto ATP_ω;

    /*------------------------------------------------------------------------*/
    ATP_γ:                                                    return ATP;
    ATP_ω:                                                    return spec_empty;
}

atp_t *bb_atp_new(const char *varname)
{
    atp_t *ζ = calloc(1, sizeof(atp_t));
    ζ->varname = varname;
                                                              return ζ;
}
