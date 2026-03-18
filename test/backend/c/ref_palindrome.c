/*
 * ref_palindrome.c — Sprint 10 Oracle: {ww^R | w ∈ {a,b}*}
 * ==========================================================
 * Automata Theory Oracle #6 — Context-Free language tier (Type 2).
 *
 * Language:  L = { ww^R | w ∈ {a,b}+ }
 *            All even-length palindromes over {a,b}.
 *            A string x is in L iff x reads the same forwards and backwards
 *            and has even length: "aa", "bb", "abba", "baab", "aabbaa", ...
 *
 * Note: we use even-length palindromes (ww^R) rather than odd-length (wcw^R)
 * because it has the cleaner CFG and is the purer test of the mechanism.
 * The grammar: P → a P a | b P b | a a | b b
 *
 * As a SNOBOL4 recursive REF pattern:
 *
 *   IR:
 *     g.add('P', Alt(
 *                  Alt(
 *                    Cat(Lit('a'), Cat(Ref('P'), Lit('a'))),  -- a P a
 *                    Cat(Lit('b'), Cat(Ref('P'), Lit('b')))   -- b P b
 *                  ),
 *                  Alt(
 *                    Cat(Lit('a'), Lit('a')),                 -- aa (base)
 *                    Cat(Lit('b'), Lit('b'))                  -- bb (base)
 *                  )
 *                ))
 *
 * What this oracle proves:
 *   1. The engine correctly recognizes palindromes — a second Type 2 language,
 *      distinct from {a^n b^n}. Palindromes require matching characters on
 *      BOTH sides of the recursion simultaneously: 'a' consumed entering,
 *      'a' consumed exiting. The call stack holds the left half; the right
 *      half is verified on the way back out.
 *   2. Four-way Alt over recursive and base cases — the deepest Alt structure
 *      in the oracle suite so far. Tests Alt backtracking across all four arms.
 *   3. Depth of recursion = half the string length. "aabbaa" requires depth 3.
 *   4. Pumping lemma: any palindrome "a^n b^2 a^n" can be pumped on the 'a'
 *      prefix while preserving palindrome structure. Boundary: "aa" (shortest),
 *      "abba", "aabbaa".
 *
 * Oracle cases (16 total):
 *   Positive:
 *     1.  "aa"       → Success  (base, a)
 *     2.  "bb"       → Success  (base, b)
 *     3.  "abba"     → Success  (a P a, P=bb)
 *     4.  "baab"     → Success  (b P b, P=aa)
 *     5.  "aabbaa"   → Success  (a P a, P=abba... wait, depth 3: a(a(bb)a)a)
 *     6.  "bbaarbb"  skipped — staying pure {a,b}
 *     6.  "aaaa"     → Success  (a P a, P=aa)
 *     7.  "bbbb"     → Success  (b P b, P=bb)
 *     8.  "aabbbbaa" → Success  (a(a(bb)a... a P a, P=abbba? no: a(abbba)a
 *                                actually: a(a(bb)a)a = "aabbaa" depth 3
 *                                "aabbbbaa": a(abbbbа)a, P=abbbba? not palindrome
 *                                Let's use: "abbaabba" → Success
 *     8.  "abbaabba" → Success
 *   Negative:
 *     9.  ""         → Failure  (empty)
 *     10. "a"        → Failure  (odd length / single char)
 *     11. "ab"       → Failure  (not palindrome)
 *     12. "ba"       → Failure  (not palindrome)
 *     13. "aab"      → Failure  (odd, not palindrome)
 *     14. "abab"     → Failure  (not palindrome)
 *     15. "abbb"     → Failure  (not palindrome)
 *     16. "aabba"    → Failure  (odd length)
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
typedef struct _P P_t;
str_t P(P_t **, int);
/*============================================================================*/
/*
 * P = Alt(Alt(Cat(Lit('a'),Cat(Ref('P'),Lit('a'))),   -- arm 1: a P a
 *             Cat(Lit('b'),Cat(Ref('P'),Lit('b')))),   -- arm 2: b P b
 *         Alt(Cat(Lit('a'),Lit('a')),                  -- arm 3: aa
 *             Cat(Lit('b'),Lit('b'))))                 -- arm 4: bb
 *
 * Tries recursive arms first (greedy), then base cases.
 * On the way IN:  consume one char (a or b)
 * Recurse:        match the inner palindrome
 * On the way OUT: consume the matching char
 *
 * The call stack holds the left-side characters — exactly a pushdown automaton.
 */
typedef struct _P {
    int   alt_i;         /* 1=aPa, 2=bPb, 3=aa, 4=bb */
    P_t * P_ζ;           /* child frame for recursive Ref('P') */
    int   lita1_s;       /* Lit('a') entering arm 1 */
    int   litaR1_s;      /* Lit('a') exiting  arm 1 */
    int   litb2_s;       /* Lit('b') entering arm 2 */
    int   litbR2_s;      /* Lit('b') exiting  arm 2 */
    int   lita3_s;       /* Lit('a') #1 arm 3 */
    int   litaR3_s;      /* Lit('a') #2 arm 3 */
    int   litb4_s;       /* Lit('b') #1 arm 4 */
    int   litbR4_s;      /* Lit('b') #2 arm 4 */
} P_t;

str_t P(P_t ** ζζ, int entry) {
    P_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(P_t)); goto P_α; }
    if (entry == β){                                        goto P_β; }

    /* ------------------------------------------------------------------ */
    /* ARM 1: a P a                                                        */
    /* ------------------------------------------------------------------ */
    str_t a1_la;
    a1_la_α:  if (Δ>=Ω||Σ[Δ]!='a') goto a1_la_ω;
              ζ->lita1_s=Δ; a1_la=str(Σ+Δ,1); Δ++; goto a1_la_γ;
    a1_la_β:  Δ=ζ->lita1_s; goto a1_la_ω;

    str_t a1_Pr;
    a1_Pr_α:  a1_Pr=P(&ζ->P_ζ,α); goto a1_Pr_λ;
    a1_Pr_β:  a1_Pr=P(&ζ->P_ζ,β); goto a1_Pr_λ;
    a1_Pr_λ:  if(is_empty(a1_Pr)) goto a1_Pr_ω; else goto a1_Pr_γ;

    str_t a1_ra;
    a1_ra_α:  if (Δ>=Ω||Σ[Δ]!='a') goto a1_ra_ω;
              ζ->litaR1_s=Δ; a1_ra=str(Σ+Δ,1); Δ++; goto a1_ra_γ;
    a1_ra_β:  Δ=ζ->litaR1_s; goto a1_ra_ω;

    str_t arm1; str_t a1_mid;
    arm1_α:   arm1=str(Σ+Δ,0); goto a1_la_α;
    arm1_β:   goto a1_mid_β;
    a1_la_γ:  arm1=cat(arm1,a1_la); goto a1_mid_α;
    a1_la_ω:  goto arm1_ω;
    a1_mid_α: a1_mid=str(Σ+Δ,0); goto a1_Pr_α;
    a1_mid_β: goto a1_ra_β;
    a1_Pr_γ:  a1_mid=cat(a1_mid,a1_Pr); goto a1_ra_α;
    a1_Pr_ω:  goto a1_la_β;
    a1_ra_γ:  a1_mid=cat(a1_mid,a1_ra); goto a1_mid_γ;
    a1_ra_ω:  goto a1_Pr_β;
    a1_mid_γ: arm1=cat(arm1,a1_mid); goto arm1_γ;
    arm1_ω:   goto P_arm1_ω;

    /* ------------------------------------------------------------------ */
    /* ARM 2: b P b                                                        */
    /* ------------------------------------------------------------------ */
    str_t a2_lb;
    a2_lb_α:  if (Δ>=Ω||Σ[Δ]!='b') goto a2_lb_ω;
              ζ->litb2_s=Δ; a2_lb=str(Σ+Δ,1); Δ++; goto a2_lb_γ;
    a2_lb_β:  Δ=ζ->litb2_s; goto a2_lb_ω;

    str_t a2_Pr;
    a2_Pr_α:  a2_Pr=P(&ζ->P_ζ,α); goto a2_Pr_λ;
    a2_Pr_β:  a2_Pr=P(&ζ->P_ζ,β); goto a2_Pr_λ;
    a2_Pr_λ:  if(is_empty(a2_Pr)) goto a2_Pr_ω; else goto a2_Pr_γ;

    str_t a2_rb;
    a2_rb_α:  if (Δ>=Ω||Σ[Δ]!='b') goto a2_rb_ω;
              ζ->litbR2_s=Δ; a2_rb=str(Σ+Δ,1); Δ++; goto a2_rb_γ;
    a2_rb_β:  Δ=ζ->litbR2_s; goto a2_rb_ω;

    str_t arm2; str_t a2_mid;
    arm2_α:   arm2=str(Σ+Δ,0); goto a2_lb_α;
    arm2_β:   goto a2_mid_β;
    a2_lb_γ:  arm2=cat(arm2,a2_lb); goto a2_mid_α;
    a2_lb_ω:  goto arm2_ω;
    a2_mid_α: a2_mid=str(Σ+Δ,0); goto a2_Pr_α;
    a2_mid_β: goto a2_rb_β;
    a2_Pr_γ:  a2_mid=cat(a2_mid,a2_Pr); goto a2_rb_α;
    a2_Pr_ω:  goto a2_lb_β;
    a2_rb_γ:  a2_mid=cat(a2_mid,a2_rb); goto a2_mid_γ;
    a2_rb_ω:  goto a2_Pr_β;
    a2_mid_γ: arm2=cat(arm2,a2_mid); goto arm2_γ;
    arm2_ω:   goto P_arm2_ω;

    /* ------------------------------------------------------------------ */
    /* ARM 3: aa (base)                                                    */
    /* ------------------------------------------------------------------ */
    str_t a3_la, a3_ra; str_t arm3;
    arm3_α:   arm3=str(Σ+Δ,0); goto a3_la_α;
    arm3_β:   goto a3_ra_β;
    a3_la_α:  if (Δ>=Ω||Σ[Δ]!='a') goto a3_la_ω;
              ζ->lita3_s=Δ; a3_la=str(Σ+Δ,1); Δ++; goto a3_la_γ;
    a3_la_β:  Δ=ζ->lita3_s; goto a3_la_ω;
    a3_la_γ:  arm3=cat(arm3,a3_la); goto a3_ra_α;
    a3_la_ω:  goto arm3_ω;
    a3_ra_α:  if (Δ>=Ω||Σ[Δ]!='a') goto a3_ra_ω;
              ζ->litaR3_s=Δ; a3_ra=str(Σ+Δ,1); Δ++; goto a3_ra_γ;
    a3_ra_β:  Δ=ζ->litaR3_s; goto a3_ra_ω;
    a3_ra_γ:  arm3=cat(arm3,a3_ra); goto arm3_γ;
    a3_ra_ω:  goto a3_la_β;
    arm3_ω:   goto P_arm3_ω;

    /* ------------------------------------------------------------------ */
    /* ARM 4: bb (base)                                                    */
    /* ------------------------------------------------------------------ */
    str_t a4_lb, a4_rb; str_t arm4;
    arm4_α:   arm4=str(Σ+Δ,0); goto a4_lb_α;
    arm4_β:   goto a4_rb_β;
    a4_lb_α:  if (Δ>=Ω||Σ[Δ]!='b') goto a4_lb_ω;
              ζ->litb4_s=Δ; a4_lb=str(Σ+Δ,1); Δ++; goto a4_lb_γ;
    a4_lb_β:  Δ=ζ->litb4_s; goto a4_lb_ω;
    a4_lb_γ:  arm4=cat(arm4,a4_lb); goto a4_rb_α;
    a4_lb_ω:  goto arm4_ω;
    a4_rb_α:  if (Δ>=Ω||Σ[Δ]!='b') goto a4_rb_ω;
              ζ->litbR4_s=Δ; a4_rb=str(Σ+Δ,1); Δ++; goto a4_rb_γ;
    a4_rb_β:  Δ=ζ->litbR4_s; goto a4_rb_ω;
    a4_rb_γ:  arm4=cat(arm4,a4_rb); goto arm4_γ;
    a4_rb_ω:  goto a4_lb_β;
    arm4_ω:   goto P_ω;

    /* ------------------------------------------------------------------ */
    /* Alt dispatcher                                                      */
    /* ------------------------------------------------------------------ */
    str_t alt;
    P_α:        ζ->alt_i=1; ζ->P_ζ=0;  goto arm1_α;
    P_β:        if(ζ->alt_i==1) goto arm1_β;
                if(ζ->alt_i==2) goto arm2_β;
                if(ζ->alt_i==3) goto arm3_β;
                if(ζ->alt_i==4) goto arm4_β;
                goto P_ω;
    arm1_γ:     alt=arm1; ζ->alt_i=1;  goto P_γ;
    P_arm1_ω:   ζ->alt_i=2;            goto arm2_α;
    arm2_γ:     alt=arm2; ζ->alt_i=2;  goto P_γ;
    P_arm2_ω:   ζ->alt_i=3;            goto arm3_α;
    arm3_γ:     alt=arm3; ζ->alt_i=3;  goto P_γ;
    P_arm3_ω:   ζ->alt_i=4;            goto arm4_α;
    arm4_γ:     alt=arm4; ζ->alt_i=4;  goto P_γ;

    P_γ:  return alt;
    P_ω:  return empty;
}
/*============================================================================*/
static void match(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0,0}; out = &tmp;
    P_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? P(&ez,α) : P(&ez,β);
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
    printf("=== {ww^R} palindrome oracle — context-free (Type 2) (16 cases) ===\n");

    /* Positive */
    printf("[1]  \"aa\"       → "); match("aa");
    printf("[2]  \"bb\"       → "); match("bb");
    printf("[3]  \"abba\"     → "); match("abba");
    printf("[4]  \"baab\"     → "); match("baab");
    printf("[5]  \"aabbaa\"   → "); match("aabbaa");
    printf("[6]  \"aaaa\"     → "); match("aaaa");
    printf("[7]  \"bbbb\"     → "); match("bbbb");
    printf("[8]  \"abbaabba\" → "); match("abbaabba");

    /* Negative */
    printf("[9]  \"\"         → "); match("");
    printf("[10] \"a\"        → "); match("a");
    printf("[11] \"ab\"       → "); match("ab");
    printf("[12] \"ba\"       → "); match("ba");
    printf("[13] \"aab\"      → "); match("aab");
    printf("[14] \"abab\"     → "); match("abab");
    printf("[15] \"abbb\"     → "); match("abbb");
    printf("[16] \"aabba\"    → "); match("aabba");

    return 0;
}
#endif
