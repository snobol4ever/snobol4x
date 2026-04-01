/*
 * bb_lit.c — LIT (literal string) Byrd Box (M-DYN-2)
 *
 * Matches a fixed literal string against the subject at the current cursor.
 * If the subject at Δ starts with the literal, γ fires and Δ advances.
 * On backtrack (β), Δ is restored and ω fires.
 *
 * Pattern:  LIT('Bird')
 * SNOBOL4:  'Bird'
 *
 * Three-column layout — one function, all four ports:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     LIT_α:              length check                   → ω if too short
 *                         byte-by-byte match             → ω on mismatch
 *                         LIT = spec(Σ+Δ, len); Δ += len; → LIT_γ
 *     LIT_β:              Δ -= len;                      → LIT_ω
                      *     LIT_γ:                            return LIT;
                      *     LIT_ω:                            return spec_empty;
 *
 * State ζ: saved cursor advance (= lit_len, for β restore).
 * Since lit_len is known at box-build time, ζ can hold it directly;
 * alternatively, no ζ needed if we just recompute from the literal length.
 * We use no ζ — the literal length IS the advance, always.
 */

#include "bb_box.h"
#include <string.h>

/* ── lit box state ───────────────────────────────────────────────────────── */
typedef struct {
    const char *lit;     /* literal bytes */
    int         len;     /* literal length */
} lit_t;

/* ── bb_lit ──────────────────────────────────────────────────────────────── */
/*
 * spec_t bb_lit(lit_t **ζζ, int entry)
 *
 * ζζ must point to a lit_t pre-filled with lit/len before first α call.
 * (Unlike named patterns, LIT has no dynamic state beyond the literal itself.)
 */
spec_t bb_lit(lit_t **ζζ, int entry)
{
    lit_t *ζ = *ζζ;

    if (entry == α)                                 goto LIT_α;
    if (entry == β)                                 goto LIT_β;

    /*------------------------------------------------------------------------*/
    spec_t         LIT;

    LIT_α:            if (Δ + ζ->len > Ω)                     goto LIT_ω;
                  if (memcmp(Σ + Δ, ζ->lit, (size_t)ζ->len) != 0)
                                                              goto LIT_ω;
                      LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len;   goto LIT_γ;

    LIT_β:            Δ -= ζ->len;                            goto LIT_ω;

    /*------------------------------------------------------------------------*/
    LIT_γ:                                                    return LIT;
    LIT_ω:                                                    return spec_empty;
}

/* ── bb_lit_new ──────────────────────────────────────────────────────────── */
/*
 * Construct a lit_t and return a pointer.  Caller owns it.
 * Used by the graph assembler (bb_build.c).
 */
lit_t *bb_lit_new(const char *lit, int len)
{
    lit_t *ζ = calloc(1, sizeof(lit_t));
    ζ->lit   = lit;
    ζ->len   = len;
                                                              return ζ;
}
