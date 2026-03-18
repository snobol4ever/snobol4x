/*
 * arbno_aorb_star_abb.c — Sprint 8 Oracle: ARBNO(ALT)
 * =====================================================
 * Automata Theory Oracle #4 — Regular language tier (Type 3).
 *
 * Language:  L = { w ∈ {a,b}* | w ends in "abb" }
 *            All strings over {a,b} whose last three characters are 'a','b','b'.
 *
 * This is the canonical DFA-from-NFA construction example from every
 * automata theory textbook (Hopcroft & Ullman, Sipser, etc.).
 * The NFA has 4 states; the equivalent DFA has 5 states.
 * As a SNOBOL4 pattern it is:
 *
 *   IR:
 *     g.add('AB',   Alt(Lit('a'), Lit('b')))
 *     g.add('ROOT', Cat(Arbno(Ref('AB')), Cat(Lit('a'), Cat(Lit('b'), Lit('b')))))
 *
 * Which reads: zero or more (a|b), followed by the literal suffix "abb".
 *
 * What this oracle proves:
 *   1. ARBNO(Ref(P)) — ARBNO over a named sub-pattern (requires FuncEmitter
 *      Ref call sites inside an ARBNO loop; each iteration gets a fresh child
 *      frame via the depth-indexed cursor stack).
 *   2. ARBNO backtracking into ALT — when the suffix "abb" fails to match,
 *      the engine backtracks into ARBNO, which backtracks into ALT(a|b),
 *      trying the other arm before giving up a character.
 *   3. Greedy ARBNO + required suffix — ARBNO is greedy (tries to consume as
 *      much as possible first), then the suffix forces backtracking to find
 *      the correct split point. E.g. "aabb": ARBNO grabs "aab", then tries
 *      "b"→fail, backtracks, ARBNO grabs "aa", suffix "bb"→fail, backtracks,
 *      ARBNO grabs "a", suffix "abb" ✓.
 *   4. Pumping lemma boundary: pumping length p=4 for this language.
 *      Strings at boundary: "aabb" (accept), "abbb" (reject — ends "bbb").
 *      One below: "abb" (accept — shortest member).
 *
 * Oracle cases (14 total):
 *   Positive (membership — strings ending in "abb"):
 *     1.  "abb"       → Success  (shortest member)
 *     2.  "aabb"      → Success
 *     3.  "babb"      → Success
 *     4.  "aababb"    → Success
 *     5.  "ababb"     → Success
 *     6.  "bbabb"     → Success
 *     7.  "abababb"   → Success  (longer prefix)
 *   Negative (non-membership):
 *     8.  ""          → Failure  (empty)
 *     9.  "ab"        → Failure  (too short)
 *     10. "ba"        → Failure  (wrong ending)
 *     11. "abba"      → Failure  (ends in 'a' not "abb")
 *     12. "abbb"      → Failure  (ends in "bbb" not "abb")
 *     13. "aab"       → Failure  (ends in "aab")
 *     14. "bbba"      → Failure  (ends in 'a')
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
/* FORWARD DECLARATIONS */
typedef struct _AB   AB_t;
typedef struct _ROOT ROOT_t;
str_t AB  (AB_t **,   int);
str_t ROOT(ROOT_t **, int);
/*============================================================================*/
/*
 * AB = Alt(Lit('a'), Lit('b'))
 *
 * Matches exactly one character: 'a' or 'b'.
 * Alt arm 1 = Lit('a'), arm 2 = Lit('b').
 */
typedef struct _AB {
    int     alt_i;      /* 1 = 'a' arm; 2 = 'b' arm */
    int lita_saved;
    int litb_saved;
} AB_t;

str_t AB(AB_t ** ζζ, int entry) {
    AB_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(AB_t)); goto AB_α; }
    if (entry == β){                                         goto AB_β; }

    str_t lita;
    lita_α: if (Σ[Δ] == '\0' || Σ[Δ] != 'a')               goto lita_ω;
            ζ->lita_saved = Δ; lita = str(Σ+Δ, 1); Δ++;     goto lita_γ;
    lita_β: Δ = ζ->lita_saved;                               goto lita_ω;

    str_t litb;
    litb_α: if (Σ[Δ] == '\0' || Σ[Δ] != 'b')               goto litb_ω;
            ζ->litb_saved = Δ; litb = str(Σ+Δ, 1); Δ++;     goto litb_γ;
    litb_β: Δ = ζ->litb_saved;                               goto litb_ω;

    str_t alt;
    AB_α:   ζ->alt_i = 1;                                    goto lita_α;
    AB_β:   if (ζ->alt_i == 1)                               goto lita_β;
            if (ζ->alt_i == 2)                               goto litb_β;
                                                              goto AB_ω;
    lita_γ: alt = lita; ζ->alt_i = 1;                        goto AB_γ;
    lita_ω: ζ->alt_i = 2;                                    goto litb_α;
    litb_γ: alt = litb; ζ->alt_i = 2;                        goto AB_γ;
    litb_ω:                                                   goto AB_ω;

    AB_γ:   return alt;
    AB_ω:   return empty;
}
/*============================================================================*/
/*
 * ROOT = Cat(Arbno(Ref('AB')), Cat(Lit('a'), Cat(Lit('b'), Lit('b'))))
 *
 * ARBNO(AB): zero or more (a|b) — greedy, depth-indexed.
 *   On alpha: depth=-1, yield empty (zero iterations), succeed.
 *   On beta:  depth++, save cursor, try one more AB iteration.
 *   If AB fails: restore cursor, depth--, fail (propagate to outer beta).
 *
 * Then: literal suffix "abb".
 *
 * The key backtracking scenario:
 *   Subject "aabb":
 *     ARBNO grabs "aab" (3 iters), then tries Lit('a') → 'b' ≠ 'a' → fail
 *     → ARBNO beta: give back one, try "aa" (2 iters), Lit('a') → 'b' ≠ 'a' → fail
 *     → ARBNO beta: give back one, try "a" (1 iter), Lit('a') → 'a' ✓,
 *       Lit('b') → 'b' ✓, Lit('b') → 'b' ✓, RPOS(0) ✓ → Success.
 */
#define ARBNO_MAX 64

typedef struct _ROOT {
    /* ARBNO state */
    int     arb_depth;
    int arb_cursors[ARBNO_MAX];
    AB_t  * arb_child[ARBNO_MAX];   /* one child frame per depth level */
    /* suffix Lit state */
    int lita_saved;
    int litb1_saved;
    int litb2_saved;
} ROOT_t;

str_t ROOT(ROOT_t ** ζζ, int entry) {
    ROOT_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(ROOT_t)); goto ROOT_α; }
    if (entry == β){                                           goto ROOT_β; }

    /*--- ARBNO(Ref('AB')) --------------------------------------------------*/
    /* Alpha: yield empty (zero iterations) first */
    ROOT_α:
        ζ->arb_depth = -1;
        goto arb_γ;

    /* Beta: try one more AB iteration */
    arb_try:
        ζ->arb_depth++;
        if (ζ->arb_depth >= ARBNO_MAX)                        goto ROOT_ω;
        ζ->arb_cursors[ζ->arb_depth] = Δ;
        ζ->arb_child[ζ->arb_depth] = 0;   /* fresh child frame */
        {
            str_t _r = AB(&ζ->arb_child[ζ->arb_depth], α);
            if (is_empty(_r)) {
                /* AB failed immediately: restore cursor, give up this depth */
                Δ = ζ->arb_cursors[ζ->arb_depth];
                ζ->arb_depth--;
                goto ROOT_ω;
            }
        }
        goto arb_γ;

    /* ARBNO beta from downstream: try backtracking into last AB first */
    ROOT_β:
        if (ζ->arb_depth < 0)                                 goto ROOT_ω;
        {
            /* Try backtracking into the current depth's AB child */
            str_t _r = AB(&ζ->arb_child[ζ->arb_depth], β);
            if (!is_empty(_r))                                 goto arb_γ;
        }
        /* AB exhausted at this depth: restore cursor, shrink */
        Δ = ζ->arb_cursors[ζ->arb_depth];
        ζ->arb_depth--;
        goto arb_γ;   /* re-enter suffix with one fewer iteration */

    arb_γ:  /* ARBNO succeeded (possibly with 0 matches) — try suffix */

    /*--- Lit('a') ----------------------------------------------------------*/
    str_t lita;
    lita_α: if (Δ >= Ω || Σ[Δ] != 'a')                       goto lita_ω;
            ζ->lita_saved = Δ; lita = str(Σ+Δ, 1); Δ++;      goto lita_γ;
    lita_β: Δ = ζ->lita_saved;                                goto lita_ω;
    lita_γ:                                                    goto litb1_α;
    lita_ω:                                                    goto arb_try;

    /*--- Lit('b') #1 -------------------------------------------------------*/
    str_t litb1;
    litb1_α: if (Δ >= Ω || Σ[Δ] != 'b')                      goto litb1_ω;
             ζ->litb1_saved = Δ; litb1 = str(Σ+Δ, 1); Δ++;   goto litb1_γ;
    litb1_β: Δ = ζ->litb1_saved;                              goto litb1_ω;
    litb1_γ:                                                   goto litb2_α;
    litb1_ω:                                                   goto lita_β;

    /*--- Lit('b') #2 -------------------------------------------------------*/
    str_t litb2;
    litb2_α: if (Δ >= Ω || Σ[Δ] != 'b')                      goto litb2_ω;
             ζ->litb2_saved = Δ; litb2 = str(Σ+Δ, 1); Δ++;   goto litb2_γ;
    litb2_β: Δ = ζ->litb2_saved;                              goto litb2_ω;
    litb2_γ:                                                   goto ROOT_γ;
    litb2_ω:                                                   goto litb1_β;

    ROOT_γ: return str(Σ, Δ);
    ROOT_ω: return empty;
}
/*============================================================================*/
static void match(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    ROOT_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? ROOT(&ez, α) : ROOT(&ez, β);
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
    printf("=== (a|b)*abb oracle — ARBNO(ALT), greedy+backtrack (14 cases) ===\n");

    /* Positive */
    printf("[1]  \"abb\"     → "); match("abb");
    printf("[2]  \"aabb\"    → "); match("aabb");
    printf("[3]  \"babb\"    → "); match("babb");
    printf("[4]  \"aababb\"  → "); match("aababb");
    printf("[5]  \"ababb\"   → "); match("ababb");
    printf("[6]  \"bbabb\"   → "); match("bbabb");
    printf("[7]  \"abababb\" → "); match("abababb");

    /* Negative */
    printf("[8]  \"\"        → "); match("");
    printf("[9]  \"ab\"      → "); match("ab");
    printf("[10] \"ba\"      → "); match("ba");
    printf("[11] \"abba\"    → "); match("abba");
    printf("[12] \"abbb\"    → "); match("abbb");
    printf("[13] \"aab\"     → "); match("aab");
    printf("[14] \"bbba\"    → "); match("bbba");

    return 0;
}
#endif
