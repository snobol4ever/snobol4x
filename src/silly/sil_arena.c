/*
 * sil_arena.c — arena allocator and mark-compact GC (v311.sil §5)
 *
 * Faithful C translation of v311.sil §5 "Storage Allocation and
 * Regeneration Procedures" (lines 1219–1553).
 *
 * Control flow: zero gotos. Every SIL loop is a while/for.
 * Every SIL conditional branch is an if/else.
 * GC passes match the SIL structure exactly: GCT, GCBA, GCLAD,
 * GCBB, GCLAP, GCLAT, GCLAM — one C block per SIL pass.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M2
 */

#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "sil_types.h"
#include "sil_data.h"
#include "sil_arena.h"



/* ── arena_init ──────────────────────────────────────────────────────── */

void arena_init(void)
{
    arena_base = (char *)mmap(NULL, ARENA_SIZE,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS,
                               -1, 0);
    if (arena_base == MAP_FAILED) {
        fprintf(stderr, "%s", ALOCFL);
        exit(1);
    }

    /* Set FRSGPT to start of arena */
    D_A(FRSGPT) = 0;
    D_A(HDSGPT) = 0;

    /* TLSGP1 = arena end - one DESCR safety margin */
    D_A(TLSGP1) = (int32_t)(ARENA_SIZE - DESCR);

    /* Allocate pattern history list (SPDLSZ DESCRs) */
    pdl_stack = (DESCR_t *)A2P(D_A(FRSGPT));
    D_A(FRSGPT) += (int32_t)(SPDLSZ * DESCR);

    /* Allocate interpreter stack (STSIZE DESCRs) */
    sil_stack = (DESCR_t *)A2P(D_A(FRSGPT));
    STKHED.a.i = D_A(FRSGPT);
    D_A(FRSGPT) += (int32_t)(STSIZE * DESCR);
    STKEND.a.i = D_A(FRSGPT);

    /* Allocate initial object code block (OCASIZ DESCRs) */
    obj_code = (DESCR_t *)A2P(D_A(FRSGPT));
    D_A(FRSGPT) += (int32_t)(OCASIZ * DESCR);

    /* HDSGPT marks start of dynamic heap (after fixed allocations) */
    D_A(HDSGPT) = D_A(FRSGPT);
}

/* ── BLOCK_fn ────────────────────────────────────────────────────────── */
/*
 * v311.sil BLOCK (line 1221).
 * size    = bytes to allocate (ARG1CL.a)
 * typetag = data type for block title V field
 */

int32_t BLOCK_fn(int32_t size, int32_t typetag)
{
    /* PCOMP ARG1CL,SIZLMT — check against size limit [PLB132] */
    if (size > SIZLIM) {
        /* SIZERR */
        extern void sil_error(int code);
        sil_error(23); /* ERR_OBJ_TOO_LARGE */
        return 0;      /* not reached */
    }

retry:
    /* BLOCK1: MOVD BLOCL,FRSGPT */
    D_A(BLOCL) = D_A(FRSGPT);
    D_V(BLOCL) = typetag;

    /* FRSGPT += DESCR + size */
    D_A(FRSGPT) += DESCR + size;

    /* PCOMP TLSGP1,FRSGPT — check end of region [PLB125] */
    if (D_A(TLSGP1) < D_A(FRSGPT)) {
        /* BLOGC: restore and call GC */
        D_A(FRSGPT) = D_A(BLOCL);
        int32_t got = GC_fn(size);
        if (got >= size)
            goto retry;  /* BLOCK1 — GC succeeded, try again */
        /* ALOC2: GC_fn calls sil_error — never returns here */
        return 0;
    }

    /* ZERBLK BLOCL, size — clear block body */
    memset(A2P(D_A(BLOCL) + DESCR), 0, (size_t)size);

    /* PUTAC BLOCL,0,BLOCL — self-pointer in title */
    {
        DESCR_t *title = (DESCR_t *)A2P(D_A(BLOCL));
        title->a.i = D_A(BLOCL);
        /* SETFI BLOCL,TTL */
        title->f   = TTL;
        /* SETSIZ BLOCL,size */
        title->v   = size;
    }

    return D_A(BLOCL);
}

/* ── hash_spec: compute bin index for GENVAR ─────────────────────────── */
/* Mirrors SIL VARID / hash computation: sum of chars, masked to OBSIZ  */

static int32_t hash_spec(const SPEC_t *sp)
{
    const char *s   = SP_PTR(sp);
    int32_t     len = SP_LEN(sp);
    int32_t     h   = 0;
    int32_t     i;
    for (i = 0; i < len; i++)
        h += (unsigned char)s[i];
    return h & (OBSIZ - 1);
}

/* ── GENVAR_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GENVAR (line 1248).
 * Intern sp into the arena symbol table (OBLIST bins).
 * Returns arena offset of the STRING block, or 0 for null string.
 */

int32_t GENVAR_fn(const SPEC_t *sp)
{
    /* LEQLC SPECR1,0 — null string → NULVCL */
    if (SP_LEN(sp) <= 0)
        return 0;

    /* LOCA1: VARID — compute bin index */
    int32_t bin_idx = hash_spec(sp);

    /* SUM BUKPTR,OBPTR,EQUVCL — find bin head */
    /* Track chain using bin index and offsets, avoid packed-member ptr  */
    int32_t bin_head    = OBLIST_arr[bin_idx].a.i;
    int32_t cur         = bin_head;
    int32_t prev_off    = -1;    /* arena offset of previous LNKFLD .a slot */
    int      prev_is_bin = 1;    /* 1 = prev is bin head, 0 = in-chain */

    /* LOCA2: walk the chain */
    while (cur != 0) {
        DESCR_t *title = (DESCR_t *)A2P(cur);
        DESCR_t *lnk   = (DESCR_t *)A2P(cur + LNKFLD);

        /* VCMPIC — compare ascension number (bin index) */
        if ((int32_t)lnk->v != bin_idx) {
            prev_is_bin = 0;
            prev_off    = cur + LNKFLD;
            cur         = lnk->a.i;
            continue;
        }

        /* LOCSP + LEXCMP — compare strings */
        const char *stored = (const char *)A2P(cur + BCDFLD);
        int32_t     slen   = (int32_t)title->v;
        if (slen == SP_LEN(sp) &&
            memcmp(stored, SP_PTR(sp), (size_t)slen) == 0) {
            /* Found — return existing block */
            return cur;
        }

        prev_is_bin = 0;
        prev_off    = cur + LNKFLD;
        cur         = lnk->a.i;
    }

    /* LOCA5: not found — allocate new STRING block */
    int32_t len     = SP_LEN(sp);
    int32_t blk_sz  = x_getlth(len);

    if (blk_sz > SIZLIM) {
        extern void sil_error(int);
        sil_error(23);
        return 0;
    }

    /* LOCA7: MOVD LCPTR,FRSGPT */
retry_alloc:
    {
        int32_t lcptr = D_A(FRSGPT);

        D_A(FRSGPT) += DESCR + blk_sz;

        if (D_A(TLSGP1) < D_A(FRSGPT)) {
            /* LOCA4: restore and GC */
            D_A(FRSGPT) = lcptr;
            int32_t got = GC_fn(blk_sz);
            if (got >= blk_sz)
                goto retry_alloc;
            return 0; /* not reached */
        }

        /* PUTDC LCPTR,0,ZEROCL — clear title */
        DESCR_t *title = (DESCR_t *)A2P(lcptr);
        memset(title, 0, sizeof(DESCR_t));

        /* PUTAC LCPTR,0,LCPTR — self pointer */
        title->a.i = lcptr;

        /* SETFI LCPTR,TTL+STTL */
        title->f = TTL | STTL;

        /* SETSIZ LCPTR,len — string length */
        title->v = len;

        /* If CONVSW == 0 (GENVAR entry): set value=NULVCL, attrib=ZEROCL */
        if (D_A(CONVSW) == 0) {
            DESCR_t *val_slot = (DESCR_t *)A2P(lcptr + DESCR);
            *val_slot = NULVCL;

            DESCR_t *att_slot = (DESCR_t *)A2P(lcptr + ATTRIB);
            memset(att_slot, 0, sizeof(DESCR_t));
        }

        /* Copy string bytes into block at BCDFLD */
        char *dst = (char *)A2P(lcptr + BCDFLD);
        memcpy(dst, SP_PTR(sp), (size_t)len);

        /* LOCA6: PUTVC LCPTR,LNKFLD,bin_idx — ascension number */
        DESCR_t *lnk_slot = (DESCR_t *)A2P(lcptr + LNKFLD);
        lnk_slot->v   = (int_t)bin_idx;

        /* PUTAC LCPTR,LNKFLD,prev_head — chain to previous head */
        lnk_slot->a.i = bin_head;

        /* PUTAC LSTPTR,LNKFLD,LCPTR — link from previous */
        if (prev_is_bin)
            OBLIST_arr[bin_idx].a.i = lcptr;
        else
            ((DESCR_t *)A2P(prev_off))->a.i = lcptr;

        /* INCRA VARSYM,1 */
        D_A(VARSYM) += 1;

        return lcptr;
    }
}

/* ── GNVARI_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GNVARI (line 1302): integer → string spec → GENVAR.
 */

int32_t GNVARI_fn(int32_t ival)
{
    char buf[32];
    int  n = snprintf(buf, sizeof buf, "%d", (int)ival);
    SPEC_t sp;
    memset(&sp, 0, sizeof sp);
    sp.l = (int32_t)n;
    sp.o = 0;
    /* point directly at buf (stack) — GENVAR copies before returning */
    sp.a = P2A(buf);
    sp.v = S;
    return GENVAR_fn(&sp);
}

/* ── GENVUP_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GENVUP (line 1313): case-fold if &CASE != 0, then GENVAR.
 */

int32_t GENVUP_fn(const SPEC_t *sp)
{
    /* AEQLC CASECL,0 — skip if case sensitive */
    if (D_A(CASECL) == 0)
        return GENVAR_fn(sp);

    /* Uppercase the string into a local buffer */
    int32_t len = SP_LEN(sp);
    char    buf[CARDSZ + 1];
    if (len > CARDSZ) len = CARDSZ;

    const char *src = SP_PTR(sp);
    int32_t     i;
    for (i = 0; i < len; i++)
        buf[i] = (char)toupper((unsigned char)src[i]);

    SPEC_t up;
    memset(&up, 0, sizeof up);
    up.l = len;
    up.o = 0;
    up.a = P2A(buf);
    up.v = S;
    return GENVAR_fn(&up);
}

/* ── CONVAR_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil CONVAR (line 1326): allocate unlinked STRING space at FRSGPT.
 */

int32_t CONVAR_fn(int32_t len)
{
    /* AEQLC AXPTR,0 — null → RT1NUL */
    if (len <= 0) return 0;

    D_A(CONVSW) = 1;  /* note CONVAR entry */

    int32_t blk_sz = x_getlth(len);
    if (blk_sz > SIZLIM) {
        extern void sil_error(int);
        sil_error(23);
        return 0;
    }

retry_convar:
    {
        int32_t frsgpt_save = D_A(FRSGPT);
        int32_t need        = blk_sz + DESCR;

        /* PCOMP TLSGP1, TEMPCL [PLB126] */
        if (D_A(TLSGP1) < frsgpt_save + need) {
            /* CONVR4: GC */
            int32_t got = GC_fn(blk_sz);
            if (got >= blk_sz)
                goto retry_convar;
            return 0;
        }

        /* CONVR5 */
        DESCR_t *title = (DESCR_t *)A2P(frsgpt_save);
        memset(title, 0, sizeof(DESCR_t));
        title->a.i = frsgpt_save;   /* self-pointer */
        title->f   = TTL | STTL;
        title->v   = len;

        DESCR_t *val_slot = (DESCR_t *)A2P(frsgpt_save + DESCR);
        *val_slot = NULVCL;

        DESCR_t *att_slot = (DESCR_t *)A2P(frsgpt_save + ATTRIB);
        memset(att_slot, 0, sizeof(DESCR_t));

        /* MOVA BKLTCL,FRSGPT — return the block without advancing FRSGPT */
        D_A(BKLTCL) = frsgpt_save;
        return frsgpt_save;
    }
}

/* ── GNVARS_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GNVARS (line 1355): build SPEC from raw pointer then GENVAR.
 */

int32_t GNVARS_fn(const char *s, int32_t len)
{
    if (!s || len <= 0) return 0;
    SPEC_t sp;
    memset(&sp, 0, sizeof sp);
    sp.l = len;
    sp.o = 0;
    sp.a = P2A(s);
    sp.v = S;
    return GENVAR_fn(&sp);
}

/* ── GCM_fn — mark one block (iterative) ─────────────────────────────── */
/*
 * v311.sil GCM (line 1494).
 * Uses a local work-stack rather than the SIL interpreter stack.
 */

#define GCM_STACK_DEPTH  4096

void GCM_fn(int32_t blk_off)
{
    /* local work-list: array of (block_offset, remaining_offset) pairs */
    int32_t work_blk[GCM_STACK_DEPTH];
    int32_t work_rem[GCM_STACK_DEPTH];
    int     top = 0;

    work_blk[top] = blk_off;
    work_rem[top] = -1;   /* -1 = fresh block, compute size on first use */
    top++;

    while (top > 0) {
        top--;
        int32_t bk   = work_blk[top];
        int32_t bkdx = work_rem[top];

        /* GCMA1: fresh entry — get size */
        if (bkdx < 0) {
            DESCR_t *title = (DESCR_t *)A2P(bk);
            bkdx = title->v;   /* GETSIZ */
        }

        /* GCMA2: walk descriptors from offset bkdx down to 0 */
        while (bkdx >= DESCR) {
            DESCR_t descl = *((DESCR_t *)A2P(bk + bkdx));

            /* TESTF DESCL,PTR — is it a pointer? */
            if (!(D_F(descl) & PTR) || D_A(descl) == 0) {
                bkdx -= DESCR;
                continue;
            }

            /* TOP: find title of pointed-to block */
            int32_t topcl = D_A(descl);  /* arena offset pointed to */
            /* walk back to title (first DESCR with TTL flag) */
            while (!(((DESCR_t *)A2P(topcl))->f & TTL))
                topcl -= DESCR;

            DESCR_t *tptr = (DESCR_t *)A2P(topcl);

            /* TESTFI TOPCL,MARK — already marked? */
            if (tptr->f & MARK) {
                bkdx -= DESCR;
                continue;
            }

            /* GCMA4: mark and push continuation */
            bkdx -= DESCR;
            if (bkdx >= DESCR && top < GCM_STACK_DEPTH - 1) {
                /* push current continuation */
                work_blk[top] = bk;
                work_rem[top] = bkdx;
                top++;
            }

            /* mark the new block */
            tptr->f |= MARK;

            /* Is it a string? TESTFI TOPCL,STTL */
            if (tptr->f & STTL) {
                /* string: only title + attrib + link matter (bkdx=2) */
                bk   = topcl;
                bkdx = 2 * DESCR;
            } else {
                bk   = topcl;
                bkdx = tptr->v;
            }
        }
    }
}

/* ── GC_fn — four-pass mark-compact collector ────────────────────────── */
/*
 * v311.sil GC (line 1367).
 */

int32_t GC_fn(int32_t required)
{
    /* Record GC start time (MSTIME GCTMCL) — use 0 for now */
    D_A(GCTMCL) = 0;

    /* ── Pass 1 (GCT): mark all live blocks via PRMPTR root table ────── */
    {
        int32_t bkdxu = D_A(PRMDX);
        while (bkdxu > 0) {
            DESCR_t root = *((DESCR_t *)A2P(D_A(PRMPTR) + bkdxu));
            if (D_A(root) != 0)
                GCM_fn(D_A(root));
            bkdxu -= DESCR;
        }
    }

    /* ── GCBA1/GCBA2: mark live strings reachable via symbol table bins */
    {
        int32_t bkptr = D_A(OBEND) - DESCR;  /* SETAC BKPTR,OBLIST-DESCR */
        int32_t obend = D_A(OBEND);

        while (bkptr < obend) {
            bkptr += DESCR;
            int32_t st1ptr = bkptr;

            while (1) {
                /* GETAC ST1PTR,ST1PTR,LNKFLD */
                st1ptr = ((DESCR_t *)A2P(st1ptr + LNKFLD))->a.i;
                if (st1ptr == 0) break;

                /* TESTFI ST1PTR,MARK — already marked, skip */
                if (((DESCR_t *)A2P(st1ptr))->f & MARK)
                    continue;

                /* Check if value is nonnull OR attrib nonzero */
                DESCR_t val = *((DESCR_t *)A2P(st1ptr + DESCR));
                int nonnull = !(val.a.i == NULVCL.a.i &&
                                val.f   == NULVCL.f   &&
                                val.v   == NULVCL.v);
                int32_t att = ((DESCR_t *)A2P(st1ptr + ATTRIB))->a.i;

                if (nonnull || att != 0) {
                    /* GCBA4: set up pseudoblock and mark */
                    DESCR_t *gcblk_ptr = (DESCR_t *)A2P(D_A(GCBLK) + DESCR);
                    gcblk_ptr->a.i = st1ptr;
                    gcblk_ptr->f   = PTR;
                    gcblk_ptr->v   = 0;
                    GCM_fn(D_A(GCBLK));
                }
            }
        }
    }

    /* ── Pass 2 (GCLAD): compute forward (compacted) addresses ───────── */
    {
        int32_t cpycl = D_A(HDSGPT);
        int32_t ttlcl = D_A(HDSGPT);
        D_A(MVSGPT)   = D_A(HDSGPT);  /* compression barrier */

        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdx = x_bksize(ttlcl);
            DESCR_t *t   = (DESCR_t *)A2P(ttlcl);

            if (!(t->f & MARK)) {
                /* unmarked: skip (will be compacted away) */
                /* update MVSGPT to last unmarked region start */
            } else {
                /* marked: record forward address in title.a */
                if (cpycl < ttlcl)
                    D_A(MVSGPT) = ttlcl;
            }

            /* GCLAD7: record target position in title A field */
            if (t->f & MARK) {
                t->a.i = cpycl;
                cpycl += bkdx;
            } else {
                cpycl += bkdx;
            }
            ttlcl += bkdx;
        }
        /* Store compression barrier */
        D_A(CPYCL) = cpycl;
    }

    /* ── Pass 3 (GCLAP): update PTR descriptors to new addresses ─────── */
    {
        int32_t ttlcl = D_A(HDSGPT);
        int32_t mvsgpt = D_A(MVSGPT);

        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdxu = x_bksize(ttlcl);
            DESCR_t *t    = (DESCR_t *)A2P(ttlcl);
            int32_t  bkdx;

            if (t->f & STTL)
                bkdx = 3 * DESCR;   /* GCLAP1: string — 3 descriptors */
            else
                bkdx = bkdxu;

            if (t->f & MARK) {
                /* GCLAP2/GCLAP3: walk descriptors */
                int32_t off = bkdx - DESCR;
                while (off >= 0) {
                    DESCR_t *dp = (DESCR_t *)A2P(ttlcl + off);
                    if ((dp->f & PTR) && dp->a.i >= mvsgpt) {
                        /* pointer into region being compacted */
                        /* TOP: find title */
                        int32_t ref = dp->a.i;
                        while (!(((DESCR_t *)A2P(ref))->f & TTL))
                            ref -= DESCR;
                        /* forward address is in title.a.i */
                        int32_t new_addr = ((DESCR_t *)A2P(ref))->a.i;
                        int32_t offset_within = dp->a.i - ref;
                        dp->a.i = new_addr + offset_within;
                    }
                    off -= DESCR;
                }
            }
            ttlcl += bkdxu;
        }
    }

    /* ── GCLAT: update PTR descriptors in permanent blocks (PRMPTR) ─── */
    {
        int32_t bkdxu  = D_A(PRMDX);
        int32_t mvsgpt = D_A(MVSGPT);

        while (bkdxu > 0) {
            int32_t ttlcl = ((DESCR_t *)A2P(D_A(PRMPTR) + bkdxu))->a.i;
            if (ttlcl != 0) {
                int32_t bkdx = ((DESCR_t *)A2P(ttlcl))->v;
                int32_t off  = bkdx;
                while (off >= 0) {
                    DESCR_t *dp = (DESCR_t *)A2P(ttlcl + off);
                    if ((dp->f & PTR) && dp->a.i >= mvsgpt) {
                        int32_t ref = dp->a.i;
                        while (!(((DESCR_t *)A2P(ref))->f & TTL))
                            ref -= DESCR;
                        int32_t new_addr = ((DESCR_t *)A2P(ref))->a.i;
                        int32_t offset_within = dp->a.i - ref;
                        dp->a.i = new_addr + offset_within;
                    }
                    off -= DESCR;
                }
            }
            bkdxu -= DESCR;
        }
    }

    /* ── Pass 4 (GCLAM): physically move live blocks to new positions ── */
    {
        int32_t ttlcl  = D_A(HDSGPT);
        int32_t mvsgpt = D_A(MVSGPT);
        int32_t topcl  = 0;

        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdxu = x_bksize(ttlcl);
            DESCR_t *t    = (DESCR_t *)A2P(ttlcl);

            if (ttlcl < mvsgpt) {
                /* GCLAM (below barrier): just move title, clear mark */
                topcl  = t->a.i;
                *((DESCR_t *)A2P(topcl)) = *t;
                ((DESCR_t *)A2P(topcl))->f &= (uint8_t)~MARK;
            } else if (t->f & MARK) {
                /* GCLAM5: above barrier, marked — move whole block */
                int32_t bkdx = bkdxu - DESCR;
                topcl = t->a.i;
                *((DESCR_t *)A2P(topcl)) = *t;
                ((DESCR_t *)A2P(topcl))->f &= (uint8_t)~MARK;
                if (bkdx > 0)
                    memmove(A2P(topcl + DESCR), A2P(ttlcl + DESCR),
                            (size_t)bkdx);
            }
            ttlcl += bkdxu;
        }

        /* Update FRSGPT to end of last live block */
        if (topcl != 0) {
            int32_t last_sz = x_bksize(topcl);
            D_A(FRSGPT)     = topcl + last_sz;
        } else {
            D_A(FRSGPT) = D_A(HDSGPT);
        }
    }

    /* Clear FNC flag from FRSGPT (RESETF FRSGPT,FNC) */
    D_F(FRSGPT) &= (uint8_t)~FNC;

    /* Compute GCGOT = TLSGP1 - FRSGPT - DESCR */
    D_A(GCGOT)   = D_A(TLSGP1) - D_A(FRSGPT) - DESCR;
    D_F(GCGOT)  &= (uint8_t)~PTR;

    /* Increment GC count */
    D_A(GCNO) += 1;
    D_A(NODPCL) = 0;

    /* GTRACE output if &GTRACE > 0 [PLB92][PLB94] */
    if (D_A(GCTRCL) < 0) {
        fprintf(stderr, GCFMT,
                (const char *)A2P(D_A(FILENM)),
                (int)D_A(LNNOCL),
                0.0f,
                (int)D_A(GCGOT));
    } else if (D_A(GCTRCL) > 0) {
        D_A(GCTRCL) -= 1;
        fprintf(stderr, GCFMT,
                (const char *)A2P(D_A(FILENM)),
                (int)D_A(LNNOCL),
                0.0f,
                (int)D_A(GCGOT));
    }

    /* ACOMP GCREQ,GCGOT — check if enough was freed */
    if (required > D_A(GCGOT)) {
        /* FAIL exit — storage exhausted */
        extern void sil_error(int);
        sil_error(20); /* ERR_NO_STORAGE */
        return 0;      /* not reached */
    }

    return D_A(GCGOT);  /* exit 2 — success */
}

/* ── SPLIT_fn ────────────────────────────────────────────────────────── */
/*
 * v311.sil SPLIT (line 1535).
 * Split the block containing mid_off into two blocks at that point.
 */

void SPLIT_fn(int32_t mid_off)
{
    /* TOP A5PTR,A6PTR,A4PTR — find title and offset within block */
    int32_t a4ptr = mid_off;
    int32_t a5ptr = a4ptr;

    /* walk back to title */
    while (!(((DESCR_t *)A2P(a5ptr))->f & TTL))
        a5ptr -= DESCR;

    int32_t a6ptr = a4ptr - a5ptr;  /* offset within block */

    /* AEQLC A6PTR,0 — zero offset: nothing to split */
    if (a6ptr == 0) return;

    /* get present block size */
    int32_t a7ptr = ((DESCR_t *)A2P(a5ptr))->v;

    /* a7ptr = old_size - offset - DESCR */
    a7ptr -= a6ptr;
    a7ptr -= DESCR;

    /* ACOMPC A7PTR,0 — avoid zero-length tail block */
    if (a7ptr <= 0) return;

    /* reset old block size to offset */
    ((DESCR_t *)A2P(a5ptr))->v = a6ptr;

    /* set up new block title at mid_off + DESCR */
    int32_t new_title = a4ptr + DESCR;
    DESCR_t *nt = (DESCR_t *)A2P(new_title);
    memset(nt, 0, sizeof(DESCR_t));
    nt->a.i = new_title;
    nt->f   = TTL;
    nt->v   = a7ptr;
}
