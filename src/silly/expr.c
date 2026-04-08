/*
 * expr.c — Expression and element analysis (v311.sil §6)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §6.
 * Covers ELEMNT, EXPR, EXPR1, NULNOD, ADDSIB, INSERT, BINOP, UNOP,
 * and the ELEARG helper for function-argument and array-subscript lists.
 *
 * Tree node layout (field offsets from node base):
 *   FATHER = 1*DESCR, LSON = 2*DESCR, RSIB = 3*DESCR, CODE = 4*DESCR
 *
 * STREAM calls (ELEMTB, BIOPTB, UNOPTB, GOTSTB) are extern stubs.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18d
 */

#include <string.h>

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "data.h"
#include "expr.h"
#include "forwrd.h"   /* FORWRD_fn */
#include "arena.h"
#include "strings.h"
#include "symtab.h"

/* Stream tables (§24 — extern stubs) */
extern DESCR_t BIOPTB;   /* binary operator table                        */
extern DESCR_t SBIPTB;   /* SPITBOL binary operator table                */
extern DESCR_t UNOPTB;   /* unary operator table                         */
extern RESULT_t STREAM_fn(SPEC_t *res, SPEC_t *src,
                              DESCR_t *tbl, int *stype_out);

/* Tree node field offsets */
#define T_FATHER  DESCR
#define T_LSON    (2*DESCR)
#define T_RSIB    (3*DESCR)
#define T_CODE    (4*DESCR)

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d))+(off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d))+(off_i), &(src),  sizeof(DESCR_t))
#define AEQLIC(d, off, val) \
    (*(int32_t*)((char*)A2P(D_A(d))+(off)) == (int32_t)(val))
#define PUTAC_B(base_d, off_i, ival) \
    memcpy((char*)A2P(D_A(base_d))+(off_i), &(ival), sizeof(int32_t))

static inline int deql(DESCR_t a, DESCR_t b)
    { return D_A(a)==D_A(b) && D_V(a)==D_V(b); }

/* Forward declarations */
static RESULT_t expr_continue(DESCR_t *out);
static RESULT_t expr7(DESCR_t *out);
static RESULT_t elearg(DESCR_t fn_code);

/* ── Tree helpers ────────────────────────────────────────────────────── */

/* ADDSON — set parent.lson=son; son.father=parent */
static inline void addson(DESCR_t parent, DESCR_t son)
{
    PUTDC_B(parent, T_LSON, son);
    PUTDC_B(son, T_FATHER, parent);
}

/*====================================================================================================================*/
/* ADDSIB — add sib as right-sibling of node */
void ADDSIB_fn(DESCR_t node, DESCR_t sib)
{
    PUTDC_B(node, T_RSIB, sib);
    PUTDC_B(sib, T_FATHER, node);
}

/*====================================================================================================================*/
/* INSERT — insert 'above' as parent of 'node' (node becomes son) */
void INSERT_fn(DESCR_t node, DESCR_t above)
{
    DESCR_t father; GETDC_B(father, node, T_FATHER);
    PUTDC_B(above, T_LSON, node); /* above.lson = node */
    PUTDC_B(node, T_FATHER, above);
    if (!AEQLIC(node, T_FATHER, 0)) { /* above.father = node's old father */
        PUTDC_B(above, T_FATHER, father);
        PUTDC_B(father, T_LSON, above);
    }
}

/*====================================================================================================================*/
/* alloc_node: allocate CNDSIZ block for one tree node */
static RESULT_t alloc_node(DESCR_t *out)
{
    int32_t blk = BLOCK_fn(D_A(CNDSIZ), B);
    if (!blk) return FAIL;
    memset(A2P(blk), 0, (size_t)D_A(CNDSIZ)); /* zero the node */
    SETAC(*out, blk); SETVC(*out, B);
    return OK;
}

/*====================================================================================================================*/
/* ── NULNOD — build LIT(null-string) node ───────────────────────────── */
RESULT_t NULNOD_fn(DESCR_t *out)
{
    if (alloc_node(&EXPRND) == FAIL) return FAIL;
    PUTDC_B(EXPRND, T_CODE, LITCL);
    if (alloc_node(&EXEXND) == FAIL) return FAIL;
    PUTDC_B(EXEXND, T_CODE, NULVCL);
    addson(EXPRND, EXEXND);
    *out = EXPRND;
    return OK;
}

/*====================================================================================================================*/
/* ── ELEARG — process argument list for function/array ref ──────────── */
/*
 * v311.sil ELEARG line 2173:
 *   Pop function code XCL, allocate function node, attach first arg
 *   (ELEXND), then loop reading EXPR args separated by commas.
 *   Sets ELEMND to last arg node; BRTYPE on exit.
 */
static RESULT_t elearg(DESCR_t fn_code)
{
    DESCR_t fn_node;
    if (alloc_node(&fn_node) == FAIL) return FAIL;
    PUTDC_B(fn_node, T_CODE, fn_code);
    if (!AEQLC(ELEMND, 0)) addson(ELEMND, fn_node);
    addson(fn_node, ELEXND);
    MOVD(ELEMND, ELEXND);
    while (1) { /* Loop reading more args */
        DESCR_t arg_nd;
        if (EXPR_fn(&arg_nd) == FAIL) return FAIL;
        ADDSIB_fn(ELEMND, arg_nd);
        MOVD(ELEMND, arg_nd);
        if (!AEQLC(BRTYPE, CMATYP)) break;
    }
    return OK;
}

/*====================================================================================================================*/
/* ── ELEMNT — compile one element ────────────────────────────────────── */
RESULT_t ELEMNT_fn(DESCR_t *out)
{
    if (UNOP_fn(&ELEMND) == FAIL) return FAIL; /* RTN2 on error */                     /* Get tree of unary operators */
    SPEC_t xsp; int stype; /* Break element from TEXTSP */
    RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &ELEMTB, &stype);
    SETAC(STYPE, stype);
    if (rc == FAIL) {
        /* ELEILI: stream EOS — determine cause */
        if (stype == 0)      { SETAC(EMSGCL, (intptr_t)ILCHAR); return FAIL; } /* ELEICH */
        if (stype == QLITYP) { SETAC(EMSGCL, (intptr_t)OPNLIT); return FAIL; } /* unclosed literal */
        /* non-zero, non-QLITYP: ELEMN9 — fall through to dispatch */
    }
    if (alloc_node(&ELEXND) == FAIL) return FAIL;
    SETAC(ELEYND, 0);
    switch (stype) {
    case ILITYP: /* ELEILT: integer literal — STYPE=2 */
        {
            DESCR_t ival;
            if (SPCINT_fn(&ival, &xsp) == FAIL) {
                SETAC(EMSGCL, (intptr_t)ILLINT); return FAIL;
            }
            PUTDC_B(ELEXND, T_CODE, LITCL);
            if (alloc_node(&ELEYND) == FAIL) return FAIL;
            PUTDC_B(ELEYND, T_CODE, ival);
            addson(ELEXND, ELEYND);
        }
        break;
    case 3: /* ELEVBL: variable — STYPE=3 */
        {
            int32_t voff = GENVUP_fn(&xsp);
            if (!voff) return FAIL;
            SETAC(XPTR, voff); SETVC(XPTR, S);
            PUTDC_B(ELEXND, T_CODE, XPTR);
        }
        break;
    case 4: /* ELENST: nested expression (e) — STYPE=4 */
        {
            DESCR_t nested;
            if (EXPR_fn(&nested) == FAIL) return FAIL;
            if (AEQLC(BRTYPE, RPTYP)) {
                MOVD(ELEXND, nested);
            } else if (AEQLC(BRTYPE, CMATYP) && !AEQLC(SPITCL, 0)) {
                MOVD(ELEXND, nested); /* CMA selection list */
                if (elearg(CMACL) == FAIL) return FAIL;
                if (!AEQLC(BRTYPE, RPTYP)) {
                    SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL;
                }
            } else {
                SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL;
            }
        }
        break;
    case 5: /* ELEFNC: function call — STYPE=5, XSP has name (minus trailing '(') */
        {
            xsp.l--; /* SHORTN XSP,1 */
            int32_t foff = GENVUP_fn(&xsp);
            if (!foff) return FAIL;
            SETAC(XPTR, foff); SETVC(XPTR, S);
            int32_t fcl_off = FINDEX_fn(&XPTR);
            if (!fcl_off) return FAIL;
            SETAC(XCL, fcl_off);
            PUTDC_B(ELEXND, T_CODE, XCL);
            if (!AEQLC(ELEMND, 0)) addson(ELEMND, ELEXND);
            MOVD(ELEMND, ELEXND);
            DESCR_t arg1; /* First argument */
            if (EXPR_fn(&arg1) == FAIL) {
                if (!AEQLC(BRTYPE, RPTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; } /* empty arg list ok */
                goto fn_pad;
            }
            addson(ELEMND, arg1); MOVD(ELEMND, arg1);
            int32_t nargs = 1;
            while (AEQLC(BRTYPE, CMATYP)) {
                DESCR_t argn;
                if (EXPR_fn(&argn) == FAIL) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
                ADDSIB_fn(ELEMND, argn); MOVD(ELEMND, argn); nargs++;
            }
            if (!AEQLC(BRTYPE, RPTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
fn_pad: { /* Pad with null args if too few — oracle L_ELEMN3/L_ELEMN4 */
                /* Climb to function node: ELEMND.FATHER = fn node, get its CODE */
                DESCR_t fn_nd; GETDC_B(fn_nd, ELEMND, T_FATHER);
                DESCR_t xcl2;  GETDC_B(xcl2,  fn_nd,  T_CODE);
                DESCR_t ycl;   GETDC_B(ycl,   xcl2,   0);
                if (TESTF(ycl, FNC)) {
                    /* nargs_expected in D_V(xcl2); actual in D_A(xcl2) after clearing */
                    int32_t xcl_a = D_A(xcl2); D_F(xcl2) = D_V(xcl2) = 0;
                    int32_t ycl_a = D_A(ycl);  D_F(ycl)  = D_V(ycl)  = 0;
                    while (xcl_a < ycl_a) {
                        /* oracle: BLOCK ELEYND (outer=LIT), BLOCK ELEXND (inner=NULVCL) */
                        DESCR_t eleynd, elexnd;
                        if (alloc_node(&eleynd) == FAIL) return FAIL;
                        if (alloc_node(&elexnd) == FAIL) return FAIL;
                        PUTDC_B(eleynd, T_CODE, LITCL);
                        PUTDC_B(elexnd, T_CODE, NULVCL);
                        addson(eleynd, elexnd);
                        ADDSIB_fn(ELEMND, eleynd); MOVD(ELEMND, eleynd);
                        xcl_a++;
                    }
                }
            }
            /* Climb to function node root (ELEXND = fn node base) */
            while (!AEQLIC(ELEXND, T_FATHER, 0))
                GETDC_B(ELEXND, ELEXND, T_FATHER);
        }
        goto elem_exit;
    case 6: /* ELEFLT: real literal — STYPE=6 */
        {
            DESCR_t rval;
            if (SPREAL_fn(&rval, &xsp) == FAIL) {
                SETAC(EMSGCL, (intptr_t)ILLDEC); return FAIL;
            }
            PUTDC_B(ELEXND, T_CODE, LITCL);
            if (alloc_node(&ELEYND) == FAIL) return FAIL;
            PUTDC_B(ELEYND, T_CODE, rval);
            addson(ELEXND, ELEYND);
        }
        break;
    case 7: /* ELEARY: array/table subscript — STYPE=7 */
        {
            xsp.l--; /* SHORTN XSP,1 — remove '[' */
            int32_t voff = GENVUP_fn(&xsp);
            if (!voff) return FAIL;
            SETAC(XPTR, voff); SETVC(XPTR, S);
            PUTDC_B(ELEXND, T_CODE, XPTR);
            /* ELEAR2: build ITEM function node with args */
            if (elearg(ITEMCL) == FAIL) return FAIL;
            if (!AEQLC(BRTYPE, RBTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
        }
        goto elem_exit;
    default: /* literal string (quoted) — STYPE=QLITYP=1 or other */
        {
            xsp.o++; xsp.l -= 2; /* Strip surrounding quotes */
            int32_t soff = GENVAR_fn(&xsp);
            if (!soff) return FAIL;
            SETAC(XPTR, soff); SETVC(XPTR, S);
            PUTDC_B(ELEXND, T_CODE, LITCL);
            if (alloc_node(&ELEYND) == FAIL) return FAIL;
            PUTDC_B(ELEYND, T_CODE, XPTR);
            addson(ELEXND, ELEYND);
        }
        break;
    }
    if (AEQLC(ELEMND, 0)) { /* ELEMN1: attach ELEXND to ELEMND (unary-op tree) */
        MOVD(ELEXND, ELEXND);
    } else {
        addson(ELEMND, ELEXND);
        MOVD(ELEXND, ELEMND);
    }
elem_exit:
    MOVD(ZPTR, ELEXND); /* Climb to root of tree */
    while (!AEQLIC(ZPTR, T_FATHER, 0))
        GETDC_B(ZPTR, ZPTR, T_FATHER);
    { /* ELEM10: peek-ahead for '<' or '[' (ITEM subscription) */
        SPEC_t peek; int pstype;
        if (STREAM_fn(&peek, &TEXTSP, &GOTSTB, &pstype) == OK) {
            if (pstype != SGOTYP) {
                SETAC(ELEMND, 0); /* Array reference on result */
                MOVD(ELEXND, ZPTR);
                if (elearg(ITEMCL) == FAIL) return FAIL;
                while (!AEQLIC(ELEXND, T_FATHER, 0))
                    GETDC_B(ELEXND, ELEXND, T_FATHER);
                MOVD(ZPTR, ELEXND);
            }
        }
    }
    *out = ZPTR;
    return OK;
}

/*====================================================================================================================*/
/* ── EXPR — compile a full expression ───────────────────────────────── */
RESULT_t EXPR_fn(DESCR_t *out)
{
    if (ELEMNT_fn(&EXELND) == FAIL) {
        return NULNOD_fn(out); /* EXPNUL: return null node */
    }
    SETAC(EXPRND, 0);
    return expr_continue(out);
}

/*====================================================================================================================*/
/* ── EXPR1 — continuation entry (called with saved EXPRND) ──────────── */
RESULT_t EXPR1_fn(DESCR_t *out)
{
    DESCR_t saved = EXPRND;
    if (ELEMNT_fn(&EXELND) == FAIL) {
        if (!AEQLC(SPITCL, 0) && /* EXPR12: SPITBOL null-right-operand handling */
            (deql(EXOPND, BISRFN) || deql(EXOPND, BIEQFN))) {
            if (NULNOD_fn(&EXELND) == FAIL) return FAIL; /* default null right operand */
            MOVD(EXPRND, saved);
            goto do_expr7;
        }
        SETAC(EMSGCL, (intptr_t)ILLEOS); return FAIL;
    }
    MOVD(EXPRND, saved);
    return expr_continue(out);
do_expr7:
    return expr7(out);
}

/*====================================================================================================================*/
static RESULT_t expr_continue(DESCR_t *out)
{
    while (1) {
        DESCR_t op; /* EXPR2: get binary operator */
        if (BINOP_fn(&op) == FAIL) break; /* EXPR7 */
        MOVD(EXOPCL, op);
        if (!AEQLC(SPITCL, 0) && deql(EXOPCL, ASGNCL) && !AEQLC(EXPRND, 0)) { /* SPITBOL: check for ASGNCL + SCAN-in-tree → convert to SJSR */
            MOVD(EXOPND, EXPRND); /* Walk up tree looking for BISNFN (SCAN) */
            while (1) {
                MOVD(EXEXND, EXOPND);
                if (AEQLIC(EXEXND, T_FATHER, 0)) break;
                GETDC_B(EXOPND, EXEXND, T_FATHER);
                if (deql(EXOPND, BISNFN)) {
                    PUTAC_B(EXOPND, T_CODE, D_A(SJSRCL)); /* Convert SCAN → SJSR */
                    ADDSIB_fn(EXPRND, EXELND);
                    MOVD(EXPRND, EXELND);
                    while (!AEQLIC(EXPRND, T_FATHER, 0)) /* Climb to top */
                        GETDC_B(EXPRND, EXPRND, T_FATHER);
                    if (EXPR1_fn(out) == FAIL) return FAIL;
                    return OK;
                }
            }
        }
        if (alloc_node(&EXOPND) == FAIL) return FAIL; /* EXPR14: allocate operator node */
        PUTDC_B(EXOPND, T_CODE, EXOPCL);
        if (!AEQLC(EXPRND, 0)) {
            DESCR_t prec_op, prec_ex; /* EXPR3: non-empty tree — compare precedences */
            GETDC_B(prec_op, EXOPCL, 2*DESCR); SETAV(prec_op, prec_op);
            GETDC_B(EXEXND, EXPRND, T_FATHER);
            GETDC_B(XPTR, EXEXND, T_CODE);
            GETDC_B(prec_ex, XPTR, 2*DESCR);
            if (ACOMP(prec_ex, prec_op) < 0) {
                ADDSIB_fn(EXPRND, EXELND); /* EXPR4: add current as sibling */
expr5_loop:
                while (!AEQLIC(EXPRND, T_FATHER, 0)) {
                    GETDC_B(EXPRND, EXPRND, T_FATHER);
                    if (AEQLIC(EXPRND, T_FATHER, 0)) goto expr11;
                    GETDC_B(EXEXND, EXPRND, T_FATHER);
                    GETDC_B(XPTR, EXEXND, T_CODE);
                    GETDC_B(prec_ex, XPTR, 2*DESCR);
                    if (ACOMP(prec_ex, prec_op) >= 0) goto expr5_loop;
                    INSERT_fn(EXPRND, EXOPND);
                    if (EXPR1_fn(out) == FAIL) return FAIL;
                    return OK;
                }
expr11:
                addson(EXOPND, EXPRND);
                if (EXPR1_fn(out) == FAIL) return FAIL;
                return OK;
            }
            ADDSIB_fn(EXPRND, EXOPND);
            MOVD(EXPRND, EXOPND);
            addson(EXPRND, EXELND);
            MOVD(EXPRND, EXELND);
            if (EXPR1_fn(out) == FAIL) return FAIL;
            return OK;
        } else {
            addson(EXOPND, EXELND); /* EXPR14: empty tree — simple addson */
            MOVD(EXPRND, EXELND);
            if (EXPR1_fn(out) == FAIL) return FAIL;
            return OK;
        }
    }
    return expr7(out);
}

/*====================================================================================================================*/
static RESULT_t expr7(DESCR_t *out)
{
    if (!AEQLC(EXPRND, 0)) { /* EXPR7: non-empty tree → EXPR10 */
        ADDSIB_fn(EXPRND, EXELND); /* add element as sibling */
        MOVD(XPTR, EXPRND);
    } else { /* EXPR7: empty tree → XPTR = EXELND directly */
        MOVD(XPTR, EXELND);
    }
    while (!AEQLIC(XPTR, T_FATHER, 0)) /* EXPR9: climb to root */
        GETDC_B(XPTR, XPTR, T_FATHER);
    *out = XPTR;
    return OK;
}

/*====================================================================================================================*/
/* ── BINOP — binary operator analysis ───────────────────────────────── */
RESULT_t BINOP_fn(DESCR_t *out)
{
    /* BINOP: RCALL ,FORBLK,,BINOP1. BINOP1 fires on FORBLK RTN1 = ST_ERROR from IBLKTB.
     * Our STREAM_fn calls error()→exit(1) on AC_ERROR before returning; FAIL here is
     * only reachable on the same fatal path. Functionally equivalent. */
    if (FORBLK_fn() == FAIL) return FAIL;
    SPEC_t xsp; int stype;
    if (AEQLC(BRTYPE, EQTYP) && !AEQLC(SPITCL, 0)) {
        /* BINOP2: SPITBOL '=' op. Oracle: D_A(STYPE)=(int_t)BIEQFN (addr of descriptor).
         * Ours: P2A(&BIEQFN) = arena offset of the BIEQFN global DESCR. Then return STYPE. */
        SETAC(STYPE, P2A(&BIEQFN));
        *out = STYPE;
        return OK;
    }
    if (AEQLC(BRTYPE, NBTYP)) return FAIL; /* RTN2 — no operator */
    /* Select table: SPITCL!=0 → SBIPTB; else BIOPTB. BLOKCL ignored (BLOCKS skipped). */
    DESCR_t *optb = (!AEQLC(SPITCL, 0)) ? &SBIPTB : &BIOPTB;
    RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, optb, &stype);
    if (rc == FAIL) {
        *out = CONCL; /* BINCON: D(ZPTR)=D(CONCL); RTZPTR. Full DESCR copy. ✓ */
        return OK;
    }
    MOVD(*out, STYPE); /* BINOP3: D(ZPTR)=D(STYPE); RTZPTR — return function descriptor. ✓ */
    return OK;
}

/*====================================================================================================================*/
/* ── UNOP — unary operator analysis ─────────────────────────────────── */
RESULT_t UNOP_fn(DESCR_t *out)
{
    if (FORWRD_fn() == FAIL) return FAIL; /* UNOP: FORWRD then STREAM UNOPTB */
    SETAC(*out, 0);
    if (!AEQLC(BRTYPE, NBTYP)) return OK; /* RTN1 — no unary ops */
    SPEC_t xsp; int stype;
    while (1) {
        RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &UNOPTB, &stype);
        if (rc == FAIL) break; /* RTXNAM — return current tree */
        DESCR_t yptr;
        if (alloc_node(&yptr) == FAIL) return FAIL;
        PUTDC_B(yptr, T_CODE, STYPE);
        if (AEQLC(*out, 0)) {
            *out = yptr;
        } else {
            addson(*out, yptr);
        }
        MOVD(*out, yptr);
    }
    return OK;
}

/*====================================================================================================================*/
/* end of expr.c */
