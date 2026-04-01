/*
 * stmt_exec_test.c — M-DYN-3 gate test for stmt_exec_dyn_str()
 *
 * Standalone build: no GC, no snobol4 runtime, no NASM.
 * Uses STMT_EXEC_STANDALONE to compile stmt_exec.c without snobol4.h.
 *
 * Build (from one4all/):
 *   gcc -Wall -Wno-unused-label -Wno-unused-variable -g -O0 \
 *       -DSTMT_EXEC_STANDALONE \
 *       -I src/runtime/dyn \
 *       src/runtime/dyn/bb_lit.c   \
 *       src/runtime/dyn/bb_alt.c   \
 *       src/runtime/dyn/bb_seq.c   \
 *       src/runtime/dyn/bb_arbno.c \
 *       src/runtime/dyn/bb_pos.c   \
 *       src/runtime/dyn/stmt_exec.c \
 *       src/runtime/dyn/stmt_exec_test.c \
 *       -o stmt_exec_test
 *
 * Gate: ALL PASS
 */

#include "bb_box.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── define globals owned here ──────────────────────────────────────────── */
const char *Σ = NULL;
int         Δ = 0;
int         Ω = 0;

/* ── minimal DESCR_t for standalone ────────────────────────────────────── */
typedef enum { DT_SNUL=0, DT_S=1, DT_P=3, DT_I=6, DT_FAIL=99 } DTYPE_t;
typedef struct {
    DTYPE_t  v;
    uint32_t slen;
    union { char *s; int64_t i; void *ptr; void *p; };
} DESCR_t;

/* ── stubs ──────────────────────────────────────────────────────────────── */
DESCR_t NV_GET_fn(const char *name) {
    (void)name;
    DESCR_t d; d.v = DT_SNUL; d.slen = 0; d.s = NULL;         return d;
}
void NV_SET_fn(const char *name, DESCR_t val) {
    if (val.v == DT_S && val.s) {
        int n = val.slen ? (int)val.slen : (int)strlen(val.s);
        printf("  capture: %s = \"%.*s\"\n", name, n, val.s);
    }
}
char *VARVAL_fn(DESCR_t d) {
    if (d.v == DT_S)                                          return d.s;
                                                              return NULL;
}

/* forward declaration */
int stmt_exec_dyn_str(const char *subject, const char *pattern,
                      const char *repl_str, char **out_subject);

/* ── test harness ───────────────────────────────────────────────────────── */
static int failures = 0;

#define CHECK(cond, msg) do { \
    if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } \
} while(0)

int main(void)
{
    printf("stmt_exec_test — M-DYN-3 five-phase executor\n\n");

    /* T1: simple literal match */
    printf("T1: literal match 'Bird' in 'BlueGoldBirdFish'\n");
    CHECK(stmt_exec_dyn_str("BlueGoldBirdFish","Bird",NULL,NULL)==1,"T1: match");

    /* T2: literal no-match */
    printf("T2: literal no-match 'Cat'\n");
    CHECK(stmt_exec_dyn_str("BlueGoldBirdFish","Cat",NULL,NULL)==0,"T2: no match");

    /* T3: replacement in middle */
    printf("T3: replace 'Bird' -> 'EAGLE'\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("BlueGoldBirdFish","Bird","EAGLE",&out);
      CHECK(r==1,"T3: match");
      CHECK(out&&strcmp(out,"BlueGoldEAGLEFish")==0,"T3: replacement");
      if(out) printf("  result: \"%s\"\n",out); }

    /* T4: spec_empty pattern = epsilon, always matches */
    printf("T4: spec_empty pattern -> epsilon\n");
    CHECK(stmt_exec_dyn_str("hello","",NULL,NULL)==1,"T4: epsilon matches");

    /* T5: match at start */
    printf("T5: match 'Blue' at start\n");
    CHECK(stmt_exec_dyn_str("BlueGoldBirdFish","Blue",NULL,NULL)==1,"T5: start match");

    /* T6: match at end */
    printf("T6: match 'Fish' at end\n");
    CHECK(stmt_exec_dyn_str("BlueGoldBirdFish","Fish",NULL,NULL)==1,"T6: end match");

    /* T7: replace at start */
    printf("T7: replace 'Blue' -> 'RED'\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("BlueGoldBirdFish","Blue","RED",&out);
      CHECK(r==1,"T7: match");
      CHECK(out&&strcmp(out,"REDGoldBirdFish")==0,"T7: replacement");
      if(out) printf("  result: \"%s\"\n",out); }

    /* T8: replace at end */
    printf("T8: replace 'Fish' -> 'WHALE'\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("BlueGoldBirdFish","Fish","WHALE",&out);
      CHECK(r==1,"T8: match");
      CHECK(out&&strcmp(out,"BlueGoldBirdWHALE")==0,"T8: replacement");
      if(out) printf("  result: \"%s\"\n",out); }

    /* T9: delete (spec_empty replacement) */
    printf("T9: delete 'Gold'\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("BlueGoldBirdFish","Gold","",&out);
      CHECK(r==1,"T9: match");
      CHECK(out&&strcmp(out,"BlueBirdFish")==0,"T9: deletion");
      if(out) printf("  result: \"%s\"\n",out); }

    /* T10: no match, out unchanged */
    printf("T10: no match\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("BlueGoldBirdFish","ZEBRA","X",&out);
      CHECK(r==0,"T10: :F");
      CHECK(out==NULL,"T10: out not set"); }

    /* T11: exact full-subject match */
    printf("T11: exact full-subject match\n");
    CHECK(stmt_exec_dyn_str("BlueGoldBirdFish","BlueGoldBirdFish",NULL,NULL)==1,"T11");

    /* T12: single char */
    printf("T12: single-char match\n");
    CHECK(stmt_exec_dyn_str("X","X",NULL,NULL)==1,"T12");

    /* T13: replace whole subject */
    printf("T13: replace whole subject\n");
    { char *out=NULL;
      int r=stmt_exec_dyn_str("HELLO","HELLO","WORLD",&out);
      CHECK(r==1,"T13: match");
      CHECK(out&&strcmp(out,"WORLD")==0,"T13: replacement");
      if(out) printf("  result: \"%s\"\n",out); }

    printf("\n%s  (%d failure%s)\n",
           failures==0?"PASS":"FAIL",
           failures, failures==1?"":"s");
                                                              return failures==0?0:1;
}
