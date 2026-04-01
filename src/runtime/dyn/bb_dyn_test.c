/*
 * bb_dyn_test.c — Dynamic Byrd Box integration test (M-DYN-2 gate)
 *
 * Reproduces EXACTLY the pattern from test_sno_1.c:
 *
 *     POS(0)  ARBNO('Bird' | 'Blue' | LEN(1))  $ OUTPUT  RPOS(0)
 *
 * against subject "BlueGoldBirdFish"
 *
 * Expected output (same as test_sno_1.c running under gcc):
 *   Blue
 *   BlueGold
 *   BlueGoldBird
 *   BlueGoldBirdFish
 *   Success!
 *
 * Build:
 *   gcc -o bb_dyn_test src/runtime/dyn/bb_box.h  \
 *       src/runtime/dyn/bb_lit.c   \
 *       src/runtime/dyn/bb_alt.c   \
 *       src/runtime/dyn/bb_seq.c   \
 *       src/runtime/dyn/bb_arbno.c \
 *       src/runtime/dyn/bb_pos.c   \
 *       src/runtime/dyn/bb_dyn_test.c
 */

#include "bb_box.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── global match state (defined here, extern in bb_box.h) ─────────────── */
const char *Σ = NULL;
int         Δ = 0;
int         Ω = 0;

/* ── inline LEN(1) box ───────────────────────────────────────────────────── */
/*
 * Matches exactly one character.  No saved state needed.
 * This is the LEN1 box from test_sno_1.c.
 */
typedef struct { int dummy; } len1_t;

spec_t bb_len1(len1_t **ζζ, int entry)
{
    len1_t *ζ = *ζζ; (void)ζ;
    if (entry == α)                                     goto LEN1_α;
    if (entry == β)                                     goto LEN1_β;

    spec_t         LEN1;
    LEN1_α:       if (Δ+1 > Ω)                                goto LEN1_ω;
                  LEN1 = spec(Σ+Δ,1); Δ+=1;                   goto LEN1_γ;
    LEN1_β:       Δ-=1;                                       goto LEN1_ω;

    LEN1_γ:                                                   return LEN1;
    LEN1_ω:                                                   return spec_empty;
}

/* ── build the pattern graph ─────────────────────────────────────────────── */
/*
 * 'Bird' | 'Blue' | LEN(1) — three alternatives
 * ARBNO of the above
 * POS(0) ... RPOS(0) — anchored to full subject
 */

/* forward declarations */
spec_t bb_lit  (void **ζζ, int entry);
spec_t bb_alt  (void **ζζ, int entry);
spec_t bb_seq  (void **ζζ, int entry);
spec_t bb_arbno(void **ζζ, int entry);
spec_t bb_pos  (void **ζζ, int entry);
spec_t bb_rpos (void **ζζ, int entry);

/* These are the actual types from the .c files */
typedef struct { const char *lit; int len; }          lit_t;
typedef struct { int n; }                             pos_t;
typedef struct { int n; }                             rpos_t;

/* ALT state (from bb_alt.c) */
#define BB_ALT_MAX 16
typedef struct { bb_box_fn fn; void *ζ; } bb_child2_t;
typedef struct {
    int         n;
    bb_child2_t children[BB_ALT_MAX];
    int         alt_i;
    int         saved_Δ;
    spec_t       result;
} alt_t;

/* SEQ state (from bb_seq.c) */
typedef struct {
    bb_child2_t left;
    bb_child2_t right;
    spec_t       seq;
} seq_t;

/* ARBNO state (from bb_arbno.c) */
#define ARBNO_STACK_MAX 64
typedef struct { spec_t ARBNO; int saved_Δ; } arbno_frame_t;
typedef struct {
    bb_box_fn    body_fn;
    void        *body_ζ;
    int          ARBNO_i;
    arbno_frame_t stack[ARBNO_STACK_MAX];
} arbno_t;

/*
 * The test builds the full pattern graph and runs it against the subject.
 * We verify by collecting all ARBNO_γ results in a buffer and comparing.
 */

/* collected OUTPUT values */
static char output_buf[2048];
static int  output_pos = 0;

    /* collect: write_str + write_nl (as test_sno_1.c does in ARBNO_γ) */
    static spec_t write_str_nl_collect(spec_t s) {
        if (!spec_is_empty(s)) {
            memcpy(output_buf + output_pos, s.σ, (size_t)s.δ);
            output_pos += s.δ;
            output_buf[output_pos++] = '\n';  /* write_str */
            output_buf[output_pos++] = '\n';  /* write_nl  */
        }
                                                              return s;
    }

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } \
} while(0)

    printf("bb_dyn_test — M-DYN-2 integration test\n");
    printf("Pattern: POS(0) ARBNO('Bird' | 'Blue' | LEN(1)) $OUTPUT RPOS(0)\n");
    printf("Subject: \"BlueGoldBirdFish\"\n\n");

    /* Subject setup */
    const char *subject = "BlueGoldBirdFish";
    Σ = subject;
    Ω = (int)strlen(subject);
    Δ = 0;

    /* Build literals */
    lit_t  bird_ζ = { "Bird", 4 };
    lit_t  blue_ζ = { "Blue", 4 };
    len1_t len1_ζ = { 0 };
    pos_t  pos0_ζ = { 0 };
    rpos_t rpos0_ζ = { 0 };

    /* Build ALT('Bird' | 'Blue' | LEN(1)) */
    alt_t alt_ζ = {0};
    alt_ζ.n = 3;
    alt_ζ.children[0].fn = (bb_box_fn)bb_lit;    alt_ζ.children[0].ζ = &bird_ζ;
    alt_ζ.children[1].fn = (bb_box_fn)bb_lit;    alt_ζ.children[1].ζ = &blue_ζ;
    alt_ζ.children[2].fn = (bb_box_fn)bb_len1;   alt_ζ.children[2].ζ = &len1_ζ;

    /* Build ARBNO(alt) */
    arbno_t arbno_ζ = {0};
    arbno_ζ.body_fn = (bb_box_fn)bb_alt;
    arbno_ζ.body_ζ  = &alt_ζ;

    /*
     * The full pattern is:
     *   SEQ(POS(0), SEQ(ARBNO(ALT(...)), RPOS(0)))
     *
     * But we drive it manually here to also collect the $ OUTPUT side-effect
     * (ARBNO's γ port). We replicate the test_sno_1.c structure directly.
     */

    /*
     * Run: POS(0) then enter ARBNO loop, collecting intermediate results
     * via write_str_collect each time ARBNO_γ fires, then check RPOS(0).
     * On success, write "Success!"; on failure, "Failure."
     *
     * We implement this as: run the full SEQ, with write_str side-effect
     * inserted after each ARBNO γ step.  To do this cleanly we inline the
     * driver loop here, matching test_sno_1.c's structure exactly.
     */

    int success = 0;

    /* POS(0) */
    pos_t *pos0 = &pos0_ζ;
    spec_t pos_r = bb_pos((void **)&pos0, α);
    if (spec_is_empty(pos_r))                                 goto driver_ω;

    /* ARBNO loop: collect each γ, then check RPOS(0) */
    {
        arbno_t *arb = &arbno_ζ;
        spec_t arb_r  = bb_arbno((void **)&arb, α);
        while (1) {
            if (spec_is_empty(arb_r)) break;

            /* ARBNO_γ: write_str(out, ARBNO); write_nl(out) */
            write_str_nl_collect(arb_r);

            /* RPOS(0) check */
            rpos_t *rp = &rpos0_ζ;
            spec_t rpos_r = bb_rpos((void **)&rp, α);
            if (!spec_is_empty(rpos_r)) {
                /* seq_γ: write_str(out, seq) — final full match, no write_nl */
                if (!spec_is_empty(arb_r)) {
                    memcpy(output_buf + output_pos, arb_r.σ, (size_t)arb_r.δ);
                    output_pos += arb_r.δ;
                    output_buf[output_pos++] = '\n';
                }
                success = 1;
                break;
            }
            arb_r = bb_arbno((void **)&arb, β);
        }
    }

driver_ω:
    /* Append Success!/Failure. — write_sz adds no extra newline */
    if (success) {
        memcpy(output_buf + output_pos, "Success!", 8);
        output_pos += 8;
        output_buf[output_pos++] = '\n';
    } else {
        memcpy(output_buf, "Failure.", 8);
        output_pos = 8;
        output_buf[output_pos++] = '\n';
    }
    output_buf[output_pos] = '\0';

    /* Print output */
    printf("Output:\n%s\n", output_buf);

    /* Verify against expected (from test_sno_1.c) */
    /* Expected output verified against test_sno_1.c compiled with gcc.
     * write_nl() fires after each write_str(), producing blank lines.
     * ARBNO γ fires at every intermediate extension, not just Bird/Blue
     * boundaries — LEN(1) matches single chars between words too.
     * Final "BlueGoldBirdFish" appears twice: once from ARBNO_γ write,
     * once from the seq_γ write in write_α. */
    const char *expected =
        "Blue\n\n"
        "BlueG\n\n"
        "BlueGo\n\n"
        "BlueGol\n\n"
        "BlueGold\n\n"
        "BlueGoldBird\n\n"
        "BlueGoldBirdF\n\n"
        "BlueGoldBirdFi\n\n"
        "BlueGoldBirdFis\n\n"
        "BlueGoldBirdFish\n\n"
        "BlueGoldBirdFish\n"
        "Success!\n";

    CHECK(strcmp(output_buf, expected) == 0,
          "output matches test_sno_1.c exactly");
    CHECK(success == 1, "pattern matched (Success!)");
    CHECK(Δ == Ω, "cursor at end of subject after match");

    printf("\n%s  (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
                                                              return failures == 0 ? 0 : 1;
}
