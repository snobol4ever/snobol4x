/*
 * ref_astar_bstar.c — Sprint 6 Oracle: a*b*
 * ==========================================
 * Automata Theory Oracle #2 — Regular language tier (Type 3).
 *
 * Language:  L = { a^m b^n | m >= 0, n >= 0 }
 *            All strings of zero or more 'a's followed by zero or more 'b's.
 *
 * IR (what emit_c.py must generate from):
 *
 *   g = Graph()
 *   g.add('A',          Alt(Lit(''), Cat(Lit('a'), Ref('A'))))
 *   g.add('B',          Alt(Lit(''), Cat(Lit('b'), Ref('B'))))
 *   g.add('ASTAR_BSTAR', Cat(Ref('A'), Ref('B')))
 *
 * Semantics:
 *   A           matches zero or more 'a's  (a*)
 *   B           matches zero or more 'b's  (b*)
 *   ASTAR_BSTAR matches a* followed by b*
 *
 * This oracle proves:
 *   1. Self-recursive REF (A calling A, B calling B) — distinct from Gemini's
 *      mutual recursion. Self-recursion is the simpler case and must work first.
 *   2. Cat of two Ref nodes — the top-level pattern chains two recursive
 *      sub-patterns. This tests that Ref return values are correctly threaded
 *      through a Cat combinator.
 *   3. Greedy + backtracking interaction — A is greedy (tries maximal match
 *      first via leftmost Alt arm = epsilon, then one-more-a). When the
 *      downstream B fails (e.g. subject is "aab" — A grabs "aa", B grabs "b",
 *      succeeds). But if subject is "aba" the engine must backtrack: A tries
 *      "ab..." — 'b' != 'a', so A = "a", then B tries "ba..." — 'a' != 'b',
 *      B = "", then RPOS(0) fails because 'b' and 'a' remain → Failure.
 *   4. Empty sub-matches — both A and B can match empty strings. The empty-
 *      string case ("") must succeed with A="" B="" → ASTAR_BSTAR="".
 *
 * Pumping lemma boundary cases:
 *   Pumping length p=1 for a* (or b*) since the DFA has 1 state per suffix.
 *   Boundary strings at p: "a" (accept), "b" (accept), "" (accept).
 *   Just beyond: "ab" (accept), "ba" (reject — b before a violates order).
 *
 * Oracle cases (12 total):
 *   Positive (membership):
 *     1.  ""        → Success   (m=0, n=0)
 *     2.  "a"       → Success   (m=1, n=0)
 *     3.  "b"       → Success   (m=0, n=1)
 *     4.  "ab"      → Success   (m=1, n=1)
 *     5.  "aab"     → Success   (m=2, n=1)
 *     6.  "abb"     → Success   (m=1, n=2)
 *     7.  "aabb"    → Success   (m=2, n=2)
 *     8.  "aaabbb"  → Success   (m=3, n=3)
 *   Negative (non-membership):
 *     9.  "ba"      → Failure   (b before a)
 *     10. "aba"     → Failure   (a after b)
 *     11. "bba"     → Failure   (a after bb)
 *     12. "aabba"   → Failure   (a after bb)
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
/* enter(): allocate-and-clear on first call, clear-only on re-entry */
static inline void * enter(void ** ζζ, size_t size) {
    void * ζ = *ζζ;
    if (size)
        if (ζ) memset(ζ, 0, size);
        else ζ = *ζζ = calloc(1, size);
    return ζ;
}
/*============================================================================*/
/* FORWARD DECLARATIONS */
typedef struct _A          A_t;
typedef struct _B          B_t;
typedef struct _ASTAR_BSTAR ASTAR_BSTAR_t;
str_t A          (A_t **,           int);
str_t B          (B_t **,           int);
str_t ASTAR_BSTAR(ASTAR_BSTAR_t **, int);
/*============================================================================*/
/*
 * A = Alt(Lit(''), Cat(Lit('a'), Ref('A')))
 *
 * Self-recursive: matches zero or more 'a's.
 * Alt arm 1 = epsilon (empty match — base case).
 * Alt arm 2 = Cat(Lit('a'), Ref('A')) — one 'a' then recurse.
 */
typedef struct _A {
    int   alt_i;   /* 1 = epsilon arm; 2 = seq(Lit('a'), Ref('A')) */
    A_t * A_ζ;     /* child frame for Ref('A')                      */
} A_t;

str_t A(A_t ** ζζ, int entry) {
    A_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(A_t));  goto A_α; }
    if (entry == β){                                         goto A_β; }
    /*--- Lit('') -----------------------------------------------------------*/
    str_t       eps1;
    eps1_α:     eps1 = str(Σ+Δ, 0);                         goto eps1_γ;
    eps1_β:                                                  goto eps1_ω;
    /*--- Lit('a') ----------------------------------------------------------*/
    str_t       lita2;
    lita2_α:    if (Σ[Δ] != 'a')                            goto lita2_ω;
                lita2 = str(Σ+Δ, 1); Δ++;                   goto lita2_γ;
    lita2_β:    Δ--;                                         goto lita2_ω;
    /*--- Ref('A') ----------------------------------------------------------*/
    str_t       A3;
    A3_α:       A3 = A(&ζ->A_ζ, α);                         goto A3_λ;
    A3_β:       A3 = A(&ζ->A_ζ, β);                         goto A3_λ;
    A3_λ:       if (is_empty(A3))                            goto A3_ω;
                else                                         goto A3_γ;
    /*--- Cat(Lit('a'), Ref('A')) -------------------------------------------*/
    str_t       seq2;
    seq2_α:     seq2 = str(Σ+Δ, 0);                         goto lita2_α;
    seq2_β:                                                  goto A3_β;
    lita2_γ:    seq2 = cat(seq2, lita2);                     goto A3_α;
    lita2_ω:                                                 goto seq2_ω;
    A3_γ:       seq2 = cat(seq2, A3);                        goto seq2_γ;
    A3_ω:                                                    goto lita2_β;
    /*--- Alt dispatcher ----------------------------------------------------*/
    str_t       alt;
    A_α:        ζ->alt_i = 1;                                goto eps1_α;
    A_β:        if (ζ->alt_i == 1)                           goto eps1_β;
                if (ζ->alt_i == 2)                           goto seq2_β;
                                                             goto A_ω;
    eps1_γ:     alt = eps1;  ζ->alt_i = 1;                   goto A_γ;
    eps1_ω:     ζ->alt_i++;                                  goto seq2_α;
    seq2_γ:     alt = seq2;  ζ->alt_i = 2;                   goto A_γ;
    seq2_ω:                                                  goto A_ω;
    /*----------------------------------------------------------------------*/
    A_γ:        return alt;
    A_ω:        return empty;
}
/*============================================================================*/
/*
 * B = Alt(Lit(''), Cat(Lit('b'), Ref('B')))
 *
 * Self-recursive: matches zero or more 'b's.
 * Symmetric to A, character 'b'.
 */
typedef struct _B {
    int   alt_i;   /* 1 = epsilon arm; 2 = seq(Lit('b'), Ref('B')) */
    B_t * B_ζ;     /* child frame for Ref('B')                      */
} B_t;

str_t B(B_t ** ζζ, int entry) {
    B_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(B_t));  goto B_α; }
    if (entry == β){                                         goto B_β; }
    /*--- Lit('') -----------------------------------------------------------*/
    str_t       eps1;
    eps1_α:     eps1 = str(Σ+Δ, 0);                         goto eps1_γ;
    eps1_β:                                                  goto eps1_ω;
    /*--- Lit('b') ----------------------------------------------------------*/
    str_t       litb2;
    litb2_α:    if (Σ[Δ] != 'b')                            goto litb2_ω;
                litb2 = str(Σ+Δ, 1); Δ++;                   goto litb2_γ;
    litb2_β:    Δ--;                                         goto litb2_ω;
    /*--- Ref('B') ----------------------------------------------------------*/
    str_t       B3;
    B3_α:       B3 = B(&ζ->B_ζ, α);                         goto B3_λ;
    B3_β:       B3 = B(&ζ->B_ζ, β);                         goto B3_λ;
    B3_λ:       if (is_empty(B3))                            goto B3_ω;
                else                                         goto B3_γ;
    /*--- Cat(Lit('b'), Ref('B')) -------------------------------------------*/
    str_t       seq2;
    seq2_α:     seq2 = str(Σ+Δ, 0);                         goto litb2_α;
    seq2_β:                                                  goto B3_β;
    litb2_γ:    seq2 = cat(seq2, litb2);                     goto B3_α;
    litb2_ω:                                                 goto seq2_ω;
    B3_γ:       seq2 = cat(seq2, B3);                        goto seq2_γ;
    B3_ω:                                                    goto litb2_β;
    /*--- Alt dispatcher ----------------------------------------------------*/
    str_t       alt;
    B_α:        ζ->alt_i = 1;                                goto eps1_α;
    B_β:        if (ζ->alt_i == 1)                           goto eps1_β;
                if (ζ->alt_i == 2)                           goto seq2_β;
                                                             goto B_ω;
    eps1_γ:     alt = eps1;  ζ->alt_i = 1;                   goto B_γ;
    eps1_ω:     ζ->alt_i++;                                  goto seq2_α;
    seq2_γ:     alt = seq2;  ζ->alt_i = 2;                   goto B_γ;
    seq2_ω:                                                  goto B_ω;
    /*----------------------------------------------------------------------*/
    B_γ:        return alt;
    B_ω:        return empty;
}
/*============================================================================*/
/*
 * ASTAR_BSTAR = Cat(Ref('A'), Ref('B'))
 *
 * Top-level pattern: chain A* then B*.
 * Cat of two Ref nodes — tests that Ref return values thread through Cat.
 */
typedef struct _ASTAR_BSTAR {
    A_t * A_ζ;     /* child frame for Ref('A') */
    B_t * B_ζ;     /* child frame for Ref('B') */
} ASTAR_BSTAR_t;

str_t ASTAR_BSTAR(ASTAR_BSTAR_t ** ζζ, int entry) {
    ASTAR_BSTAR_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(ASTAR_BSTAR_t)); goto AB_α; }
    if (entry == β){                                                   goto AB_β; }
    /*--- Ref('A') ----------------------------------------------------------*/
    str_t       Ar;
    Ar_α:       Ar = A(&ζ->A_ζ, α);                          goto Ar_λ;
    Ar_β:       Ar = A(&ζ->A_ζ, β);                          goto Ar_λ;
    Ar_λ:       if (is_empty(Ar))                             goto Ar_ω;
                else                                          goto Ar_γ;
    /*--- Ref('B') ----------------------------------------------------------*/
    str_t       Br;
    Br_α:       Br = B(&ζ->B_ζ, α);                          goto Br_λ;
    Br_β:       Br = B(&ζ->B_ζ, β);                          goto Br_λ;
    Br_λ:       if (is_empty(Br))                             goto Br_ω;
                else                                          goto Br_γ;
    /*--- Cat(Ref('A'), Ref('B')) -------------------------------------------*/
    str_t       seq;
    AB_α:       seq = str(Σ+Δ, 0);                            goto Ar_α;
    AB_β:                                                      goto Br_β;
    Ar_γ:       seq = cat(seq, Ar);                            goto Br_α;
    Ar_ω:                                                      goto AB_ω;
    Br_γ:       seq = cat(seq, Br);                            goto AB_γ;
    Br_ω:                                                      goto Ar_β;
    /*----------------------------------------------------------------------*/
    AB_γ:       return seq;
    AB_ω:       return empty;
}
/*============================================================================*/
/*
 * Harness: POS(0) ASTAR_BSTAR $ OUTPUT RPOS(0)
 */
static void match_astar_bstar(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    ASTAR_BSTAR_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? ASTAR_BSTAR(&ez, α) : ASTAR_BSTAR(&ez, β);
        first = 0;
        if (is_empty(r)) { write_sz(out, "Failure."); write_nl(out); return; }
        if (Δ == Ω) {
            write_str(out, r); write_nl(out);
            write_sz(out, "Success!"); write_nl(out);
            return;
        }
    }
}
/*============================================================================*/
#ifdef __GNUC__
int main() {
    printf("=== a*b* oracle — Ref/self-recursion, Cat(Ref,Ref) (12 cases) ===\n");

    /* --- Positive cases (membership) --- */
    printf("[1]  \"\"       → "); match_astar_bstar("");       /* Success (m=0,n=0) */
    printf("[2]  \"a\"      → "); match_astar_bstar("a");      /* Success (m=1,n=0) */
    printf("[3]  \"b\"      → "); match_astar_bstar("b");      /* Success (m=0,n=1) */
    printf("[4]  \"ab\"     → "); match_astar_bstar("ab");     /* Success (m=1,n=1) */
    printf("[5]  \"aab\"    → "); match_astar_bstar("aab");    /* Success (m=2,n=1) */
    printf("[6]  \"abb\"    → "); match_astar_bstar("abb");    /* Success (m=1,n=2) */
    printf("[7]  \"aabb\"   → "); match_astar_bstar("aabb");   /* Success (m=2,n=2) */
    printf("[8]  \"aaabbb\" → "); match_astar_bstar("aaabbb"); /* Success (m=3,n=3) */

    /* --- Negative cases (non-membership) --- */
    printf("[9]  \"ba\"     → "); match_astar_bstar("ba");     /* Failure (b before a) */
    printf("[10] \"aba\"    → "); match_astar_bstar("aba");    /* Failure (a after b)  */
    printf("[11] \"bba\"    → "); match_astar_bstar("bba");    /* Failure (a after bb) */
    printf("[12] \"aabba\"  → "); match_astar_bstar("aabba"); /* Failure (a after bb) */

    return 0;
}
#endif
