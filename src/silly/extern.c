/*
 * extern.c — External functions (v311.sil §13 lines 4471–4643)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §13.
 *
 * LOAD/UNLOAD require STREAM/VARATB (prototype parsing) — stubbed.
 * LNKFNC: argument coercion + LINK call (platform stub).
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M17
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "extern.h"
#include "argval.h"
#include "arena.h"
#include "strings.h"
#include "symtab.h"

/* Platform stubs */
extern RESULT_t XCALL_LOAD(DESCR_t *entry_out, SPEC_t *fn_sp, SPEC_t *lib_sp);
extern void       XCALL_UNLOAD(SPEC_t *fn_sp);
extern RESULT_t XCALL_LINK(DESCR_t *result, DESCR_t *arg_base,
                              int32_t nargs, DESCR_t entry);
extern RESULT_t XCALL_RELSTRING(DESCR_t str);

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))
#define GETD_B(dst, base_d, off_d) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + D_A(off_d), sizeof(DESCR_t))

static inline int deql(DESCR_t a, DESCR_t b)
{
    return D_A(a) == D_A(b) && D_V(a) == D_V(b);
}

/*====================================================================================================================*/
static DESCR_t ext_stk[32];
static int ext_top = 0;
static inline void    ext_push(DESCR_t d) { ext_stk[ext_top++] = d; }
static inline DESCR_t ext_pop(void)        { return ext_stk[--ext_top]; }

/* ── LOAD(P,L) ───────────────────────────────────────────────────────── */
/*
 * Requires STREAM/VARATB for prototype parsing — stubbed until M15+
 * infrastructure (STREAM) is available.
 */
RESULT_t LOAD_fn(void) { return FAIL; }

/* ── UNLOAD(F) ───────────────────────────────────────────────────────── */
RESULT_t UNLOAD_fn(void)
{
    if (VARVUP_fn() == FAIL) return FAIL;
    int32_t zcl_off = FINDEX_fn(&XPTR); /* FINDEX — get function descriptor */
    if (!zcl_off) return FAIL;
    SETAC(ZCL, zcl_off);
    PUTDC_B(ZCL, 0, UNDFCL); /* Reset to UNDFCL */
    LOCSP_fn(&XSP, &XPTR); /* Platform unload */
    XCALL_UNLOAD(&XSP);
    MOVD(XPTR, NULVCL); return OK;
}

/*====================================================================================================================*/
/* ── LNKFNC — invoke external function ──────────────────────────────── */
/*
 * Coerces each argument to the declared type, then calls XCALL_LINK.
 * Type coercions:
 *   STRING→INTEGER (LNKVI), INTEGER→STRING (LNKIV)
 *   REAL→INTEGER  (LNKRI), INTEGER→REAL   (LNKIR)
 *   STRING→REAL   (LNKVR), REAL→STRING    (LNKRV)
 */
RESULT_t LNKFNC_fn(void)
{
    SETAV(XCL, INCL); /* actual arg count       */
    MOVD(YCL, INCL); /* save function desc     */
    DESCR_t WCL_d; GETDC_B(WCL_d, YCL, 0); SETAV(WCL_d, WCL_d); /* formal count */
    DESCR_t WPTR_d; MOVD(WPTR_d, ZEROCL); /* passed-arg counter */
    DESCR_t ZCL_d; GETDC_B(ZCL_d, YCL, DESCR); /* definition block */
    DESCR_t YPTR_d; /* stack position — we use ext_top as proxy */
    int stack_base = ext_top;
    DESCR_t TCL_d; SETAC(TCL_d, 2*DESCR); /* offset into def block */
    int32_t nactual = D_A(XCL);
    for (int32_t i = 0; i < nactual; i++) { /* Evaluate and coerce each actual argument */
        ext_push(XCL); ext_push(ZCL_d); ext_push(TCL_d);
        ext_push(YPTR_d); ext_push(WCL_d); ext_push(YCL); ext_push(WPTR_d);
        if (ARGVAL_fn() == FAIL) { ext_top = stack_base; return FAIL; }
        WPTR_d = ext_pop(); YCL = ext_pop(); WCL_d = ext_pop();
        YPTR_d = ext_pop(); TCL_d = ext_pop(); ZCL_d = ext_pop(); XCL = ext_pop();
        DECRA(WCL_d, 1);
        if (D_A(WCL_d) >= 0) { /* Coerce if within formal range and type specified */
            DESCR_t ZPTR_d; GETD_B(ZPTR_d, ZCL_d, TCL_d);
            if (!AEQLC(ZPTR_d, 0) && !VEQLC(ZPTR_d, D_V(XPTR))) {
                SETAV(DTCL, XPTR); MOVV(DTCL, ZPTR_d);
                if (deql(DTCL, VIDTP)) { /* STRING→INTEGER */
                    LOCSP_fn(&XSP, &XPTR);
                    SPCINT_fn(&XPTR, &XSP);
                }
                else if (deql(DTCL, IVDTP)) { /* INTEGER→STRING */
                    int32_t off = GNVARI_fn(D_A(XPTR));
                    if (off) { SETAC(XPTR, off); SETVC(XPTR, S); }
                }
                else if (deql(DTCL, RIDTP)) { /* REAL→INTEGER */
                    D_A(XPTR) = (int32_t)D_R(XPTR); SETVC(XPTR, I); /* RLINT stub — truncate real to int */
                }
                else if (deql(DTCL, IRDTP)) { /* INTEGER→REAL */
                    D_R(XPTR) = (float)D_A(XPTR); SETVC(XPTR, R);
                }
                else if (deql(DTCL, RVDTP)) { /* REAL→STRING */
                    SPEC_t rsp; REALST_fn(&rsp, &XPTR);
                    int32_t off = GENVAR_fn(&rsp);
                    if (off) { SETAC(XPTR, off); SETVC(XPTR, S); }
                }
                else if (deql(DTCL, VRDTP)) { /* STRING→REAL */
                    LOCSP_fn(&XSP, &XPTR);
                    if (SPCINT_fn(&XPTR, &XSP) == FAIL) {
                        if (SPREAL_fn(&XPTR, &XSP) == FAIL) return FAIL;
                    }
                }
            }
        }
        ext_push(XPTR);
        INCRA(TCL_d, DESCR);
        INCRA(WPTR_d, 1);
    }
    while (D_A(WCL_d) > 0) { /* Pad with nulls for omitted formals */
        ext_push(NULVCL);
        INCRA(WPTR_d, 1);
        DECRA(WCL_d, 1);
    }
    int32_t sz = x_bksize(D_A(ZCL_d)); /* Get definition block end: entry point and target type */
    DESCR_t xptr2; GETDC_B(xptr2, ZCL_d, sz - DESCR); /* target type   */
    DESCR_t zcl2; GETDC_B(zcl2, ZCL_d, 0); /* entry address — LOAD stores at slot 0 (PUTDC XPTR,0,YPTR) */
    int32_t nargs = ext_top - stack_base; /* Pointer to argument list on our ext_stk */
    DESCR_t *arg_base = &ext_stk[stack_base];
    if (XCALL_LINK(&ZPTR, arg_base, nargs, zcl2) == FAIL) { /* LINK: call external function */
        ext_top = stack_base; return FAIL;
    }
    ext_top = stack_base;
    if (D_V(ZPTR) == M) { /* Handle return value */
        LOCSP_fn(&ZSP, &ZPTR); /* malloc'd linked string [PLB130] */
        int32_t off = GENVAR_fn(&ZSP);
        DESCR_t old_zptr = ZPTR;
        if (off) { SETAC(ZPTR, off); SETVC(ZPTR, S); }
        XCALL_RELSTRING(old_zptr);
        MOVD(XPTR, ZPTR); return OK;
    }
    if (D_V(ZPTR) == L) {
        LOCSP_fn(&ZSP, &ZPTR); /* linked string */
        int32_t off = GENVAR_fn(&ZSP);
        if (off) { SETAC(ZPTR, off); SETVC(ZPTR, S); }
        MOVD(XPTR, ZPTR); return OK;
    }
    LOCSP_fn(&ZSP, &ZPTR); /* Generate variable from ZSP */
    int32_t off = GENVAR_fn(&ZSP);
    if (!off) return FAIL;
    SETAC(ZPTR, off); SETVC(ZPTR, S);
    MOVD(XPTR, ZPTR); return OK;
}
