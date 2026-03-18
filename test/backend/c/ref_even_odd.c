/*
 * Sprint 6 Oracle: Ref / Mutual Recursion
 * =========================================
 * IR (what emit_c.py must generate from):
 *
 *   g = Graph()
 *   g.add('EVEN', Alt(Lit(''), Cat(Lit('x'), Ref('ODD'))))
 *   g.add('ODD',  Cat(Lit('x'), Ref('EVEN')))
 *
 * Semantics:
 *   EVEN matches strings with an even number of 'x's: "", "xx", "xxxx", ...
 *   ODD  matches strings with an odd  number of 'x's: "x", "xxx", "xxxxx", ...
 *
 * Key insight for Ref code-gen:
 *   - Each named pattern becomes a C function:  str_t NAME(NAME_t**, int)
 *   - Forward declarations are required because the graph has cycles
 *   - Each call site allocs a child context pointer inside parent's struct
 *   - Entry convention: alpha=0 (first call), beta=1 (backtrack)
 *   - Return convention: empty  → match failed; non-empty → matched str_t
 *
 * Oracle cases (7 total):
 *   1. EVEN("xxxx") → "xxxx\nSuccess!\n"   ✓
 *   2. EVEN("")     → "\nSuccess!\n"        ✓  (empty string is even)
 *   3. EVEN("xx")   → "xx\nSuccess!\n"     ✓
 *   4. EVEN("xxx")  → "Failure.\n"         ✓
 *   5. ODD("x")     → "x\nSuccess!\n"      ✓
 *   6. ODD("xxx")   → "xxx\nSuccess!\n"    ✓
 *   7. ODD("xx")    → "Failure.\n"         ✓
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
/* FORWARD DECLARATIONS — required because Ref creates a cycle in the graph. */
/* emit_c.py must topologically order the forward decls before any function  */
/* body, or emit them all as a block at the top of the output file.          */
typedef struct _EVEN EVEN_t;
typedef struct _ODD  ODD_t;
str_t EVEN(EVEN_t **, int);
str_t ODD(ODD_t  **, int);
/*============================================================================*/
/*
 * EVEN = Alt(Lit(''), Cat(Lit('x'), Ref('ODD')))
 *
 *  State stored per invocation frame (EVEN_t):
 *    alt_i  — which Alt arm is active (1=epsilon, 2=seq(x, ODD))
 *    ODD_ζ  — child context pointer for the Ref('ODD') call site
 */
typedef struct _EVEN {
    int     alt_i;      /* 1 = epsilon arm; 2 = seq(Lit('x'), Ref('ODD')) */
    ODD_t * ODD_ζ;      /* child frame for Ref('ODD')                      */
} EVEN_t;

str_t EVEN(EVEN_t ** ζζ, int entry) {
    EVEN_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(EVEN_t));  goto EVEN_α; }
    if (entry == β){                                            goto EVEN_β; }
    /*--- Lit('') -----------------------------------------------------------*/
    str_t       eps1;
    eps1_α:     eps1 = str(Σ+Δ, 0);                            goto eps1_γ;
    eps1_β:                                                     goto eps1_ω;
    /*--- Lit('x') (first element of seq arm) --------------------------------*/
    str_t       litx2;
    litx2_α:    if (Σ[Δ] != 'x')                               goto litx2_ω;
                litx2 = str(Σ+Δ, 1); Δ++;                      goto litx2_γ;
    litx2_β:    Δ--;                                            goto litx2_ω;
    /*--- Ref('ODD') ---------------------------------------------------------*/
    str_t       ODD3;
    ODD3_α:     ODD3 = ODD(&ζ->ODD_ζ, α);                      goto ODD3_λ;
    ODD3_β:     ODD3 = ODD(&ζ->ODD_ζ, β);                      goto ODD3_λ;
    ODD3_λ:     if (is_empty(ODD3))                             goto ODD3_ω;
                else                                            goto ODD3_γ;
    /*--- Cat(Lit('x'), Ref('ODD')) -----------------------------------------*/
    str_t       seq2;
    seq2_α:     seq2 = str(Σ+Δ, 0);                            goto litx2_α;
    seq2_β:                                                     goto ODD3_β;
    litx2_γ:    seq2 = cat(seq2, litx2);                        goto ODD3_α;
    litx2_ω:                                                    goto seq2_ω;
    ODD3_γ:     seq2 = cat(seq2, ODD3);                         goto seq2_γ;
    ODD3_ω:                                                     goto litx2_β;
    /*--- Alt dispatcher ---------------------------------------------------- */
    str_t       alt;
    EVEN_α:     ζ->alt_i = 1;                                   goto eps1_α;
    EVEN_β:     if (ζ->alt_i == 1)                              goto eps1_β;
                if (ζ->alt_i == 2)                              goto seq2_β;
                                                                goto EVEN_ω;
    eps1_γ:     alt = eps1;  ζ->alt_i = 1;                      goto EVEN_γ;
    eps1_ω:     ζ->alt_i++;                                     goto seq2_α;
    seq2_γ:     alt = seq2;  ζ->alt_i = 2;                      goto EVEN_γ;
    seq2_ω:                                                     goto EVEN_ω;
    /*------------------------------------------------------------------------*/
    EVEN_γ:     return alt;
    EVEN_ω:     return empty;
}
/*============================================================================*/
/*
 * ODD = Cat(Lit('x'), Ref('EVEN'))
 *
 *  State stored per invocation frame (ODD_t):
 *    EVEN_ζ  — child context pointer for the Ref('EVEN') call site
 */
typedef struct _ODD {
    EVEN_t * EVEN_ζ;    /* child frame for Ref('EVEN') */
} ODD_t;

str_t ODD(ODD_t ** ζζ, int entry) {
    ODD_t * ζ = *ζζ;
    if (entry == α){ ζ = enter((void **) ζζ, sizeof(ODD_t));   goto ODD_α; }
    if (entry == β){                                            goto ODD_β; }
    /*--- Lit('x') -----------------------------------------------------------*/
    str_t       litx1;
    litx1_α:    if (Σ[Δ] != 'x')                               goto litx1_ω;
                litx1 = str(Σ+Δ, 1); Δ++;                      goto litx1_γ;
    litx1_β:    Δ--;                                            goto litx1_ω;
    /*--- Ref('EVEN') --------------------------------------------------------*/
    str_t       EVEN2;
    EVEN2_α:    EVEN2 = EVEN(&ζ->EVEN_ζ, α);                   goto EVEN2_λ;
    EVEN2_β:    EVEN2 = EVEN(&ζ->EVEN_ζ, β);                   goto EVEN2_λ;
    EVEN2_λ:    if (is_empty(EVEN2))                            goto EVEN2_ω;
                else                                            goto EVEN2_γ;
    /*--- Cat(Lit('x'), Ref('EVEN')) ----------------------------------------*/
    str_t       seq;
    ODD_α:      seq = str(Σ+Δ, 0);                             goto litx1_α;
    ODD_β:                                                      goto EVEN2_β;
    litx1_γ:    seq = cat(seq, litx1);                          goto EVEN2_α;
    litx1_ω:                                                    goto ODD_ω;
    EVEN2_γ:    seq = cat(seq, EVEN2);                          goto ODD_γ;
    EVEN2_ω:                                                    goto litx1_β;
    /*------------------------------------------------------------------------*/
    ODD_γ:      return seq;
    ODD_ω:      return empty;
}
/*============================================================================*/
/*
 * Harness: POS(0) <pat> $ OUTPUT RPOS(0)
 *
 * The engine calls <pat>(α), then checks RPOS(0) (Δ==Ω).
 * If RPOS fails, it calls <pat>(β) to backtrack and try the next alternative.
 * When RPOS succeeds, it writes the matched string and "Success!".
 * If <pat> returns empty, it writes "Failure.".
 *
 * This is the correct wiring for the Ref sprint: the $ OUTPUT fires
 * exactly once, on the unique match that also satisfies RPOS(0).
 */
static void match_even(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    EVEN_t * ez = 0;
    int first = 1;
    while (1) {
        str_t r = first ? EVEN(&ez, α) : EVEN(&ez, β);
        first = 0;
        if (is_empty(r)) { write_sz(out, "Failure."); write_nl(out); return; }
        if (Δ == Ω) {
            write_str(out, r); write_nl(out);
            write_sz(out, "Success!"); write_nl(out);
            return;
        }
    }
}

static void match_odd(const char * input) {
    Σ = input; Ω = len(Σ); Δ = 0;
    output_t tmp = {0, 0}; out = &tmp;
    ODD_t * oz = 0;
    int first = 1;
    while (1) {
        str_t r = first ? ODD(&oz, α) : ODD(&oz, β);
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
    /* 7 oracle cases */
    printf("=== ref_even_odd oracle (7 cases) ===\n");

    printf("[1] EVEN(\"xxxx\") → ");
    match_even("xxxx");     /* expect: xxxx\nSuccess! */

    printf("[2] EVEN(\"\")     → ");
    match_even("");          /* expect: \nSuccess!   */

    printf("[3] EVEN(\"xx\")   → ");
    match_even("xx");        /* expect: xx\nSuccess! */

    printf("[4] EVEN(\"xxx\")  → ");
    match_even("xxx");       /* expect: Failure.     */

    printf("[5] ODD(\"x\")     → ");
    match_odd("x");          /* expect: x\nSuccess!  */

    printf("[6] ODD(\"xxx\")   → ");
    match_odd("xxx");        /* expect: xxx\nSuccess! */

    printf("[7] ODD(\"xx\")    → ");
    match_odd("xx");         /* expect: Failure.     */

    return 0;
}
#endif
