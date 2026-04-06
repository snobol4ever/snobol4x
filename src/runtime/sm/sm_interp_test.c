/*
 * sm_interp_test.c — M-SCRIP-U2 unit test
 *
 * Tests the SM_Program builder and C dispatch loop in isolation.
 * Uses a stub NV table (no full SNOBOL4 runtime required).
 *
 * Compile (from src/runtime/sm/):
 *   gcc -O0 -g -I. -I../.. -I../../runtime -I../../runtime/snobol4 \
 *       sm_prog.c sm_interp.c sm_interp_test.c -lm -o sm_interp_test
 * Run: ./sm_interp_test
 */

#include "sm_prog.h"
#include "sm_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ── Minimal runtime stubs (no full snobol4.c needed) ─────────────── */

/* Simple NV store: 16 slots */
#define NV_MAX 16
static struct { char name[64]; DESCR_t val; } nv_store[NV_MAX];
static int nv_count = 0;

DESCR_t NV_GET_fn(const char *name) {
    for (int i = 0; i < nv_count; i++)
        if (strcmp(nv_store[i].name, name) == 0) return nv_store[i].val;
    return NULVCL;
}
void NV_SET_fn(const char *name, DESCR_t val) {
    for (int i = 0; i < nv_count; i++)
        if (strcmp(nv_store[i].name, name) == 0) { nv_store[i].val = val; return; }
    if (nv_count < NV_MAX) {
        strncpy(nv_store[nv_count].name, name, 63);
        nv_store[nv_count].val = val;
        nv_count++;
    }
}
static void nv_reset(void) { nv_count = 0; }

int64_t to_int(DESCR_t v) {
    if (v.v == DT_I) return v.i;
    if (v.v == DT_R) return (int64_t)v.r;
    if (v.v == DT_S && v.s) return atoll(v.s);
    return 0;
}
double to_real(DESCR_t v) {
    if (v.v == DT_R) return v.r;
    if (v.v == DT_I) return (double)v.i;
    if (v.v == DT_S && v.s) return atof(v.s);
    return 0.0;
}
DESCR_t CONCAT_fn(DESCR_t a, DESCR_t b) {
    /* minimal: concat two strings */
    const char *as = (a.v == DT_S && a.s) ? a.s : "";
    const char *bs = (b.v == DT_S && b.s) ? b.s : "";
    size_t la = strlen(as), lb = strlen(bs);
    char *buf = malloc(la + lb + 1);
    memcpy(buf, as, la); memcpy(buf + la, bs, lb); buf[la+lb] = '\0';
    return STRVAL(buf);
}
DESCR_t INVOKE_fn(const char *name, DESCR_t *args, int nargs) {
    (void)name; (void)args; (void)nargs;
    return NULVCL;
}

/* ── Test harness ───────────────────────────────────────────────────── */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (msg)); failures++; } \
    else          { fprintf(stdout, "PASS: %s\n", (msg)); } \
} while(0)

/* ── Test 1: push_i push_i add store_var halt → X == 5 ────────────── */

static void test_add_and_store(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    sm_emit_i(p, SM_PUSH_LIT_I, 2);
    sm_emit_i(p, SM_PUSH_LIT_I, 3);
    sm_emit(p,   SM_ADD);
    sm_emit_s(p, SM_STORE_VAR, "X");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    int r = sm_interp_run(p, &st);
    CHECK(r == 0, "test_add_and_store: run returns 0");

    DESCR_t x = NV_GET_fn("X");
    CHECK(x.v == DT_I && x.i == 5, "test_add_and_store: X == 5");

    sm_prog_free(p);
}

/* ── Test 2: push_var push_i add store_var — uses NV_GET ───────────── */

static void test_push_var(void)
{
    nv_reset();
    NV_SET_fn("Y", INTVAL(10));
    SM_Program *p = sm_prog_new();
    sm_emit_s(p, SM_PUSH_VAR,   "Y");
    sm_emit_i(p, SM_PUSH_LIT_I, 7);
    sm_emit(p,   SM_ADD);
    sm_emit_s(p, SM_STORE_VAR,  "Z");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t z = NV_GET_fn("Z");
    CHECK(z.v == DT_I && z.i == 17, "test_push_var: Z == 17");
    sm_prog_free(p);
}

/* ── Test 3: integer multiply ──────────────────────────────────────── */

static void test_mul(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    sm_emit_i(p, SM_PUSH_LIT_I, 6);
    sm_emit_i(p, SM_PUSH_LIT_I, 7);
    sm_emit(p,   SM_MUL);
    sm_emit_s(p, SM_STORE_VAR, "R");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t r = NV_GET_fn("R");
    CHECK(r.v == DT_I && r.i == 42, "test_mul: R == 42");
    sm_prog_free(p);
}

/* ── Test 4: SM_JUMP unconditional ─────────────────────────────────── */

static void test_jump(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    /* jump over the "bad" store */
    int jmp = sm_emit_i(p, SM_JUMP, 0);          /* placeholder target */
    sm_emit_i(p, SM_PUSH_LIT_I, 999);
    sm_emit_s(p, SM_STORE_VAR, "BAD");
    int target = sm_label(p);                     /* label = index of next instr */
    sm_patch_jump(p, jmp, target);
    sm_emit_i(p, SM_PUSH_LIT_I, 1);
    sm_emit_s(p, SM_STORE_VAR, "GOOD");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t bad  = NV_GET_fn("BAD");
    DESCR_t good = NV_GET_fn("GOOD");
    CHECK(bad.v  == DT_SNUL,              "test_jump: BAD not set (skipped)");
    CHECK(good.v == DT_I && good.i == 1,  "test_jump: GOOD == 1");
    sm_prog_free(p);
}

/* ── Test 5: SM_JUMP_S / SM_JUMP_F conditional ──────────────────────── */

static void test_conditional_jump(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    /* 2 + 3 succeeds → jump_s taken */
    sm_emit_i(p, SM_PUSH_LIT_I, 2);
    sm_emit_i(p, SM_PUSH_LIT_I, 3);
    sm_emit(p,   SM_ADD);
    sm_emit_s(p, SM_STORE_VAR, "SUM");
    int js = sm_emit_i(p, SM_JUMP_S, 0);    /* placeholder */
    sm_emit_i(p, SM_PUSH_LIT_I, 0);
    sm_emit_s(p, SM_STORE_VAR, "BRANCH");
    int skip = sm_emit_i(p, SM_JUMP, 0);
    int s_label = sm_label(p);
    sm_patch_jump(p, js, s_label);
    sm_emit_i(p, SM_PUSH_LIT_I, 1);
    sm_emit_s(p, SM_STORE_VAR, "BRANCH");
    int end_label = sm_label(p);
    sm_patch_jump(p, skip, end_label);
    sm_emit(p, SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t br = NV_GET_fn("BRANCH");
    CHECK(br.v == DT_I && br.i == 1, "test_conditional_jump: success branch taken");
    sm_prog_free(p);
}

/* ── Test 6: string concat ──────────────────────────────────────────── */

static void test_concat(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    sm_emit_s(p, SM_PUSH_LIT_S, "hello ");
    sm_emit_s(p, SM_PUSH_LIT_S, "world");
    sm_emit(p,   SM_CONCAT);
    sm_emit_s(p, SM_STORE_VAR, "MSG");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t msg = NV_GET_fn("MSG");
    CHECK(msg.v == DT_S && msg.s && strcmp(msg.s, "hello world") == 0,
          "test_concat: MSG == 'hello world'");
    sm_prog_free(p);
}

/* ── Test 7: SM_INCR / SM_DECR ──────────────────────────────────────── */

static void test_incr_decr(void)
{
    nv_reset();
    SM_Program *p = sm_prog_new();
    sm_emit_i(p, SM_PUSH_LIT_I, 10);
    sm_emit_i(p, SM_INCR, 5);
    sm_emit_s(p, SM_STORE_VAR, "A");
    sm_emit_i(p, SM_PUSH_LIT_I, 10);
    sm_emit_i(p, SM_DECR, 3);
    sm_emit_s(p, SM_STORE_VAR, "B");
    sm_emit(p,   SM_HALT);

    SM_State st; sm_state_init(&st);
    sm_interp_run(p, &st);
    DESCR_t a = NV_GET_fn("A");
    DESCR_t b = NV_GET_fn("B");
    CHECK(a.v == DT_I && a.i == 15, "test_incr_decr: A == 15");
    CHECK(b.v == DT_I && b.i ==  7, "test_incr_decr: B == 7");
    sm_prog_free(p);
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== SM_Program dispatch loop tests (M-SCRIP-U2) ===\n");
    test_add_and_store();
    test_push_var();
    test_mul();
    test_jump();
    test_conditional_jump();
    test_concat();
    test_incr_decr();

    if (failures == 0)
        printf("\nAll tests PASSED.\n");
    else
        printf("\n%d test(s) FAILED.\n", failures);
    return failures ? 1 : 0;
}
