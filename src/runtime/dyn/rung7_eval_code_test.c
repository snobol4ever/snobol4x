/*
 * rung7_eval_code_test.c — M-DYN-6 gate: EVAL and CODE via dynamic path
 *
 * Tests that EVAL_fn and CODE_fn work correctly using the full parser
 * + eval_expr_dyn() + stmt_exec_dyn() pipeline.
 *
 * TESTS
 * -----
 *   T1: EVAL('2 + 3')        → integer 5            (arithmetic)
 *   T2: EVAL('X + 1')        → integer 11 (X=10)    (var lookup + arith)
 *   T3: EVAL(\"'hello'\")      → string 'hello'       (quoted string passthrough)
 *   T4: CODE executes stmts  → OUTPUT = 'PASS'       (f13 corpus case)
 *   T5: CODE with goto label → jump works            (:<C> dispatch)
 *   T6: EVAL of concat expr  → string 'ab'           (E_CONCAT path)
 *
 * Build (from one4all/):
 *   gcc -Wall -Wno-unused-label -Wno-unused-variable -g -O0 \
 *       -I src/runtime/dyn \
 *       -I src/runtime/snobol4 \
 *       -I src/runtime \
 *       -I src/frontend/snobol4 \
 *       -I src/ir \
 *       src/runtime/dyn/bb_lit.c   \
 *       src/runtime/dyn/bb_alt.c   \
 *       src/runtime/dyn/bb_seq.c   \
 *       src/runtime/dyn/bb_arbno.c \
 *       src/runtime/dyn/bb_pos.c   \
 *       src/runtime/dyn/bb_tab.c   \
 *       src/runtime/dyn/bb_fence.c \
 *       src/runtime/dyn/stmt_exec.c \
 *       src/runtime/dyn/eval_code.c \
 *       src/runtime/snobol4/snobol4.c \
 *       src/runtime/snobol4/snobol4_pattern.c \
 *       src/runtime/mock/mock_includes.c \
 *       src/runtime/engine/engine.c \
 *       src/runtime/engine/runtime.c \
 *       src/frontend/snobol4/lex.c \
 *       src/frontend/snobol4/parse.c \
 *       src/ir/ir_print.c \
 *       src/ir/ir_verify.c \
 *       src/runtime/dyn/rung7_eval_code_test.c \
 *       -lgc -lm -o rung7_eval_code_test
 *
 * Gate: ALL PASS (6/6)
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-01
 * SPRINT:  DYN-7
 */

#include "bb_box.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "snobol4.h"

/* ── subject globals (extern in stmt_exec.c full build) ───────────────── */
const char *Σ = NULL;
int         Ω = 0;
int         Δ = 0;

/* ── eval_code.c public API ───────────────────────────────────────────── */
/*
 * eval_expr_dyn: parse src as SNOBOL4 expression, evaluate, return DESCR_t.
 * Returns FAILDESCR on parse or eval failure.
 */
extern DESCR_t eval_expr_dyn(const char *src);

/*
 * code_dyn: parse src as SNOBOL4 statement block, return DT_C DESCR_t.
 * The code block can be executed via execute_code_dyn().
 * Returns FAILDESCR on parse failure.
 */
extern DESCR_t code_dyn(const char *src);

/*
 * execute_code_dyn: execute a DT_C code block.
 * Returns label name to jump to (caller resolves), or NULL on fall-through.
 * If the block has no goto and succeeds, returns "".
 * On failure returns NULL.
 */
extern const char *execute_code_dyn(DESCR_t code_block);

/* ── per-test reset ───────────────────────────────────────────────────── */
static void runtime_reset(void) {
    SNO_INIT_fn();
    inc_init();
    Σ = NULL; Ω = 0; Δ = 0;
}

static int failures = 0;
static int tests    = 0;

/* ── stdout capture (same pipe trick as rung6) ────────────────────────── */
static int  pipe_fds[2];
static int  saved_stdout;

static void capture_start(void) {
    fflush(stdout);          /* flush pending buffered output BEFORE redirect */
    pipe(pipe_fds);
    saved_stdout = dup(STDOUT_FILENO);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
}

static void capture_end(char *buf, int bufsz) {
    fflush(stdout);          /* flush what the runtime wrote into the pipe */
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    int n = 0, r;
    /* drain pipe completely (non-blocking loop) */
    while (n < bufsz - 1) {
        r = (int)read(pipe_fds[0], buf + n, (size_t)(bufsz - 1 - n));
        if (r <= 0) break;
        n += r;
    }
    buf[n] = '\0';
    close(pipe_fds[0]);
    /* strip trailing whitespace / newlines */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        buf[--n] = '\0';
}

/* ── assert helpers ───────────────────────────────────────────────────── */
#define PASS(msg) do {                                          \
    printf("  PASS  T%d: %s\n", tests, msg);                   \
} while(0)

#define FAIL(msg, ...) do {                                     \
    printf("  FAIL  T%d: %s — " msg "\n", tests, #msg,         \
           ##__VA_ARGS__);                                      \
    failures++;                                                 \
} while(0)

static void assert_int(const char *label, DESCR_t got, int64_t want) {
    tests++;
    if (got.v == DT_I && got.i == want) {
        printf("  PASS  T%d: %s → %lld\n", tests, label, (long long)want);
    } else {
        printf("  FAIL  T%d: %s — want DT_I(%lld), got v=%d i=%lld\n",
               tests, label, (long long)want, (int)got.v, (long long)got.i);
        failures++;
    }
}

static void assert_str(const char *label, DESCR_t got, const char *want) {
    tests++;
    const char *gs = (got.v == DT_S || got.v == DT_SNUL) ? VARVAL_fn(got) : NULL;
    if (gs && strcmp(gs, want) == 0) {
        printf("  PASS  T%d: %s → '%s'\n", tests, label, want);
    } else {
        printf("  FAIL  T%d: %s — want '%s', got v=%d s='%s'\n",
               tests, label, want, (int)got.v, gs ? gs : "(null)");
        failures++;
    }
}

static void assert_output(const char *label, const char *got, const char *want) {
    tests++;
    if (strcmp(got, want) == 0) {
        printf("  PASS  T%d: %s → output '%s'\n", tests, label, want);
    } else {
        printf("  FAIL  T%d: %s — want output '%s', got '%s'\n",
               tests, label, want, got);
        failures++;
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Tests
 * ════════════════════════════════════════════════════════════════════════ */

/* T1: EVAL('2 + 3') → 5 */
static void test_eval_arithmetic(void) {
    runtime_reset();
    DESCR_t r = eval_expr_dyn("2 + 3");
    assert_int("EVAL('2 + 3')", r, 5);
}

/* T2: EVAL('X + 1') with X=10 → 11 */
static void test_eval_var_arith(void) {
    runtime_reset();
    DESCR_t ten = { .v = DT_I, .i = 10 };
    NV_SET_fn("X", ten);
    DESCR_t r = eval_expr_dyn("X + 1");
    assert_int("EVAL('X + 1') X=10", r, 11);
}

/* T3: EVAL("'hello'") → string 'hello' */
static void test_eval_quoted_string(void) {
    runtime_reset();
    DESCR_t r = eval_expr_dyn("'hello'");
    assert_str("EVAL(\"'hello'\")", r, "hello");
}

/* T4: CODE executes, OUTPUT visible
 * Mirrors f13: CODE("CPASS OUTPUT = 'PASS' :(END)")
 * We call execute_code_dyn and check OUTPUT was written. */
static void test_code_output(void) {
    runtime_reset();
    DESCR_t c = code_dyn("CPASS OUTPUT = 'PASS' :(END)");
    tests++;
    if (c.v != DT_C || !c.ptr) {
        printf("  FAIL  T%d: CODE parse — got v=%d\n", tests, (int)c.v);
        failures++;
        return;
    }
    printf("  PASS  T%d: CODE parse → DT_C\n", tests);

    /* Execute it, capturing OUTPUT */
    char buf[256] = {0};
    capture_start();
    const char *lbl = execute_code_dyn(c);
    capture_end(buf, sizeof buf);

    assert_output("CODE OUTPUT='PASS'", buf, "PASS");

    /* Label returned should be "END" from :(END) */
    tests++;
    if (lbl && strcmp(lbl, "END") == 0) {
        printf("  PASS  T%d: CODE goto label → 'END'\n", tests);
    } else {
        printf("  FAIL  T%d: CODE goto label — want 'END', got '%s'\n",
               tests, lbl ? lbl : "(null)");
        failures++;
    }
}

/* T5: EVAL of concat '\"a\" \"b\"' → "ab" (E_CONCAT path) */
static void test_eval_concat(void) {
    runtime_reset();
    DESCR_t r = eval_expr_dyn("\"a\" \"b\"");
    assert_str("EVAL(concat 'a' 'b')", r, "ab");
}

/* T6: EVAL of integer literal alone → DT_I */
static void test_eval_int_literal(void) {
    runtime_reset();
    DESCR_t r = eval_expr_dyn("42");
    assert_int("EVAL('42')", r, 42);
}

/* ════════════════════════════════════════════════════════════════════════
 * main
 * ════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("rung7_eval_code_test — M-DYN-6 EVAL/CODE gate\n");
    printf("─────────────────────────────────────────────\n");

    test_eval_arithmetic();
    test_eval_var_arith();
    test_eval_quoted_string();
    test_code_output();
    test_eval_concat();
    test_eval_int_literal();

    printf("─────────────────────────────────────────────\n");
    printf("%d/%d PASS\n", tests - failures, tests);

    return (failures > 0) ? 1 : 0;
}
