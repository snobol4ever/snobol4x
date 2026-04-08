/*
 * sil_symtab.c — symbol table and support procedures (v311.sil §4)
 *
 * Faithful C translation of v311.sil §4 "Support Procedures"
 * (lines 1088–1218): AUGATL, CODSKP, DTREP, FINDEX.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M3
 */

#include <string.h>
#include <stdio.h>
#include "types.h"
#include "data.h"
#include "arena.h"
#include "symtab.h"

/* ── locapt_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil LOCAPT (macro): walk a pair list looking for a hole (zero
 * first field) when key is ZEROCL, or matching type field otherwise.
 * Pair list layout: [title][type0][val0][type1][val1]...
 * Returns arena offset of matching type DESCR, or 0 if not found.
 */

int32_t locapt_fn(int32_t list_off, DESCR_t *key_d)
{
    if (list_off == 0) return 0;
    DESCR_t *title = (DESCR_t *)A2P(list_off);
    int32_t size = (int32_t)title->v; /* GETSIZ */
    int32_t off = DESCR; /* skip title */
    while (off <= size) {
        DESCR_t *slot = (DESCR_t *)A2P(list_off + off);
        if (key_d->a.i == 0 && key_d->f == 0 && key_d->v == 0) { /* looking for hole: zero A field */
            if (slot->a.i == 0 && slot->f == 0 && slot->v == 0)
                return list_off + off;
        } else {
            if ((int32_t)slot->v == (int32_t)key_d->v && /* match on V field (type tag) */
                slot->a.i == key_d->a.i)
                return list_off + off;
        }
        off += 2 * DESCR; /* each pair is 2 DESCRs */
    }
    return 0;
}

/*====================================================================================================================*/
/* ── locapv_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil LOCAPV: walk a pair list matching the VALUE DESCR (second of
 * each pair) against key_d.
 * Returns arena offset of the matching value DESCR, or 0.
 */

int32_t locapv_fn(int32_t list_off, DESCR_t *key_d)
{
    if (list_off == 0) return 0;
    DESCR_t *title = (DESCR_t *)A2P(list_off);
    int32_t size = (int32_t)title->v;
    int32_t off = DESCR;
    while (off < size) {
        DESCR_t *val_slot = (DESCR_t *)A2P(list_off + off + DESCR); /* value DESCR is at off + DESCR (second of the pair) */
        if (val_slot->a.i == key_d->a.i &&
            val_slot->f == key_d->f &&
            (int32_t)val_slot->v == (int32_t)key_d->v)
            return list_off + off + DESCR;
        off += 2 * DESCR;
    }
    return 0;
}

/*====================================================================================================================*/
/* ── AUGATL_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil AUGATL (line 1092).
 */

int32_t AUGATL_fn(int32_t list_off, DESCR_t type_d, DESCR_t val_d)
{
    DESCR_t zero = ZEROD; /* LOCAPT A4PTR,A1PTR,ZEROCL — look for hole */
    int32_t hole = locapt_fn(list_off, &zero);
    if (hole != 0) {
        *((DESCR_t *)A2P(hole)) = type_d; /* Found a hole — fill it in place  PUTDC A4PTR,DESCR,A2PTR — insert type */
        *((DESCR_t *)A2P(hole + DESCR)) = val_d; /* PUTDC A4PTR,2*DESCR,A3PTR — insert value */
        return list_off; /* A5RTN: return same list */
    }
    DESCR_t *old_title = (DESCR_t *)A2P(list_off); /* AUG1: no hole — allocate a larger block */
    int32_t old_sz = (int32_t)old_title->v;
    int32_t new_sz = old_sz + 2 * DESCR; /* INCRA A4PTR,2*DESCR */
    int32_t new_off = BLOCK_fn(new_sz, B); /* SETVC A4PTR,B — block type */
    if (new_off == 0) return list_off; /* allocation failed */
    *((DESCR_t *)A2P(new_off + new_sz)) = val_d; /* PUTD A5PTR,A4PTR,A3PTR — insert value at end of new block */
    *((DESCR_t *)A2P(new_off + new_sz - DESCR)) = type_d; /* PUTD A5PTR,A4PTR-DESCR,A2PTR — insert type just before */
    /* MOVBLK A5PTR,A1PTR,A4PTR: oracle A4PTR = old_sz after second DECRA.
     * MOVBLK(dst,src,count) copies count bytes from src+DESCR to dst+DESCR. */
    if (old_sz > 0)
        memmove(A2P(new_off + DESCR),
                A2P(list_off + DESCR),
                (size_t)old_sz);
    return new_off; /* A5RTN: return new list */
}

/*====================================================================================================================*/
/* ── DTREP_fn ────────────────────────────────────────────────────────── */
/*
 * v311.sil DTREP (line 1135).
 * Builds a type representation string in DPSP.
 * Returns pointer to DPSP.
 */

SPEC_t *DTREP_fn(DESCR_t *d)
{
    extern SPEC_t DTARSP; /* External scratch specifier for building strings */
    if ((int32_t)d->v == A) { /* VEQLC A2PTR,A — ARRAY? */
        DESCR_t proto = *((DESCR_t *)A2P(d->a.i + DESCR)); /* DTARRY: get prototype string */
        SPEC_t zsp;
        memset(&zsp, 0, sizeof zsp);
        if (proto.a.i != 0) {
            zsp.a = proto.a.i; /* LOCSP ZSP,A3PTR — specifier to prototype string */
            zsp.o = BCDFLD;
            zsp.l = ((DESCR_t *)A2P(proto.a.i))->v;
            zsp.v = S;
        }
        if (zsp.l <= ARRLEN) { /* ACOMPC A3PTR,ARRLEN — check excessive length */
            memset(&DTARSP, 0, sizeof DTARSP); /* Build "ARRAY('proto')" in DTARSP */
            static char dtrep_buf[256]; /* We'll write to a static buffer via DPSP */
            int n = snprintf(dtrep_buf, sizeof dtrep_buf,
                             "ARRAY('%.*s')",
                             (int)zsp.l,
                             zsp.a ? (const char *)A2P(zsp.a) + zsp.o : "");
            DPSP.a = P2A(dtrep_buf);
            DPSP.o = 0;
            DPSP.l = (int32_t)n;
            DPSP.v = S;
            return &DPSP;
        }
    } /* Fall through to DTREP1 for oversized prototype */
    if ((int32_t)d->v == T) { /* VEQLC A2PTR,T — TABLE? */
        DESCR_t *tbl = (DESCR_t *)A2P(d->a.i); /* DTABLE: compute size and extent */
        int32_t tsz = (int32_t)tbl->v;
        int32_t a1ptr = ((DESCR_t *)A2P(d->a.i + tsz))->a.i;
        int32_t a2ptr = ((DESCR_t *)A2P(d->a.i + tsz - DESCR))->a.i;
        while (a1ptr != 1) { /* Walk to find item count (DTABL1 loop) */
            a2ptr += a1ptr; /* SUM */
            a2ptr -= 2 * DESCR;
            a1ptr = ((DESCR_t *)A2P(a1ptr + a2ptr))->a.i;
        }
        int32_t count = (a2ptr - DESCR) / (2 * DESCR);
        int32_t extent = (a2ptr - 2 * DESCR) / (2 * DESCR);
        static char dtrep_buf2[64];
        int n = snprintf(dtrep_buf2, sizeof dtrep_buf2,
                         "TABLE(%d,%d)", (int)count, (int)extent);
        DPSP.a = P2A(dtrep_buf2);
        DPSP.o = 0;
        DPSP.l = (int32_t)n;
        DPSP.v = S;
        return &DPSP;
    }
    if ((int32_t)d->v == R) { /* VEQLC A2PTR,R — REAL? */
        static char rbuf[64]; /* REALST DPSP,A2PTR — convert real to string */
        int n = snprintf(rbuf, sizeof rbuf, "%g", (double)d->a.f);
        DPSP.a = P2A(rbuf);
        DPSP.o = 0;
        DPSP.l = (int32_t)n;
        DPSP.v = S;
        return &DPSP;
    }
    { /* DTREP1: look up type name in DTLIST */
        DESCR_t dt1cl;
        dt1cl.a.i = 0;
        dt1cl.f = 0;
        dt1cl.v = d->v; /* MOVV DT1CL,A2PTR */
        int32_t a3ptr = locapt_fn(P2A(&DTLIST), &dt1cl);
        if (a3ptr != 0) {
            DESCR_t name_d = *((DESCR_t *)A2P(a3ptr + 2 * DESCR)); /* GETDC A3PTR,A3PTR,2*DESCR — get type name descriptor */
            if (name_d.a.i != 0) { /* LOCSP DPSP,A3PTR */
                DPSP.a = name_d.a.i + BCDFLD;
                DPSP.o = 0;
                DPSP.l = ((DESCR_t *)A2P(name_d.a.i))->v;
                DPSP.v = S;
                return &DPSP;
            }
        }
        DPSP = EXDTSP; /* DTREPE: SETSP DPSP,EXDTSP — full specifier copy (oracle) */
        return &DPSP;
    }
}

/*====================================================================================================================*/
/* ── FINDEX_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil FINDEX (line 1195).
 */

int32_t FINDEX_fn(DESCR_t *name_d)
{
    int32_t f2ptr = locapv_fn(D_A(FNCPL), name_d); /* LOCAPV F2PTR,FNCPL,F1PTR — look for function pair */
    if (f2ptr != 0) {
        f2ptr = ((DESCR_t *)A2P(f2ptr + DESCR))->a.i; /* Found — GETDC F2PTR,F2PTR,DESCR — get function descriptor */
        return f2ptr; /* FATBAK: RRTURN F2PTR,1 */
    }
    D_A(NEXFCL) += 2 * DESCR; /* FATNF: not found  INCRA NEXFCL,2*DESCR */
    if (D_A(NEXFCL) > FBLKSZ) { /* ACOMPC NEXFCL,FBLKSZ — check for end of current block */
        int32_t new_blk = BLOCK_fn(FBLKSZ, B); /* FATBLK: allocate new function block — oracle: RCALL FBLOCK,BLOCK,FBLKRQ (type=B) */
        D_A(FBLOCK) = new_blk;
        D_F(FBLOCK) |= FNC; /* SETF FBLOCK,FNC */
        D_V(FBLOCK) = 0; /* SETVC FBLOCK,0 */
        D_A(NEXFCL) = DESCR; /* SETAC NEXFCL,DESCR */
    }
    f2ptr = D_A(FBLOCK) + D_A(NEXFCL); /* FATNXT: SUM F2PTR,FBLOCK,NEXFCL */
    DESCR_t f2d; /* RCALL FNCPL,AUGATL,(FNCPL,F2PTR,F1PTR) */
    f2d.a.i = f2ptr;
    f2d.f = 0;
    f2d.v = 0;
    D_A(FNCPL) = AUGATL_fn(D_A(FNCPL), f2d, *name_d);
    extern DESCR_t UNDFCL; /* PUTDC F2PTR,0,UNDFCL — insert undefined function marker */
    *((DESCR_t *)A2P(f2ptr)) = UNDFCL;
    *((DESCR_t *)A2P(f2ptr + DESCR)) = *name_d; /* PUTDC F2PTR,DESCR,F1PTR — insert name */
    return f2ptr; /* FATBAK */
}
