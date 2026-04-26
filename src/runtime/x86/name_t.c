/*
 * name_t.c — SN-21: name_commit_value() dispatcher.
 *
 * One entry point, one switch on NameKind_t, one correct commit per kind.
 * This file intentionally contains no other logic — boxes and NAM commit
 * both call through here so there is exactly one place where an lvalue
 * actually becomes a store.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc.h>

#include "name_t.h"
#include "snobol4.h"    /* NV_SET_fn, NV_GET_fn, g_user_call_hook, DT_*       */

/* SN-21e: DT_E thaw is the single-dispatch commit point for values produced
 * by pattern-context `*var` references that bound a frozen expression
 * (DT_E).  Before SN-21e the thaw was fragmented across interp_eval_pat and
 * NAME_commit; folding it into name_commit_value means every lvalue kind
 * (NM_VAR / NM_PTR / NM_CALL / NM_IDX) sees an already-thawed value with
 * zero per-kind handling.  EVAL_fn is idempotent for DT_S / DT_I / DT_R and
 * only pays cost for DT_E — see snobol4_pattern.c:EVAL_fn.  Closes the
 * SN-20 `*var-holds-DT_E` remainder. */
DESCR_t EVAL_fn(DESCR_t expr);

/*---------------------------------------------------------------------------*/
/* name_commit_value — commit `value` into the location described by *nm     */
/*---------------------------------------------------------------------------*/

/* Returns 0 on success, -1 on failure.
 * Failure only possible for NM_CALL when the called function FRETURNs —
 * used by bb_cap immediate ($) path to propagate failure into the pattern. */
int name_commit_value(const NAME_t *nm, DESCR_t value)
{
    if (!nm) return 0;

    /* SN-21e: DT_E thaw.  If a pattern-context *var held a frozen expression,
     * the match-time value arrives here as DT_E; EVAL_fn drives it through
     * EXPVAL_fn with proper NAM save/restore.  Non-DT_E values pass through
     * unchanged (EVAL_fn idempotent for DT_S / DT_I / DT_R). */
    if (value.v == DT_E) value = EVAL_fn(value);

    switch (nm->kind) {

    case NM_VAR:
        if (nm->var_name && nm->var_name[0])
            NV_SET_fn(nm->var_name, value);
        return 0;

    case NM_PTR:
        if (nm->var_ptr)
            *nm->var_ptr = value;
        return 0;

    case NM_IDX:
        /* Reserved — A[i,j] path is still ad-hoc; SN-21c+ will land real
         * evaluation + commit here.  No-op for now so accidental use is
         * visible rather than crashing. */
        return 0;

    case NM_CALL:
        /* Indirect call (pat . *fn()): invoke fn, obtain DT_N return, then
         * write value into the cell that DT_N points at.  Arg-name-deferred
         * resolution (TL-2) happens here if fnc_arg_names is set. */
        if (!g_user_call_hook || !nm->fnc_name || !nm->fnc_name[0]) return 0;

        DESCR_t *call_args = nm->fnc_args;
        int      call_n    = nm->fnc_nargs;
        DESCR_t  resolved_buf[8];
        DESCR_t *resolved  = NULL;

        if (nm->fnc_arg_names && nm->fnc_n_arg_names > 0) {
            call_n   = nm->fnc_n_arg_names;
            resolved = (call_n <= 8) ? resolved_buf
                                     : (DESCR_t *)GC_MALLOC((size_t)call_n * sizeof(DESCR_t));
            for (int k = 0; k < call_n; k++) {
                resolved[k] = NV_GET_fn(nm->fnc_arg_names[k]
                                        ? nm->fnc_arg_names[k] : "");
            }
            call_args = resolved;
        } else {
            /* SN-26c-parseerr-c: thaw DT_E args at match time.  Args wrapped
             * as DT_E by lowerer (E_FNC sub-args of *fn(args) patterns)
             * resolve here via EVAL_fn — counter values, etc., reflect
             * post-ARBNO state, not pattern-build-time state. */
            int have_dte = 0;
            for (int k = 0; k < call_n; k++) {
                if (nm->fnc_args[k].v == DT_E) { have_dte = 1; break; }
            }
            if (have_dte && call_n > 0) {
                resolved = (call_n <= 8) ? resolved_buf
                                         : (DESCR_t *)GC_MALLOC((size_t)call_n * sizeof(DESCR_t));
                for (int k = 0; k < call_n; k++) {
                    resolved[k] = (nm->fnc_args[k].v == DT_E)
                                  ? EVAL_fn(nm->fnc_args[k])
                                  : nm->fnc_args[k];
                }
                call_args = resolved;
            }
        }

        DESCR_t name_d = g_user_call_hook(nm->fnc_name, call_args, call_n);
        if (getenv("ONE4ALL_USERCALL_TRACE")) {
            fprintf(stderr, "NM_CALL name=%s nargs=%d arg_names=%s\n",
                    nm->fnc_name ? nm->fnc_name : "(null)",
                    call_n,
                    (nm->fnc_arg_names && nm->fnc_n_arg_names > 0) ? "yes" : "no");
            for (int k = 0; k < call_n; k++) {
                const char *kind = "?";
                switch ((int)call_args[k].v) {
                    case DT_SNUL: kind = "DT_SNUL"; break;
                    case DT_S:    kind = "DT_S";    break;
                    case DT_E:    kind = "DT_E";    break;
                    case DT_I:    kind = "DT_I";    break;
                    case DT_R:    kind = "DT_R";    break;
                    case DT_N:    kind = "DT_N";    break;
                    case DT_P:    kind = "DT_P";    break;
                    case DT_FAIL: kind = "DT_FAIL"; break;
                }
                const char *str = (call_args[k].v == DT_S && call_args[k].s) ? call_args[k].s : "";
                const char *raw_kind = nm->fnc_args ? "?" : "(name)";
                if (nm->fnc_args) { switch ((int)nm->fnc_args[k].v) {
                    case DT_SNUL: raw_kind = "DT_SNUL"; break;
                    case DT_S:    raw_kind = "DT_S";    break;
                    case DT_E:    raw_kind = "DT_E";    break;
                    case DT_I:    raw_kind = "DT_I";    break;
                    case DT_R:    raw_kind = "DT_R";    break;
                    case DT_N:    raw_kind = "DT_N";    break;
                    case DT_P:    raw_kind = "DT_P";    break;
                    case DT_FAIL: raw_kind = "DT_FAIL"; break;
                } }
                fprintf(stderr, "  arg[%d] raw v=%s   eff v=%s s=\"%s\"\n",
                        k, raw_kind, kind, str);
            }
        }
        DESCR_t *cell  = (name_d.v == DT_N && name_d.ptr)
                         ? (DESCR_t *)name_d.ptr : NULL;
        /* SN-26c-parseerr-f: if fn FRETURNs (no DT_N cell), return -1 so
         * bb_cap's immediate ($) path can propagate failure into the pattern. */
        if (!cell) return -1;
        *cell = value;
        return 0;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Convenience initialisers                                                   */
/*---------------------------------------------------------------------------*/

void name_init_as_var(NAME_t *nm, const char *var_name)
{
    if (!nm) return;
    memset(nm, 0, sizeof(*nm));
    nm->kind     = NM_VAR;
    nm->var_name = var_name;
}

void name_init_as_ptr(NAME_t *nm, DESCR_t *var_ptr)
{
    if (!nm) return;
    memset(nm, 0, sizeof(*nm));
    nm->kind    = NM_PTR;
    nm->var_ptr = var_ptr;
}

void name_init_as_call(NAME_t *nm,
                       const char *fnc_name,
                       DESCR_t *fnc_args, int fnc_nargs,
                       char **fnc_arg_names, int fnc_n_arg_names)
{
    if (!nm) return;
    memset(nm, 0, sizeof(*nm));
    nm->kind            = NM_CALL;
    nm->fnc_name        = fnc_name;
    nm->fnc_args        = fnc_args;
    nm->fnc_nargs       = fnc_nargs;
    nm->fnc_arg_names   = fnc_arg_names;
    nm->fnc_n_arg_names = fnc_n_arg_names;
}
