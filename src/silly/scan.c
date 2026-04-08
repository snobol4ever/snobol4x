/*
 * sil_scan.c — Pattern Matching Procedures (v311.sil §11 lines 3323–4239)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §11.
 * Top-level procs: SCAN_fn, SJSR_fn, SCNR_fn.
 * 36 XPROC sub-procedures dispatched via PATBRA/SELBRA.
 *
 * Control flow:
 *   SIL BRANCH SALT,SCNR  → longjmp(ctx->salt_jmp, 1)
 *   SIL BRANCH SALF,SCNR  → longjmp(ctx->salf_jmp, 1)
 *   SIL BRANCH SCOK,SCNR  → longjmp(ctx->scok_jmp, 1)
 *   Global FAIL exit       → longjmp(ctx->fail_jmp, 1)
 *   TSALT/TSALF/TSCOK      → same targets (aliases in LEN/POS section)
 *
 * SPEC_t field layout (sil_types.h):
 *   .l = length, .o = byte-offset into string block, .a = arena offset,
 *   .v = type (S=1), .f = flags (MBZ).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M8
 */

#include <stdint.h>
#include <string.h>
#include <setjmp.h>

#include "types.h"
#include "data.h"
#include "scan.h"
#include "argval.h"
#include "patval.h"
#include "strings.h"
#include "arena.h"
#include "symtab.h"

/* ── External stubs: runtime functions not yet implemented ───────────── */
/* These are declared here so sil_scan.c compiles (-c); they will be     */
/* resolved at link time when their milestone is complete.               */
extern RESULT_t NMD_fn(void);
extern RESULT_t INVOKE_fn(void);
extern RESULT_t PUTIN_fn(DESCR_t zptr, DESCR_t wptr);
extern void       PUTOUT_fn(DESCR_t yptr, DESCR_t val);
extern RESULT_t TRPHND_fn(DESCR_t atptr);
extern void       maknod_scalar(DESCR_t *out, int32_t blk_off,
                                int32_t len_val, int32_t alt_val,
                                int32_t fn_idx, int32_t arg_off);
extern int32_t    lvalue_scalar(int32_t pat_off);
extern void       cpypat_scalar(int32_t dst_off, int32_t src_off,
                                int32_t link_val, int32_t dst_base,
                                int32_t src_base, int32_t count);
extern int32_t    getbal_fn(SPEC_t *sp, int32_t maxlen);
extern int32_t    stream_fn(SPEC_t *res, const SPEC_t *src,
                             const DESCR_t *table);
extern int        xany_fn(const SPEC_t *subj, const DESCR_t *set);
extern void       plugtb_fn(DESCR_t *table, DESCR_t sentinel,
                             const SPEC_t *chars);
extern void       clertb_fn(DESCR_t *table, DESCR_t fill);
extern int        deql_fn(DESCR_t a, DESCR_t b);
extern void       realst_fn(SPEC_t *sp, DESCR_t dp);
extern void       intspc_fn(SPEC_t *sp, DESCR_t dp);

/* ── Globals defined here ────────────────────────────────────────────── */
Scan_ctx *scan_ctx_g = NULL;

/* ── SPEC helpers using real field names ─────────────────────────────── */

/* SP_OFF / SP_ARN — additional SPEC_t field accessors */

/* sp_getbytes — raw pointer to first byte of specifier's string */
static inline const char *sp_bytes(const SPEC_t *sp)
{
    return (const char *)A2P(sp->a) + sp->o;
}

/*====================================================================================================================*/
/* sp_setfrom_descr — fill a SPEC_t from a STRING DESCR (LOCSP) */
static inline void sp_setfrom_descr(SPEC_t *sp, DESCR_t d)
{
    sp->a = D_A(d); /* arena offset of block */
    sp->l = D_V(d); /* length stored in V    */
    sp->o = 0;
    sp->v = S;
    sp->f = 0;
}

/*====================================================================================================================*/
/* sp_copy — SETSP */
static inline void sp_copy(SPEC_t *dst, SPEC_t src) { *dst = src; }

/* sp_addlg — ADDLG(sp, n_descr): add A field of n to sp.l */
static inline void sp_addlg(SPEC_t *sp, DESCR_t n) { sp->l += D_A(n); }

/* sp_addlg_c — ADDLG with constant */
static inline void sp_addlg_c(SPEC_t *sp, int32_t n) { sp->l += n; }

/* sp_setlc — SETLC(sp, n) */
static inline void sp_setlc(SPEC_t *sp, int32_t n) { sp->l = n; }

/* sp_putlg — PUTLG(sp, src_d): set sp.l from A field of src */
static inline void sp_putlg(SPEC_t *sp, DESCR_t src) { sp->l = D_A(src); }

/* sp_getlg — GETLG(dst_d, sp): store sp.l into A field of dst */
static inline void sp_getlg(DESCR_t *dst, SPEC_t sp) { D_A(*dst) = sp.l; }

/* sp_leqlc — LEQLC(sp, n): sp.l == n */
static inline int sp_leqlc(SPEC_t sp, int32_t n) { return sp.l == n; }

/* sp_lcomp_lt — LCOMP: a.l < b.l */
static inline int sp_lcomp_lt(SPEC_t a, SPEC_t b) { return a.l < b.l; }
static inline int sp_lcomp_le(SPEC_t a, SPEC_t b) { return a.l <= b.l; }

/* sp_remsp — REMSP(res, a, b): res = a with .l = a.l - b.l, .o advanced */
static inline void sp_remsp(SPEC_t *res, SPEC_t a, SPEC_t b)
{
    *res = a;
    res->l = a.l - b.l;
    res->o = a.o + b.l;
}

/*====================================================================================================================*/
/* sp_subsp — SUBSP(res, a, b): res = a capped at min(a.l, b.l).
 * Returns 1 if a.l >= b.l (no short), 0 if a.l < b.l (failure path). */
static inline int sp_subsp(SPEC_t *res, SPEC_t a, SPEC_t b)
{
    *res = a;
    res->l = a.l < b.l ? a.l : b.l;
    return a.l >= b.l;
}

/*====================================================================================================================*/
/* sp_fshrtn — FSHRTN(sp, n): delete n leading chars */
static inline void sp_fshrtn(SPEC_t *sp, int32_t n)
{
    sp->o += n;
    sp->l -= n;
}

/*====================================================================================================================*/
/* sp_lexcmp — LEXCMP via LEXCMP_fn */
static inline int sp_lexcmp(SPEC_t a, SPEC_t b)
{
    return LEXCMP_fn(&a, &b);
}

/*====================================================================================================================*/
/* ── Descriptor helpers ──────────────────────────────────────────────── */
#define GETD_BLK(dst, base_d, off_d)  \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))

#define GETDC_BLK(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i),    sizeof(DESCR_t))

#define PUTDC_BLK(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),    sizeof(DESCR_t))

/* ── Operand stacks ──────────────────────────────────────────────────── */
#define OPSTACK_MAX   256
#define SPECSTACK_MAX  64
static DESCR_t opstack[OPSTACK_MAX];
static int     optop = 0;
static SPEC_t  spstack[SPECSTACK_MAX];
static int     sptop = 0;

static inline void   opush(DESCR_t d)  { opstack[optop++] = d; }
static inline DESCR_t opop(void)       { return opstack[--optop]; }
static inline void   spush_sp(SPEC_t s){ spstack[sptop++] = s; }
static inline SPEC_t spop_sp(void)     { return spstack[--sptop]; }

/* getac — read int32_t from arena block at base+off into dst.a         */
static inline void getac(DESCR_t *dst, DESCR_t base, int32_t off)
{
    const char *p = (const char *)A2P(D_A(base)) + off;
    memcpy(&D_A(*dst), p, sizeof(int32_t));
}

/*====================================================================================================================*/
/* ── longjmp aliases ─────────────────────────────────────────────────── */
#define GOTO_SALT   longjmp(scan_ctx_g->salt_jmp, 1)
#define GOTO_SALF   longjmp(scan_ctx_g->salf_jmp, 1)
#define GOTO_SCOK   longjmp(scan_ctx_g->scok_jmp, 1)
#define GOTO_FAIL   longjmp(scan_ctx_g->fail_jmp, 1)
/* T-prefixed aliases (LEN/POS section) map to same targets */
#define GOTO_TSALT  GOTO_SALT
#define GOTO_TSALF  GOTO_SALF
#define GOTO_TSCOK  GOTO_SCOK

/* ── PDL push (3 DESCRs per history entry) ───────────────────────────── */
static void pdl_push3(DESCR_t d0, DESCR_t d1, DESCR_t d2)
{
    INCRA(PDLPTR, 3*DESCR);
    if (D_A(PDLPTR) >= D_A(PDLEND)) GOTO_FAIL;
    PUTDC_BLK(PDLPTR, 0, d0);
    PUTDC_BLK(PDLPTR, DESCR, d1);
    PUTDC_BLK(PDLPTR, 2*DESCR, d2);
}

/*====================================================================================================================*/
/* ── Error handler (SCERST path) ─────────────────────────────────────── */
static void scan_error(int32_t code)
{
    SETAC(ERRTYP, code);
    GOTO_TSALF;
}

/*====================================================================================================================*/
/* ── Forward declarations for sub-procedures ─────────────────────────── */
static void do_ABNS(void);
static void do_LPRRT(void);
static void do_SCIN(void);
static void do_SCIN1A(void);
static void do_SCIN2(void);
static void do_ONAR2(void);
static void do_BAL_inner(void);
static void do_FNME_inner(void);
static void do_ENME3(void);

static void do_ANYC(void);
static void do_ARBF(void);
static void do_ARBN(void);
static void do_ATP(void);
static void do_CHR(void);
static void do_BAL(void);
static void do_BALF(void);
static void do_BRKC(void);
static void do_BRKX(void);
static void do_BRKXF(void);
static void do_DNME(void);
static void do_DNME1(void);
static void do_EARB(void);
static void do_DSAR(void);
static void do_ENME(void);
static void do_ENMI(void);
static void do_FARB(void);
static void do_FNME(void);
static void do_LNTH(void);
static void do_NME(void);
static void do_NNYC(void);
static void do_ONAR(void);
static void do_ONRF(void);
static void do_POSI(void);
static void do_RPSI(void);
static void do_RTB(void);
static void do_FAIL_d(void);
static void do_SALF_d(void);
static void do_SCOK_d(void);
static void do_SCON(void);
static void do_SPNC(void);
static void do_STAR(void);
static void do_TB(void);
static void do_RTNUL3(void);
static void do_FNCE(void);
static void do_SUCF(void);

/* ── Dispatch table ──────────────────────────────────────────────────── */
scan_fn_t scan_dispatch[SCAN_DISPATCH_SZ] = {
    do_ANYC,   do_ARBF,   do_ARBN,   do_ATP,    do_CHR,
    do_BAL,    do_BALF,   do_BRKC,   do_BRKX,   do_BRKXF,
    do_DNME,   do_DNME1,  do_EARB,   do_DSAR,   do_ENME,
    do_ENMI,   do_FARB,   do_FNME,   do_LNTH,   do_NME,
    do_NNYC,   do_ONAR,   do_ONRF,   do_POSI,   do_RPSI,
    do_RTB,    do_FAIL_d, do_SALF_d, do_SCOK_d, do_SCON,
    do_SPNC,   do_STAR,   do_TB,     do_RTNUL3, do_FNCE,
    do_SUCF,
};

/* ── SCIN2 — main pattern dispatch loop ─────────────────────────────── */
static void do_SCIN2(void)
{
    for (;;) {
        DESCR_t ZCL_l, PTBRCL;
        SETAC(LENFCL, 1);
        INCRA(PATICL, DESCR); /* fetch 3 DESCRs from pattern block at current offset */
        GETD_BLK(ZCL_l, PATBCL, PATICL); /* function code   */
        INCRA(PATICL, DESCR);
        GETD_BLK(XCL, PATBCL, PATICL); /* then-or link    */
        INCRA(PATICL, DESCR);
        GETD_BLK(YCL, PATBCL, PATICL); /* value/residual  */
        { /* push history: then-or, cursor, LENFCL */
            DESCR_t cur; sp_getlg(&cur, TXSP);
            pdl_push3(XCL, cur, LENFCL);
        }
        if (!AEQLC(FULLCL, 0)) { /* FULLSCAN: CHKVAL MAXLEN,YCL,TXSP,SALT */
            int32_t cur = TXSP.l, res = D_A(YCL), mx = D_A(MAXLEN);
            if (cur + res > mx) GOTO_SALT;
        }
        GETDC_BLK(PTBRCL, ZCL_l, 0); /* GETDC PTBRCL,ZCL,0 — function index */
        {
            int32_t idx = D_A(PTBRCL);
            if ((uint32_t)idx >= SCAN_DISPATCH_SZ) scan_error(SCAN_ERR_ILLEGAL_TYPE);
            scan_dispatch[idx]();
        }
    }
}

/*====================================================================================================================*/
static void do_SCIN(void)  { SETAC(UNSCCL, 0); do_SCIN1A(); }

static void do_SCIN1A(void)
{
    MOVD(PATBCL, YPTR);
    if (!AEQLC(UNSCCL, 0)) { SETAC(UNSCCL, 0); MOVD(PATBCL, YPTR); }
    SETAC(PATICL, 0);
    do_SCIN2();
}

/*====================================================================================================================*/
/* ── SCNR — inner scanning engine ───────────────────────────────────── */
RESULT_t SCNR_fn(void)
{
    Scan_ctx ctx;
    Scan_ctx *prev = scan_ctx_g;
    scan_ctx_g = &ctx;
    D_A(MAXLEN) = XSP.l; /* GETLG MAXLEN,XSP */
    DESCR_t YSIZ_l; SETAC(YSIZ_l, lvalue_scalar(D_A(YPTR))); /* LVALUE YSIZ,YPTR  — min match length of pattern */
    if (!AEQLC(FULLCL, 0)) { /* AEQLC FULLCL,0,SCNR1 — if fullscan off, check min vs max */
        if (D_A(YSIZ_l) > D_A(MAXLEN)) { scan_ctx_g = prev; return FAIL; }
    }
    sp_copy(&TXSP, XSP); sp_setlc(&TXSP, 0); /* SETSP TXSP,XSP; SETLC TXSP,0 */
    MOVD(PDLPTR, PDLHED);
    MOVD(NAMICL, NHEDCL);
    if (AEQLC(ANCCL, 0)) { /* AEQLC ANCCL,0,SCNR3 — non-anchored? */
        sp_setlc(&HEADSP, 0); /* anchored — SCNR3 */
        { DESCR_t cur; DESCR_t lf; sp_getlg(&cur, TXSP);
          MOVD(lf, LENFCL); SETAC(LENFCL, 1);
          pdl_push3(SCFLCL, cur, lf); }
    } else {
        if (!AEQLC(FULLCL, 0)) /* non-anchored — compute max advance count */
            D_A(YSIZ_l) = D_A(MAXLEN);
        else
            D_A(YSIZ_l) = D_A(MAXLEN) - D_A(YSIZ_l);
        D_A(YSIZ_l) += D_A(CHARCL); /* +1 */
        opush(YPTR); opush(YSIZ_l);
        sp_copy(&HEADSP, TXSP);
        { DESCR_t cur; DESCR_t lf;
          sp_getlg(&cur, TXSP); MOVD(lf, LENFCL); SETAC(LENFCL, 1);
          pdl_push3(SCONCL, cur, lf); }
    }
    int s_ok, s_alt, s_alf, s_fl; /* ── setjmp targets ─────────────────────────────────────────── */
    if ((s_fl = setjmp(ctx.fail_jmp))) goto scan_fail;
    if ((s_ok = setjmp(ctx.scok_jmp))) goto scan_ok;
    if ((s_alt = setjmp(ctx.salt_jmp))) goto scan_alt;
    if ((s_alf = setjmp(ctx.salf_jmp))) goto scan_alf;
    do_SCIN();
    goto scan_fail; /* do_SCIN never returns normally — it loops via do_SCIN2 forever  and exits only via longjmp. Silence the no-return warning: */
scan_ok:
    SETAV(PATICL, XCL); /* SCOK: SETAV PATICL,XCL; AEQLC PATICL,0 → SCIN2 else RTN2 */
    if (AEQLC(PATICL, 0)) {
        if (setjmp(ctx.fail_jmp)) goto scan_fail; /* re-enter dispatch loop — reinstate setjmps first */
        if (setjmp(ctx.scok_jmp)) goto scan_ok;
        if (setjmp(ctx.salt_jmp)) goto scan_alt;
        if (setjmp(ctx.salf_jmp)) goto scan_alf;
        do_SCIN2();
        goto scan_fail;
    }
    scan_ctx_g = prev; return OK;
scan_alt: /* SALT1: GETDC LENFCL,PDLPTR,3*DESCR — read lenfcl from PDL, then SALT2 */
    GETDC_BLK(LENFCL, PDLPTR, 2*DESCR);
    goto scan_backtrack; /* SALT2 */
scan_alf: /* SALF1: SETAC LENFCL,0; then SALT2 */
    SETAC(LENFCL, 0);
scan_backtrack: /* SALT2 */
    {
        DESCR_t XCL2, YCL2;
        GETDC_BLK(XCL2, PDLPTR, 0);
        GETDC_BLK(YCL2, PDLPTR, DESCR);
        DECRA(PDLPTR, 3*DESCR);
        MOVD(PATICL, XCL2);
        if (!AEQLC(PATICL, 0)) { /* AEQLC PATICL,0,,SALT3 — non-zero: resume OR link */
            sp_putlg(&TXSP, YCL2); /* PUTLG TXSP,YCL — restore cursor */
            MOVD(XCL, XCL2); MOVD(YCL, YCL2);
            if (setjmp(ctx.fail_jmp)) goto scan_fail; /* reinstate setjmps */
            if (setjmp(ctx.scok_jmp)) goto scan_ok;
            if (setjmp(ctx.salt_jmp)) goto scan_alt;
            if (setjmp(ctx.salf_jmp)) goto scan_alf;
            if (TESTF(PATICL, FNC)) { /* TESTF PATICL,FNC,SCIN3 */
                do_SCIN2(); /* SCIN3 re-enters dispatch from PATICL directly */
            } else {
                DESCR_t PTBRCL; GETDC_BLK(PTBRCL, PATICL, 0);
                int32_t idx = D_A(PTBRCL);
                if ((uint32_t)idx < SCAN_DISPATCH_SZ) scan_dispatch[idx]();
                do_SCIN2();
            }
            goto scan_fail;
        }
        /* SALT3: PATICL==0 — no OR link; check lenfcl */
        if (AEQLC(LENFCL, 0)) { /* AEQLC LENFCL,0,SALT1 — re-read lenfcl from PDL */
            GETDC_BLK(LENFCL, PDLPTR, 2*DESCR); /* SALT1 */
            goto scan_backtrack; /* SALT2 again */
        }
        /* else SALF1: SETAC LENFCL,0 then SALT2 */
        SETAC(LENFCL, 0);
        goto scan_backtrack;
    }
scan_fail:
    scan_ctx_g = prev;
    return FAIL;
}

/*====================================================================================================================*/
/* ── SCAN — top-level pattern match (no replacement) ────────────────── */
RESULT_t SCAN_fn(void)
{
    if (ARGVAL_fn() == FAIL) return FAIL; /* RCALL XPTR,ARGVAL,,FAIL */
    opush(XPTR);
    if (PATVAL_fn() == FAIL) { optop--; return FAIL; } /* RCALL YPTR,PATVAL,,FAIL */
    XPTR = opop();
    SETAV(DTCL, XPTR); MOVV(DTCL, YPTR);
    INCRA(SCNCL, 1);
    if (D_V(XPTR) == I) { /* Coerce subject to string if integer or real */
        int32_t off = GNVARI_fn(D_A(XPTR));
        if (!off) return FAIL;
        D_A(XPTR) = off; D_V(XPTR) = S;
    }
    if (D_V(XPTR) == R) {
        SPEC_t rsp; realst_fn(&rsp, XPTR);
        int32_t off = GENVAR_fn(&rsp);
        if (!off) return FAIL;
        D_A(XPTR) = off; D_V(XPTR) = S;
    }
    if (D_V(XPTR) == S && D_V(YPTR) == S) { /* STRING-STRING: inline scan */
        sp_setfrom_descr(&XSP, XPTR);
        sp_setfrom_descr(&YSP, YPTR);
        for (;;) { /* SCANVB loop */
            SPEC_t TSP2;
            if (!sp_subsp(&TSP2, YSP, XSP)) return FAIL;
            if (sp_lexcmp(TSP2, YSP) == 0) return OK;
            if (AEQLC(ANCCL, 0)) return FAIL;
            sp_fshrtn(&XSP, 1);
        }
    }
    sp_setfrom_descr(&XSP, XPTR); /* Pattern case: run SCNR */
    if (SCNR_fn() == FAIL) return FAIL;
    if (NMD_fn() == FAIL) return FAIL;
    if (sp_lcomp_lt(TXSP, HEADSP) || TXSP.l == HEADSP.l) /* LCOMP TXSP,HEADSP,SCANV1,SCANV1 */
        sp_remsp(&XSP, TXSP, HEADSP);  /* SCANV1: REMSP XSP,TXSP,HEADSP */
    else
        sp_remsp(&XSP, HEADSP, TXSP);  /* SCANV2: REMSP XSP,HEADSP,TXSP */
    { /* RCALL YPTR,GENVAR,XSPPTR,RTYPTR */
        int32_t off = GENVAR_fn(&XSP);
        if (!off) return FAIL;
        D_A(YPTR) = off; D_V(YPTR) = S;
    }
    return OK;
}

/*====================================================================================================================*/
/* ── SJSR — pattern match with replacement ───────────────────────────── */
RESULT_t SJSR_fn(void)
{
    INCRA(OCICL, DESCR); /* INCRA OCICL,DESCR; GETD WPTR,OCBSCL,OCICL */
    GETD_BLK(WPTR, OCBSCL, OCICL);
    if (!TESTF(WPTR, FNC)) {
        if (!AEQLC(INSW, 0)) { /* SJSR1 */
            int32_t assoc = locapv_fn(D_A(INATL), &WPTR);
            if (assoc) {
                GETDC_BLK(ZPTR, WPTR, DESCR); /* approx — needs GETDC */
                PUTIN_fn(ZPTR, WPTR); /* ignore fail for now  */
            }
        }
        GETDC_BLK(XPTR, WPTR, DESCR);
    } else {
        if (INVOKE_fn() == FAIL) return FAIL;
        GETDC_BLK(XPTR, WPTR, DESCR);
    }
    opush(WPTR); opush(XPTR);
    if (PATVAL_fn() == FAIL) { optop -= 2; return FAIL; }
    XPTR = opop(); WPTR = opop();
    if (D_V(XPTR) == I) { /* Coerce subject */
        int32_t off = GNVARI_fn(D_A(XPTR));
        if (!off) return FAIL;
        D_A(XPTR) = off; D_V(XPTR) = S;
    }
    if (D_V(XPTR) == R) {
        SPEC_t rsp; realst_fn(&rsp, XPTR);
        int32_t off = GENVAR_fn(&rsp);
        if (!off) return FAIL;
        D_A(XPTR) = off; D_V(XPTR) = S;
    }
    sp_setfrom_descr(&XSP, XPTR);
    if (D_V(YPTR) == S) { /* STRING-STRING inline */
        sp_setfrom_descr(&YSP, YPTR);
        sp_copy(&HEADSP, XSP); sp_setlc(&HEADSP, 0);
        SPEC_t TSP2;
        for (;;) {
            if (!sp_subsp(&TSP2, YSP, XSP)) goto sjss_fail;
            if (sp_lexcmp(TSP2, YSP) != 0) { SETAC(NAMGCL, 0); break; }
            if (AEQLC(ANCCL, 0)) goto sjss_fail;
            sp_addlg(&HEADSP, ONECL);
            sp_fshrtn(&XSP, 1);
        }
        sp_remsp(&TAILSP, XSP, TSP2);
        goto sjss1;
    }
    if (SCNR_fn() == FAIL) goto sjss_fail; /* STRING-PATTERN via SCNR */
    SETAC(NAMGCL, 1);
    sp_remsp(&TAILSP, XSP, TXSP);
sjss1:
    spush_sp(TAILSP); spush_sp(HEADSP);
    if (!AEQLC(NAMGCL, 0)) {
        if (NMD_fn() == FAIL) { sptop -= 2; return FAIL; }
    }
    if (ARGVAL_fn() == FAIL) { sptop -= 2; return FAIL; }
    MOVD(ZPTR, XPTR);
    HEADSP = spop_sp(); TAILSP = spop_sp();
    WPTR = opop();
    { /* Build replacement */
        int32_t zt = D_V(ZPTR);
        if (zt == S) goto sjsrv;
        if (zt == I) { SPEC_t isp; intspc_fn(&isp, ZPTR); ZSP = isp; goto sjsrs; }
        if (zt == R) { SPEC_t rsp; realst_fn(&rsp, ZPTR); ZSP = rsp; goto sjsrs; }
        if (zt == P) goto sjsrp;
        { /* E or other — allocate STARPT wrapper */
            int32_t off = BLOCK_fn(D_A(STARSZ), P);
            if (!off) return FAIL;
            SETAC(TPTR, off);
            memcpy(A2P(off), A2P(D_A(STRPAT)), (size_t)D_A(STARSZ));
            PUTDC_BLK(TPTR, 4*DESCR, ZPTR);
            MOVD(ZPTR, TPTR);
        }
sjsrp:
        {
            int32_t hoff = GENVAR_fn(&HEADSP); /* Build head-object-tail pattern concatenation */
            if (!hoff) return FAIL;
            D_A(XPTR) = hoff; D_V(XPTR) = S;
            int32_t hlen = HEADSP.l;
            int32_t blk1 = BLOCK_fn(D_A(LNODSZ), P);
            if (!blk1) return FAIL;
            maknod_scalar(&XPTR, blk1, hlen, 0, SCAN_IDX_CHR, hoff);
            int32_t toff = GENVAR_fn(&TAILSP);
            if (!toff) return FAIL;
            D_A(YPTR) = toff; D_V(YPTR) = S;
            int32_t tlen = TAILSP.l;
            int32_t blk2 = BLOCK_fn(D_A(LNODSZ), P);
            if (!blk2) return FAIL;
            maknod_scalar(&YPTR, blk2, tlen, 0, SCAN_IDX_CHR, toff);
            int32_t xsz = x_bksize(D_A(XPTR));
            int32_t ysz = x_bksize(D_A(YPTR));
            int32_t zsz = x_bksize(D_A(ZPTR));
            int32_t tot = xsz + ysz + zsz;
            int32_t pblk = BLOCK_fn(tot, P);
            if (!pblk) return FAIL;
            SETAC(TPTR, pblk);
            int32_t lv_z = lvalue_scalar(D_A(ZPTR));
            int32_t lv_y = lvalue_scalar(D_A(YPTR));
            cpypat_scalar(pblk, D_A(XPTR), lv_z, 0, 0, xsz);
            cpypat_scalar(pblk, D_A(ZPTR), lv_y, xsz, 0, zsz);
            cpypat_scalar(pblk, D_A(YPTR), 0, xsz+zsz, 0, ysz);
            SETAC(ZPTR, pblk); D_V(ZPTR) = P;
            goto sjsrv1;
        }
sjsrv:
        sp_setfrom_descr(&ZSP, ZPTR);
sjsrs:
        {
            int32_t total = TAILSP.l + HEADSP.l + ZSP.l;
            if (total > D_A(MLENCL)) { scan_error(SCAN_ERR_STR_OVERFLOW); }
            int32_t soff = CONVAR_fn(total);
            if (!soff) return FAIL;
            sp_setfrom_descr(&TSP, ZPTR); sp_setlc(&TSP, 0);
            APDSP_fn(&TSP, &HEADSP);
            APDSP_fn(&TSP, &ZSP);
            APDSP_fn(&TSP, &TAILSP);
            int32_t goff = GNVARS_fn(sp_bytes(&TSP), total);
            if (!goff) return FAIL;
            D_A(ZPTR) = goff; D_V(ZPTR) = S;
        }
sjsrv1:
        PUTDC_BLK(WPTR, DESCR, ZPTR);
        if (!AEQLC(OUTSW, 0)) {
            int32_t assoc = locapv_fn(D_A(OUTATL), &WPTR);
            if (assoc) {
                GETDC_BLK(YPTR, WPTR, DESCR);
                PUTOUT_fn(YPTR, ZPTR);
            }
        }
        if (ACOMPC(TRAPCL, 0) <= 0) {
            int32_t assoc = locapt_fn(D_A(TVALL), &WPTR);
            if (assoc) {
                SETAC(ATPTR, assoc);
                TRPHND_fn(ATPTR);
            }
        }
        return OK;
    }
sjss_fail:
    return FAIL;
}

/*====================================================================================================================*/
/* ════════════════════════════════════════════════════════════════════════
 * XPROC sub-procedures
 * ════════════════════════════════════════════════════════════════════════ */

/* BRKC/BRKX/NNYC/SPNC/ANYC — set SCL then fall into ABNS */
static void do_BRKC(void) { SETAC(SCL, 2); do_ABNS(); }
static void do_BRKX(void) { SETAC(SCL, 5); do_ABNS(); }
static void do_NNYC(void) { SETAC(SCL, 3); do_ABNS(); }
static void do_SPNC(void) { SETAC(SCL, 4); do_ABNS(); }
static void do_ANYC(void) { SETAC(SCL, 1); do_ABNS(); }

static void do_ABNS(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(XPTR, PATBCL, PATICL);
abns1:
    switch (D_V(XPTR)) {
    case S: break; /* abnsv */
    case E:
        opush(SCL);
        if (EXPVAL_fn() == FAIL) { SCL = opop(); GOTO_SALF; }
        SCL = opop();
        goto abns1;
    case I:
        { int32_t off = GNVARI_fn(D_A(XPTR));
          if (!off) GOTO_SALF;
          D_A(XPTR) = off; D_V(XPTR) = S; }
        break;
    default:
        scan_error(SCAN_ERR_ILLEGAL_TYPE);
    }
    if (D_A(XPTR) == 0) scan_error(SCAN_ERR_NULL_STRING); /* abnsv: AEQLC XPTR,0,,SCNAME — null string */
    {
        int32_t scl = D_A(SCL);
        if (scl == 4) goto span_path;
        if (!deql_fn(XPTR, TBLBCS)) { /* BREAK / BREAKX / ANY / NOTANY — build stream table if needed */
            clertb_fn(&BRKTB, CONTIN);
            sp_setfrom_descr(&YSP, XPTR);
            plugtb_fn(&BRKTB, STOPSH, &YSP);
            MOVD(TBLBCS, XPTR);
        }
        sp_copy(&VSP, XSP); /* anyc3: set up VSP */
        if (!AEQLC(FULLCL, 0)) {
            sp_putlg(&VSP, MAXLEN);
            if (sp_lcomp_lt(VSP, TXSP)) GOTO_SALT;
            /* CHKVAL MAXLEN,ZEROCL,XSP,,ANYC4,ANYC4 — skip ADDLG if cur >= MAXLEN */
            if (TXSP.l < D_A(MAXLEN))
                sp_addlg_c(&VSP, 1);
        }
        sp_remsp(&YSP, VSP, TXSP);
        if (scl == 1 || scl == 3) {
            if (sp_leqlc(YSP, 0)) GOTO_SALT; /* ANY / NOTANY */
            int found = xany_fn(&YSP, &XPTR);
            if (scl == 1) { if (!found) GOTO_SALF; } /* ANY: must find */
            else { if ( found) GOTO_SALF; } /* NOTANY: must not */
            sp_addlg_c(&TXSP, 1);
            GOTO_SCOK;
        }
        { /* BREAK / BREAKX */
            SPEC_t ZSP2;
            int32_t acc = stream_fn(&ZSP2, &YSP, &BRKTB);
            if (acc < 0) GOTO_SALF;
            sp_addlg_c(&TXSP, acc);
            if (scl == 5) {
                DESCR_t zero; SETAC(zero, 0); /* BREAKX: push two extra history entries */
                DESCR_t pic; MOVD(pic, PATICL);
                pdl_push3(zero, pic, LENFCL);
                DESCR_t tmv; sp_getlg(&tmv, TXSP);
                pdl_push3(BRXFCL, tmv, LENFCL);
            }
            GOTO_SCOK;
        }
    }
span_path:
    if (!deql_fn(XPTR, TBLSCS)) {
        clertb_fn(&SPANTB, STOPSH);
        sp_setfrom_descr(&YSP, XPTR);
        plugtb_fn(&SPANTB, CONTIN, &YSP);
        MOVD(TBLSCS, XPTR);
    }
    if (sp_lcomp_lt(XSP, TXSP)) GOTO_SALT;
    sp_remsp(&YSP, XSP, TXSP);
    {
        SPEC_t ZSP2;
        int32_t acc = stream_fn(&ZSP2, &YSP, &SPANTB);
        if (acc <= 0) GOTO_SALF;
        if (!AEQLC(FULLCL, 0)) {
            int32_t cur = TXSP.l;
            if (cur + acc > D_A(MAXLEN)) GOTO_SALT;
        }
        sp_addlg_c(&TXSP, acc);
        GOTO_SCOK;
    }
}

/*====================================================================================================================*/
/* LNTH/POSI/RPSI/RTB/TB — LEN/POS/RPOS/RTAB/TAB */
static void do_LNTH(void) { SETAC(SCL, 1); do_LPRRT(); }
static void do_POSI(void) { SETAC(SCL, 2); do_LPRRT(); }
static void do_RPSI(void) { SETAC(SCL, 3); do_LPRRT(); }
static void do_RTB(void)  { SETAC(SCL, 4); do_LPRRT(); }
static void do_TB(void)   { SETAC(SCL, 5); do_LPRRT(); }

static void do_LPRRT(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(XPTR, PATBCL, PATICL);
    opush(SCL);
lprrt1:
    switch (D_V(XPTR)) {
    case I: goto lprrri;
    case E:
        if (EXPVAL_fn() == FAIL) { SCL = opop(); GOTO_TSALF; }
        SCL = opop(); goto lprrt1;
    case S:
        { SPEC_t zsp; sp_setfrom_descr(&zsp, XPTR);
          if (SPCINT_fn(&XPTR, &zsp) == FAIL) scan_error(SCAN_ERR_ILLEGAL_TYPE); }
        break;
    default:
        SCL = opop(); scan_error(SCAN_ERR_ILLEGAL_TYPE);
    }
lprrri:
    SCL = opop();
    {
        int32_t n = D_A(XPTR), scl = D_A(SCL);
        if (scl == 1) {
            if (n < 0) scan_error(SCAN_ERR_NEGATIVE); /* LEN */
            if (TXSP.l + n > D_A(MAXLEN)) GOTO_TSALT;
            sp_addlg_c(&TXSP, n);
            GOTO_TSCOK;
        }
        if (scl == 2) {
            if (n < 0) scan_error(SCAN_ERR_NEGATIVE); /* POS */
            if (n > D_A(MAXLEN)) GOTO_TSALT;
            if (n < TXSP.l) GOTO_TSALF;
            if (n > TXSP.l) GOTO_TSALT;
            GOTO_TSCOK;
        }
        if (scl == 3) {
            if (n < 0) scan_error(SCAN_ERR_NEGATIVE); /* RPOS */
            int32_t desired = XSP.l - n;
            if (TXSP.l < desired) GOTO_TSALT;
            if (TXSP.l > desired) GOTO_TSALF;
            GOTO_TSCOK;
        }
        if (scl == 4) {
            if (n < 0) scan_error(SCAN_ERR_NEGATIVE); /* RTAB */
            int32_t desired = XSP.l - n;
            if (TXSP.l > desired) GOTO_TSALT;
            if (!AEQLC(FULLCL, 0)) {
                int32_t resid = D_A(YCL);
                if (D_A(MAXLEN) - resid > desired) GOTO_TSALT;
            }
            { DESCR_t dv; SETAC(dv, desired); sp_putlg(&TXSP, dv); }
            GOTO_TSCOK;
        }
        if (n < 0) scan_error(SCAN_ERR_NEGATIVE); /* TB */
        if (TXSP.l > n) GOTO_TSALT;
        if (n > D_A(MAXLEN)) GOTO_TSALT;
        { DESCR_t dv; SETAC(dv, n); sp_putlg(&TXSP, dv); }
        GOTO_TSCOK;
    }
}

/*====================================================================================================================*/
/* ARBN — ARBNO forward: accept empty match, save cursor */
static void do_ARBN(void)
{
    DESCR_t tmv; sp_getlg(&tmv, TXSP);
    opush(tmv);
    GOTO_SCOK;
}

/*====================================================================================================================*/
/* ARBF — ARBNO backup */
static void do_ARBF(void) { TMVAL = opop(); do_ONAR2(); }

static void do_ONAR2(void)
{
    if (AEQLC(LENFCL, 0)) GOTO_TSALT;
    GOTO_SALF;
}

/*====================================================================================================================*/
/* EARB — ARBNO extension */
static void do_EARB(void)
{
    TMVAL = opop(); /* POP (TMVAL) — restore saved cursor position */
    PUTDC_BLK(PDLPTR, DESCR, TMVAL);   /* PUTDC PDLPTR,DESCR,TMVAL  — old cursor → slot 1 */
    sp_getlg(&TMVAL, TXSP);
    PUTDC_BLK(PDLPTR, 2*DESCR, TMVAL); /* PUTDC PDLPTR,2*DESCR,TMVAL — new cursor → slot 2 */
    PUTDC_BLK(PDLPTR, 3*DESCR, ZEROCL);/* PUTDC PDLPTR,3*DESCR,ZEROCL */
    GOTO_SCOK;
}

/*====================================================================================================================*/
/* ONAR — ARBNO on-match (progress check) */
static void do_ONAR(void)
{
    if (AEQLC(FULLCL, 0)) GOTO_TSCOK; /* AEQLC FULLCL,0,TSCOK — fullscan off → succeed */
    SETAC(TVAL, 0);
    getac(&TVAL, PDLPTR, -2*DESCR);   /* GETAC TVAL,PDLPTR,-2*DESCR — old cursor */
    sp_getlg(&TMVAL, TXSP);
    /* ACOMP TVAL,TMVAL,TSCOK,,TSCOK — branch TSCOK unless TVAL==TMVAL (no progress) */
    if (D_A(TVAL) != D_A(TMVAL)) GOTO_TSCOK;
    opush(TVAL); DECRA(PDLPTR, 6*DESCR); do_ONAR2();
}

/*====================================================================================================================*/
/* ONRF */
static void do_ONRF(void)
{
    SETAC(TVAL, 0);
    getac(&TVAL, PDLPTR, -2*DESCR);
    opush(TVAL); DECRA(PDLPTR, 6*DESCR); do_ONAR2();
}

/*====================================================================================================================*/
/* FARB — ARB forward */
static void do_FARB(void)
{
    int32_t nval;
    if (AEQLC(FULLCL, 0)) { nval = 0; } /* AEQLC FULLCL,0,,FARB2: OFF→0 */
    else {
        if (AEQLC(LENFCL, 0)) goto farb1; /* FARB2: AEQLC LENFCL,0,FARB1 */
        nval = D_A(YCL);                   /* SETAV NVAL,YCL */
    }
    {   /* FARB3 */
        int32_t cur = TXSP.l;
        /* ACOMP TVAL,MAXLEN,FARB1,FARB1 — both targets FARB1 when cur+nval >= MAXLEN */
        if (cur + nval >= D_A(MAXLEN)) goto farb1;
        sp_addlg_c(&TXSP, 1);
        { DESCR_t cv; sp_getlg(&cv, TXSP);
          PUTDC_BLK(PDLPTR, DESCR, cv); } /* PUTAC PDLPTR,2*DESCR,TVAL (our slot 1) */
        GOTO_SCOK;
    }
farb1:
    DECRA(PDLPTR, 3*DESCR); GOTO_SALT;
}

/*====================================================================================================================*/
/* ATP — @X cursor capture */
static void do_ATP(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(XPTR, PATBCL, PATICL);
atp1:
    if (D_V(XPTR) == E) {
        if (EXPEVL_fn() == FAIL) GOTO_TSALF;
        goto atp1;
    }
    { DESCR_t nv; sp_getlg(&nv, TXSP); D_V(nv) = I; PUTDC_BLK(XPTR, DESCR, nv); } /* assign cursor as value */
    if (!AEQLC(OUTSW, 0)) { /* AEQLC OUTSW,0,,ATP2 */
        int32_t a = locapv_fn(D_A(OUTATL), &XPTR);
        if (a) {
            DESCR_t zptr; SETAC(zptr, a);
            DESCR_t assoc; GETDC_BLK(assoc, zptr, DESCR); /* GETDC ZPTR,ZPTR,DESCR */
            DESCR_t nv;   GETDC_BLK(nv, XPTR, DESCR);     /* get NVAL from XPTR */
            PUTOUT_fn(assoc, nv);
        }
    }
    if (ACOMPC(TRAPCL, 0) <= 0) { /* AEQLC TRAPCL,0,,TSCOK */
        int32_t a = locapt_fn(D_A(TVALL), &XPTR);
        if (a) { SETAC(ATPTR, a); TRPHND_fn(ATPTR); }
    }
    GOTO_TSCOK;
}

/*====================================================================================================================*/
/* BAL */
static void do_BAL(void)  { do_BAL_inner(); }
static void do_BALF(void) { do_BAL_inner(); }

static void do_BAL_inner(void)
{
    int32_t nval;
    if (AEQLC(FULLCL, 0)) { nval = 0; } /* BALF1: AEQLC FULLCL,0,,BALF4: OFF→0 */
    else {
        if (AEQLC(LENFCL, 0)) goto bal1; /* BALF4→BALF2: check lenfcl */
        nval = D_A(YCL);                  /* SETAV NVAL,YCL */
    }
    {
        int32_t cur = TXSP.l;
        int32_t avail = D_A(MAXLEN) - cur - nval;
        if (avail <= 0) goto bal1;
        int32_t got = getbal_fn(&TXSP, avail);
        if (got < 0) goto bal1;
        { DESCR_t cv; sp_getlg(&cv, TXSP); PUTDC_BLK(PDLPTR, DESCR, cv); }
        GOTO_SCOK;
    }
bal1:
    DECRA(PDLPTR, 3*DESCR); GOTO_TSALF;
}

/*====================================================================================================================*/
/* BRKXF — BREAKX rematch */
static void do_BRKXF(void)
{
    GETDC_BLK(PATICL, PDLPTR, DESCR);
    DECRA(PATICL, DESCR);
    DECRA(PDLPTR, 3*DESCR);
    int32_t nval;
    if (!AEQLC(FULLCL, 0)) { nval = 0; }
    else {
        if (AEQLC(LENFCL, 0)) GOTO_SALT;
        nval = D_A(YCL);
    }
    { int32_t cur = TXSP.l;
      if (cur + nval > D_A(MAXLEN)) GOTO_SALT; }
    GETDC_BLK(XCL, PDLPTR, DESCR); /* GETDC XCL,PDLPTR,DESCR — cursor lock */
    sp_addlg_c(&TXSP, 1);
    do_BRKX();
    return;
    nval = 0;
}

/*====================================================================================================================*/
/* CHR — match literal string */
static void do_CHR(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(YPTR, PATBCL, PATICL);
    sp_setfrom_descr(&TSP, YPTR);
    {
        SPEC_t vsp, tsub;
        sp_remsp(&vsp, XSP, TXSP);
        if (!sp_subsp(&tsub, TSP, vsp)) GOTO_TSALT;
        if (sp_lexcmp(tsub, TSP) != 0) GOTO_TSALF;
        sp_addlg(&TXSP, YPTR);
        GOTO_TSCOK;
    }
}

/*====================================================================================================================*/
/* STAR — *X (expression-valued pattern) */
static void do_STAR(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(YPTR, PATBCL, PATICL);
star2:
    if (EXPVAL_fn() == FAIL) GOTO_TSALF;
    if (D_V(YPTR) == E) goto star2;
    { DESCR_t ptr; SUM(ptr, PATBCL, PATICL); /* store back into backup node */
      PUTDC_BLK(ptr, 7*DESCR, YPTR); }
    if (D_V(YPTR) == S) {
        sp_setfrom_descr(&TSP, YPTR);
        goto chr2;
    }
    if (D_V(YPTR) == P) goto starp;
    { SPEC_t isp; intspc_fn(&isp, YPTR); sp_copy(&TSP, isp); } /* INTEGER: convert to spec */
chr2:
    {
        SPEC_t vsp, tsub;
        sp_remsp(&vsp, XSP, TXSP);
        if (!sp_subsp(&tsub, TSP, vsp)) GOTO_TSALT;
        if (sp_lexcmp(tsub, TSP) != 0) GOTO_TSALF;
        { DESCR_t lv; sp_getlg(&lv, TSP); sp_addlg(&TXSP, lv); }
        GOTO_TSCOK;
    }
starp:
    {
        int32_t nval = AEQLC(FULLCL, 0) ? 0 : D_A(YCL); /* AEQLC FULLCL,0,,STARP1: OFF->0, ON->YCL */
        nval = D_A(MAXLEN) - nval;
        if (nval <= 0) GOTO_TSALT;
        int32_t lv = lvalue_scalar(D_A(YPTR));
        if (lv > nval) GOTO_TSALT;
        { DESCR_t _cur; sp_getlg(&_cur, TXSP); pdl_push3(SCFLCL, _cur, LENFCL); }
        opush(MAXLEN); opush(PATBCL); opush(PATICL); opush(XCL); opush(YCL);
        { DESCR_t mv; SETAC(mv, nval); MOVD(MAXLEN, mv); }
        { /* nested SCNR */
            Scan_ctx inner; Scan_ctx *prev = scan_ctx_g; scan_ctx_g = &inner;
            int rc = setjmp(inner.fail_jmp);
            if (!rc) { do_SCIN(); }
            scan_ctx_g = prev;
            YCL=opop(); XCL=opop(); PATICL=opop(); PATBCL=opop(); MAXLEN=opop();
            if (rc) {
                if (AEQLC(LENFCL, 0)) GOTO_TSALT;
                GOTO_SALF;
            }
        }
        GOTO_TSCOK;
    }
}

/*====================================================================================================================*/
/* DSAR — backup for *X */
static void do_DSAR(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(YPTR, PATBCL, PATICL);
    if (D_V(YPTR) != P) {
        if (AEQLC(LENFCL, 0)) { GOTO_TSALT; }
        GOTO_SALF;
    }
    {
        int32_t nval = AEQLC(FULLCL, 0) ? 0 : D_A(YCL); /* AEQLC FULLCL,0,,STARP1: OFF->0, ON->YCL */
        nval = D_A(MAXLEN) - nval;
        opush(MAXLEN); opush(PATBCL); opush(PATICL); opush(XCL); opush(YCL);
        { DESCR_t mv; SETAC(mv, nval); MOVD(MAXLEN, mv); }
        SETAC(UNSCCL, 1);
        {
            Scan_ctx inner; Scan_ctx *prev = scan_ctx_g; scan_ctx_g = &inner;
            int rc = setjmp(inner.fail_jmp);
            if (!rc) { do_SCIN1A(); }
            scan_ctx_g = prev;
            YCL=opop(); XCL=opop(); PATICL=opop(); PATBCL=opop(); MAXLEN=opop();
            if (rc) {
                if (AEQLC(LENFCL, 0)) { GOTO_TSALT; }
                GOTO_SALF;
            }
        }
        GOTO_TSCOK;
    }
}

/*====================================================================================================================*/
/* FNCE — FENCE */
static void do_FNCE(void)
{
    INCRA(PDLPTR, 3*DESCR);
    if (D_A(PDLPTR) >= D_A(PDLEND)) GOTO_FAIL;
    { DESCR_t cv; sp_getlg(&cv, TXSP);
      PUTDC_BLK(PDLPTR, 0, FNCFCL);
      PUTDC_BLK(PDLPTR, DESCR, cv);
      PUTDC_BLK(PDLPTR, 2*DESCR, LENFCL); }
    SETAC(LENFCL, 1);
    GOTO_SCOK;
}

/*====================================================================================================================*/
/* NME — X . Y (push cursor, push PDL backup) */
static void do_NME(void)
{
    INCRA(PDLPTR, 3*DESCR);
    if (D_A(PDLPTR) >= D_A(PDLEND)) GOTO_FAIL;
    { DESCR_t cv; sp_getlg(&cv, TXSP);
      PUTDC_BLK(PDLPTR, 0, FNMECL);
      PUTDC_BLK(PDLPTR, DESCR, cv);
      PUTDC_BLK(PDLPTR, 2*DESCR, LENFCL);
      opush(cv); }
    SETAC(LENFCL, 1);
    GOTO_SCOK;
}

/*====================================================================================================================*/
/* FNME — NME backup */
static void do_FNME(void) { TVAL = opop(); do_FNME_inner(); }
static void do_FNME_inner(void)
{
    if (AEQLC(LENFCL, 0)) GOTO_TSALT;
    GOTO_TSALF;
}

/*====================================================================================================================*/
/* ENME — X . Y naming (conditional assignment at end of match) */
static void do_ENME(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(YPTR, PATBCL, PATICL);
    NVAL = opop();
    D_V(YCL) = D_A(NVAL); /* SETVA YCL,NVAL */
    sp_copy(&TSP, TXSP); sp_putlg(&TSP, NVAL);
    sp_remsp(&TSP, TXSP, TSP);
    { /* PUTSPC into name list */
        DESCR_t tptr2; SUM(tptr2, NBSPTR, NAMICL);
        char *p = (char*)A2P(D_A(tptr2)) + DESCR;
        memcpy(p, &TSP, sizeof(SPEC_t));
        p += sizeof(SPEC_t);
        memcpy(p, &YPTR, sizeof(DESCR_t));
        D_A(NAMICL) += DESCR + (int32_t)sizeof(SPEC_t);
    }
    if (D_A(NAMICL) >= NMOVER) {
        SETAC(WCL, NMOVER); /* grow name list */
        NMOVER += NAMLSZ * SPDR;
        int32_t off = BLOCK_fn(NMOVER, B);
        if (!off) GOTO_FAIL;
        SETAC(TPTR, off);
        memcpy(A2P(off), A2P(D_A(NBSPTR)), (size_t)D_A(WCL));
        MOVD(NBSPTR, TPTR);
    }
    do_ENME3();
}

/*====================================================================================================================*/
static void do_ENME3(void)
{
    INCRA(PDLPTR, 3*DESCR);
    if (D_A(PDLPTR) >= D_A(PDLEND)) GOTO_FAIL;
    { DESCR_t cv; sp_getlg(&cv, TXSP);
      MOVV(cv, YCL);
      PUTDC_BLK(PDLPTR, 0, DNMECL);
      PUTDC_BLK(PDLPTR, DESCR, cv);
      PUTDC_BLK(PDLPTR, 2*DESCR, LENFCL); }
    SETAC(LENFCL, 1);
    GOTO_SCOK;
}

/*====================================================================================================================*/
/* DNME — unravel X . Y */
static void do_DNME(void)
{
    D_A(NAMICL) -= DESCR + (int32_t)sizeof(SPEC_t);
    do_DNME1();
}
/*====================================================================================================================*/
static void do_DNME1(void)
{
    SETAV(VVAL, YCL);
    opush(VVAL);
    do_FNME_inner();
}

/*====================================================================================================================*/
/* ENMI — X $ Y (immediate assignment) */
static void do_ENMI(void)
{
    INCRA(PATICL, DESCR);
    GETD_BLK(YPTR, PATBCL, PATICL);
    NVAL = opop();
    D_V(YCL) = D_A(NVAL);
    sp_copy(&TSP, TXSP); sp_putlg(&TSP, NVAL);
    sp_remsp(&TSP, TXSP, TSP);
    { DESCR_t zl; sp_getlg(&zl, TSP);
      if (D_A(zl) > D_A(MLENCL)) scan_error(SCAN_ERR_STR_OVERFLOW); }
    if (D_V(YPTR) == E) {
        opush(ZEROCL);
        if (EXPEVL_fn() == FAIL) { opop(); GOTO_TSALF; }
        opop();
    }
    if (D_V(YPTR) == K) { /* enmi5 */
        if (SPCINT_fn(&VVAL, &TSP) == FAIL) scan_error(SCAN_ERR_ILLEGAL_TYPE);
        goto enmi3;
    }
    { int32_t off = GENVAR_fn(&TSP);
      if (!off) GOTO_TSALF;
      D_A(VVAL) = off; D_V(VVAL) = S; }
enmi3:
    PUTDC_BLK(YPTR, DESCR, VVAL);
    if (!AEQLC(OUTSW, 0)) {
        int32_t a = locapv_fn(D_A(OUTATL), &YPTR);
        if (a) { DESCR_t zd; GETDC_BLK(zd, YPTR, DESCR); PUTOUT_fn(zd, VVAL); }
    }
    if (ACOMPC(TRAPCL, 0) <= 0) {
        int32_t a = locapt_fn(D_A(TVALL), &YPTR);
        if (a) { SETAC(ATPTR, a); TRPHND_fn(ATPTR); }
    }
    do_ENME3();
}

/*====================================================================================================================*/
/* SUCF — SUCCEED failure: reenter SCON */
static void do_SUCF(void)
{
    GETDC_BLK(XCL, PDLPTR, 0);       /* GETDC XCL,PDLPTR,DESCR  (our slot 0) */
    GETDC_BLK(YCL, PDLPTR, 2*DESCR); /* GETDC YCL,PDLPTR,2*DESCR (our slot 2) */
    do_SCON(); /* BRANCH SUCE — re-enter SUCCEED path */
}

/*====================================================================================================================*/
/* SCON — advance head by 1, retry pattern */
static void do_SCON(void)
{
    if (AEQLC(FULLCL, 0)) GOTO_FAIL;  /* AEQLC FULLCL,0,FAIL — fullscan OFF → fail */
    if (AEQLC(LENFCL, 0)) GOTO_FAIL;  /* AEQLC LENFCL,0,FAIL */
    TMVAL = opop(); /* YSIZ */
    YPTR  = opop(); /* YPTR */
    DECRA(TMVAL, 1);
    if (D_A(TMVAL) < 0)  scan_error(SCAN_ERR_ILLEGAL_TYPE); /* ACOMPC YSIZ,0,,FAIL,INTR13 */
    if (D_A(TMVAL) == 0) GOTO_FAIL;
    sp_addlg_c(&TXSP, 1);
    opush(YPTR); opush(TMVAL);
    sp_copy(&HEADSP, TXSP);
    SETAC(LENFCL, 1); /* SETAC LENFCL,1 before pdl_push3 */
    { DESCR_t cv; sp_getlg(&cv, TXSP); pdl_push3(SCONCL, cv, LENFCL); }
    do_SCIN1A();
}

/*====================================================================================================================*/
/* dispatch stubs for longjmp targets in table */
static void do_FAIL_d(void)   { GOTO_FAIL; }
static void do_SALF_d(void)   { GOTO_SALF; }
static void do_SCOK_d(void)   { GOTO_SCOK; }
static void do_RTNUL3(void)   { GOTO_SCOK; }

/* end of sil_scan.c */
