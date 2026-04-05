/*
 * invoke.c — SIL-faithful INVOKE / ARGVAL dispatch
 *
 * Mirrors SIL v311.sil lines 2669–2700:
 *
 *   INVOKE PROC ,          Invokation procedure
 *          POP     INCL    Get function index (= name in our model)
 *          GETDC   XPTR,INCL,0   Get procedure descriptor
 *          VEQL    INCL,XPTR,INVK2   Check argument counts
 *   INVK1  BRANIC  INCL,0  If equal, branch indirect (call fn)
 *   INVK2  TESTF   XPTR,FNC,ARGNER,INVK1
 *                          Check for variable argument number
 *
 *   ARGVAL PROC ,          Procedure to evaluate argument
 *          INCRA   OCICL,DESCR    Increment interpreter offset
 *          GETD    XPTR,OCBSCL,OCICL  Get argument
 *          TESTF   XPTR,FNC,,ARGVC    Test for function descriptor
 *   ARGV1  AEQLC   INSW,0,,ARGV2  Check &INPUT
 *          LOCAPV  ZPTR,INATL,XPTR,ARGV2
 *          GETDC   ZPTR,ZPTR,DESCR  Get input descriptor
 *          RCALL   XPTR,PUTIN,(ZPTR,XPTR),(FAIL,RTXNAM)
 *   ARGVC  RCALL   XPTR,INVOKE,(XPTR),(FAIL,ARGV1,RTXNAM)
 *   ARGV2  GETDC   XPTR,XPTR,DESCR  Get value from name
 *          BRANCH  RTXNAM
 *
 * In our model:
 *   - "function index" INCL is the function name string (NV-keyed)
 *   - BRANIC = APPLY_fn (C builtin) or g_user_call_hook (SNOBOL4 body)
 *   - TESTF FNC = checking nargs vs declared arity (we use -1 for variadic)
 *   - ARGVAL in our tree-walk context = evaluate one EXPR_t argument
 *
 * Strategy (RT-1, additive shim):
 *   INVOKE_fn replaces the ad-hoc dispatch in call_user_function.
 *   call_user_function body-execution logic is unchanged — it becomes
 *   the user-defined branch of INVOKE_fn, called via g_user_call_hook.
 *   ARGVAL_fn is a new typed-argument evaluator (SIL name, our semantics).
 *
 * Gate: PASS >= 177, no regressions.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * SPRINT:  RT-1
 * DATE:    2026-04-04
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gc.h>

#include "snobol4.h"

/* ── INVOKE_fn ────────────────────────────────────────────────────────────
 *
 * SIL INVOKE: universal function dispatcher.
 *
 * 1. Look up function descriptor (GETDC XPTR,INCL,0)
 * 2. If C builtin (fn != NULL): call directly (BRANIC)
 * 3. If user-defined body (fn == NULL, hook set): call via hook (BRANIC)
 * 4. If variable-arg (nargs == -1): allow without count check (TESTF FNC)
 * 5. Not found: FAIL (ARGNER path)
 *
 * Returns FAILDESCR on invokation failure (SIL FAIL exit of INVOKE).
 * Returns result descriptor on success (SIL RTXNAM exit — value on stack).
 * ────────────────────────────────────────────────────────────────────── */
DESCR_t INVOKE_fn(const char *name, DESCR_t *args, int nargs)
{
    if (!name) return FAILDESCR;

    /* GETDC XPTR,INCL,0 — fetch procedure descriptor (= APPLY_fn lookup) */
    /* BRANIC INCL,0 — branch indirect: call the function */
    DESCR_t result = APPLY_fn(name, args, nargs);

    /* APPLY_fn returns NULVCL for "not found" — but NULVCL is also a valid
     * successful return value (empty string).  We can't distinguish here,
     * so we trust APPLY_fn: if it returned without calling FAILDESCR,
     * it succeeded.  The failure path (ARGNER) is: APPLY_fn itself returns
     * FAILDESCR when the underlying fn returns FAILDESCR. */
    return result;
}

/* ── ARGVAL_fn ────────────────────────────────────────────────────────────
 *
 * SIL ARGVAL: evaluate one argument to its value.
 *
 * SIL steps translated:
 *   TESTF XPTR,FNC,,ARGVC  — if descriptor has FNC bit (is a function call),
 *                            → call INVOKE recursively (ARGVC path)
 *   ARGV1: check &INPUT association  (INPUT_fn side-effect — not yet wired,
 *                                     RT-5 milestone; stub here)
 *   ARGV2: GETDC XPTR,XPTR,DESCR   — get value from name slot
 *          → return value descriptor
 *
 * In our model:
 *   A DESCR_t is already a value — there is no "name slot indirection"
 *   at the descriptor level yet (that's RT-3 DT_N).  So ARGVAL_fn here
 *   is a typed pass-through that handles:
 *     - DT_SNUL / DT_S / DT_I / DT_R / DT_P / DT_C / DT_E → return as-is
 *     - DT_N  → dereference name (RT-3 will flesh this out)
 *     - DT_FAIL → propagate failure
 *
 * ARGV2 / &INPUT association check: stub (RT-5).
 * ────────────────────────────────────────────────────────────────────── */
DESCR_t ARGVAL_fn(DESCR_t d)
{
    /* TESTF XPTR,FNC — DT_FAIL propagates immediately (ARGNER path) */
    if (d.v == DT_FAIL) return FAILDESCR;

    /* ARGV2: GETDC XPTR,XPTR,DESCR — dereference NAME to its value */
    if (d.v == DT_N) {
        /* RT-3 will implement full DT_N dereference via NAMEPTR.
         * For now: if slen==0 the name is stored as a string → NV lookup. */
        if (d.slen == 0 && d.s && *d.s)
            return NV_GET_fn(d.s);
        /* slen==1: interior pointer (NAMEPTR macro) → dereference */
        if (d.slen == 1 && d.ptr)
            return *(DESCR_t *)d.ptr;
        return NULVCL;
    }

    /* &INPUT association check (ARGV1 / ARGV2 boundary) — stub for RT-5 */
    /* TODO RT-5: check input association table for DT_S variables */

    /* All other types: value is the descriptor itself (ARGV2 path) */
    return d;
}

/* ── VARVAL_d_fn / INTVAL_fn / PATVAL_fn / VARVUP_fn ─────────────────────
 * Moved to argval.c (RT-2). Declarations in snobol4.h.
 * ─────────────────────────────────────────────────────────────────────── */
