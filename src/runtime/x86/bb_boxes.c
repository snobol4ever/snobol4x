/* bb_boxes.c — All Byrd box C implementations, consolidated
 * Generated from per-box sources. One function per box.
 * AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
 */
#include "bb_box.h"
#include "snobol4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ───── lit ───── */
/* _XCHR     LIT         literal string match */


spec_t bb_lit(void *zeta, int entry)
{
    lit_t *ζ = zeta;
    spec_t LIT;
    if (entry==α)                                                               goto LIT_α;
    if (entry==β)                                                               goto LIT_β;
    LIT_α:          if (Δ + ζ->len > Ω)                                         goto LIT_ω;
                    if (memcmp(Σ+Δ, ζ->lit, (size_t)ζ->len) != 0)               goto LIT_ω;
                    LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len;                       goto LIT_γ;
    LIT_β:          Δ -= ζ->len;                                                goto LIT_ω;
    LIT_γ:                                                                      return LIT;
    LIT_ω:                                                                      return spec_empty;
}

lit_t *bb_lit_new(const char *lit, int len)
{ lit_t *ζ=calloc(1,sizeof(lit_t)); ζ->lit=lit; ζ->len=len; return ζ; }

/* ───── seq ───── */
/* _XCAT     SEQ         concatenation: left then right; β retries right then left */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
typedef struct { bb_box_fn fn; void *state; } bb_child_t;
typedef struct { bb_child_t left; bb_child_t right; spec_t matched; } seq_t;

spec_t bb_seq(void *zeta, int entry)
{
    seq_t *ζ = zeta;
    spec_t SEQ; spec_t lr; spec_t rr;
    if (entry==α)                                                               goto SEQ_α;
    if (entry==β)                                                               goto SEQ_β;
    SEQ_α:          ζ->matched=spec(Σ+Δ,0);                                     
                    lr=ζ->left.fn(ζ->left.state,α);                             
                    if (spec_is_empty(lr))                                      goto left_ω;
                                                                                goto left_γ;
    SEQ_β:          rr=ζ->right.fn(ζ->right.state,β);                           
                    if (spec_is_empty(rr))                                      goto right_ω;
                                                                                goto right_γ;
    left_γ:         ζ->matched=spec_cat(ζ->matched,lr);                         
                    rr=ζ->right.fn(ζ->right.state,α);                           
                    if (spec_is_empty(rr))                                      goto right_ω;
                                                                                goto right_γ;
    left_ω:                                                                     goto SEQ_ω;
    right_γ:        SEQ=spec_cat(ζ->matched,rr);                                goto SEQ_γ;
    right_ω:        lr=ζ->left.fn(ζ->left.state,β);                             
                    if (spec_is_empty(lr))                                      goto left_ω;
                                                                                goto left_γ;
    SEQ_γ:                                                                      return SEQ;
    SEQ_ω:                                                                      return spec_empty;
}

seq_t *bb_seq_new(bb_box_fn lf, void *ls, bb_box_fn rf, void *rs)
{ seq_t *ζ=calloc(1,sizeof(seq_t)); ζ->left.fn=lf; ζ->left.state=ls; ζ->right.fn=rf; ζ->right.state=rs; return ζ; }

/* ───── alt ───── */
/* _XOR      ALT         alternation: try each child on α; β retries same child only */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define BB_ALT_MAX 16
typedef struct { bb_box_fn fn; void *state; } bb_altchild_t;
typedef struct { int n; bb_altchild_t children[BB_ALT_MAX]; int current; int position; spec_t result; } alt_t;

spec_t bb_alt(void *zeta, int entry)
{
    alt_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto ALT_α;
    if (entry==β)                                                               goto ALT_β;
    ALT_α:          ζ->position=Δ; ζ->current=1;                                
                    cr=ζ->children[0].fn(ζ->children[0].state,α);               
                    if (spec_is_empty(cr))                                      goto child_α_ω;
                                                                                goto child_α_γ;
    ALT_β:          cr=ζ->children[ζ->current-1].fn(ζ->children[ζ->current-1].state,β);
                    if (spec_is_empty(cr))                                      goto ALT_ω;
                                                                                goto child_β_γ;
    child_α_γ:      ζ->result=cr;                                               goto ALT_γ;
    child_α_ω:      ζ->current++;                                               
                    if (ζ->current > ζ->n)                                      goto ALT_ω;
                    Δ=ζ->position;                                              
                    cr=ζ->children[ζ->current-1].fn(ζ->children[ζ->current-1].state,α);
                    if (spec_is_empty(cr))                                      goto child_α_ω;
                                                                                goto child_α_γ;
    child_β_γ:      ζ->result=cr;                                               goto ALT_γ;
    ALT_γ:                                                                      return ζ->result;
    ALT_ω:                                                                      return spec_empty;
}

alt_t *bb_alt_new(int n, bb_box_fn *fns)
{ alt_t *ζ=calloc(1,sizeof(alt_t)); ζ->n=n; for(int i=0;i<n&&i<BB_ALT_MAX;i++) ζ->children[i].fn=fns[i]; return ζ; }

/* ───── arb ───── */
/* _XFARB    ARB         match 0..n chars lazily; β extends by 1 */


spec_t bb_arb(void *zeta, int entry)
{
    arb_t *ζ = zeta;
    spec_t ARB;
    if (entry==α)                                                               goto ARB_α;
    if (entry==β)                                                               goto ARB_β;
    ARB_α:          ζ->count=0; ζ->start=Δ; ARB=spec(Σ+Δ,0);                    goto ARB_γ;
    ARB_β:          ζ->count++;                                                 
                    if (ζ->start+ζ->count > Ω)                                  goto ARB_ω;
                    Δ=ζ->start; ARB=spec(Σ+Δ,ζ->count); Δ+=ζ->count;            goto ARB_γ;
    ARB_γ:                                                                      return ARB;
    ARB_ω:                                                                      return spec_empty;
}

arb_t *bb_arb_new(void)
{ return calloc(1,sizeof(arb_t)); }

/* ───── arbno ───── */
/* _XARBN    ARBNO       zero-or-more greedy; zero-advance guard; β unwinds stack */

#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#define ARBNO_STACK_MAX 64
typedef struct { spec_t matched; int start; } arbno_frame_t;
typedef struct { bb_box_fn fn; void *state; int depth; arbno_frame_t stack[ARBNO_STACK_MAX]; } arbno_t;

spec_t bb_arbno(void *zeta, int entry)
{
    arbno_t *ζ = zeta;
    spec_t ARBNO; spec_t br; arbno_frame_t *fr;
    if (entry==α)                                                               goto ARBNO_α;
    if (entry==β)                                                               goto ARBNO_β;
    ARBNO_α:        ζ->depth=0; fr=&ζ->stack[0];                                
                    fr->matched=spec(Σ+Δ,0); fr->start=Δ;                       
    ARBNO_try:      br=ζ->fn(ζ->state,α);                                       
                    if (spec_is_empty(br))                                      goto body_ω;
                                                                                goto body_γ;
    ARBNO_β:        if (ζ->depth<=0)                                            goto ARBNO_ω;
                    ζ->depth--; fr=&ζ->stack[ζ->depth]; Δ=fr->start;            goto ARBNO_γ;
    body_γ:         fr=&ζ->stack[ζ->depth];                                     
                    if (Δ==fr->start)                                           goto ARBNO_γ_now;
                    ARBNO=spec_cat(fr->matched,br);                             
                    if (ζ->depth+1<ARBNO_STACK_MAX) {                           
                        ζ->depth++; fr=&ζ->stack[ζ->depth];                     
                        fr->matched=ARBNO; fr->start=Δ; }                       goto ARBNO_try;
    body_ω:         ARBNO=ζ->stack[ζ->depth].matched;                           goto ARBNO_γ;
    ARBNO_γ_now:    ARBNO=ζ->stack[ζ->depth].matched;                           goto ARBNO_γ;
    ARBNO_γ:                                                                    return ARBNO;
    ARBNO_ω:                                                                    return spec_empty;
}

arbno_t *bb_arbno_new(bb_box_fn fn, void *state)
{ arbno_t *ζ=calloc(1,sizeof(arbno_t)); ζ->fn=fn; ζ->state=state; return ζ; }

/* ───── any ───── */
/* _XANYC    ANY         match one char if in set */


spec_t bb_any(void *zeta, int entry)
{
    any_t *ζ = zeta;
    spec_t ANY;
    if (entry==α)                                                               goto ANY_α;
    if (entry==β)                                                               goto ANY_β;
    ANY_α:          if (Δ>=Ω || !strchr(ζ->chars,Σ[Δ]))                         goto ANY_ω;
                    ANY = spec(Σ+Δ,1); Δ++;                                     goto ANY_γ;
    ANY_β:          Δ--;                                                        goto ANY_ω;
    ANY_γ:                                                                      return ANY;
    ANY_ω:                                                                      return spec_empty;
}

any_t *bb_any_new(const char *chars)
{ any_t *ζ=calloc(1,sizeof(any_t)); ζ->chars=chars; return ζ; }

/* ───── notany ───── */
/* _XNNYC    NOTANY      match one char if NOT in set */


spec_t bb_notany(void *zeta, int entry)
{
    notany_t *ζ = zeta;
    spec_t NOTANY;
    if (entry==α)                                                               goto NOTANY_α;
    if (entry==β)                                                               goto NOTANY_β;
    NOTANY_α:       if (Δ>=Ω || strchr(ζ->chars,Σ[Δ]))                          goto NOTANY_ω;
                    NOTANY = spec(Σ+Δ,1); Δ++;                                  goto NOTANY_γ;
    NOTANY_β:       Δ--;                                                        goto NOTANY_ω;
    NOTANY_γ:                                                                   return NOTANY;
    NOTANY_ω:                                                                   return spec_empty;
}

notany_t *bb_notany_new(const char *chars)
{ notany_t *ζ=calloc(1,sizeof(notany_t)); ζ->chars=chars; return ζ; }

/* ───── span ───── */
/* _XSPNC    SPAN        longest prefix of chars in set (≥1) */


spec_t bb_span(void *zeta, int entry)
{
    span_t *ζ = zeta;
    spec_t SPAN;
    if (entry==α)                                                               goto SPAN_α;
    if (entry==β)                                                               goto SPAN_β;
    SPAN_α:         ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Ω && strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;      
                    if (ζ->δ <= 0)                                              goto SPAN_ω;
                    SPAN = spec(Σ+Δ, ζ->δ); Δ += ζ->δ;                          goto SPAN_γ;
    SPAN_β:         Δ -= ζ->δ;                                                  goto SPAN_ω;
    SPAN_γ:                                                                     return SPAN;
    SPAN_ω:                                                                     return spec_empty;
}

span_t *bb_span_new(const char *chars)
{ span_t *ζ=calloc(1,sizeof(span_t)); ζ->chars=chars; return ζ; }

/* ───── brk ───── */
/* _XBRKC    BRK         scan to first char in set (may be zero-width) */


spec_t bb_brk(void *zeta, int entry)
{
    brk_t *ζ = zeta;
    spec_t BRK;
    if (entry==α)                                                               goto BRK_α;
    if (entry==β)                                                               goto BRK_β;
    BRK_α:          ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Ω && !strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;     
                    if (Δ+ζ->δ >= Ω)                                            goto BRK_ω;
                    BRK = spec(Σ+Δ,ζ->δ); Δ += ζ->δ;                            goto BRK_γ;
    BRK_β:          Δ -= ζ->δ;                                                  goto BRK_ω;
    BRK_γ:                                                                      return BRK;
    BRK_ω:                                                                      return spec_empty;
}

brk_t *bb_brk_new(const char *chars)
{ brk_t *ζ=calloc(1,sizeof(brk_t)); ζ->chars=chars; return ζ; }

/* ───── breakx ───── */
/* _XBRKX    BREAKX      like BRK but fails on zero advance */


spec_t bb_breakx(void *zeta, int entry)
{
    brkx_t *ζ = zeta;
    spec_t BREAKX;
    if (entry==α)                                                               goto BREAKX_α;
    if (entry==β)                                                               goto BREAKX_β;
    BREAKX_α:       ζ->δ=0;                                                     
                    while (Δ+ζ->δ<Ω && !strchr(ζ->chars,Σ[Δ+ζ->δ])) ζ->δ++;     
                    if (ζ->δ==0 || Δ+ζ->δ>=Ω)                                   goto BREAKX_ω;
                    BREAKX = spec(Σ+Δ,ζ->δ); Δ += ζ->δ;                         goto BREAKX_γ;
    BREAKX_β:       Δ -= ζ->δ;                                                  goto BREAKX_ω;
    BREAKX_γ:                                                                   return BREAKX;
    BREAKX_ω:                                                                   return spec_empty;
}

brkx_t *bb_breakx_new(const char *chars)
{ brkx_t *ζ=calloc(1,sizeof(brkx_t)); ζ->chars=chars; return ζ; }

/* ───── len ───── */
/* _XLNTH    LEN         match exactly n characters */


spec_t bb_len(void *zeta, int entry)
{
    len_t *ζ = zeta;
    spec_t LEN;
    if (entry==α)                                                               goto LEN_α;
    if (entry==β)                                                               goto LEN_β;
    LEN_α:          if (Δ + ζ->n > Ω)                                           goto LEN_ω;
                    LEN = spec(Σ+Δ, ζ->n); Δ += ζ->n;                           goto LEN_γ;
    LEN_β:          Δ -= ζ->n;                                                  goto LEN_ω;
    LEN_γ:                                                                      return LEN;
    LEN_ω:                                                                      return spec_empty;
}

len_t *bb_len_new(int n)
{ len_t *ζ=calloc(1,sizeof(len_t)); ζ->n=n; return ζ; }

/* ───── pos ───── */
/* _XPOSI    POS         assert cursor == n (zero-width) */


spec_t bb_pos(void *zeta, int entry)
{
    pos_t *ζ = zeta;
    spec_t POS;
    if (entry==α)                                                               goto POS_α;
    if (entry==β)                                                               goto POS_β;
    POS_α:          if (Δ != ζ->n)                                              goto POS_ω;
                    POS = spec(Σ+Δ,0);                                          goto POS_γ;
    POS_β:                                                                      goto POS_ω;
    POS_γ:                                                                      return POS;
    POS_ω:                                                                      return spec_empty;
}

pos_t *bb_pos_new(int n)
{ pos_t *ζ=calloc(1,sizeof(pos_t)); ζ->n=n; return ζ; }

/* ───── tab ───── */
/* _XTB      TAB         advance cursor TO absolute position n */


spec_t bb_tab(void *zeta, int entry)
{
    tab_t *ζ = zeta;
    spec_t TAB;
    if (entry==α)                                                               goto TAB_α;
    if (entry==β)                                                               goto TAB_β;
    TAB_α:          if (Δ > ζ->n)                                               goto TAB_ω;
                    ζ->advance=ζ->n-Δ; TAB=spec(Σ+Δ,ζ->advance); Δ=ζ->n;        goto TAB_γ;
    TAB_β:          Δ -= ζ->advance;                                            goto TAB_ω;
    TAB_γ:                                                                      return TAB;
    TAB_ω:                                                                      return spec_empty;
}

tab_t *bb_tab_new(int n)
{ tab_t *ζ=calloc(1,sizeof(tab_t)); ζ->n=n; return ζ; }

/* ───── rem ───── */
/* _XSTAR    REM         match entire remainder; no backtrack */


spec_t bb_rem(void *zeta, int entry)
{
    (void)zeta;
    spec_t REM;
    if (entry==α)                                                               goto REM_α;
    if (entry==β)                                                               goto REM_β;
    REM_α:          REM = spec(Σ+Δ, Ω-Δ); Δ = Ω;                                goto REM_γ;
    REM_β:                                                                      goto REM_ω;
    REM_γ:                                                                      return REM;
    REM_ω:                                                                      return spec_empty;
}

rem_t *bb_rem_new(void)
{ return calloc(1,sizeof(rem_t)); }

/* ───── eps ───── */
/* _XEPS     EPS         zero-width success once; done flag prevents double-γ */


spec_t bb_eps(void *zeta, int entry)
{
    eps_t *ζ = zeta;
    spec_t EPS;
    if (entry==α)   ζ->done=0;                                                  goto EPS_α;
    if (entry==β)                                                               goto EPS_β;
    EPS_α:          if (ζ->done)                                                goto EPS_ω;
                    ζ->done=1; EPS=spec(Σ+Δ,0);                                 goto EPS_γ;
    EPS_β:                                                                      goto EPS_ω;
    EPS_γ:                                                                      return EPS;
    EPS_ω:                                                                      return spec_empty;
}

eps_t *bb_eps_new(void)
{ return calloc(1,sizeof(eps_t)); }

/* ───── bal ───── */
/* _XBAL     BAL         balanced parens — STUB; M-DYN-BAL pending */

typedef struct { int δ; int start; }  bal_t;

spec_t bb_bal(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    fprintf(stderr,"bb_bal: unimplemented — ω\n");
    return spec_empty;
}

bal_t *bb_bal_new(void)
{ return calloc(1,sizeof(bal_t)); }

/* ───── abort ───── */
/* _XABRT    ABORT       always ω — force match failure */


spec_t bb_abort(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    if (entry==α)                                                               goto ABORT_α;
    if (entry==β)                                                               goto ABORT_β;
    ABORT_α:                                                                    goto ABORT_ω;
    ABORT_β:                                                                    goto ABORT_ω;
    ABORT_ω:                                                                    return spec_empty;
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

spec_t bb_not(void *zeta, int entry)
{
    not_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto NOT_α;
    if (entry==β)                                                               goto NOT_β;
    NOT_α:          ζ->start=Δ;                                                 \
                    cr=ζ->fn(ζ->state,α);                                       \
                    if (!spec_is_empty(cr))                                     goto NOT_ω;
                    Δ=ζ->start;                                                 goto NOT_γ;
    NOT_β:                                                                      goto NOT_ω;
    NOT_γ:                                                                      return spec(Σ+Δ,0);
    NOT_ω:                                                                      return spec_empty;
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

spec_t bb_interr(void *zeta, int entry)
{
    interr_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto INT_α;
    if (entry==β)                                                               goto INT_β;
    INT_α:          ζ->start=Δ;                                                 \
                    cr=ζ->fn(ζ->state,α);                                       \
                    if (spec_is_empty(cr))                                      goto INT_ω;
                    Δ=ζ->start;                                                 goto INT_γ;
    INT_β:                                                                      goto INT_ω;
    INT_γ:                                                                      return spec(Σ+Δ,0);
    INT_ω:                                                                      return spec_empty;
}

interr_t *bb_interr_new(bb_box_fn fn, void *state)
{ interr_t *ζ=calloc(1,sizeof(interr_t)); ζ->fn=fn; ζ->state=state; return ζ; }

/* ───── capture ───── */
/* _XNME/_XFNME  CAPTURE     $ writes on every γ; . buffers for Phase-5 commit */

typedef struct {
    bb_box_fn fn; void *state; const char *varname;
    int immediate; spec_t pending; int has_pending;
} capture_t;

spec_t bb_capture(void *zeta, int entry)
{
    capture_t *ζ = zeta;
    spec_t cr;
    if (entry==α)                                                               goto CAP_α;
    if (entry==β)                                                               goto CAP_β;
    CAP_α:          cr=ζ->fn(ζ->state,α);                                       
                    if (spec_is_empty(cr))                                      goto CAP_ω;
                                                                                goto CAP_γ_core;
    CAP_β:          cr=ζ->fn(ζ->state,β);                                       
                    if (spec_is_empty(cr))                                      goto CAP_ω;
                                                                                goto CAP_γ_core;
    CAP_γ_core:     if (ζ->varname && *ζ->varname && ζ->immediate) {            
                        char *s=GC_MALLOC(cr.δ+1);                              
                        memcpy(s,cr.σ,(size_t)cr.δ); s[cr.δ]=0;                 
                        DESCR_t v={.v=DT_S,.slen=(uint32_t)cr.δ,.s=s};          
                        NV_SET_fn(ζ->varname,v);                                
                    } else if (ζ->varname && *ζ->varname) {                     
                        ζ->pending=cr; ζ->has_pending=1; }                      
                                                                                return cr;
    CAP_ω:          ζ->has_pending=0;                                           return spec_empty;
}

/* ───── atp ───── */
/* _XATP     ATP         @var — write cursor Δ as DT_I into varname; no backtrack */


spec_t bb_atp(void *zeta, int entry)
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
    ATP_γ:                                                                      return ATP;
    ATP_ω:                                                                      return spec_empty;
}

atp_t *bb_atp_new(const char *varname)
{ atp_t *ζ=calloc(1,sizeof(atp_t)); ζ->varname=varname; return ζ; }

/* ───── dvar ───── */
/* _XDSAR/_XVAR  DVAR        *VAR/VAR — re-resolve live value on every α */

typedef struct { const char *name; bb_box_fn child_fn; void *child_state; size_t child_size; } dvar_t;

spec_t bb_deferred_var(void *zeta, int entry)
{
    dvar_t *ζ = zeta;
    spec_t DVAR;
    if (entry==α)                                                               goto DVAR_α;
    if (entry==β)                                                               goto DVAR_β;
    DVAR_α:         { DESCR_t val=NV_GET_fn(ζ->name); int rebuilt=0;            
                      if (val.v==DT_P && val.p && val.p!=ζ->child_state) {      
                          bb_node_t c=bb_build(val.p);                          
                          ζ->child_fn=c.fn; ζ->child_state=c.ζ; ζ->child_size=c.ζ_size; rebuilt=1; }
                      else if (val.v==DT_S && val.s) {                          
                          lit_t *lz=(lit_t*)ζ->child_state;                   
                          if (!lz||lz->lit!=val.s) {                            
                              lz=calloc(1,sizeof(lit_t)); lz->lit=val.s; lz->len=(int)strlen(val.s);
                              ζ->child_fn=(bb_box_fn)bb_lit; ζ->child_state=lz; 
                              ζ->child_size=sizeof(lit_t); rebuilt=1; } }      
                      if (!rebuilt&&ζ->child_state&&ζ->child_size               
                          &&ζ->child_fn!=(bb_box_fn)bb_lit)                     
                          memset(ζ->child_state,0,ζ->child_size); }             
                    if (!ζ->child_fn)                                           goto DVAR_ω;
                    DVAR=ζ->child_fn(ζ->child_state,α);                         
                    if (spec_is_empty(DVAR))                                    goto DVAR_ω;
                                                                                goto DVAR_γ;
    DVAR_β:         if (!ζ->child_fn)                                           goto DVAR_ω;
                    DVAR=ζ->child_fn(ζ->child_state,β);                         
                    if (spec_is_empty(DVAR))                                    goto DVAR_ω;
                                                                                goto DVAR_γ;
    DVAR_γ:                                                                     return DVAR;
    DVAR_ω:                                                                     return spec_empty;
}

dvar_t *bb_dvar_new(const char *name)
{ dvar_t *ζ=calloc(1,sizeof(dvar_t)); ζ->name=name; return ζ; }

/* ───── fence ───── */
/* _XFNCE    FENCE       succeed once; β cuts (no retry) */


spec_t bb_fence(void *zeta, int entry)
{
    fence_t *ζ = zeta;
    if (entry==α)                                                               goto FENCE_α;
    if (entry==β)                                                               goto FENCE_β;
    FENCE_α:        ζ->fired=1;                                                 goto FENCE_γ;
    FENCE_β:                                                                    goto FENCE_ω;
    FENCE_γ:                                                                    return spec(Σ+Δ,0);
    FENCE_ω:                                                                    return spec_empty;
}

fence_t *bb_fence_new(void)
{ return calloc(1,sizeof(fence_t)); }

/* ───── fail ───── */
/* _XFAIL    FAIL        always ω — force backtrack */


spec_t bb_fail(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return spec_empty;
}

fail_t *bb_fail_new(void)
{ return calloc(1,sizeof(fail_t)); }

/* ───── rpos ───── */
/* _XRPSI    RPOS        assert cursor == Ω-n (zero-width) */


spec_t bb_rpos(void *zeta, int entry)
{
    rpos_t *ζ = zeta;
    spec_t RPOS;
    if (entry==α)                                                               goto RPOS_α;
    if (entry==β)                                                               goto RPOS_β;
    RPOS_α:         if (Δ != Ω-ζ->n)                                            goto RPOS_ω;
                    RPOS = spec(Σ+Δ,0);                                         goto RPOS_γ;
    RPOS_β:                                                                     goto RPOS_ω;
    RPOS_γ:                                                                     return RPOS;
    RPOS_ω:                                                                     return spec_empty;
}

rpos_t *bb_rpos_new(int n)
{ rpos_t *ζ=calloc(1,sizeof(rpos_t)); ζ->n=n; return ζ; }

/* ───── rtab ───── */
/* _XRTB     RTAB        advance cursor TO position Ω-n */


spec_t bb_rtab(void *zeta, int entry)
{
    rtab_t *ζ = zeta;
    spec_t RTAB;
    if (entry==α)                                                               goto RTAB_α;
    if (entry==β)                                                               goto RTAB_β;
    RTAB_α:         if (Δ > Ω-ζ->n)                                             goto RTAB_ω;
                    ζ->advance=(Ω-ζ->n)-Δ; RTAB=spec(Σ+Δ,ζ->advance); Δ=Ω-ζ->n; goto RTAB_γ;
    RTAB_β:         Δ -= ζ->advance;                                            goto RTAB_ω;
    RTAB_γ:                                                                     return RTAB;
    RTAB_ω:                                                                     return spec_empty;
}

rtab_t *bb_rtab_new(int n)
{ rtab_t *ζ=calloc(1,sizeof(rtab_t)); ζ->n=n; return ζ; }

/* ───── succeed ───── */
/* _XSUCF    SUCCEED     always γ zero-width; outer loop retries */


spec_t bb_succeed(void *zeta, int entry)
{
    (void)zeta; (void)entry;
    return spec(Σ+Δ,0);
}

succeed_t *bb_succeed_new(void)
{ return calloc(1,sizeof(succeed_t)); }

