/*
 * symtab.h — symbol table and support procedures (v311.sil §4)
 *
 * Faithful C translation of v311.sil §4 "Support Procedures"
 * (lines 1088–1218): AUGATL, CODSKP, DTREP, FINDEX.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M3
 */

#ifndef SIL_SYMTAB_H
#define SIL_SYMTAB_H

#include "types.h"

/* ── AUGATL — augment a pair list ───────────────────────────────────── */
/*
 * v311.sil AUGATL (line 1092).
 * Adds (type, value) pair to list at list_off.
 * Finds first hole (zero entry) and fills it; or allocates a larger
 * block, copies the old list, appends the new pair at the end.
 * Returns arena offset of the (possibly new) list block.
 */
int32_t AUGATL_fn(int32_t list_off, DESCR_t type_d, DESCR_t val_d);

/* ── CODSKP — skip object code descriptors ──────────────────────────── */
/*
 * v311.sil CODSKP (line 1116).
 * Advances OCICL past n non-function descriptors in the current code
 * block (OCBSCL). FNC-flagged descriptors carry an argument count in V;
 * CODSKP recurses to skip those arguments too.
 * Mirrors the SIL recursive call via a while loop + stack in C.
 */
void CODSKP_fn(int32_t n);

/* ── DTREP — produce a printable data-type representation ───────────── */
/*
 * v311.sil DTREP (line 1135).
 * Given a value DESCR, returns a SPEC_t* pointing to a human-readable
 * type string in DPSP:
 *   REAL   → the real number as a string (REALST)
 *   ARRAY  → "ARRAY('prototype')"
 *   TABLE  → "TABLE(size,extent)"
 *   other  → looked up in DTLIST by type code
 *   unknown→ "EXTERNAL"
 * Returns pointer to DPSP (the output specifier, in data).
 */
SPEC_t *DTREP_fn(DESCR_t *d);

/* ── FINDEX — find or create function descriptor block ──────────────── */
/*
 * v311.sil FINDEX (line 1195).
 * Given a name descriptor F1PTR, looks it up in the function pair list
 * (FNCPL). If found, returns the existing function descriptor.
 * If not found, allocates a slot in the current function block (FBLOCK),
 * augments FNCPL with the new (descriptor, name) pair, marks it
 * undefined (UNDFCL), and returns the new descriptor's arena offset.
 * Returns arena offset of the function descriptor DESCR.
 */
int32_t FINDEX_fn(DESCR_t *name_d);

/* ── locapt — locate in attribute list by key ───────────────────────── */
/*
 * Used by AUGATL and DTREP (LOCAPT macro in SIL).
 * Walks a pair list (alternating type/value DESCRs) looking for a zero
 * (hole) entry if key is ZEROCL, or a matching type entry otherwise.
 * Returns arena offset of the matching pair's first DESCR, or 0.
 */
int32_t locapt_fn(int32_t list_off, DESCR_t *key_d);

/* ── locapv — locate in value list by key ────────────────────────────── */
/*
 * Used by FINDEX (LOCAPV macro in SIL).
 * Like locapt but matches on value field rather than type.
 * Returns arena offset of the matching value DESCR, or 0.
 */
int32_t locapv_fn(int32_t list_off, DESCR_t *key_d);

#endif /* SIL_SYMTAB_H */
