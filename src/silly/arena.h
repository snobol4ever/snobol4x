/*
 * arena.h — arena allocator and GC (v311.sil §5)
 *
 * Faithful C translation of v311.sil §5 "Storage Allocation and
 * Regeneration Procedures" (lines 1219–1553):
 *   BLOCK, GENVAR, GNVARI, GENVUP, CONVAR, GNVARS, GC, GCM, SPLIT
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M2
 */

#ifndef SIL_ARENA_H
#define SIL_ARENA_H

#include "types.h"

/* ── Arena initialisation ────────────────────────────────────────────── */

/* arena_init: mmap 128 MB, set arena_base, FRSGPT, TLSGP1, HDSGPT.
 * Must be called before any other arena function.                       */
void arena_init(void);

/* ── BLOCK — allocate a block in the arena ───────────────────────────── */
/*
 * v311.sil BLOCK (line 1221):
 *   POP ARG1CL          — size to allocate
 *   check <= SIZLMT
 *   set BLOCL = FRSGPT
 *   FRSGPT += DESCR + ARG1CL
 *   if FRSGPT > TLSGP1 → call GC
 *   ZERBLK BLOCL        — clear block
 *   set self-pointer, TTL flag, size
 *   RRTURN BLOCL, 1     — return arena offset of block title
 *
 * Returns arena offset of the new block's title DESCR, or 0 on fatal
 * storage exhaustion (after GC).
 * The V field of the title is set to the caller's data-type tag.
 */
int32_t BLOCK_fn(int32_t size, int32_t typetag);

/* ── GENVAR — intern a string specifier into the arena ──────────────── */
/*
 * v311.sil GENVAR (line 1248):
 *   Look up SPECR1 in hash table (OBLIST bins by ascension number).
 *   If found: return existing arena offset.
 *   If not found: allocate new STRING block, copy bytes, link into bin.
 *   Null specifier → RRTURN to RT1NUL (returns NULVCL).
 *
 * Returns arena offset of the STRING block, or 0 for null string.
 */
int32_t GENVAR_fn(const SPEC_t *sp);

/* ── GNVARI — GENVAR for integer (convert then intern) ──────────────── */
/*
 * v311.sil GNVARI (line 1302):
 *   INTSPC converts integer DESCR to string in SPECR1, then → LOCA1.
 */
int32_t GNVARI_fn(int32_t ival);

/* ── GENVUP — GENVAR with case folding (honours &CASE) ──────────────── */
/*
 * v311.sil GENVUP (line 1313):
 *   If CASECL != 0: uppercase the specifier, then GENVAR.
 *   If CASECL == 0: straight GENVAR.
 */
int32_t GENVUP_fn(const SPEC_t *sp);

/* ── CONVAR — allocate unlinked space for a new variable ─────────────── */
/*
 * v311.sil CONVAR (line 1326):
 *   Allocate BKLTCL bytes (= X_GETLTH(len)) in-place at FRSGPT.
 *   Sets self-pointer, TTL+STTL, size, value=NULVCL, attrib=ZEROCL.
 *   Does NOT link into hash table — caller does that.
 *   Returns arena offset of the new block.
 */
int32_t CONVAR_fn(int32_t len);

/* ── GNVARS — intern a raw C string ──────────────────────────────────── */
/*
 * v311.sil GNVARS (line 1355):
 *   Build a SPEC_t from (s, len), then → GENVAR path (LOCA1).
 */
int32_t GNVARS_fn(const char *s, int32_t len);

/* ── GC — storage regeneration (mark-compact) ───────────────────────── */
/*
 * v311.sil GC (line 1367): four-pass mark-compact collector.
 *   Pass 1 (GCT):   mark all live blocks via PRMPTR root table + bins.
 *   Pass 2 (GCLAD): compute forward addresses (sliding compaction).
 *   Pass 3 (GCLAP): update all PTR-flagged DESCRs to new addresses.
 *   Pass 4 (GCLAM): physically move blocks to compacted positions.
 *   Then: update FRSGPT, compute GCGOT (bytes freed).
 *   Returns bytes freed; exits via ALOC2 (fatal) if GCGOT < required.
 *
 * SIL exit 2 = success (GCGOT >= required).
 * SIL exit 1 (ALOC2) = storage exhausted — calls error(ERR_NO_STORAGE).
 */
int32_t GC_fn(int32_t required);

/* ── GCM — mark one block and all blocks it points to ───────────────── */
/*
 * v311.sil GCM (line 1494): iterative mark using explicit stack.
 *   For each PTR descriptor in the block: follow to title, mark, recurse.
 *   Uses the SIL interpreter stack as a work-list (PUSH/POP in SIL).
 *   In C: uses a local stack array (no PUSH/POP of global SIL stack).
 */
void GCM_fn(int32_t blk_off);

/* ── SPLIT — split a block at a given offset ─────────────────────────── */
/*
 * v311.sil SPLIT (line 1535):
 *   Given a pointer to the middle of a block, split into two blocks.
 *   Used by string operations that need to subdivide an allocated region.
 *   Returns via RTN1.
 */
void SPLIT_fn(int32_t mid_off);

/* ── Block layout helpers ─────────────────────────────────────────────── */

/* X_GETLTH: compute storage size for a string of len bytes.
 * Mirrors SIL macro X_GETLTH: DESCR * (3 + (len-1)/CPD + 1)
 * = space for title + attrib + link + string data rounded up to DESCRs */
static inline int32_t x_getlth(int32_t len)
{
    if (len <= 0) return 3 * DESCR;
    return DESCR * (3 + ((len - 1) / CPD + 1));
}

/*====================================================================================================================*/
/* X_BKSIZE: get total block size from title DESCR.
 * If STTL set: string block = DESCR*(4 + (v-1)/CPD + 1)
 * Otherwise:   generic block = v + DESCR                               */
static inline int32_t x_bksize(int32_t title_off)
{
    DESCR_t *t = (DESCR_t *)A2P(title_off);
    if (D_F(*t) & STTL)
        return DESCR * (4 + ((D_V(*t) - 1) / CPD + 1));
    return D_V(*t) + DESCR;
}

/*====================================================================================================================*/
#endif /* SIL_ARENA_H */
