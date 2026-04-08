/*
 * trepub.c — Code tree publication (v311.sil §6 TREPUB line 2466)
 *
 * Faithful C translation of Phil Budne's CSNOBOL4 v311.sil §6 TREPUB.
 *
 * Tree node layout (offsets from node base, in DESCRs):
 *   FATHER = 1*DESCR  — parent node pointer
 *   LSON   = 2*DESCR  — left child pointer
 *   RSIB   = 3*DESCR  — right sibling pointer
 *   CODE   = 4*DESCR  — code descriptor to emit
 *
 * (These are EQU constants in v311.sil lines 838–841.)
 *
 * TREPUB algorithm:
 *   Walk: emit CODE → follow LSON if present → else follow RSIB →
 *         else follow FATHER and then RSIB.
 *   When object code buffer fills, spill to a new block.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M18a
 */

#include <string.h>

#include "types.h"
#include "data.h"
#include "trepub.h"
#include "arena.h"

/* SPLIT_fn declared in arena.h as void(int32_t) */

/* Tree node field offsets — from v311.sil lines 838–841 */
#define T_FATHER  DESCR
#define T_LSON    (2*DESCR)
#define T_RSIB    (3*DESCR)
#define T_CODE    (4*DESCR)

#define GETDC_B(dst, base_d, off_i) \
    memcpy(&(dst), (char*)A2P(D_A(base_d)) + (off_i), sizeof(DESCR_t))
#define PUTD_B(base_d, off_d, src) \
    memcpy((char*)A2P(D_A(base_d)) + D_A(off_d), &(src), sizeof(DESCR_t))
#define PUTDC_B(base_d, off_i, src) \
    memcpy((char*)A2P(D_A(base_d)) + (off_i), &(src),  sizeof(DESCR_t))
#define AEQLIC(d, off, val) \
    (*(int32_t*)((char*)A2P(D_A(d)) + (off)) == (int32_t)(val))

/* ── TREPUB ──────────────────────────────────────────────────────────── */
RESULT_t TREPUB_fn(DESCR_t node)
{
    MOVD(YPTR, node);
trepu1:
    GETDC_B(XPTR, YPTR, T_CODE); /* Get code descriptor from node */
    INCRA(CMOFCL, DESCR); /* INCRA CMOFCL,DESCR; PUTD CMBSCL,CMOFCL,XPTR */
    PUTD_B(CMBSCL, CMOFCL, XPTR);
    SUM(ZPTR, CMBSCL, CMOFCL); /* PCOMP ZPTR,OCLIM: oracle D_PTR, ours D_A; arena model equiv */
    if (D_A(ZPTR) <= D_A(OCLIM)) { /* oracle: > → spill; ours: <= → continue. Inverted, identical. */
        goto trepu4;
    }
    { /* TREPU5: buffer full — allocate new block */
        DESCR_t new_sz;
        SUM(new_sz, CMOFCL, CODELT);
        SETVC(new_sz, C);
        int32_t new_blk = BLOCK_fn(D_A(new_sz), C);
        if (!new_blk) return FAIL;
        SETAC(XCL, new_blk);
        if (!AEQLC(LPTR, 0)) { /* If there's a pending label, point it at the new block */
            PUTDC_B(LPTR, ATTRIB, XCL);
        }
        memmove((char*)A2P(new_blk  + DESCR),  /* MOVBLK(XCL,CMBSCL,CMOFCL): skip title on both sides */
                (char*)A2P(D_A(CMBSCL) + DESCR),
                (size_t)D_A(CMOFCL));
        PUTDC_B(CMBSCL, DESCR, GOTGCL); /* Insert direct goto in old block: GOTG, LIT1, ptr-to-new */
        PUTDC_B(CMBSCL, 2*DESCR, LIT1CL);
        PUTDC_B(CMBSCL, 3*DESCR, XCL);
        INCRA(CMBSCL, 3*DESCR);
        SPLIT_fn(D_A(CMBSCL)); /* SPLIT off old block */
        MOVD(CMBSCL, XCL); /* Switch to new block */
        SUM(OCLIM, CMBSCL, new_sz);
        DECRA(OCLIM, 7*DESCR);
    }
trepu4:
    if (!AEQLIC(YPTR, T_LSON, 0)) { /* AEQLIC YPTR,LSON,0,,TREPU2 — has left son? */
        GETDC_B(YPTR, YPTR, T_LSON);
        goto trepu1;
    }
    if (!AEQLIC(YPTR, T_RSIB, 0)) { /* trepu2:  AEQLIC YPTR,RSIB,0,,TREPU3 — has right sibling? */
        GETDC_B(YPTR, YPTR, T_RSIB);
        goto trepu1;
    }
    while (1) { /* trepu3: climb until we find a node with a right sibling */
        if (AEQLIC(YPTR, T_FATHER, 0)) return OK; /* AEQLIC YPTR,FATHER,0,,RTN1 — no father → done */
        GETDC_B(YPTR, YPTR, T_FATHER);
        if (!AEQLIC(YPTR, T_RSIB, 0)) { /* BRANCH TREPU2 — check for right sibling */
            GETDC_B(YPTR, YPTR, T_RSIB);
            goto trepu1;
        }
    }
}

/*====================================================================================================================*/
/* ── ADDSON — add 'son' as left son of 'parent' ─────────────────────── */
/*
 * SIL ADDSON macro:
 *   PUTDC PARENT,LSON,SON    — parent.lson = son
 *   PUTDC SON,FATHER,PARENT  — son.father  = parent
 */
void ADDSON_fn(DESCR_t parent, DESCR_t son)
{
    PUTDC_B(parent, T_LSON, son);
    PUTDC_B(son, T_FATHER, parent);
}
