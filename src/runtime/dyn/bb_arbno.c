/*
 * bb_arbno.c — ARBNO (zero-or-more) Byrd Box (M-DYN-2)
 *
 * ARBNO(body): match body zero or more times.
 * Possessive zero-advance guard: if body matched but cursor didn't move,
 * stop immediately (prevents infinite loop).
 *
 * Pattern:  ARBNO('Bird' | 'Blue' | LEN(1))
 * SNOBOL4:  ARBNO(P)
 *
 * Three-column layout — canonical form from test_sno_1.c §ARBNO:
 *
 *     LABEL:              ACTION                          GOTO
 *     ─────────────────────────────────────────────────────────
 *     ARBNO_alpha:            zeta = &stack[ARBNO_i=0];
 *                         zeta->ARBNO = spec(Σ+Δ,0);         → alt_alpha    (body alpha)
 *     ARBNO_beta:            zeta = &stack[++ARBNO_i];
 *                         zeta->ARBNO = ARBNO;              → alt_alpha    (body alpha again)
 *     alt_gamma:              ARBNO = spec_cat(zeta->ARBNO, alt);    → ARBNO_gamma  (accumulated so far)
 *     alt_omega:              if (ARBNO_i <= 0)              → ARBNO_omega
 *                         ARBNO_i--; zeta = &stack[ARBNO_i];→ alt_beta    (backtrack into body)
 *
 *     ARBNO_gamma:            return ARBNO;
 *     ARBNO_omega:            return spec_empty;
 *
 * The ARBNO stack records one entry per successful body match.
 * On beta, we walk back through the stack retrying each body iteration.
 *
 * Zero-advance guard (SNOBOL4 spec §3): if body matched zero chars,
 * ARBNO succeeds immediately at current position (prevents infinite loop).
 */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "bb_box.h"
#include <stdlib.h>

#define ARBNO_STACK_MAX 64

typedef struct {
    spec_t   ARBNO;    /* accumulated match up to this level */
    int     saved_Δ;  /* cursor before this iteration's body match */
} arbno_frame_t;

typedef struct {
    bb_box_fn    body_fn;                    /* body box function */
    void        *body_zeta;                     /* body box state */
    int          ARBNO_i;                    /* current stack depth */
    arbno_frame_t stack[ARBNO_STACK_MAX];    /* frame stack */
} arbno_t;

/* ── bb_arbno ────────────────────────────────────────────────────────────── */
spec_t bb_arbno(arbno_t **zetazeta, int entry)
{
    arbno_t *zeta = *zetazeta;

    if (entry == alpha)                                     goto ARBNO_alpha;
    if (entry == beta)                                     goto ARBNO_beta;

    /*------------------------------------------------------------------------*/
    spec_t             ARBNO;
    spec_t             body_r;
    arbno_frame_t    *frame;

    ARBNO_alpha:      zeta->ARBNO_i          = 0;
                  frame               = &zeta->stack[0];
                  frame->ARBNO        = spec(Σ+Δ, 0);
                  frame->saved_Δ      = Δ;
                  body_r = zeta->body_fn(&zeta->body_zeta, alpha);
                  if (spec_is_empty(body_r))                 goto body_omega;
                  else                                  goto body_gamma;

    ARBNO_beta:      zeta->ARBNO_i++;
                  frame               = &zeta->stack[zeta->ARBNO_i];
                  frame->ARBNO        = zeta->stack[zeta->ARBNO_i-1].ARBNO;
                  frame->saved_Δ      = Δ;
                  /* try to match body one more time */
                  body_r = zeta->body_fn(&zeta->body_zeta, alpha);
                  if (spec_is_empty(body_r))                 goto body_omega;
                  else                                  goto body_gamma;

    body_gamma:       frame  = &zeta->stack[zeta->ARBNO_i];
                  /* zero-advance guard */
                  if (Δ == frame->saved_Δ)              goto ARBNO_gamma_now;
                  ARBNO  = spec_cat(frame->ARBNO, body_r);
                  zeta->stack[zeta->ARBNO_i].ARBNO = ARBNO;  goto ARBNO_gamma_now;

    ARBNO_gamma_now:  ARBNO  = zeta->stack[zeta->ARBNO_i].ARBNO; goto ARBNO_gamma;

    body_omega:       if (zeta->ARBNO_i <= 0)                 goto ARBNO_omega;
                  zeta->ARBNO_i--;
                  frame  = &zeta->stack[zeta->ARBNO_i];
                  /* restore cursor to before this failed iteration */
                  Δ      = frame->saved_Δ;
                  body_r = zeta->body_fn(&zeta->body_zeta, beta);
                  if (spec_is_empty(body_r))                 goto body_omega;
                  else                                  goto body_gamma;

    /*------------------------------------------------------------------------*/
    ARBNO_gamma:      return ARBNO;
    ARBNO_omega:      return spec_empty;
}

/* ── bb_arbno_new ────────────────────────────────────────────────────────── */
arbno_t *bb_arbno_new(bb_box_fn body_fn)
{
    arbno_t *zeta = calloc(1, sizeof(arbno_t));
    zeta->body_fn = body_fn;
    zeta->body_zeta  = NULL;
    return zeta;
}
