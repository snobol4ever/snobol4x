/* bb_boxes.c — All Byrd box C implementations, consolidated
 * Generated from per-box sources. One function per box.
 * AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
 */
#include "bb_box.h"
#include "bb_convert.h"
#include "snobol4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ───── lit ───── */
/* _XCHR     LIT         literal string match */


DESCR_t bb_lit(void *zeta, int entry)
{
    lit_t *ζ = zeta;
    spec_t LIT;
    if (entry==α)                                                               goto LIT_α;
    if (entry==β)                                                               goto LIT_β;
    LIT_α:          if (Δ + ζ->len > Σlen)                                         goto LIT_ω;
                    if (memcmp(Σ+Δ, ζ->lit, (size_t)ζ->len) != 0)               goto LIT_ω;
                    LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len;                       goto LIT_γ;
    LIT_β:          Δ -= ζ->len;                                                goto LIT_ω;
    LIT_γ:                                                                      return descr_from_spec(LIT);
    LIT_ω:                                                                      return FAILDESCR;
}

lit_t *bb_lit_new(const char *lit, int len)
{ lit_t *ζ=calloc(1,sizeof(lit_t)); ζ->lit=lit; ζ->len=len; return ζ; }

/* ───── seq ───── */
/* _XCAT     SEQ         concatenation: left then right; β retries right then left */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
typedef struct { bb_box_fn fn; void *state; } bb_child_t;
typedef struct { bb_child_t left; bb_child_t right; spec_t matched; } seq_t;

DESCR_t bb_seq(void *zeta, int entry)
{
    seq_t *ζ = zeta;
    spec_t SEQ; spec_t lr; spec_t rr;
    if (entry==α)                                                               goto SEQ_α;
    if (entry==β)                                                               goto SEQ_β;
    SEQ_α:          ζ->matched=spec(Σ+Δ,0);                                     
                    lr=spec_from_descr(ζ->left.fn(ζ->left.state,α));                             
                    if (spec_is_empty(lr))                                      goto left_ω;
                                                                                goto left_γ;
    SEQ_β:          rr=spec_from_descr(ζ->right.fn(ζ->right.state,β));                           
                    if (spec_is_empty(rr))                                      goto right_ω;
                                                                                goto right_γ;
    left_γ:         ζ->matched=lr;                                              /* C5-3 fix: left result replaces, not accumulates */
                    rr=spec_from_descr(ζ->right.fn(ζ->right.state,α));                           
                    if (spec_is_empty(rr))                                      goto right_ω;
                                                                                goto right_γ;
    left_ω:                                                                     goto SEQ_ω;
    right_γ:        SEQ=spec_cat(ζ->matched,rr);                                goto SEQ_γ;
    right_ω:        lr=spec_from_descr(ζ->left.fn(ζ->left.state,β));                             
                    if (spec_is_empty(lr))                                      goto left_ω;
                                                                                goto left_γ;
    SEQ_γ:                                                                      return descr_from_spec(SEQ);
    SEQ_ω:                                                                      return FAILDESCR;
}

seq_t *bb_seq_new(bb_box_fn lf, void *ls, bb_box_fn rf, void *rs)
{ seq_t *ζ=calloc(1,sizeof(seq_t)); ζ->left.fn=lf; ζ->left.state=ls; ζ->right.fn=rf; ζ->right.state=rs; return ζ; }

/* ───── alt ───── */
/* _XOR      ALT         alternation: try each child on α; β retries same child only */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define BB_ALT_INIT 4
typedef struct { bb_box_fn fn; void *state; } bb_altchild_t;
typedef struct { int n; int cap; bb_altchild_t *children; int current; int position; spec_t result; } alt_t;

DESCR_t bb_alt(void *zeta, int entry)
{
    alt_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto ALT_α;
    if (entry==β)                                                               goto ALT_β;
    ALT_α:          ζ->position=Δ; ζ->current=1;                                
                    cr=spec_from_descr(ζ->children[0].fn(ζ->children[0].state,α));               
                    if (spec_is_empty(cr))                                      goto child_α_ω;
                                                                                goto child_α_γ;
    ALT_β:          cr=spec_from_descr(ζ->children[ζ->current-1].fn(ζ->children[ζ->current-1].state,β));
                    if (spec_is_empty(cr))                                      goto ALT_ω;
                                                                                goto child_β_γ;
    child_α_γ:      ζ->result=cr;                                               goto ALT_γ;
    child_α_ω:      ζ->current++;                                               
                    if (ζ->current > ζ->n)                                      goto ALT_ω;
                    Δ=ζ->position;                                              
                    cr=spec_from_descr(ζ->children[ζ->current-1].fn(ζ->children[ζ->current-1].state,α));
                    if (spec_is_empty(cr))                                      goto child_α_ω;
                                                                                goto child_α_γ;
    child_β_γ:      ζ->result=cr;                                               goto ALT_γ;
    ALT_γ:                                                                      return descr_from_spec(ζ->result);
    ALT_ω:                                                                      return FAILDESCR;
}

alt_t *bb_alt_new(int n, bb_box_fn *fns)
{
    alt_t *ζ = calloc(1, sizeof(alt_t));
    ζ->cap      = n > BB_ALT_INIT ? n : BB_ALT_INIT;
    ζ->children = malloc(ζ->cap * sizeof(bb_altchild_t));
    ζ->n = n;
    for (int i = 0; i < n; i++) ζ->children[i].fn = fns[i];
    return ζ;
}

/* ───── arb ───── */
/* _XFARB    ARB         match 0..n chars lazily; β extends by 1 */


DESCR_t bb_arb(void *zeta, int entry)
{
    arb_t *ζ = zeta;
    spec_t ARB;
    if (entry==α)                                                               goto ARB_α;
    if (entry==β)                                                               goto ARB_β;
    ARB_α:          ζ->count=0; ζ->start=Δ; ARB=spec(Σ+Δ,0);                    goto ARB_γ;
    ARB_β:          ζ->count++;                                                 
                    if (ζ->start+ζ->count > Σlen)                                  goto ARB_ω;
                    Δ=ζ->start; ARB=spec(Σ+Δ,ζ->count); Δ+=ζ->count;            goto ARB_γ;
    ARB_γ:                                                                      return descr_from_spec(ARB);
    ARB_ω:                                                                      return FAILDESCR;
}

arb_t *bb_arb_new(void)
{ return calloc(1,sizeof(arb_t)); }

/* ───── arbno ───── */
/* _XARBN    ARBNO       zero-or-more greedy; zero-advance guard; β unwinds stack */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define ARBNO_INIT 8
typedef struct { spec_t matched; int start; } arbno_frame_t;
typedef struct { bb_box_fn fn; void *state; int depth; int cap; arbno_frame_t *stack; } arbno_t;

DESCR_t bb_arbno(void *zeta, int entry)
{
    arbno_t *ζ = zeta;
    spec_t ARBNO; spec_t br; arbno_frame_t *fr;
    if (entry==α)                                                               goto ARBNO_α;
    if (entry==β)                                                               goto ARBNO_β;
    ARBNO_α:        ζ->depth=0; fr=&ζ->stack[0];
                    fr->matched=spec(Σ+Δ,0); fr->start=Δ;
    ARBNO_try:      br=spec_from_descr(ζ->fn(ζ->state,α));
                    if (spec_is_empty(br))                                      goto body_ω;
                                                                                goto body_γ;
    ARBNO_β:        if (ζ->depth<=0)                                            goto ARBNO_ω;
                    ζ->depth--; fr=&ζ->stack[ζ->depth]; Δ=fr->start;            goto ARBNO_γ;
    body_γ:         fr=&ζ->stack[ζ->depth];
                    if (Δ==fr->start)                                           goto ARBNO_γ_now;
                    ARBNO=spec_cat(fr->matched,br);
                    ζ->depth++;
                    if (ζ->depth >= ζ->cap) {
                        ζ->cap *= 2;
                        ζ->stack = realloc(ζ->stack, ζ->cap * sizeof(arbno_frame_t));
                        if (!ζ->stack) { fprintf(stderr, "bb_arbno: OOM\n"); abort(); }
                    }
                    fr=&ζ->stack[ζ->depth];
                    fr->matched=ARBNO; fr->start=Δ;                             goto ARBNO_try;
    body_ω:         ARBNO=ζ->stack[ζ->depth].matched;                           goto ARBNO_γ;
    ARBNO_γ_now:    ARBNO=ζ->stack[ζ->depth].matched;                           goto ARBNO_γ;
    ARBNO_γ:                                                                    return descr_from_spec(ARBNO);
    ARBNO_ω:                                                                    return FAILDESCR;
}

arbno_t *bb_arbno_new(bb_box_fn fn, void *state)
{
    arbno_t *ζ = calloc(1, sizeof(arbno_t));
    ζ->fn    = fn;
    ζ->state = state;
    ζ->cap   = ARBNO_INIT;
    ζ->stack = malloc(ζ->cap * sizeof(arbno_frame_t));
    return ζ;
}

/* ───── any ───── */
/* _XANYC    ANY         match one char if in set */


DESCR_t bb_any(void *zeta, int entry)
{
    any_t *ζ = zeta;
    spec_t ANY;
    if (entry==α)                                                               goto ANY_α;
    if (entry==β)                                                               goto ANY_β;
    ANY_α:          if (Δ>=Σlen || !strchr(ζ->chars,Σ[Δ]))                         goto ANY_ω;
                    ANY = spec(Σ+Δ,1); Δ++;                                     goto ANY_γ;
    ANY_β:          Δ--;                                                        goto ANY_ω;
    ANY_γ:                                                                      return descr_from_spec(ANY);
    ANY_ω:                                                                      return FAILDESCR;
}

any_t *bb_any_new(const char *chars)
{ any_t *ζ=calloc(1,sizeof(any_t)); ζ->chars=chars; return ζ; }

/* ───── notany ───── */
/* _XNNYC    NOTANY      match one char if NOT in set */


DESCR_t bb_notany(void *zeta, int entry)
{
    notany_t *ζ = zeta;
    spec_t NOTANY;
    if (entry==α)                                                               goto NOTANY_α;
    if (entry==β)                                                               goto NOTANY_β;
    NOTANY_α:       if (Δ>=Σlen || strchr(ζ->chars,Σ[Δ]))                          goto NOTANY_ω;
                    NOTANY = spec(Σ+Δ,1); Δ++;                                  goto NOTANY_γ;
    NOTANY_β:       Δ--;                                                        goto NOTANY_ω;
    NOTANY_γ:                                                                   return descr_from_spec(NOTANY);
    NOTANY_ω:                                                                   return FAILDESCR;
}

notany_t *bb_notany_new(const char *chars)
{ notany_t *ζ=calloc(1,sizeof(notany_t)); ζ->chars=chars; return ζ; }

/* ───── span ───── */
/* _XSPNC    SPAN        longest prefix of chars in set (≥1) */


DESCR_t bb_span(void *zeta, int entry)
{
    span_t *ζ = zeta;
    spec_t SPAN;
    if (entry==α)                                                               goto SPAN_α;
    if (entry==β)                                                               goto SPAN_β;
    SPAN_α:         ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Σlen && strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;      
                    if (ζ->δ <= 0)                                              goto SPAN_ω;
                    SPAN = spec(Σ+Δ, ζ->δ); Δ += ζ->δ;                          goto SPAN_γ;
    SPAN_β:         Δ -= ζ->δ;                                                  goto SPAN_ω;
    SPAN_γ:                                                                     return descr_from_spec(SPAN);
    SPAN_ω:                                                                     return FAILDESCR;
}

span_t *bb_span_new(const char *chars)
{ span_t *ζ=calloc(1,sizeof(span_t)); ζ->chars=chars; return ζ; }

/* ───── brk ───── */
/* _XBRKC    BRK         scan to first char in set (may be zero-width) */


DESCR_t bb_brk(void *zeta, int entry)
{
    brk_t *ζ = zeta;
    spec_t BRK;
    if (entry==α)                                                               goto BRK_α;
    if (entry==β)                                                               goto BRK_β;
    BRK_α:          ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Σlen && !strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;     
                    if (Δ+ζ->δ >= Σlen)                                            goto BRK_ω;
                    BRK = spec(Σ+Δ,ζ->δ); Δ += ζ->δ;                            goto BRK_γ;
    BRK_β:          Δ -= ζ->δ;                                                  goto BRK_ω;
    BRK_γ:                                                                      return descr_from_spec(BRK);
    BRK_ω:                                                                      return FAILDESCR;
}

brk_t *bb_brk_new(const char *chars)
{ brk_t *ζ=calloc(1,sizeof(brk_t)); ζ->chars=chars; return ζ; }

/* ───── breakx ───── */
/* _XBRKX    BREAKX      like BRK but fails on zero advance */


DESCR_t bb_breakx(void *zeta, int entry)
{
    brkx_t *ζ = zeta;
    spec_t BREAKX;
    if (entry==α)                                                               goto BREAKX_α;
    if (entry==β)                                                               goto BREAKX_β;
    BREAKX_α:       ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Σlen && !strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;     
                    if (ζ->δ==0 || Δ+ζ->δ>=Σlen)                                   goto BREAKX_ω;
                    BREAKX = spec(Σ+Δ,ζ->δ); Δ += ζ->δ;                         goto BREAKX_γ;
    BREAKX_β:       Δ -= ζ->δ;                                                  goto BREAKX_ω;
    BREAKX_γ:                                                                   return descr_from_spec(BREAKX);
    BREAKX_ω:                                                                   return FAILDESCR;
}

brkx_t *bb_breakx_new(const char *chars)
{ brkx_t *ζ=calloc(1,sizeof(brkx_t)); ζ->chars=chars; return ζ; }

/* ───── len ───── */
/* _XLNTH    LEN         match exactly n characters */


DESCR_t bb_len(void *zeta, int entry)
{
    len_t *ζ = zeta;
    spec_t LEN;
    if (entry==α)                                                               goto LEN_α;
    if (entry==β)                                                               goto LEN_β;
    LEN_α:          if (Δ + ζ->n > Σlen)                                           goto LEN_ω;
                    LEN = spec(Σ+Δ, ζ->n); Δ += ζ->n;                           goto LEN_γ;
    LEN_β:          Δ -= ζ->n;                                                  goto LEN_ω;
    LEN_γ:                                                                      return descr_from_spec(LEN);
    LEN_ω:                                                                      return FAILDESCR;
}

len_t *bb_len_new(int n)
{ len_t *ζ=calloc(1,sizeof(len_t)); ζ->n=n; return ζ; }

/* ───── pos ───── */
/* _XPOSI    POS         assert cursor == n (zero-width) */


DESCR_t bb_pos(void *zeta, int entry)
{
    pos_t *ζ = zeta;
    spec_t POS;
    if (entry==α)                                                               goto POS_α;
    if (entry==β)                                                               goto POS_β;
    POS_α:          if (Δ != ζ->n)                                              goto POS_ω;
                    POS = spec(Σ+Δ,0);                                          goto POS_γ;
    POS_β:                                                                      goto POS_ω;
    POS_γ:                                                                      return descr_from_spec(POS);
    POS_ω:                                                                      return FAILDESCR;
}

pos_t *bb_pos_new(int n)
{ pos_t *ζ=calloc(1,sizeof(pos_t)); ζ->n=n; return ζ; }

/* ───── tab ───── */
/* _XTB      TAB         advance cursor TO absolute position n */


DESCR_t bb_tab(void *zeta, int entry)
{
    tab_t *ζ = zeta;
    spec_t TAB;
    if (entry==α)                                                               goto TAB_α;
    if (entry==β)                                                               goto TAB_β;
    TAB_α:          if (Δ > ζ->n)                                               goto TAB_ω;
                    ζ->advance=ζ->n-Δ; TAB=spec(Σ+Δ,ζ->advance); Δ=ζ->n;        goto TAB_γ;
    TAB_β:          Δ -= ζ->advance;                                            goto TAB_ω;
    TAB_γ:                                                                      return descr_from_spec(TAB);
    TAB_ω:                                                                      return FAILDESCR;
}

tab_t *bb_tab_new(int n)
{ tab_t *ζ=calloc(1,sizeof(tab_t)); ζ->n=n; return ζ; }

/* ───── rem ───── */
/* _XSTAR    REM         match entire remainder; no backtrack */


DESCR_t bb_rem(void *zeta, int entry)
{
    (void)zeta;
    spec_t REM;
    if (entry==α)                                                               goto REM_α;
    if (entry==β)                                                               goto REM_β;
    REM_α:          REM = spec(Σ+Δ, Σlen-Δ); Δ = Σlen;                                goto REM_γ;
    REM_β:                                                                      goto REM_ω;
    REM_γ:                                                                      return descr_from_spec(REM);
    REM_ω:                                                                      return FAILDESCR;
}

rem_t *bb_rem_new(void)
{ return calloc(1,sizeof(rem_t)); }

/* ───── eps ───── */
/* _XEPS     EPS         zero-width success once; done flag prevents double-γ */


DESCR_t bb_eps(void *zeta, int entry)
{
    eps_t *ζ = zeta;
    spec_t EPS;
    if (entry==α)   ζ->done=0;                                                  goto EPS_α;
    if (entry==β)                                                               goto EPS_β;
    EPS_α:          if (ζ->done)                                                goto EPS_ω;
                    ζ->done=1; EPS=spec(Σ+Δ,0);                                 goto EPS_γ;
    EPS_β:                                                                      goto EPS_ω;
    EPS_γ:                                                                      return descr_from_spec(EPS);
    EPS_ω:                                                                      return FAILDESCR;
}

eps_t *bb_eps_new(void)
{ return calloc(1,sizeof(eps_t)); }

/* ───── bal ───── */
/* _XBAL     BAL         balanced parens — matches a "balanced" string:
 * zero or more chars that are not ( or ), or a ( followed by a BAL string
 * followed by ), all concatenated.  Equivalent to the SNOBOL4 primitive BAL.
 * On α: scan from Δ consuming the maximal balanced prefix (may be zero-width).
 * On β: undo the match and fail (no shorter alternative — BAL is deterministic). */

DESCR_t bb_bal(void *zeta, int entry)
{
    bal_t *ζ = zeta;
    spec_t BAL;
    if (entry==α)                                                               goto BAL_α;
    if (entry==β)                                                               goto BAL_β;
    BAL_α: {
        int pos = Δ;
        int depth = 0;
        while (pos < Σlen) {
            char c = Σ[pos];
            if (c == '(') { depth++; pos++; }
            else if (c == ')') {
                if (depth == 0) break;   /* unmatched ')' stops scan */
                depth--; pos++;
                /* depth==0 after closing: continue scanning for more balanced content */
            } else { pos++; }
        }
        ζ->δ = pos - Δ;
        BAL = spec(Σ+Δ, ζ->δ); Δ += ζ->δ;                                     goto BAL_γ;
    }
    BAL_β:          Δ -= ζ->δ;                                                 goto BAL_ω;
    BAL_γ:                                                                      return descr_from_spec(BAL);
    BAL_ω:                                                                      return FAILDESCR;
}

bal_t *bb_bal_new(void)
{ return calloc(1,sizeof(bal_t)); }

/* ───── abort ───── */
/* _XABRT    ABORT       always ω — force match failure */


DESCR_t bb_abort(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    if (entry==α)                                                               goto ABORT_α;
    if (entry==β)                                                               goto ABORT_β;
    ABORT_α:                                                                    goto ABORT_ω;
    ABORT_β:                                                                    goto ABORT_ω;
    ABORT_ω:                                                                    return FAILDESCR;
}

abort_t *bb_abort_new(void)
{ return calloc(1,sizeof(abort_t)); }

/* ───── not ───── */
/* _XNOT     NOT         \X — succeed iff X fails; β always ω (no retry) */

/* o$nta/b/c three-entry semantics mapped to two-entry BB:
 *   α: run child with α; if child γ → NOT_ω (child succeeded → we fail);
 *                         if child ω → NOT_γ zero-width (child failed → we succeed)
 *   β: unconditional NOT_ω — negation succeeds at most once per position */
typedef struct { bb_box_fn fn; void *state; int start; } not_t;

DESCR_t bb_not(void *zeta, int entry)
{
    not_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto NOT_α;
    if (entry==β)                                                               goto NOT_β;
    NOT_α:          ζ->start=Δ;                                                 \
                    cr=spec_from_descr(ζ->fn(ζ->state,α));                                       \
                    if (!spec_is_empty(cr))                                     goto NOT_ω;
                    Δ=ζ->start;                                                 goto NOT_γ;
    NOT_β:                                                                      goto NOT_ω;
    NOT_γ:                                                                      return descr_from_spec(spec(Σ+Δ,0));
    NOT_ω:                                                                      return FAILDESCR;
}

not_t *bb_not_new(bb_box_fn fn, void *state)
{ not_t *ζ=calloc(1,sizeof(not_t)); ζ->fn=fn; ζ->state=state; return ζ; }

/* ───── interr ───── */
/* _XINT     INTERR      ?X — null result if X succeeds; ω if X fails (o$int) */

/* o$int: replace operand with null, continue.
 * In BB terms: run child; if child γ → discard match, return zero-width at
 * the *original* cursor (null string); if child ω → propagate ω.
 * β: unconditional ω — interrogation succeeds at most once. */
typedef struct { bb_box_fn fn; void *state; int start; } interr_t;

DESCR_t bb_interr(void *zeta, int entry)
{
    interr_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto INT_α;
    if (entry==β)                                                               goto INT_β;
    INT_α:          ζ->start=Δ;                                                 \
                    cr=spec_from_descr(ζ->fn(ζ->state,α));                                       \
                    if (spec_is_empty(cr))                                      goto INT_ω;
                    Δ=ζ->start;                                                 goto INT_γ;
    INT_β:                                                                      goto INT_ω;
    INT_γ:                                                                      return descr_from_spec(spec(Σ+Δ,0));
    INT_ω:                                                                      return FAILDESCR;
}

interr_t *bb_interr_new(bb_box_fn fn, void *state)
{ interr_t *ζ=calloc(1,sizeof(interr_t)); ζ->fn=fn; ζ->state=state; return ζ; }

/* ───── capture ───── */
/* _XNME/_XFNME  CAPTURE     $ writes on every γ; . buffers for Phase-5 commit
 *
 * UNIFIED 2026-04-19 (SN-20 session 17): previously had two copies
 * (bb_boxes.c and stmt_exec.c). Consolidated here as the single source
 * of truth across --ir-run, --sm-run, and --jit-run.
 *
 * Fields:
 *   varname   — DT_S target name (write via NV_SET_fn — fires I/O hooks for OUTPUT/PUNCH)
 *   var_ptr   — DT_N target pointer (write directly through ptr — SIL NAME semantics)
 *   immediate — 1 for XFNME ($): write now on every γ.  0 for XNME (.): defer via NAM_push.
 *
 * SN-20 (self-unwinding): every γ that pushes to the NAM list saves the
 * returned handle; β (retry) and ω (failure exit) call NAME_pop to undo.
 * No external combinator-level NAME_mark / NAME_rollback_to required — the
 * box is symmetric in its own right.
 *
 * cap_t definition now in bb_box.h so the stmt_exec.c dispatcher can
 * allocate state directly (mirrors other box struct exposure pattern).
 */

/* forward decl — used in bb_cap body below */
static void register_capture(cap_t *c);

/* SN-21c: bb_cap — unified (.) / ($) capture box.
 *
 * State is cap_t with an embedded NAME_t; immediate ($) writes route through
 * name_commit_value, deferred (.) writes push via NAME_push at γ and are
 * popped by NAME_pop on β / ω so the flat NAM stack self-unwinds.
 *
 * Registry (g_capture_list) and has_pending bookkeeping are retained until
 * SN-21e cleanup — they keep statement-boundary resets correct regardless
 * of whether a box ever completed its γ/β/ω handshake. */
DESCR_t bb_cap(void *zeta, int entry)
{
    cap_t *ζ = zeta;
    spec_t cr;

    if (entry == α)                                                             goto CAP_α;
    if (entry == β)                                                             goto CAP_β;

    CAP_α:       /* SN-23d-follow-up: defeat M-DYN-OPT cache poisoning.
                  * cache_get_fresh memcpys the dirty template into each
                  * fresh copy, so has_pending may arrive set from a prior
                  * match's execution.  Reset it at α so β/ω's guard
                  * reflects only THIS α's push. */
                 ζ->has_pending = 0;
                 if (!ζ->immediate) register_capture(ζ);
                 cr = spec_from_descr(ζ->fn(ζ->state, α));
                 if (spec_is_empty(cr))                                         goto CAP_ω;
                                                                                goto CAP_γ_core;

    CAP_β:       /* SN-23d: bare LIFO pop.  Box self-unwind invariant (SN-22d)
                  * guarantees the top of the current NAM ctx is our own γ
                  * push — every intervening peer/child γ push has already
                  * been popped by its own β/ω.  No handle needed.
                  *
                  * KNOWN LIMITATION (to be fixed): relying on has_pending as
                  * the guard inherits stale values from the M-DYN-OPT cache
                  * template when cap_t is cached.  SN-23d alone does NOT
                  * close SN-6c — needs has_pending reset at α or cache fix. */
                 if (!ζ->immediate && ζ->has_pending) {
                     NAME_pop_top();
                     ζ->has_pending = 0;
                 }
                 cr = spec_from_descr(ζ->fn(ζ->state, β));
                 if (spec_is_empty(cr))                                         goto CAP_ω;
                                                                                goto CAP_γ_core;

    CAP_γ_core:  if (ζ->immediate) {
                     /* XFNME ($): commit now on every γ, through the one
                      * dispatcher that knows about every NameKind_t. */
                     char *s = (char *)GC_MALLOC(cr.δ + 1);
                     if (cr.σ && cr.δ > 0) memcpy(s, cr.σ, (size_t)cr.δ);
                     s[cr.δ] = '\0';
                     DESCR_t val = { .v = DT_S, .slen = (uint32_t)cr.δ, .s = s };
                     name_commit_value(&ζ->name, val);
                 } else {
                     /* XNME (.): push the lvalue + matched substring onto the
                      * NAM ctx.  Statement-level NAME_commit walks the slots
                      * at full-match success and calls name_commit_value on
                      * each.  SN-23d: discard handle — CAP_β / CAP_ω drop
                      * the top via NAME_pop_top (pure LIFO). */
                     (void) NAME_push(&ζ->name, cr.σ, (int)cr.δ);
                     ζ->pending     = cr;
                     ζ->has_pending = 1;
                 }
                                                                                return descr_from_spec(cr);

    CAP_ω:       /* SN-23d: bare LIFO pop on failure exit (if we pushed). */
                 if (!ζ->immediate && ζ->has_pending) {
                     NAME_pop_top();
                     ζ->has_pending = 0;
                 }                                                              return FAILDESCR;
}

/* Capture registry (moved from stmt_exec.c — used by exec_stmt for Phase-5 reset).
 * MAX_CAPTURES raised from 64 to 256 to match stmt_exec.c's original value. */
#define MAX_CAPTURES 256
static cap_t *g_capture_list[MAX_CAPTURES];
static int    g_capture_count = 0;

/* Called from bb_cap CAP_α whenever a conditional (.) capture fires.
 * Also callable from bb_build for eager registration if desired. */
static void register_capture(cap_t *c)
{
    for (int i = 0; i < g_capture_count; i++)
        if (g_capture_list[i] == c) return;
    if (g_capture_count < MAX_CAPTURES)
        g_capture_list[g_capture_count++] = c;
}

/* Reset pending flags after Phase 3 success.
 * RT-4: NAME_commit() now owns all conditional (.) capture writes.
 * This function only clears has_pending bookkeeping so the scan-loop
 * reset logic stays correct on subsequent statements.
 * Exported for exec_stmt (see external decl in bb_box.h). */
void flush_pending_captures(void)
{
    for (int i = 0; i < g_capture_count; i++)
        g_capture_list[i]->has_pending = 0;
    g_capture_count = 0;
}

/* Called at start of exec_stmt to clear the registry for a fresh statement. */
void reset_capture_registry(void)
{
    g_capture_count = 0;
}

/* Called before the scan sweep to clear stale has_pending flags without
 * emptying the registry itself. RT-4 equivalent of the inline loop that
 * previously lived in exec_stmt. */
void clear_pending_flags(void)
{
    for (int i = 0; i < g_capture_count; i++)
        g_capture_list[i]->has_pending = 0;
}

/* Unified constructor — external linkage; called from stmt_exec.c bb_build
 * dispatcher and from bb_build.c JIT emitter.  Signature matches the
 * cap_t_bin mirror in bb_build.c (var_ptr is void* there, DESCR_t* here).
 * Builds the embedded NAME_t via name_init_as_{ptr,var} so no call site
 * constructs a NAME_t by hand. */
cap_t *bb_cap_new(bb_box_fn child_fn, void *child_state,
                  const char *varname, DESCR_t *var_ptr, int immediate)
{
    cap_t *ζ = calloc(1, sizeof(cap_t));
    if (!ζ) return NULL;
    ζ->fn        = child_fn;
    ζ->state     = child_state;
    ζ->immediate = immediate;
    if (var_ptr)            name_init_as_ptr(&ζ->name, var_ptr);
    else if (varname)       name_init_as_var(&ζ->name, varname);
    /* else: name.kind==NM_VAR, var_name==NULL — name_commit_value / push are
     * safe no-ops on empty names (mirrors previous varname==NULL behaviour). */
    return ζ;
}

/* SN-21d: NM_CALL constructor for `pat . *fn(args)` (XCALLCAP).
 *
 * Same bb_cap state machine as NM_VAR / NM_PTR — the only difference is the
 * embedded NAME_t's kind, which routes the commit through name_commit_value's
 * NM_CALL branch.  No separate box function, no separate registry, no
 * per-firing cc_event bookkeeping: the flat NAM stack already supplies all of
 * that via NAME_push / NAME_pop γ / β / ω self-unwinding.
 *
 * Deferred (.) flow at commit time:
 *   NAME_commit walks live slots → name_commit_value(NM_CALL) →
 *   g_user_call_hook(fnc_name, args, nargs) → DT_N cell → store matched text.
 *
 * Immediate ($) flow at γ:
 *   name_commit_value(NM_CALL, matched_text) — fires the hook on every γ.
 *   (SPITBOL semantics: immediate assignment fires on every γ, even if the
 *   outer match later fails.) */
cap_t *bb_cap_new_call(bb_box_fn child_fn, void *child_state,
                       const char *fnc_name,
                       DESCR_t *fnc_args, int fnc_nargs,
                       char **fnc_arg_names, int fnc_n_arg_names,
                       int immediate)
{
    cap_t *ζ = calloc(1, sizeof(cap_t));
    if (!ζ) return NULL;
    ζ->fn        = child_fn;
    ζ->state     = child_state;
    ζ->immediate = immediate;
    name_init_as_call(&ζ->name, fnc_name,
                      fnc_args, fnc_nargs,
                      fnc_arg_names, fnc_n_arg_names);
    return ζ;
}

/* ───── atp ───── */
/* _XATP     ATP         @var — write cursor Δ as DT_I into varname; no backtrack */


DESCR_t bb_atp(void *zeta, int entry)
{
    atp_t *ζ = zeta;
    spec_t ATP;
    if (entry==α)                                                               goto ATP_α;
    if (entry==β)                                                               goto ATP_β;
    ATP_α:          ζ->done=1;                                                  
                    if (ζ->varname && ζ->varname[0]) {                          
                        DESCR_t v={.v=DT_I,.i=(int64_t)Δ};                      
                        NV_SET_fn(ζ->varname, v); }                             
                    ATP = spec(Σ+Δ,0);                                          goto ATP_γ;
    ATP_β:                                                                      goto ATP_ω;
    ATP_γ:                                                                      return descr_from_spec(ATP);
    ATP_ω:                                                                      return FAILDESCR;
}

atp_t *bb_atp_new(const char *varname)
{ atp_t *ζ=calloc(1,sizeof(atp_t)); ζ->varname=varname; return ζ; }

/* ───── dvar ───── */
/* ───── fence ───── */
/* _XFNCE    FENCE       succeed once; β cuts (no retry) */


DESCR_t bb_fence(void *zeta, int entry)
{
    fence_t *ζ = zeta;
    if (entry==α)                                                               goto FENCE_α;
    if (entry==β)                                                               goto FENCE_β;
    FENCE_α:        ζ->fired=1;                                                 goto FENCE_γ;
    FENCE_β:                                                                    goto FENCE_ω;
    FENCE_γ:                                                                    return descr_from_spec(spec(Σ+Δ,0));
    FENCE_ω:                                                                    return FAILDESCR;
}

fence_t *bb_fence_new(void)
{ return calloc(1,sizeof(fence_t)); }

/* ───── fail ───── */
/* _XFAIL    FAIL        always ω — force backtrack */


DESCR_t bb_fail(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return FAILDESCR;
}

fail_t *bb_fail_new(void)
{ return calloc(1,sizeof(fail_t)); }

/* ───── rpos ───── */
/* _XRPSI    RPOS        assert cursor == Σlen-n (zero-width) */


DESCR_t bb_rpos(void *zeta, int entry)
{
    rpos_t *ζ = zeta;
    spec_t RPOS;
    if (entry==α)                                                               goto RPOS_α;
    if (entry==β)                                                               goto RPOS_β;
    RPOS_α:         if (Δ != Σlen-ζ->n)                                            goto RPOS_ω;
                    RPOS = spec(Σ+Δ,0);                                         goto RPOS_γ;
    RPOS_β:                                                                     goto RPOS_ω;
    RPOS_γ:                                                                     return descr_from_spec(RPOS);
    RPOS_ω:                                                                     return FAILDESCR;
}

rpos_t *bb_rpos_new(int n)
{ rpos_t *ζ=calloc(1,sizeof(rpos_t)); ζ->n=n; return ζ; }

/* ───── rtab ───── */
/* _XRTB     RTAB        advance cursor TO position Σlen-n */


DESCR_t bb_rtab(void *zeta, int entry)
{
    rtab_t *ζ = zeta;
    spec_t RTAB;
    if (entry==α)                                                               goto RTAB_α;
    if (entry==β)                                                               goto RTAB_β;
    RTAB_α:         if (Δ > Σlen-ζ->n)                                             goto RTAB_ω;
                    ζ->advance=(Σlen-ζ->n)-Δ; RTAB=spec(Σ+Δ,ζ->advance); Δ=Σlen-ζ->n; goto RTAB_γ;
    RTAB_β:         Δ -= ζ->advance;                                            goto RTAB_ω;
    RTAB_γ:                                                                     return descr_from_spec(RTAB);
    RTAB_ω:                                                                     return FAILDESCR;
}

rtab_t *bb_rtab_new(int n)
{ rtab_t *ζ=calloc(1,sizeof(rtab_t)); ζ->n=n; return ζ; }

/* ───── succeed ───── */
/* _XSUCF    SUCCEED     always γ zero-width; outer loop retries */


DESCR_t bb_succeed(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return descr_from_spec(spec(Σ+Δ,0));
}

succeed_t *bb_succeed_new(void)
{ return calloc(1,sizeof(succeed_t)); }

