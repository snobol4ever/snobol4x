/*
 * sil_strings.c — string helper procedures (v311.sil §5 string ops + §4 DTREP string ops)
 *
 * Faithful C translation of the SIL string macro operations.
 * See sil_strings.h for full API documentation.
 *
 * Source oracle: v311.sil (CSNOBOL4 2.3.3, Phil Budne)
 * Reference C:   snobol4-2.3.3/lib/str.c, lib/lexcmp.c,
 *                lib/c99/intspc.c, lib/c99/spcint.c,
 *                lib/generic/spreal.c, lib/realst.c,
 *                include/macros.h (X_LOCSP, X_REMSP, X_REMSP)
 *
 * Platform: 32-bit clean (-m32).  int_t = int32_t.  real_t = float.
 * Arena: SP_PTR(sp) = arena_base + sp->a + sp->o.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M3
 */

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sil_types.h"
#include "sil_strings.h"

/* ── internal helper: raw pointer to first byte of a specifier ───────── */
static inline char *sp_ptr(const SPEC_t *sp)
{
    return (char *)A2P(sp->a) + sp->o;
}

/* ════════════════════════════════════════════════════════════════════════
 * APDSP — append specifier STR to buffer specifier BASE
 * v311.sil APDSP macro → lib/str.c apdsp()
 * ════════════════════════════════════════════════════════════════════════ */
void APDSP_fn(SPEC_t *base, const SPEC_t *str)
{
    int32_t len = str->l;
    if (len <= 0) return;

    char *src = sp_ptr(str);
    char *dst = sp_ptr(base) + base->l;   /* append after existing content */
    base->l += len;

    if (len >= 4)
        memcpy(dst, src, (size_t)len);
    else {
        while (len-- > 0) *dst++ = *src++;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * REMSP — remove leading match from specifier
 * v311.sil X_REMSP(A,B,C): A = B advanced past C.l bytes
 * ════════════════════════════════════════════════════════════════════════ */
void REMSP_fn(SPEC_t *dst, const SPEC_t *src, const SPEC_t *match)
{
    /* copy all fields from src first (handles dst == src alias) */
    SPEC_t tmp = *src;
    tmp.o += match->l;
    tmp.l -= match->l;
    *dst = tmp;
}

/* ════════════════════════════════════════════════════════════════════════
 * TRIMSP — trim trailing blanks
 * v311.sil TRIMSP → lib/str.c trimsp()
 * ════════════════════════════════════════════════════════════════════════ */
void TRIMSP_fn(SPEC_t *dst, const SPEC_t *src)
{
    int32_t len = src->l;
    const char *cp = sp_ptr(src) + len - 1;
    while (len > 0 && (unsigned char)*cp == ' ') { len--; cp--; }
    *dst = *src;
    dst->l = len;
}

/* ════════════════════════════════════════════════════════════════════════
 * LEXCMP — lexicographic compare
 * v311.sil LEXCMP → lib/lexcmp.c lexcmp()
 * Returns < 0 / 0 / > 0.
 * ════════════════════════════════════════════════════════════════════════ */
int LEXCMP_fn(const SPEC_t *a, const SPEC_t *b)
{
    int32_t i = a->l, j = b->l;
    const char *x = sp_ptr(a), *y = sp_ptr(b);
    while (i > 0 && j > 0) {
        if (*x != *y) return (unsigned char)*x - (unsigned char)*y;
        x++; y++; i--; j--;
    }
    return i - j;
}

/* ════════════════════════════════════════════════════════════════════════
 * SPCINT — parse integer from specifier
 * v311.sil SPCINT → lib/c99/spcint.c spcint()
 * Adaption: we use int32_t (not int64_t); always strip leading whitespace.
 * ════════════════════════════════════════════════════════════════════════ */
SilResult SPCINT_fn(DESCR_t *dp, const SPEC_t *sp)
{
    char buf[32];
    size_t len = (size_t)sp->l;
    const char *cp = sp_ptr(sp);
    long val;
    char *end;

    /* strip leading whitespace (SPITBOL-compatible, always) */
    while (len > 0 && (*cp == ' ' || *cp == '\t')) { cp++; len--; }

    if (len == 0) return FAIL;
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    memcpy(buf, cp, len);
    buf[len] = '\0';

    errno = 0;
    val = strtol(buf, &end, 10);
    if (*end != '\0') return FAIL;
    if (errno == ERANGE) return FAIL;

    dp->a.i = (int32_t)val;
    dp->f   = 0;
    dp->v   = I;
    return OK;
}

/* ════════════════════════════════════════════════════════════════════════
 * SPREAL — parse real from specifier
 * v311.sil SPREAL → lib/generic/spreal.c spreal()
 * Adaption: result stored as float (real_t = float), not double.
 * ════════════════════════════════════════════════════════════════════════ */
SilResult SPREAL_fn(DESCR_t *dp, const SPEC_t *sp)
{
    /* sentinel-terminate trick from lib/generic/spreal.c */
#define SPREAL_TC  '|'
    char buf[66];
    size_t len = (size_t)sp->l;
    const char *cp = sp_ptr(sp);
    double d;
    char t;

    /* strip leading whitespace */
    while (len > 0 && (*cp == ' ' || *cp == '\t')) { cp++; len--; }

    if (len == 0) {
        dp->a.f = 0.0f;
        dp->f   = 0;
        dp->v   = R;
        return OK;
    }

    if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
    memcpy(buf, cp, len);
    buf[len++] = SPREAL_TC;
    buf[len]   = '\0';

    if (sscanf(buf, "%lf%c", &d, &t) != 2 || t != SPREAL_TC) return FAIL;

    dp->a.f = (real_t)d;
    dp->f   = 0;
    dp->v   = R;
    return OK;
#undef SPREAL_TC
}

/* ════════════════════════════════════════════════════════════════════════
 * REALST — format real DESCR to specifier (static buffer)
 * v311.sil REALST → lib/realst.c realst()
 * sp->a is a raw pointer cast to int32_t — NOT an arena offset.
 * ════════════════════════════════════════════════════════════════════════ */
void REALST_fn(SPEC_t *sp, const DESCR_t *dp)
{
    static char strbuf[64];
    char *bp;

    sprintf(strbuf, "%g", (double)dp->a.f);

    /* ensure dot or exponent present (matching v311 behaviour) */
    bp = strbuf;
    while (*bp && (isdigit((unsigned char)*bp) || *bp == '-')) bp++;
    if (*bp == '\0') { *bp++ = '.'; *bp = '\0'; }

    sp->a    = (int32_t)(intptr_t)strbuf;   /* raw C ptr, not arena offset */
    sp->f    = 0;
    sp->v    = 0;
    sp->o    = 0;
    sp->l    = (int32_t)strlen(strbuf);
    sp->unused = 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * INTSPC — format integer DESCR to specifier (static buffer)
 * v311.sil INTSPC → lib/c99/intspc.c intspc()
 * sp->a is a raw pointer cast to int32_t — NOT an arena offset.
 * ════════════════════════════════════════════════════════════════════════ */
void INTSPC_fn(SPEC_t *sp, const DESCR_t *dp)
{
    static char strbuf[32];

    sprintf(strbuf, "%d", dp->a.i);

    sp->a    = (int32_t)(intptr_t)strbuf;   /* raw C ptr, not arena offset */
    sp->f    = 0;
    sp->v    = 0;
    sp->o    = 0;
    sp->l    = (int32_t)strlen(strbuf);
    sp->unused = 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * LOCSP — build specifier from a STRING block DESCR_t
 * v311.sil X_LOCSP(A, B) — macros.h
 * ════════════════════════════════════════════════════════════════════════ */
void LOCSP_fn(SPEC_t *sp, const DESCR_t *dp)
{
    if (dp->a.i == 0) {
        sp->l = 0;
        return;
    }
    /* title DESCR's V field holds string byte length */
    DESCR_t *title = (DESCR_t *)A2P(dp->a.i);
    sp->a    = dp->a.i;
    sp->f    = dp->f;
    sp->v    = dp->v;
    sp->o    = BCDFLD;
    sp->l    = title->v;
    sp->unused = 0;
}

/* ════════════════════════════════════════════════════════════════════════
 * SUBSP — substring specifier
 * ════════════════════════════════════════════════════════════════════════ */
void SUBSP_fn(SPEC_t *dst, const SPEC_t *src, int32_t off, int32_t len)
{
    *dst   = *src;
    dst->o = src->o + off;
    dst->l = len;
}
