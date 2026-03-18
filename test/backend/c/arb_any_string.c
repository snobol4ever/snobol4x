/*
 * arb_any_string.c — Sprint 7 Oracle: ARB
 * =========================================
 * Automata Theory Oracle #3 — Regular language tier (Type 3).
 *
 * Language:  L = Σ*  (all strings over any alphabet)
 *            Every string is a member.  This is the trivially universal language.
 *
 * IR (what emit_c.py must generate from):
 *
 *   g = Graph()
 *   g.add('ROOT', Arb())
 *
 * Semantics:
 *   ARB matches zero or more characters at the current cursor position.
 *   On alpha: yields empty string (0 chars) immediately — try shortest first.
 *   On each beta: extends by one character.
 *   When cursor reaches subject_len, further beta fails.
 *
 *   Anchored with POS(0) ... RPOS(0) the harness drives ARB to find the
 *   unique match that consumes the entire subject.
 *
 * Why this proves ARB works:
 *   1. Empty subject "" — ARB yields "" on alpha, RPOS(0) satisfied immediately.
 *   2. Non-empty subject — ARB must be driven via beta until Delta == Omega.
 *   3. ARB never fails the language test — Σ* contains every string.
 *      The harness confirms this by checking no subject ever produces "Failure."
 *
 * Depth-indexed model:
 *   ARB maintains a depth counter.  depth starts at 0 (try 0 chars).
 *   On each beta, depth++, cursor is reset to start, advanced by depth.
 *   When depth > subject_len the cursor would overshoot — ARB fails.
 *
 * Oracle cases (8 total):
 *   All positive — Σ* has no non-members.
 *   1. ""        → ""        Success  (0 chars)
 *   2. "a"       → "a"       Success  (1 char, ARB driven once via beta)
 *   3. "ab"      → "ab"      Success  (2 chars)
 *   4. "abc"     → "abc"     Success  (3 chars)
 *   5. "hello"   → "hello"   Success  (5 chars)
 *   6. "x"       → "x"       Success  (single char)
 *   7. "aabb"    → "aabb"    Success  (4 chars, overlaps with a*b* oracle)
 *   8. "ba"      → "ba"      Success  (this was a non-member of a*b* — Σ* accepts it)
 *
 * The last two cases are deliberate: strings that were rejected by the a*b*
 * oracle are accepted here.  Σ* is strictly larger than a*b*.
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
/*
 * ARB — matches zero or more characters, shortest first.
 *
 * State:
 *   arb_start  — cursor position when ARB was entered (fixed for this invocation)
 *   arb_depth  — number of characters matched so far (0 on first try)
 *
 * Alpha: record start, set depth=0, advance cursor by 0, succeed.
 * Beta:  reset cursor to start, depth++, if depth > Ω-start fail, else advance and succeed.
 */
typedef struct {
    int arb_start;
    int arb_depth;
} ROOT_t;

str_t ROOT(ROOT_t ** ζζ, int entry) {
    ROOT_t * ζ = *ζζ;
    if (entry == α) {
        ζ = enter((void **) ζζ, sizeof(ROOT_t));
        ζ->arb_start = Δ;
        ζ->arb_depth = 0;
        /* yield 0 characters — try shortest first */
        goto ROOT_γ;
    }
    /* beta: extend by one more character */
    ζ->arb_depth++;
    if (ζ->arb_start + ζ->arb_depth > Ω) goto ROOT_ω;
    Δ = ζ->arb_start + ζ->arb_depth;
    goto ROOT_γ;

    ROOT_γ: return str(Σ + ζ->arb_start, ζ->arb_depth);
    ROOT_ω: return empty;
}
/*============================================================================*/
/*
 * Harness: POS(0) ARB $ OUTPUT RPOS(0)
 *
 * Drives ROOT via alpha/beta until Delta == Omega (full subject consumed).
 * For Σ* this always succeeds — the harness confirms no subject produces Failure.
 */
static void match_arb(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    ROOT_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? ROOT(&ez, α) : ROOT(&ez, β);
        first = 0;
        if (is_empty(r)) { write_sz(out, "Failure."); write_nl(out); return; }
        if (Δ == Ω) {
            /* print matched string in quotes for clarity, then Success */
            printf("\"");
            write_str(out, r);
            printf("\"");
            write_nl(out);
            write_sz(out, "Success!"); write_nl(out);
            return;
        }
    }
}
/*============================================================================*/
#ifdef __GNUC__
int main() {
    printf("=== ARB oracle — Sigma* universal language (8 cases) ===\n");

    /* All positive — Sigma* accepts everything */
    printf("[1] \"\"      → "); match_arb("");       /* \"\" Success  */
    printf("[2] \"a\"     → "); match_arb("a");      /* \"a\" Success */
    printf("[3] \"ab\"    → "); match_arb("ab");     /* \"ab\" Success */
    printf("[4] \"abc\"   → "); match_arb("abc");    /* \"abc\" Success */
    printf("[5] \"hello\" → "); match_arb("hello");  /* \"hello\" Success */
    printf("[6] \"x\"     → "); match_arb("x");      /* \"x\" Success */

    /* These were non-members of a*b* — Sigma* takes them anyway */
    printf("[7] \"aabb\"  → "); match_arb("aabb");   /* \"aabb\" Success */
    printf("[8] \"ba\"    → "); match_arb("ba");     /* \"ba\" Success */

    return 0;
}
#endif
