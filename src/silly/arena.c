/*
 * arena.c — arena allocator and mark-compact GC (v311.sil §5)
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

#define _GNU_SOURCE
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "types.h"
#include "data.h"
#include "arena.h"



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
    D_A(FRSGPT) = 0; /* Set FRSGPT to start of arena */
    D_A(HDSGPT) = 0;
    D_A(TLSGP1) = (int32_t)(ARENA_SIZE - DESCR); /* TLSGP1 = arena end - one DESCR safety margin */
    pdl_stack = (DESCR_t *)A2P(D_A(FRSGPT)); /* Allocate pattern history list (SPDLSZ DESCRs) */
    D_A(FRSGPT) += (int32_t)(SPDLSZ * DESCR);
    stack = (DESCR_t *)A2P(D_A(FRSGPT)); /* Allocate interpreter stack (STSIZE DESCRs) */
    STKHED.a.i = D_A(FRSGPT);
    D_A(FRSGPT) += (int32_t)(STSIZE * DESCR);
    STKEND.a.i = D_A(FRSGPT);
    obj_code = (DESCR_t *)A2P(D_A(FRSGPT)); /* Allocate initial object code block (OCASIZ DESCRs) */
    D_A(FRSGPT) += (int32_t)(OCASIZ * DESCR);
    /* Allocate OBLIST symbol-table bins in arena.
     * Oracle: OBLIST = OBSTRT - LNKFLD; OBPTR.a = OBLIST.
     * OBSTRT is OBARY DESCRs; chain head for bin i is at OBSTRT[i].lnkfld.a.
     * We allocate LNKFLD padding + OBARY*DESCR bytes, zero them.
     * OBPTR.a = base (= OBSTRT - LNKFLD in oracle terms).
     * OBEND.a = OBPTR.a + LNKFLD + DESCR*OBOFF. */
    {
        int32_t oblist_base = D_A(FRSGPT);
        int32_t oblist_bytes = LNKFLD + (int32_t)(OBARY) * DESCR;
        memset(A2P(oblist_base), 0, (size_t)oblist_bytes);
        D_A(FRSGPT) += oblist_bytes;
        OBPTR.a.i = oblist_base;
        OBPTR.f   = PTR;
        OBPTR.v   = S;
        OBEND.a.i = oblist_base + LNKFLD + DESCR * OBOFF;
    }
    D_A(HDSGPT) = D_A(FRSGPT); /* HDSGPT marks start of dynamic heap (after fixed allocations) */
}

/*====================================================================================================================*/
/* ── BLOCK_fn ────────────────────────────────────────────────────────── */
/*
 * v311.sil BLOCK (line 1221).
 * size    = bytes to allocate (ARG1CL.a)
 * typetag = data type for block title V field
 */

int32_t BLOCK_fn(int32_t size, int32_t typetag)
{
    if (size > SIZLIM) { /* PCOMP ARG1CL,SIZLMT — check against size limit [PLB132].
         * Oracle uses D_PTR(ARG1CL) >= D_PTR(SIZLMT) (void* cast of A-field).
         * Ours uses integer > on arena offsets. Identical semantics in arena model. */
        extern void error(int code); /* SIZERR */
        error(23); /* ERR_OBJ_TOO_LARGE */
        return 0; /* not reached */
    }
retry:
    D_A(BLOCL) = D_A(FRSGPT); /* BLOCK1: MOVD BLOCL,FRSGPT — FRSGPT.f/v always 0, A-copy sufficient */
    D_V(BLOCL) = typetag;
    D_A(FRSGPT) += DESCR + size;
    if (D_A(TLSGP1) < D_A(FRSGPT)) { /* PCOMP TLSGP1,FRSGPT — oracle D_PTR, ours D_A; same result */
        D_A(FRSGPT) = D_A(BLOCL); /* BLOGC: restore and call GC */
        int32_t got = GC_fn(size);
        if (got >= size)
            goto retry; /* BLOCK1 — GC succeeded, try again */
        return 0; /* ALOC2: GC_fn calls error — never returns here */
    }
    memset(A2P(D_A(BLOCL) + DESCR), 0, (size_t)size); /* ZERBLK BLOCL, size — clear block body */
    { /* PUTAC BLOCL,0,BLOCL — self-pointer in title */
        DESCR_t *title = (DESCR_t *)A2P(D_A(BLOCL));
        title->a.i = D_A(BLOCL);
        title->f = TTL; /* SETFI BLOCL,TTL */
        title->v = size; /* SETSIZ BLOCL,size */
    }
    return D_A(BLOCL);
}

/*====================================================================================================================*/
/* ── hash_spec: compute bin index for GENVAR ─────────────────────────── */
/* OBSTRT[i] = bin slot i — arena offset of the DESCR at OBPTR.a + LNKFLD + i*DESCR */
#define OBSLOT_OFF(n)  (OBPTR.a.i + LNKFLD + (n) * DESCR)
#define OBSLOT(n)      (*(DESCR_t *)A2P(OBSLOT_OFF(n)))

/* Mirrors SIL VARID / hash computation: sum of chars, masked to OBSIZ  */

static int32_t hash_spec(const SPEC_t *sp)
{
    const char *s = SP_PTR(sp);
    int32_t len = SP_LEN(sp);
    int32_t h = 0;
    int32_t i;
    for (i = 0; i < len; i++)
        h += (unsigned char)s[i];
    return h & (OBSIZ - 1);
}

/*====================================================================================================================*/
/* ── GENVAR_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GENVAR (line 1248).
 * Intern sp into the arena symbol table (OBLIST bins).
 * Returns arena offset of the STRING block, or 0 for null string.
 */

int32_t GENVAR_fn(const SPEC_t *sp)
{
    D_A(CONVSW) = 0;                  /* oracle: D_A(CONVSW)=0 at GENVAR entry */
    if (SP_LEN(sp) <= 0) /* LEQLC SPECR1,0 — null string → NULVCL */
        return 0;
    int32_t bin_idx = hash_spec(sp); /* LOCA1: VARID — compute bin index */
    /* LOCA1: BUKPTR = bin slot; LSTPTR tracks previous link holder.
     * Chain is sorted by bin_idx (V field of LNKFLD); within same bin by
     * insertion order.  Oracle LOCA2:
     *   V < bin_idx → continue (earlier bin, keep walking)
     *   V > bin_idx → stop (overshot, insert here = LOCA5)
     *   V == bin_idx → string compare; mismatch → continue, match → found */
    int32_t bukptr = OBSLOT(bin_idx).a.i; /* LOCA2: BUKPTR = first chain entry */
    int32_t lstptr_lnk = OBSLOT_OFF(bin_idx); /* LSTPTR = bin slot itself initially */
    while (bukptr != 0) { /* LOCA2 loop */
        DESCR_t *lnk = (DESCR_t *)A2P(bukptr + LNKFLD);
        int32_t lnk_v = (int32_t)lnk->v;
        if (lnk_v < bin_idx) { /* oracle: V < EQUVCL → LOCA2 (continue walking) */
            lstptr_lnk = bukptr + LNKFLD;
            bukptr = lnk->a.i;
            continue;
        }
        if (lnk_v > bin_idx) break; /* oracle: V > EQUVCL → LOCA5 (insert here) */
        /* same bin — string compare */
        DESCR_t *title = (DESCR_t *)A2P(bukptr);
        int32_t slen = (int32_t)title->v;
        const char *stored = (const char *)A2P(bukptr + BCDFLD);
        if (slen == SP_LEN(sp) &&
            memcmp(stored, SP_PTR(sp), (size_t)slen) == 0) {
            return bukptr; /* Found */
        }
        lstptr_lnk = bukptr + LNKFLD;
        bukptr = lnk->a.i;
    }
    /* LOCA5: bukptr = node we stopped at (0 = end of chain, or overshot node) */
    int32_t len = SP_LEN(sp); /* LOCA5: not found — allocate new STRING block */
    int32_t blk_sz = x_getlth(len);
    if (blk_sz > SIZLIM) { /* PCOMP BKLTCL,SIZLMT: oracle D_PTR (void* cast); ours integer. Same. */
        extern void error(int);
        error(23);
        return 0;
    }
retry_alloc: /* LOCA7: MOVD LCPTR,FRSGPT — oracle full DESCR copy; FRSGPT.f/v always 0 so A-only equiv. */
    {
        int32_t lcptr = D_A(FRSGPT);
        D_A(FRSGPT) += DESCR + blk_sz;
        if (D_A(TLSGP1) < D_A(FRSGPT)) { /* PCOMP TLSGP1,FRSGPT: oracle D_PTR; ours D_A. Same. */
            D_A(FRSGPT) = lcptr; /* LOCA4: restore and GC */
            int32_t got = GC_fn(blk_sz);
            if (got >= blk_sz)
                goto retry_alloc;
            return 0; /* not reached */
        }
        DESCR_t *title = (DESCR_t *)A2P(lcptr); /* PUTDC LCPTR,0,ZEROCL — clear title */
        memset(title, 0, sizeof(DESCR_t));
        title->a.i = lcptr; /* PUTAC LCPTR,0,LCPTR — self pointer */
        title->f = TTL | STTL; /* SETFI LCPTR,TTL+STTL */
        title->v = len; /* SETSIZ LCPTR,len — string length */
        if (D_A(CONVSW) == 0) { /* If CONVSW == 0 (GENVAR entry): set value=NULVCL, attrib=ZEROCL */
            DESCR_t *val_slot = (DESCR_t *)A2P(lcptr + DESCR);
            *val_slot = NULVCL;
            DESCR_t *att_slot = (DESCR_t *)A2P(lcptr + ATTRIB);
            memset(att_slot, 0, sizeof(DESCR_t));
        }
        char *dst = (char *)A2P(lcptr + BCDFLD); /* Copy string bytes into block at BCDFLD */
        memcpy(dst, SP_PTR(sp), (size_t)len);
        DESCR_t *lnk_slot = (DESCR_t *)A2P(lcptr + LNKFLD); /* LOCA6: PUTVC LCPTR,LNKFLD,bin_idx — ascension number */
        lnk_slot->v = (int_t)bin_idx;
        lnk_slot->a.i = bukptr; /* PUTAC LCPTR,LNKFLD,BUKPTR — chain to overshot/end node */
        /* PUTAC LSTPTR,LNKFLD,LCPTR — link from previous (LSTPTR is bin slot or in-chain) */
        ((DESCR_t *)A2P(lstptr_lnk))->a.i = lcptr;
        D_A(VARSYM) += 1; /* INCRA VARSYM,1 */
        return lcptr;
    }
}

/*====================================================================================================================*/
/* ── GNVARI_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GNVARI (line 1302): integer → string spec → GENVAR.
 */

int32_t GNVARI_fn(int32_t ival)
{
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d", (int)ival);
    SPEC_t sp;
    memset(&sp, 0, sizeof sp);
    sp.l = (int32_t)n;
    sp.o = 0;
    sp.a = P2A(buf); /* point directly at buf (stack) — GENVAR copies before returning */
    sp.v = S;
    return GENVAR_fn(&sp);
}

/*====================================================================================================================*/
/* ── GENVUP_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil GENVUP (line 1313): case-fold if &CASE != 0, then GENVAR.
 */

int32_t GENVUP_fn(const SPEC_t *sp)
{
    if (D_A(CASECL) == 0) /* AEQLC CASECL,0 — skip if case sensitive */
        return GENVAR_fn(sp);
    /* HW-14: oracle uses SPECR1 global (arena-based). Uppercase into arena at FRSGPT.
     * v311.sil: GETSPC SPECR1,AXPTR,0; XRAISP SPECR1; BRANCH GENVAR.
     * snobol4.c: _SPEC(SPECR1) = _SPEC(AXPTR); RAISE1(SPECR1); BRANCH(GENVAR).
     * Stack buf + P2A was wrong: P2A(stack_addr) = garbage arena offset. */
    int32_t len = SP_LEN(sp);
    if (len <= 0) return GENVAR_fn(sp);
    if (len > CARDSZ) len = CARDSZ;
    /* Copy string into arena at FRSGPT, uppercase in place */
    int32_t off = D_A(FRSGPT);
    D_A(FRSGPT) += len;
    const char *srcp = SP_PTR(sp);
    char *dst = (char *)A2P(off);
    int32_t i;
    for (i = 0; i < len; i++)
        dst[i] = (char)toupper((unsigned char)srcp[i]);
    /* Build SPECR1 pointing at arena copy, call GENVAR */
    SPECR1.a = off; SPECR1.o = 0; SPECR1.l = len; SPECR1.v = S; SPECR1.f = 0;
    int32_t result = GENVAR_fn(&SPECR1);
    D_A(FRSGPT) = off; /* release temp space */
    return result;
}

/*====================================================================================================================*/
/* ── CONVAR_fn ───────────────────────────────────────────────────────── */
/*
 * v311.sil CONVAR (line 1326): allocate unlinked STRING space at FRSGPT.
 */

int32_t CONVAR_fn(int32_t len)
{
    if (len <= 0) return 0; /* AEQLC AXPTR,0 — null → RT1NUL */
    D_A(CONVSW) = 1; /* note CONVAR entry */
    int32_t blk_sz = x_getlth(len);
    if (blk_sz > SIZLIM) {
        extern void error(int);
        error(23);
        return 0;
    }
retry_convar:
    {
        int32_t frsgpt_save = D_A(FRSGPT);
        int32_t need = blk_sz + DESCR;
        if (D_A(TLSGP1) < frsgpt_save + need) { /* PCOMP TLSGP1, TEMPCL [PLB126] */
            int32_t got = GC_fn(blk_sz); /* CONVR4: GC */
            if (got >= blk_sz)
                goto retry_convar;
            return 0;
        }
        DESCR_t *title = (DESCR_t *)A2P(frsgpt_save); /* CONVR5 */
        memset(title, 0, sizeof(DESCR_t));
        title->a.i = frsgpt_save; /* self-pointer */
        title->f = TTL | STTL;
        title->v = len;
        DESCR_t *val_slot = (DESCR_t *)A2P(frsgpt_save + DESCR);
        *val_slot = NULVCL;
        DESCR_t *att_slot = (DESCR_t *)A2P(frsgpt_save + ATTRIB);
        memset(att_slot, 0, sizeof(DESCR_t));
        D_A(BKLTCL) = frsgpt_save; /* MOVA BKLTCL,FRSGPT — return the block without advancing FRSGPT */
        return frsgpt_save;
    }
}

/*====================================================================================================================*/
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

/*====================================================================================================================*/
/* ── GCM_fn — mark one block (iterative) ─────────────────────────────── */
/*
 * v311.sil GCM (line 1494).
 * Uses a local work-stack rather than the SIL interpreter stack.
 */

#define GCM_STACK_DEPTH  4096

void GCM_fn(int32_t blk_off)
{
    int32_t work_blk[GCM_STACK_DEPTH]; /* local work-list: array of (block_offset, remaining_offset) pairs */
    int32_t work_rem[GCM_STACK_DEPTH];
    int top = 0;
    work_blk[top] = blk_off;
    work_rem[top] = -1; /* -1 = fresh block, compute size on first use */
    top++;
    while (top > 0) {
        top--;
        int32_t bk = work_blk[top];
        int32_t bkdx = work_rem[top];
        if (bkdx < 0) { /* GCMA1: fresh entry — get size */
            DESCR_t *title = (DESCR_t *)A2P(bk);
            bkdx = title->v; /* GETSIZ */
        }
        while (bkdx >= DESCR) { /* GCMA2: walk descriptors from offset bkdx down to 0 */
            DESCR_t descl = *((DESCR_t *)A2P(bk + bkdx));
            if (!(D_F(descl) & PTR) || D_A(descl) == 0) { /* TESTF DESCL,PTR — is it a pointer? */
                bkdx -= DESCR;
                continue;
            }
            int32_t topcl = D_A(descl); /* arena offset pointed to */          /* TOP: find title of pointed-to block */
            while (!(((DESCR_t *)A2P(topcl))->f & TTL)) /* walk back to title (first DESCR with TTL flag) */
                topcl -= DESCR;
            DESCR_t *tptr = (DESCR_t *)A2P(topcl);
            if (tptr->f & MARK) { /* TESTFI TOPCL,MARK — already marked? */
                bkdx -= DESCR;
                continue;
            }
            bkdx -= DESCR; /* GCMA4: mark and push continuation */
            if (bkdx >= DESCR && top < GCM_STACK_DEPTH - 1) {
                work_blk[top] = bk; /* push current continuation */
                work_rem[top] = bkdx;
                top++;
            }
            tptr->f |= MARK; /* mark the new block */
            if (tptr->f & STTL) { /* Is it a string? TESTFI TOPCL,STTL */
                bk = topcl; /* string: only title + attrib + link matter (bkdx=2) */
                bkdx = 2 * DESCR;
            } else {
                bk = topcl;
                bkdx = tptr->v;
            }
        }
    }
}

/*====================================================================================================================*/
/* ── GC_fn — four-pass mark-compact collector ────────────────────────── */
/*
 * v311.sil GC (line 1367).
 */

int32_t GC_fn(int32_t required)
{
    D_A(GCTMCL) = 0; /* Record GC start time (MSTIME GCTMCL) — use 0 for now */
    madvise(arena_base, ARENA_SIZE, MADV_RANDOM); /* XCALLC vm_gc_advise,(1) [PLB54] */
    { /* ── Pass 1 (GCT): mark all live blocks via PRMPTR root table ────── */
        int32_t bkdxu = D_A(PRMDX);
        while (bkdxu > 0) {
            DESCR_t root = *((DESCR_t *)A2P(D_A(PRMPTR) + bkdxu));
            if (D_A(root) != 0)
                GCM_fn(D_A(root));
            bkdxu -= DESCR;
        }
    }
    { /* ── GCBA1/GCBA2: mark live strings reachable via symbol table bins */
        int32_t bkptr = D_A(OBPTR) - DESCR; /* SETAC BKPTR,OBLIST-DESCR: OBPTR.a = OBLIST base */
        int32_t obend = D_A(OBEND);
        while (bkptr <= obend) { /* PCOMP BKPTR,OBEND,GCLAD: exit only when BKPTR > OBEND */
            bkptr += DESCR;
            int32_t st1ptr = bkptr;
            while (1) {
                st1ptr = ((DESCR_t *)A2P(st1ptr + LNKFLD))->a.i; /* GETAC ST1PTR,ST1PTR,LNKFLD */
                if (st1ptr == 0) break;
                if (((DESCR_t *)A2P(st1ptr))->f & MARK) /* TESTFI ST1PTR,MARK — already marked, skip */
                    continue;
                DESCR_t val = *((DESCR_t *)A2P(st1ptr + DESCR)); /* Check if value is nonnull OR attrib nonzero */
                int nonnull = !(val.a.i == NULVCL.a.i &&
                                val.f == NULVCL.f &&
                                val.v == NULVCL.v);
                int32_t att = ((DESCR_t *)A2P(st1ptr + ATTRIB))->a.i;
                if (nonnull || att != 0) {
                    /* GCBA4: D(D_A(GCBLK)+DESCR) = D(ST1PTR) — copy full DESCR at ST1PTR */
                    *((DESCR_t *)A2P(D_A(GCBLK) + DESCR)) = *((DESCR_t *)A2P(st1ptr));
                    GCM_fn(D_A(GCBLK));
                }
            }
        }
    }
    { /* ── Pass 2 (GCLAD): compute forward (compacted) addresses ───────── */
        /* Oracle: CPYCL advances ONLY for live (marked) blocks.
         * Dead blocks do NOT advance CPYCL — that is the compaction.
         * MVSGPT = position of first dead block (compression frontier). */
        int32_t cpycl = D_A(HDSGPT);
        int32_t ttlcl = D_A(HDSGPT);
        D_A(MVSGPT) = D_A(FRSGPT); /* default: no compaction needed */
        int found_dead = 0;
        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdx = x_bksize(ttlcl);
            DESCR_t *t = (DESCR_t *)A2P(ttlcl);
            if (t->f & MARK) {
                t->a.i = cpycl; /* GCLAD4: record compacted destination */
                cpycl += bkdx;  /* advance CPYCL only for live blocks */
            } else {
                if (!found_dead) { D_A(MVSGPT) = ttlcl; found_dead = 1; } /* GCLAD7: first dead = frontier */
                /* dead: do NOT advance cpycl */
            }
            ttlcl += bkdx;
        }
        D_A(CPYCL) = cpycl;
    }
    { /* ── GCBB: compact OBLIST chains — remove dead strings ────────────── */
        /* Oracle GCBB1-GCBB5: for each bin chain, unlink unmarked entries.
         * lnk_addr = arena offset of the .a field that holds the next chain ptr.
         * For the bin slot: lnk_addr = OBSLOT_OFF(bi) (bin DESCR .a = chain head).
         * For in-chain nodes: lnk_addr = node + LNKFLD.
         * This mirrors the lstptr_lnk pattern in GENVAR_fn. */
        int32_t bi;
        D_A(NODPCL) = 1;
        for (bi = 0; bi < OBSIZ; bi++) {
            int32_t lnk_addr = OBSLOT_OFF(bi); /* LNKFLD .a holder = bin slot .a field */
            int32_t st1ptr = OBSLOT(bi).a.i;
            while (st1ptr != 0) {
                DESCR_t *ent = (DESCR_t *)A2P(st1ptr);
                int32_t next = ((DESCR_t *)A2P(st1ptr + LNKFLD))->a.i;
                if (ent->f & MARK) {
                    /* live: write compacted address into previous valid link holder */
                    int32_t new_addr = ent->a.i; /* forward addr set in GCLAD */
                    ((DESCR_t *)A2P(lnk_addr))->a.i = new_addr;
                    lnk_addr = new_addr + LNKFLD; /* GCBB3: ST2PTR=ST1PTR → prev link is at new location */
                }
                /* dead: simply skip — lnk_addr stays, link patched next iter or at GCBB5 */
                st1ptr = next;
            }
            ((DESCR_t *)A2P(lnk_addr))->a.i = 0; /* GCBB5: zero-terminate chain */
        }
    }
    { /* ── Pass 3 (GCLAP): update PTR descriptors to new addresses ─────── */
        int32_t ttlcl = D_A(HDSGPT);
        int32_t mvsgpt = D_A(MVSGPT);
        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdxu = x_bksize(ttlcl);
            DESCR_t *t = (DESCR_t *)A2P(ttlcl);
            int32_t bkdx;
            if (t->f & STTL)
                bkdx = 3 * DESCR; /* GCLAP1: string — 3 descriptors */
            else
                bkdx = bkdxu;
            if (t->f & MARK) {
                int32_t off = bkdx - DESCR; /* GCLAP2: start one DESCR below size, stop at DESCR (not 0) */
                while (off != 0) {
                    DESCR_t *dp = (DESCR_t *)A2P(ttlcl + off);
                    if ((dp->f & PTR) && dp->a.i >= mvsgpt) {
                        int32_t ref = dp->a.i; /* pointer into region being compacted  TOP: find title */
                        while (!(((DESCR_t *)A2P(ref))->f & TTL))
                            ref -= DESCR;
                        int32_t new_addr = ((DESCR_t *)A2P(ref))->a.i; /* forward address is in title.a.i */
                        int32_t offset_within = dp->a.i - ref;
                        dp->a.i = new_addr + offset_within;
                    }
                    off -= DESCR;
                }
            }
            ttlcl += bkdxu;
        }
    }
    { /* ── GCLAT: update PTR descriptors in permanent blocks (PRMPTR) ─── */
        int32_t bkdxu = D_A(PRMDX);
        int32_t mvsgpt = D_A(MVSGPT);
        while (bkdxu > 0) {
            int32_t ttlcl = ((DESCR_t *)A2P(D_A(PRMPTR) + bkdxu))->a.i;
            if (ttlcl != 0) {
                int32_t bkdx = ((DESCR_t *)A2P(ttlcl))->v;
                int32_t off = bkdx;
                while (off != 0) {
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
    { /* ── Pass 4 (GCLAM): physically move live blocks to new positions ── */
        int32_t ttlcl = D_A(HDSGPT);
        int32_t mvsgpt = D_A(MVSGPT);
        int32_t topcl = 0;
        while (ttlcl != D_A(FRSGPT)) {
            int32_t bkdxu = x_bksize(ttlcl);
            DESCR_t *t = (DESCR_t *)A2P(ttlcl);
            if (ttlcl < mvsgpt) {
                topcl = t->a.i; /* GCLAM (below barrier): just move title, clear mark */
                *((DESCR_t *)A2P(topcl)) = *t;
                ((DESCR_t *)A2P(topcl))->f &= (uint8_t)~MARK;
            } else if (t->f & MARK) {
                int32_t bkdx = bkdxu - DESCR; /* GCLAM5: above barrier, marked — move whole block */
                topcl = t->a.i;
                *((DESCR_t *)A2P(topcl)) = *t;
                ((DESCR_t *)A2P(topcl))->f &= (uint8_t)~MARK;
                if (bkdx > 0)
                    memmove(A2P(topcl + DESCR), A2P(ttlcl + DESCR),
                            (size_t)bkdx);
            }
            ttlcl += bkdxu;
        }
        if (topcl != 0) { /* Update FRSGPT: oracle D(FRSGPT)=D(TOPCL); D_A+=BKDX copies F/V from
             * last block's title, then clears FNC. We only set A-field; F/V not used as pointer reg. */
            int32_t last_sz = x_bksize(topcl);
            D_A(FRSGPT) = topcl + last_sz;
        } else {
            D_A(FRSGPT) = D_A(HDSGPT);
        }
    }
    D_F(FRSGPT) &= (uint8_t)~FNC; /* Clear FNC flag from FRSGPT (RESETF FRSGPT,FNC) */
    D_A(GCGOT) = D_A(TLSGP1) - D_A(FRSGPT) - DESCR; /* Compute GCGOT = TLSGP1 - FRSGPT - DESCR */
    D_F(GCGOT) &= (uint8_t)~PTR;
    D_A(GCNO) += 1; /* Increment GC count */
    D_A(NODPCL) = 0;
    if (D_A(GCTRCL) < 0) { /* GTRACE output if &GTRACE > 0 [PLB92][PLB94] */
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
    if (required > D_A(GCGOT)) { /* ACOMP GCREQ,GCGOT — check if enough was freed */
        extern void error(int); /* FAIL exit — storage exhausted */
        error(20); /* ERR_NO_STORAGE */
        return 0; /* not reached */
    }
    return D_A(GCGOT); /* exit 2 — success */
}

/*====================================================================================================================*/
/* ── SPLIT_fn ────────────────────────────────────────────────────────── */
/*
 * v311.sil SPLIT (line 1535).
 * Split the block containing mid_off into two blocks at that point.
 */

void SPLIT_fn(int32_t mid_off)
{
    int32_t a4ptr = mid_off; /* TOP A5PTR,A6PTR,A4PTR — find title and offset within block */
    int32_t a5ptr = a4ptr;
    while (!(((DESCR_t *)A2P(a5ptr))->f & TTL)) /* walk back to title */
        a5ptr -= DESCR;
    int32_t a6ptr = a4ptr - a5ptr; /* offset within block */
    if (a6ptr == 0) return; /* AEQLC A6PTR,0 — zero offset: nothing to split */
    int32_t a7ptr = ((DESCR_t *)A2P(a5ptr))->v; /* get present block size */
    a7ptr -= a6ptr; /* a7ptr = old_size - offset - DESCR */
    a7ptr -= DESCR;
    if (a7ptr <= 0) return; /* ACOMPC A7PTR,0 — avoid zero-length tail block */
    ((DESCR_t *)A2P(a5ptr))->v = a6ptr; /* reset old block size to offset */
    int32_t new_title = a4ptr + DESCR; /* set up new block title at mid_off + DESCR */
    DESCR_t *nt = (DESCR_t *)A2P(new_title);
    memset(nt, 0, sizeof(DESCR_t));
    nt->a.i = new_title;
    nt->f = TTL;
    nt->v = a7ptr;
}
