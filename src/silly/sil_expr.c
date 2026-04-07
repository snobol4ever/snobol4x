/*
 * sil_expr.c — Expression and element analysis (v311.sil §6)
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

#include "sil_types.h"
#include "sil_data.h"
#include "sil_expr.h"
#include "sil_forwrd.h"   /* FORWRD_fn */
#include "sil_arena.h"
#include "sil_strings.h"
#include "sil_symtab.h"

/* Stream tables (§24 — extern stubs) */
extern DESCR_t BIOPTB;   /* binary operator table                        */
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
        if (stype == 0) { SETAC(EMSGCL, (intptr_t)ILCHAR); return FAIL; } /* ELEILI / ELEICH */
        if (stype == QLITYP) {
        } else { /* quoted literal run-out — fall through */
            SETAC(EMSGCL, (intptr_t)OPNLIT); return FAIL;
        }
    }
    if (alloc_node(&ELEXND) == FAIL) return FAIL;
    SETAC(ELEYND, 0);
    switch (stype) {
    case 0: /* ELEILT: integer literal */
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
    case 1: /* ELEVBL: variable */
        {
            int32_t voff = GENVUP_fn(&xsp);
            if (!voff) return FAIL;
            SETAC(XPTR, voff); SETVC(XPTR, S);
            PUTDC_B(ELEXND, T_CODE, XPTR);
        }
        break;
    case 2: /* ELENST: nested expression (e) */
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
    case 3: /* ELEFNC: function call — XSP has name (minus trailing '(') */
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
            DESCR_t arg1; /* First argument */
            if (EXPR_fn(&arg1) == FAIL) {
                if (!AEQLC(BRTYPE, RPTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; } /* empty arg list ok */
                goto fn_pad;
            }
            MOVD(ELEMND, ELEXND); addson(ELEMND, arg1); MOVD(ELEMND, arg1);
            int32_t nargs = 1;
            while (AEQLC(BRTYPE, CMATYP)) {
                DESCR_t argn;
                if (EXPR_fn(&argn) == FAIL) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
                ADDSIB_fn(ELEMND, argn); MOVD(ELEMND, argn); nargs++;
            }
            if (!AEQLC(BRTYPE, RPTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
fn_pad: { /* Pad with null args if too few */
                GETDC_B(XPTR, XCL, 0);
                if (TESTF(XPTR, FNC)) {
                    int32_t expected = D_V(XCL);
                    while (nargs < expected) {
                        DESCR_t lit_nd, null_nd;
                        if (alloc_node(&lit_nd) == FAIL) return FAIL;
                        if (alloc_node(&null_nd) == FAIL) return FAIL;
                        PUTDC_B(lit_nd, T_CODE, LITCL);
                        PUTDC_B(null_nd, T_CODE, NULVCL);
                        addson(lit_nd, null_nd);
                        ADDSIB_fn(ELEMND, lit_nd); MOVD(ELEMND, lit_nd);
                        nargs++;
                    }
                }
            }
            while (!AEQLIC(ELEXND, T_FATHER, 0)) /* Climb to function node root */
                GETDC_B(ELEXND, ELEXND, T_FATHER);
        }
        goto elem_exit;
    case 4: /* ELEFLT: real literal */
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
    case 5: /* ELEARY: array/table subscript */
        {
            xsp.l--; /* SHORTN XSP,1 — remove '[' */
            int32_t voff = GENVUP_fn(&xsp);
            if (!voff) return FAIL;
            SETAC(XPTR, voff); SETVC(XPTR, S);
            PUTDC_B(ELEXND, T_CODE, XPTR);
            MOVD(ELEXND, ELEXND); /* ELEAR2: build ITEM function node with args */
            if (elearg(ITEMCL) == FAIL) return FAIL;
            if (!AEQLC(BRTYPE, RBTYP)) { SETAC(EMSGCL, (intptr_t)ILLBRK); return FAIL; }
        }
        goto elem_exit;
    default: /* literal string (quoted) */
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
            (deql(EXOPND, BISRFN) || deql(EXOPND, BISNFN))) {
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
        if (AEQLC(EXPRND, 0)) {
            DESCR_t prec_op, prec_ex; /* EXPR3: empty tree — compare precedences */
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
            addson(EXOPND, EXELND); /* Non-empty tree */
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
    if (AEQLC(EXPRND, 0)) { /* EXPR7: assemble result */
        ADDSIB_fn(EXPRND, EXELND); /* EXPR10: add as sibling */
        MOVD(XPTR, EXPRND);
    } else {
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
    if (FORBLK_fn() == FAIL) return FAIL; /* BINOP: STREAM BIOPTB */
    SPEC_t xsp; int stype;
    if (AEQLC(BRTYPE, EQTYP) && !AEQLC(SPITCL, 0)) {
        SETAC(STYPE, D_A(BIEQFN)); /* SPITBOL assignment operator */
        *out = BIEQFN;
        return OK;
    }
    if (AEQLC(BRTYPE, NBTYP)) return FAIL; /* RTN2 — no operator */
    RESULT_t rc = STREAM_fn(&xsp, &TEXTSP, &BIOPTB, &stype);
    if (rc == FAIL) {
        *out = CONCL; /* BINCON: concatenation */
        return OK;
    }
    MOVD(*out, STYPE); /* STYPE holds the function descriptor */
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
/* end of sil_expr.c */
