/*
 * ref_anbn.c — Sprint 9 Oracle: {a^n b^n | n >= 1}
 * ==================================================
 * Automata Theory Oracle #5 — Context-Free language tier (Type 2).
 *
 * Language:  L = { a^n b^n | n >= 1 }
 *            Exactly n 'a's followed by exactly n 'b's, n >= 1.
 *            { "ab", "aabb", "aaabbb", "aaaabbbb", ... }
 *
 * This is THE canonical non-regular, context-free language.
 * Its non-regularity is proven by the pumping lemma for regular languages:
 * no finite automaton can count. Its context-freeness is witnessed by the
 * grammar: S → a S b | a b.
 *
 * As a SNOBOL4 recursive REF pattern:
 *
 *   IR:
 *     g.add('S', Alt(
 *                  Cat(Lit('a'), Cat(Ref('S'), Lit('b'))),  -- a S b
 *                  Cat(Lit('a'), Lit('b'))                  -- a b (base case)
 *                ))
 *
 * Reading it: S matches either "a" + recursive-S + "b", or the base case "ab".
 * This is the direct encoding of the CFG production S → a S b | a b.
 *
 * What this oracle proves:
 *   1. The engine correctly recognizes a non-regular language — something
 *      no finite automaton can do. This is the first Type 2 proof.
 *   2. Left-recursive REF nesting: S calls S calls S... to depth n-1,
 *      then unwinds. The frame stack depth equals n.
 *   3. Balanced structure: 'a' consumed on the way in, 'b' consumed on the
 *      way out. The engine must hold the count implicitly in the call stack —
 *      exactly as a pushdown automaton holds it in its stack.
 *   4. Pumping lemma boundary cases:
 *      Pumping length p for a^n b^n is n itself — any string "a^p b^p" can
 *      be pumped by splitting the 'a' prefix. Boundary: "ab" (n=1, shortest
 *      member), "aabb" (n=2), "aaabbb" (n=3).
 *      Just outside: "aab" (2a, 1b → reject), "abb" (1a, 2b → reject).
 *
 * Oracle cases (14 total):
 *   Positive (n >= 1):
 *     1.  "ab"         → Success  (n=1, base case)
 *     2.  "aabb"       → Success  (n=2)
 *     3.  "aaabbb"     → Success  (n=3)
 *     4.  "aaaabbbb"   → Success  (n=4)
 *     5.  "aaaaabbbbb" → Success  (n=5)
 *   Negative (non-members):
 *     6.  ""           → Failure  (empty)
 *     7.  "a"          → Failure  (no b)
 *     8.  "b"          → Failure  (no a)
 *     9.  "ba"         → Failure  (wrong order)
 *     10. "aab"        → Failure  (2a, 1b)
 *     11. "abb"        → Failure  (1a, 2b)
 *     12. "aaabb"      → Failure  (3a, 2b)
 *     13. "aabbb"      → Failure  (2a, 3b)
 *     14. "abab"       → Failure  (interleaved)
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
/* FORWARD DECLARATION — S is self-recursive */
typedef struct _S S_t;
str_t S(S_t **, int);
/*============================================================================*/
/*
 * S = Alt(Cat(Lit('a'), Cat(Ref('S'), Lit('b'))),   -- arm 1: a S b
 *         Cat(Lit('a'), Lit('b')))                   -- arm 2: a b
 *
 * Alt arm 1 (recursive): consume 'a', recurse into S, consume 'b'.
 * Alt arm 2 (base):      consume 'a', consume 'b'.
 *
 * Alt tries arm 1 first (greedy — matches the longest balanced string).
 * If arm 1 fails at any depth, backtrack to arm 2.
 *
 * Frame state:
 *   alt_i     — which Alt arm is active (1 = recursive, 2 = base)
 *   S_ζ       — child frame for the Ref('S') call in arm 1
 *   lita1_saved, litb1_saved — cursor saves for arm 1's Lit nodes
 *   lita2_saved, litb2_saved — cursor saves for arm 2's Lit nodes
 */
typedef struct _S {
    int   alt_i;
    S_t * S_ζ;           /* child frame for Ref('S') in arm 1 */
    int   lita1_saved;   /* Lit('a') in arm 1 */
    int   litb1_saved;   /* Lit('b') in arm 1 */
    int   lita2_saved;   /* Lit('a') in arm 2 */
    int   litb2_saved;   /* Lit('b') in arm 2 */
} S_t;

str_t S(S_t ** ζζ, int entry) {
    S_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(S_t)); goto S_α; }
    if (entry == β){                                        goto S_β; }

    /* ------------------------------------------------------------------ */
    /* ARM 1: Cat(Lit('a'), Cat(Ref('S'), Lit('b')))  — a S b             */
    /* ------------------------------------------------------------------ */
    str_t a1_lita;
    a1_lita_α:  if (Δ >= Ω || Σ[Δ] != 'a')               goto a1_lita_ω;
                ζ->lita1_saved = Δ;
                a1_lita = str(Σ+Δ, 1); Δ++;               goto a1_lita_γ;
    a1_lita_β:  Δ = ζ->lita1_saved;                       goto a1_lita_ω;

    str_t a1_Sr;
    a1_Sr_α:    a1_Sr = S(&ζ->S_ζ, α);                    goto a1_Sr_λ;
    a1_Sr_β:    a1_Sr = S(&ζ->S_ζ, β);                    goto a1_Sr_λ;
    a1_Sr_λ:    if (is_empty(a1_Sr))                       goto a1_Sr_ω;
                else                                       goto a1_Sr_γ;

    str_t a1_litb;
    a1_litb_α:  if (Δ >= Ω || Σ[Δ] != 'b')               goto a1_litb_ω;
                ζ->litb1_saved = Δ;
                a1_litb = str(Σ+Δ, 1); Δ++;               goto a1_litb_γ;
    a1_litb_β:  Δ = ζ->litb1_saved;                       goto a1_litb_ω;

    /* Cat(Ref('S'), Lit('b')) — inner cat of arm 1 */
    str_t a1_inner;
    a1_inner_α: a1_inner = str(Σ+Δ, 0);                   goto a1_Sr_α;
    a1_inner_β:                                            goto a1_litb_β;
    a1_Sr_γ:    a1_inner = cat(a1_inner, a1_Sr);           goto a1_litb_α;
    a1_Sr_ω:                                               goto a1_lita_β;
    a1_litb_γ:  a1_inner = cat(a1_inner, a1_litb);         goto a1_inner_γ;
    a1_litb_ω:                                             goto a1_Sr_β;

    /* Cat(Lit('a'), Cat(Ref('S'), Lit('b'))) — outer cat of arm 1 */
    str_t arm1;
    arm1_α:     arm1 = str(Σ+Δ, 0);                       goto a1_lita_α;
    arm1_β:                                                goto a1_inner_β;
    a1_lita_γ:  arm1 = cat(arm1, a1_lita);                 goto a1_inner_α;
    a1_lita_ω:                                             goto arm1_ω;
    a1_inner_γ: arm1 = cat(arm1, a1_inner);                goto arm1_γ;
    arm1_ω:                                                goto S_arm1_ω;

    /* ------------------------------------------------------------------ */
    /* ARM 2: Cat(Lit('a'), Lit('b'))  — base case                        */
    /* ------------------------------------------------------------------ */
    str_t a2_lita;
    a2_lita_α:  if (Δ >= Ω || Σ[Δ] != 'a')               goto a2_lita_ω;
                ζ->lita2_saved = Δ;
                a2_lita = str(Σ+Δ, 1); Δ++;               goto a2_lita_γ;
    a2_lita_β:  Δ = ζ->lita2_saved;                       goto a2_lita_ω;

    str_t a2_litb;
    a2_litb_α:  if (Δ >= Ω || Σ[Δ] != 'b')               goto a2_litb_ω;
                ζ->litb2_saved = Δ;
                a2_litb = str(Σ+Δ, 1); Δ++;               goto a2_litb_γ;
    a2_litb_β:  Δ = ζ->litb2_saved;                       goto a2_litb_ω;

    str_t arm2;
    arm2_α:     arm2 = str(Σ+Δ, 0);                       goto a2_lita_α;
    arm2_β:                                                goto a2_litb_β;
    a2_lita_γ:  arm2 = cat(arm2, a2_lita);                 goto a2_litb_α;
    a2_lita_ω:                                             goto arm2_ω;
    a2_litb_γ:  arm2 = cat(arm2, a2_litb);                 goto arm2_γ;
    a2_litb_ω:                                             goto a2_lita_β;
    arm2_ω:                                                goto S_ω;

    /* ------------------------------------------------------------------ */
    /* Alt dispatcher                                                      */
    /* ------------------------------------------------------------------ */
    str_t alt;
    S_α:        ζ->alt_i = 1;   ζ->S_ζ = 0;               goto arm1_α;
    S_β:        if (ζ->alt_i == 1)                         goto arm1_β;
                if (ζ->alt_i == 2)                         goto arm2_β;
                                                           goto S_ω;
    arm1_γ:     alt = arm1; ζ->alt_i = 1;                  goto S_γ;
    S_arm1_ω:   ζ->alt_i = 2;                              goto arm2_α;
    arm2_γ:     alt = arm2; ζ->alt_i = 2;                  goto S_γ;

    S_γ:        return alt;
    S_ω:        return empty;
}
/*============================================================================*/
static void match(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    S_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? S(&ez, α) : S(&ez, β);
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
    printf("=== {a^n b^n} oracle — first context-free (Type 2) proof (14 cases) ===\n");

    /* Positive */
    printf("[1]  \"ab\"         → "); match("ab");
    printf("[2]  \"aabb\"       → "); match("aabb");
    printf("[3]  \"aaabbb\"     → "); match("aaabbb");
    printf("[4]  \"aaaabbbb\"   → "); match("aaaabbbb");
    printf("[5]  \"aaaaabbbbb\" → "); match("aaaaabbbbb");

    /* Negative */
    printf("[6]  \"\"           → "); match("");
    printf("[7]  \"a\"          → "); match("a");
    printf("[8]  \"b\"          → "); match("b");
    printf("[9]  \"ba\"         → "); match("ba");
    printf("[10] \"aab\"        → "); match("aab");
    printf("[11] \"abb\"        → "); match("abb");
    printf("[12] \"aaabb\"      → "); match("aaabb");
    printf("[13] \"aabbb\"      → "); match("aabbb");
    printf("[14] \"abab\"       → "); match("abab");

    return 0;
}
#endif
