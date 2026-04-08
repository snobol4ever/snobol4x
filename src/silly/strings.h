/*
 * sil_strings.h — string helper procedures (v311.sil §5 string ops + §4 DTREP string ops)
 *
 * Faithful C translation of the SIL string macro operations:
 *   APDSP  — append specifier to buffer specifier
 *   REMSP  — remove leading match (specifier arithmetic)
 *   TRIMSP — trim trailing blanks
 *   LEXCMP — lexicographic compare of two specifiers
 *   SPCINT — parse integer from specifier
 *   SPREAL — parse real from specifier
 *   REALST — format real to specifier (static buffer)
 *   INTSPC — format integer to specifier (static buffer)
 *   LOCSP  — build specifier from a STRING DESCR_t
 *   SUBSP  — substring specifier (offset + length into existing spec)
 *
 * NOTE: x_getlth is declared in sil_arena.h — do not duplicate here.
 * NOTE: DTREP_fn is in sil_symtab.c — do not duplicate here.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date:    2026-04-06
 * Milestone: M3
 */

#ifndef SIL_STRINGS_H
#define SIL_STRINGS_H

#include "types.h"

/* ── APDSP — append specifier STR to buffer specifier BASE ──────────── */
/*
 * v311.sil APDSP macro (macros.h line 86):
 *   if (S_L(STR) > 0) { apdsp(BASE, STR); }
 * apdsp copies SP_LEN(str) bytes from SP_PTR(str) to SP_PTR(base)+SP_LEN(base)
 * and increments base->l by str->l.
 *
 * Precondition: base must have enough allocated arena space after its
 * current end to hold str's bytes (caller's responsibility).
 * No-op if str->l == 0.
 */
void APDSP_fn(SPEC_t *base, const SPEC_t *str);

/* ── REMSP — remove leading match (specifier slice) ─────────────────── */
/*
 * v311.sil X_REMSP(A,B,C) — A = B with C's length removed from front:
 *   A.a = B.a; A.f = B.f; A.v = B.v;
 *   A.o = B.o + C.l;
 *   A.l = B.l - C.l;
 * A may alias B; C must not alias A.
 * No bounds check — caller ensures C.l <= B.l.
 */
void REMSP_fn(SPEC_t *dst, const SPEC_t *src, const SPEC_t *match);

/* ── TRIMSP — trim trailing blanks (spaces) ──────────────────────────── */
/*
 * v311.sil TRIMSP(A,B): A = B with trailing spaces removed.
 * Scans backwards from SP_PTR(b)+b->l-1, reducing length for each ' '.
 * A may alias B.
 * Result: dst->l reduced; all other fields copied from src.
 */
void TRIMSP_fn(SPEC_t *dst, const SPEC_t *src);

/* ── LEXCMP — lexicographic compare ─────────────────────────────────── */
/*
 * v311.sil LEXCMP: compare two specifiers byte-by-byte.
 * Returns:
 *   < 0  if a < b
 *   = 0  if a == b (same bytes AND same length)
 *   > 0  if a > b
 * Shorter string is less if it is a prefix of the longer.
 */
int LEXCMP_fn(const SPEC_t *a, const SPEC_t *b);

/* ── SPCINT — parse integer from specifier ───────────────────────────── */
/*
 * v311.sil SPCINT(dp, sp): convert string specifier to INTEGER DESCR.
 * Reads decimal integer from sp's bytes (strtol semantics).
 * Returns OK  (1) and sets dp->{a.i=val, f=0, v=I} on success.
 * Returns FAIL(0) on parse failure (non-numeric, empty, overflow).
 * Strips leading whitespace when SPITBOL mode is active (not wired
 * here — pass pre-stripped specifier if needed; we always strip).
 */
RESULT_t SPCINT_fn(DESCR_t *dp, const SPEC_t *sp);

/* ── SPREAL — parse real from specifier ─────────────────────────────── */
/*
 * v311.sil SPREAL(dp, sp): convert string specifier to REAL DESCR.
 * Returns OK  (1) and sets dp->{a.f=val, f=0, v=R} on success.
 * Returns FAIL(0) on parse failure.
 */
RESULT_t SPREAL_fn(DESCR_t *dp, const SPEC_t *sp);

/* ── REALST — format real to specifier (points into static buffer) ───── */
/*
 * v311.sil REALST(sp, dp): convert REAL DESCR dp to string, setting sp
 * to point at a static 64-byte buffer containing the formatted value.
 * Format: "%g" with trailing "." appended if no dot/exponent present
 * (matching v311 behaviour: integers print as "1." not "1").
 *
 * WARNING: sp->a is a raw C pointer cast to int32_t (not an arena
 * offset) — the static buffer lives in .bss, not the arena.  Callers
 * must consume the specifier before the next REALST/INTSPC call.
 */
void REALST_fn(SPEC_t *sp, const DESCR_t *dp);

/* ── INTSPC — format integer to specifier (points into static buffer) ── */
/*
 * v311.sil INTSPC(sp, dp): convert INTEGER DESCR dp to decimal string,
 * setting sp to point at a static 32-byte buffer.
 * Same static-buffer lifetime caveat as REALST.
 */
void INTSPC_fn(SPEC_t *sp, const DESCR_t *dp);

/* ── LOCSP — build specifier from a STRING block DESCR_t ─────────────── */
/*
 * v311.sil X_LOCSP(A, B):
 *   if D_A(B) == 0: sp->l = 0 (null specifier)
 *   else:
 *     sp->a = D_A(B)          (arena offset of STRING block)
 *     sp->f = D_F(B)
 *     sp->v = D_V(B)
 *     sp->o = BCDFLD          (byte offset of string data in block)
 *     sp->l = D_V(*title)     (length stored in block's title V field)
 *
 * B is a DESCR_t whose A field is an arena offset to a STRING title.
 * B may be zero (null variable) — produces zero-length specifier.
 */
void LOCSP_fn(SPEC_t *sp, const DESCR_t *dp);

/* ── SUBSP — substring specifier ─────────────────────────────────────── */
/*
 * Not a direct SIL macro name, but used throughout for slicing:
 *   dst = src sliced to [off, off+len).
 * Equivalent to:
 *   dst->a = src->a; dst->f = src->f; dst->v = src->v;
 *   dst->o = src->o + off;
 *   dst->l = len;
 * No bounds check — caller must ensure off+len <= src->l.
 */
void SUBSP_fn(SPEC_t *dst, const SPEC_t *src, int32_t off, int32_t len);

#endif /* SIL_STRINGS_H */
