/*
 * bb_tab_fence_test.c — Unit tests for TAB, FENCE, ABORT (M-DYN-5)
 *
 * Build:
 *   gcc -Wall -g -I src/runtime/dyn \
 *       src/runtime/dyn/bb_lit.c src/runtime/dyn/bb_alt.c \
 *       src/runtime/dyn/bb_seq.c src/runtime/dyn/bb_arbno.c \
 *       src/runtime/dyn/bb_pos.c src/runtime/dyn/bb_tab.c \
 *       src/runtime/dyn/bb_fence.c \
 *       src/runtime/dyn/bb_tab_fence_test.c -o bb_tab_fence_test
 */

#include "bb_box.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── global match state ─────────────────────────────────────────────────── */
const char *Σ;
int         Δ;
int         Ω;

/* ── test helpers ───────────────────────────────────────────────────────── */
static int failures = 0;
#define PASS(msg)  do { printf("  PASS  %s\n", msg); } while(0)
#define FAIL(msg)  do { printf("  FAIL  %s\n", msg); failures++; } while(0)

static void set_subject(const char *s) {
    Σ = s; Ω = (int)strlen(s); Δ = 0;
}

/* ── TAB box constructors (mirror tab_t from bb_tab.c) ───────────────────── */
typedef struct { int n; int advance; } tab_t;
typedef struct { int n; int advance; } rtab_t;
typedef struct { int fired; }          fence_t;
typedef struct { int dummy; }          abort_t;
typedef struct { int n; }              pos_t;

extern spec_t bb_tab   (void **ζζ, int entry);
extern spec_t bb_rtab  (void **ζζ, int entry);
extern spec_t bb_fence (void **ζζ, int entry);
extern spec_t bb_abort (void **ζζ, int entry);
extern spec_t bb_pos   (void **ζζ, int entry);

/* ── TAB tests ───────────────────────────────────────────────────────────── */
static void test_tab(void)
{
    printf("\nTAB tests\n");

    /* T1: TAB(5) with Δ=0 in "HelloWorld" — should advance to 5 */
    set_subject("HelloWorld");
    tab_t *ζ1 = calloc(1, sizeof(tab_t)); ζ1->n = 5;
    void *z1 = ζ1;
    spec_t r = bb_tab((void **)&z1, α);
    if (!spec_is_empty(r) && Δ == 5 && r.δ == 5) PASS("T1: TAB(5) from 0 advances to 5");
    else FAIL("T1: TAB(5) from 0");

    /* T2: TAB(5) with Δ already at 5 — should succeed (zero advance) */
    set_subject("HelloWorld"); Δ = 5;
    tab_t *ζ2 = calloc(1, sizeof(tab_t)); ζ2->n = 5;
    void *z2 = ζ2;
    r = bb_tab((void **)&z2, α);
    if (!spec_is_empty(r) && Δ == 5 && r.δ == 0) PASS("T2: TAB(5) at 5 is zero-advance success");
    else FAIL("T2: TAB(5) at 5");

    /* T3: TAB(3) with Δ=7 — past position, should ω */
    set_subject("HelloWorld"); Δ = 7;
    tab_t *ζ3 = calloc(1, sizeof(tab_t)); ζ3->n = 3;
    void *z3 = ζ3;
    r = bb_tab((void **)&z3, α);
    if (spec_is_empty(r)) PASS("T3: TAB(3) with Δ=7 fails (past target)");
    else FAIL("T3: TAB(3) with Δ=7 should fail");

    /* T4: TAB β restores cursor */
    set_subject("HelloWorld"); Δ = 0;
    tab_t *ζ4 = calloc(1, sizeof(tab_t)); ζ4->n = 4;
    void *z4 = ζ4;
    bb_tab((void **)&z4, α);          /* α: Δ becomes 4, advance=4 */
    int delta_after_alpha = Δ;
    r = bb_tab((void **)&z4, β);      /* β: Δ restored to 0, ω */
    if (spec_is_empty(r) && Δ == 0 && delta_after_alpha == 4)
        PASS("T4: TAB β restores cursor and ω's");
    else FAIL("T4: TAB β restore");

    /* T5: TAB differs from POS — POS(5) with Δ=0 fails; TAB(5) succeeds */
    set_subject("HelloWorld"); Δ = 0;
    pos_t *ζp = calloc(1, sizeof(pos_t)); ζp->n = 5;
    void *zp = ζp;
    r = bb_pos((void **)&zp, α);
    if (spec_is_empty(r)) PASS("T5a: POS(5) with Δ=0 fails (TAB≠POS confirmed)");
    else FAIL("T5a: POS(5) should fail with Δ=0");

    set_subject("HelloWorld"); Δ = 0;
    tab_t *ζt = calloc(1, sizeof(tab_t)); ζt->n = 5;
    void *zt = ζt;
    r = bb_tab((void **)&zt, α);
    if (!spec_is_empty(r)) PASS("T5b: TAB(5) with Δ=0 succeeds (TAB≠POS confirmed)");
    else FAIL("T5b: TAB(5) should succeed with Δ=0");
}

/* ── RTAB tests ──────────────────────────────────────────────────────────── */
static void test_rtab(void)
{
    printf("\nRTAB tests\n");

    /* T6: RTAB(3) on "HelloWorld" (Ω=10): target = 7, Δ=0 → advance to 7 */
    set_subject("HelloWorld");
    rtab_t *ζ6 = calloc(1, sizeof(rtab_t)); ζ6->n = 3;
    void *z6 = ζ6;
    spec_t r = bb_rtab((void **)&z6, α);
    if (!spec_is_empty(r) && Δ == 7 && r.δ == 7) PASS("T6: RTAB(3) advances to Ω-3=7");
    else FAIL("T6: RTAB(3)");

    /* T7: RTAB(3) with Δ=8 — past (Ω-3)=7, should ω */
    set_subject("HelloWorld"); Δ = 8;
    rtab_t *ζ7 = calloc(1, sizeof(rtab_t)); ζ7->n = 3;
    void *z7 = ζ7;
    r = bb_rtab((void **)&z7, α);
    if (spec_is_empty(r)) PASS("T7: RTAB(3) with Δ=8 fails (past target)");
    else FAIL("T7: RTAB(3) past target should fail");
}

/* ── FENCE tests ─────────────────────────────────────────────────────────── */
static void test_fence(void)
{
    printf("\nFENCE tests\n");

    /* T8: FENCE α succeeds with epsilon match */
    set_subject("Hello"); Δ = 2;
    fence_t *ζ8 = calloc(1, sizeof(fence_t));
    void *z8 = ζ8;
    spec_t r = bb_fence((void **)&z8, α);
    if (!spec_is_empty(r) && r.δ == 0 && Δ == 2)
        PASS("T8: FENCE α succeeds with epsilon, cursor unchanged");
    else FAIL("T8: FENCE α");

    /* T9: FENCE β always ω — the cut is in effect */
    r = bb_fence((void **)&z8, β);
    if (spec_is_empty(r)) PASS("T9: FENCE β always ω (cut)");
    else FAIL("T9: FENCE β should ω");

    /* T10: second FENCE β also ω */
    r = bb_fence((void **)&z8, β);
    if (spec_is_empty(r)) PASS("T10: FENCE β repeated still ω");
    else FAIL("T10: FENCE β repeated");
}

/* ── ABORT tests ─────────────────────────────────────────────────────────── */
static void test_abort(void)
{
    printf("\nABORT tests\n");

    /* T11: ABORT α immediately ω */
    set_subject("Hello"); Δ = 0;
    abort_t *ζ11 = calloc(1, sizeof(abort_t));
    void *z11 = ζ11;
    spec_t r = bb_abort((void **)&z11, α);
    if (spec_is_empty(r)) PASS("T11: ABORT α immediately ω");
    else FAIL("T11: ABORT α should ω");

    /* T12: ABORT β also ω */
    r = bb_abort((void **)&z11, β);
    if (spec_is_empty(r)) PASS("T12: ABORT β also ω");
    else FAIL("T12: ABORT β should ω");
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("bb_tab_fence_test — M-DYN-5 TAB/FENCE/ABORT unit tests\n");

    test_tab();
    test_rtab();
    test_fence();
    test_abort();

    printf("\n%s  (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
                                                              return failures ? 1 : 0;
}
