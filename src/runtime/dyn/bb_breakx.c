/*
 * bb_breakx.c — BREAKX(chars) Byrd Box (M-DYN-2)
 *
 * Like BREAK(chars), but also fails if δ == 0 (zero advance).
 * Scan forward until a character IN `chars` is found; match the prefix
 * BEFORE that character.  Fails if no chars-character found before EOS,
 * or if the break character is at the current position (zero advance).
 *
 * Pattern:  BREAKX('.')
 * SNOBOL4:  BREAKX('.')
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     BRKX_α:             scan until Σ[Δ+δ] ∈ chars → δ
 *                         if δ==0 or EOS               → BRKX_ω
 *                         BRKX = spec(Σ+Δ, δ); Δ += δ;  → BRKX_γ
 *     BRKX_β:             Δ -= δ;                       → BRKX_ω
 *     BRKX_γ:                                           return BRKX;
 *     BRKX_ω:                                           return spec_empty;
 *
 * State ζ: chars pointer + saved advance δ (for β restore).
 */

#include "bb_box.h"
#include <string.h>
#include <stdlib.h>

typedef struct { const char *chars; int δ; } brkx_t;

spec_t bb_breakx(brkx_t **ζζ, int entry)
{
    brkx_t *ζ = *ζζ;

    if (entry == α)                                 goto BRKX_α;
    if (entry == β)                                 goto BRKX_β;

    /*------------------------------------------------------------------------*/
    spec_t         BRKX;

    BRKX_α:       for (ζ->δ = 0; Δ+ζ->δ < Ω; ζ->δ++)
                      if (strchr(ζ->chars, Σ[Δ+ζ->δ])) break;
                  if (ζ->δ == 0)                            goto BRKX_ω;
                  if (Δ + ζ->δ >= Ω)                        goto BRKX_ω;
                  BRKX = spec(Σ+Δ, ζ->δ); Δ += ζ->δ;        goto BRKX_γ;

    BRKX_β:       Δ -= ζ->δ;                                goto BRKX_ω;

    /*------------------------------------------------------------------------*/
    BRKX_γ:                                                   return BRKX;
    BRKX_ω:                                                   return spec_empty;
}

brkx_t *bb_breakx_new(const char *chars)
{
    brkx_t *ζ = calloc(1, sizeof(brkx_t));
    ζ->chars = chars;
                                                              return ζ;
}
