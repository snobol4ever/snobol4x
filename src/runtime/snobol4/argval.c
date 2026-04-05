/*
 * argval.c — SIL-faithful typed argument evaluators
 *
 * SIL procs: VARVAL (line 2836), INTVAL (line 2774), PATVAL (line 2800),
 *            VARVUP (line 2867)
 *
 * These replace the ad-hoc coercions scattered through interp_eval().
 * Gate: PASS >= 177, no regressions.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * SPRINT:  RT-2
 * DATE:    2026-04-05
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gc.h>

#include "snobol4.h"

/* ── VARVAL_fn (DESCR_t form) ─────────────────────────────────────────────
 *
 * SIL VARVAL (line 2836): evaluate argument as STRING.
 * ARGVAL → get value → coerce to string:
 *   DT_I  → integer string via INTSP
 *   DT_R  → real string via REALST
 *   DT_S / DT_SNUL → already string
 *   others → FAIL
 *
 * Named VARVAL_d_fn to avoid collision with the existing char*-returning
 * VARVAL_fn in snobol4.c. TODO RT-2 follow-up: unify to single form.
 * ─────────────────────────────────────────────────────────────────────── */
DESCR_t VARVAL_d_fn(DESCR_t d)
{
    /* ARGVAL first */
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_N) {
        if (d.slen == 0 && d.s && *d.s) d = NV_GET_fn(d.s);
        else if (d.slen == 1 && d.ptr)  d = *(DESCR_t *)d.ptr;
        else return NULVCL;
    }
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_S || d.v == DT_SNUL) return d;
    if (d.v == DT_I) {
        char buf[64];
        snprintf(buf, sizeof buf, "%lld", (long long)d.i);
        return STRVAL(GC_strdup(buf));
    }
    if (d.v == DT_R) {
        char buf[64];
        snprintf(buf, sizeof buf, "%g", d.r);
        return STRVAL(GC_strdup(buf));
    }
    return FAILDESCR;
}

/* ── INTVAL_fn ────────────────────────────────────────────────────────────
 *
 * SIL INTVAL (line 2774): evaluate argument as INTEGER.
 * ARGVAL → coerce:
 *   DT_I              → already integer
 *   DT_R              → RLINT (truncate real to integer)
 *   DT_S / DT_SNUL   → SPCINT (parse integer from string, FAIL if not numeric)
 *   others            → FAIL
 * ─────────────────────────────────────────────────────────────────────── */
DESCR_t INTVAL_fn(DESCR_t d)
{
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_N) {
        if (d.slen == 0 && d.s && *d.s) d = NV_GET_fn(d.s);
        else if (d.slen == 1 && d.ptr)  d = *(DESCR_t *)d.ptr;
        else return FAILDESCR;
    }
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_I)   return d;
    if (d.v == DT_R)   return INTVAL((int64_t)d.r);   /* RLINT */
    if (d.v == DT_S || d.v == DT_SNUL) {
        const char *s = d.s ? d.s : "";
        if (!*s) return FAILDESCR;
        char *end;
        long long n = strtoll(s, &end, 10);
        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\0') return FAILDESCR;
        return INTVAL((int64_t)n);
    }
    return FAILDESCR;
}

/* ── PATVAL_fn ────────────────────────────────────────────────────────────
 *
 * SIL PATVAL (line 2800): evaluate argument as PATTERN.
 * ARGVAL → coerce:
 *   DT_P              → already pattern
 *   DT_S / DT_SNUL   → bb_lit (string literal pattern)
 *   DT_I / DT_R      → coerce to string, then bb_lit
 *   DT_E              → EXPVAL stub (RT-6)
 *   others            → FAIL
 * ─────────────────────────────────────────────────────────────────────── */
DESCR_t PATVAL_fn(DESCR_t d)
{
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_N) {
        if (d.slen == 0 && d.s && *d.s) d = NV_GET_fn(d.s);
        else if (d.slen == 1 && d.ptr)  d = *(DESCR_t *)d.ptr;
        else return FAILDESCR;
    }
    if (d.v == DT_FAIL) return FAILDESCR;
    if (d.v == DT_P) return d;
    if (d.v == DT_E) {
        /* RT-6 stub: EXPVAL not yet implemented */
        return FAILDESCR;
    }
    /* Coerce to string then build literal pattern */
    DESCR_t s = VARVAL_d_fn(d);
    if (s.v == DT_FAIL) return FAILDESCR;
    {
        extern DESCR_t pat_lit(const char *s);
        const char *sp = (s.v == DT_SNUL || !s.s) ? "" : s.s;
        return pat_lit(sp);
    }
}

/* ── VARVUP_fn ────────────────────────────────────────────────────────────
 *
 * SIL VARVUP (line 2867): evaluate argument as uppercase STRING.
 * VARVAL_d_fn + case-fold if &CASE == 0 (default: fold to upper).
 * ─────────────────────────────────────────────────────────────────────── */
DESCR_t VARVUP_fn(DESCR_t d)
{
    d = VARVAL_d_fn(d);
    if (d.v == DT_FAIL) return FAILDESCR;
    /* &CASE: 0 = case-fold (default), non-zero = case-sensitive */
    extern int64_t kw_case;
    if (kw_case != 0) return d;   /* case-sensitive — no fold */
    const char *s = (d.v == DT_SNUL || !d.s) ? "" : d.s;
    size_t len = strlen(s);
    char *up = GC_malloc(len + 1);
    for (size_t i = 0; i <= len; i++)
        up[i] = (char)toupper((unsigned char)s[i]);
    return STRVAL(up);
}
