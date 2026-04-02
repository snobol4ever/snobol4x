/*
 * bb_notany.c — NOTANY(chars) Byrd Box (M-DYN-2)
 *
 * Match one character if it is NOT in the set `chars`.
 * Fails at end-of-subject or if Σ[Δ] ∈ chars.
 * β restores cursor by 1.
 *
 * Pattern:  NOTANY('aeiou')
 * SNOBOL4:  NOTANY('aeiou')
 *
 * Three-column layout:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     NOTANY_α:           if EOS or Σ[Δ] ∈ chars         → NOTANY_ω
 *                         NOTANY = spec(Σ+Δ,1); Δ+=1;    → NOTANY_γ
 *     NOTANY_β:           Δ -= 1;                        → NOTANY_ω
 *     NOTANY_γ:                                          return NOTANY;
 *     NOTANY_ω:                                          return spec_empty;
 *
 * State ζ: chars pointer only (advance is always 1).
 */

#include "bb_box.h"
#include <string.h>
#include <stdlib.h>

typedef struct { const char *chars; } notany_t;

spec_t bb_notany(notany_t **ζζ, int entry)
{
    notany_t *ζ = *ζζ;

    if (entry == α)                                 goto NOTANY_α;
    if (entry == β)                                 goto NOTANY_β;

    /*------------------------------------------------------------------------*/
    spec_t         NOTANY;

    NOTANY_α:         if (Δ >= Ω || strchr(ζ->chars, Σ[Δ]))  goto NOTANY_ω;
                      NOTANY = spec(Σ+Δ, 1); Δ += 1;         goto NOTANY_γ;

    NOTANY_β:         Δ -= 1;                                 goto NOTANY_ω;

    /*------------------------------------------------------------------------*/
    NOTANY_γ:                                                 return NOTANY;
    NOTANY_ω:                                                 return spec_empty;
}

notany_t *bb_notany_new(const char *chars)
{
    notany_t *ζ = calloc(1, sizeof(notany_t));
    ζ->chars = chars;
                                                              return ζ;
}
