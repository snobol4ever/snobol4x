/*
 * ref_balanced_parens.c — Sprint 11 Oracle: Dyck Language
 * =========================================================
 * Automata Theory Oracle #7 — Context-Free language tier (Type 2).
 *
 * Language:  L = Dyck language over {(, )}
 *            All strings of balanced parentheses.
 *            { "", "()", "(())", "()()", "((()))", "(()())", ... }
 *
 * This is the Dyck language — named after mathematician Walther von Dyck.
 * It is the canonical language of nested structure. Every programming
 * language's expression grammar contains it. Every XML/HTML parser validates
 * it. Every compiler checks it. It is the heartbeat of structured syntax.
 *
 * The grammar: D → ε | ( D ) D
 * Reading: a balanced string is either empty, or an open paren, a balanced
 * interior, a close paren, followed by more balanced content.
 *
 * As a SNOBOL4 recursive REF pattern:
 *
 *   IR:
 *     g.add('D', Alt(
 *                  Lit(''),                              -- ε (base: empty)
 *                  Cat(Lit('('), Cat(Ref('D'),           -- ( D ) D
 *                      Cat(Lit(')'), Ref('D'))))
 *                ))
 *
 * What this oracle proves:
 *   1. Third distinct Type 2 language — completes the Type 2 tier.
 *   2. The grammar D → ε | (D)D has TWO recursive Ref calls in a single
 *      production — Ref('D') appears twice in arm 2. This tests that each
 *      Ref call site has its own independent child frame (ζ1, ζ2), and that
 *      backtracking into one does not corrupt the other.
 *   3. The epsilon base case fires first (Alt arm 1 = Lit('')). This means
 *      the engine must be driven by RPOS(0) to find the maximal balanced
 *      match, backtracking through epsilon yields until the full subject
 *      is consumed.
 *   4. Nested depth is unbounded — "(((...)))" of any depth must succeed.
 *
 * Oracle cases (16 total):
 *   Positive:
 *     1.  ""         → Success  (empty string is balanced — ε)
 *     2.  "()"       → Success  (single pair)
 *     3.  "(())"     → Success  (nested)
 *     4.  "()()"     → Success  (sequential)
 *     5.  "((()))"   → Success  (depth 3)
 *     6.  "(()())"   → Success  (mixed nesting)
 *     7.  "()(())"   → Success  (sequential + nested)
 *     8.  "(()(()))" → Success  (deep mixed)
 *   Negative:
 *     9.  "("        → Failure  (unclosed)
 *     10. ")"        → Failure  (unopened)
 *     11. ")("       → Failure  (wrong order)
 *     12. "(()"      → Failure  (one unclosed)
 *     13. "(])"      skipped — alphabet is only ( )
 *     13. "())"      → Failure  (extra close)
 *     14. "((("      → Failure  (all unclosed)
 *     15. "(()("     → Failure  (unclosed inner)
 *     16. "))(("     → Failure  (all wrong)
 */

#ifdef __GNUC__
#define __kernel
#define __global
#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
extern int printf(const char *, ...);
#endif
/*----------------------------------------------------------------------------*/
typedef struct { const char * σ; int δ; } str_t;
typedef struct { unsigned int pos; __global char * buffer; } output_t;
/*----------------------------------------------------------------------------*/
#if 1
void    write_nl(output_t * out)                 { printf("%s", "\n"); }
int     write_int(output_t * out, int v)         { printf("%d", v); return v; }
void    write_sz(output_t * out, const char * s) { printf("%s", s); }
str_t   write_str(output_t * out, str_t str) {
            printf("%.*s", str.δ, str.σ);
            return str;
        }
void    write_flush(output_t * out) {}
#endif
/*----------------------------------------------------------------------------*/
static int Δ = 0;
static int Ω = 0;
static const char * Σ = (const char *) 0;
static const int α = 0;
static const int β = 1;
static const str_t empty = (str_t) {(const char *) 0, 0};
static inline bool is_empty(str_t x) { return x.σ == (const char *) 0; }
static inline int len(const char * s) { int δ = 0; for (; *s; δ++) s++; return δ; }
static inline str_t str(const char * σ, int δ) { return (str_t) {σ, δ}; }
static inline str_t cat(str_t x, str_t y) { return (str_t) {x.σ, x.δ + y.δ}; }
static output_t * out = (output_t *) 0;
/*----------------------------------------------------------------------------*/
static inline void * enter(void ** ζζ, size_t size) {
    void * ζ = *ζζ;
    if (size)
        if (ζ) memset(ζ, 0, size);
        else ζ = *ζζ = calloc(1, size);
    return ζ;
}
/*============================================================================*/
/* FORWARD DECLARATION */
typedef struct _D D_t;
str_t D(D_t **, int);
/*============================================================================*/
/*
 * D = Alt(Lit(''),                                        -- arm 1: ε
 *         Cat(Lit('('), Cat(Ref('D'), Cat(Lit(')'), Ref('D')))))  -- arm 2: (D)D
 *
 * Two independent Ref('D') call sites in arm 2: D_ζ1 (inner) and D_ζ2 (tail).
 * Each has its own frame pointer in the parent struct.
 *
 * Backtrack discipline for arm 2:
 *   On β from downstream: first try backtracking D_ζ2 (tail).
 *   If D_ζ2 exhausted: backtrack Lit(')').
 *   If Lit(')') exhausted: backtrack D_ζ1 (inner).
 *   If D_ζ1 exhausted: backtrack Lit('(').
 *   If Lit('(') exhausted: arm 2 fails → Alt falls to arm 1 (ε) → already
 *   tried → P_ω.
 */
typedef struct _D {
    int   alt_i;     /* 1 = ε, 2 = (D)D */
    D_t * D_ζ1;      /* Ref('D') — inner, after '(' */
    D_t * D_ζ2;      /* Ref('D') — tail,  after ')' */
    int   lp_saved;  /* Lit('(') cursor save */
    int   rp_saved;  /* Lit(')') cursor save */
} D_t;

str_t D(D_t ** ζζ, int entry) {
    D_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(D_t)); goto D_α; }
    if (entry == β){                                        goto D_β; }

    /* ------------------------------------------------------------------ */
    /* ARM 1: ε                                                            */
    /* ------------------------------------------------------------------ */
    str_t eps;
    eps_α:  eps = str(Σ+Δ, 0);  goto eps_γ;
    eps_β:                       goto eps_ω;

    /* ------------------------------------------------------------------ */
    /* ARM 2: ( D ) D                                                      */
    /* ------------------------------------------------------------------ */

    /* Lit('(') */
    str_t lp;
    lp_α:   if (Δ>=Ω || Σ[Δ]!='(') goto lp_ω;
            ζ->lp_saved=Δ; lp=str(Σ+Δ,1); Δ++; goto lp_γ;
    lp_β:   Δ=ζ->lp_saved; goto lp_ω;

    /* Ref('D') — inner */
    str_t D1r;
    D1_α:   ζ->D_ζ1=0; D1r=D(&ζ->D_ζ1,α); goto D1_λ;
    D1_β:              D1r=D(&ζ->D_ζ1,β); goto D1_λ;
    D1_λ:   if(is_empty(D1r)) goto D1_ω; else goto D1_γ;

    /* Lit(')') */
    str_t rp;
    rp_α:   if (Δ>=Ω || Σ[Δ]!=')') goto rp_ω;
            ζ->rp_saved=Δ; rp=str(Σ+Δ,1); Δ++; goto rp_γ;
    rp_β:   Δ=ζ->rp_saved; goto rp_ω;

    /* Ref('D') — tail */
    str_t D2r;
    D2_α:   ζ->D_ζ2=0; D2r=D(&ζ->D_ζ2,α); goto D2_λ;
    D2_β:              D2r=D(&ζ->D_ζ2,β); goto D2_λ;
    D2_λ:   if(is_empty(D2r)) goto D2_ω; else goto D2_γ;

    /*
     * Cat wiring: ( → D1 → ) → D2
     * Accumulate the full matched span in arm2.
     */
    str_t arm2;
    str_t mid1;   /* span after '(' */
    str_t mid2;   /* span after D1  */
    str_t mid3;   /* span after ')'  */

    arm2_α:  arm2=str(Σ+Δ,0);           goto lp_α;
    arm2_β:                              goto D2_β;

    lp_γ:    arm2=cat(arm2,lp);
             mid1=str(Σ+Δ,0);           goto D1_α;
    lp_ω:                               goto arm2_ω;

    D1_γ:    mid1=cat(mid1,D1r);
             mid2=str(Σ+Δ,0);           goto rp_α;
    D1_ω:                               goto lp_β;

    rp_γ:    mid2=cat(mid2,rp);
             mid3=str(Σ+Δ,0);           goto D2_α;
    rp_ω:                               goto D1_β;

    D2_γ:    mid3=cat(mid3,D2r);
             arm2=cat(arm2,cat(mid1,cat(mid2,mid3)));
             goto arm2_γ;
    D2_ω:                               goto rp_β;

    arm2_ω:                             goto D_ω;

    /* ------------------------------------------------------------------ */
    /* Alt dispatcher                                                      */
    /* ------------------------------------------------------------------ */
    str_t alt;
    D_α:      ζ->alt_i=1; ζ->D_ζ1=0; ζ->D_ζ2=0; goto eps_α;
    D_β:      if(ζ->alt_i==1) goto eps_β;
              if(ζ->alt_i==2) goto arm2_β;
              goto D_ω;
    eps_γ:    alt=eps;  ζ->alt_i=1; goto D_γ;
    eps_ω:    ζ->alt_i=2;           goto arm2_α;
    arm2_γ:   alt=arm2; ζ->alt_i=2; goto D_γ;

    D_γ:  return alt;
    D_ω:  return empty;
}
/*============================================================================*/
static void match(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0,0}; out = &tmp;
    D_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? D(&ez,α) : D(&ez,β);
        first = 0;
        if (is_empty(r)) { write_sz(out,"Failure."); write_nl(out); return; }
        if (Δ == Ω) {
            write_str(out,r); write_nl(out);
            write_sz(out,"Success!"); write_nl(out);
            return;
        }
    }
}
/*============================================================================*/
#ifdef __GNUC__
int main() {
    printf("=== Dyck language oracle — balanced parens, Type 2 (16 cases) ===\n");

    /* Positive */
    printf("[1]  \"\"         → "); match("");
    printf("[2]  \"()\"       → "); match("()");
    printf("[3]  \"(())\"     → "); match("(())");
    printf("[4]  \"()()\"     → "); match("()()");
    printf("[5]  \"((()))\"   → "); match("((()))");
    printf("[6]  \"(()())\"   → "); match("(()())");
    printf("[7]  \"()(())\"   → "); match("()(())");
    printf("[8]  \"(()(()))\" → "); match("(()(()))");

    /* Negative */
    printf("[9]  \"(\"        → "); match("(");
    printf("[10] \")\"        → "); match(")");
    printf("[11] \")(\"       → "); match(")(");
    printf("[12] \"(()\"      → "); match("(()");
    printf("[13] \"())\"      → "); match("())");
    printf("[14] \"(((\"      → "); match("(((");
    printf("[15] \"(()(\")]   → "); match("(()(");
    printf("[16] \"))((\"     → "); match("))((");

    return 0;
}
#endif
