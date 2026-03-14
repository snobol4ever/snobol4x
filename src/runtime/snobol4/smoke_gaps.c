/*
 * smoke_gaps.c — Targeted smoke tests for the 3 critical gaps
 * before attempting to compile beautiful.c
 *
 * Gap 1: Pattern engine (pat_*, match_pattern, match_and_replace)
 * Gap 2: Array/Table/Tree subscript API (array_create, subscript_get/set)
 * Gap 3: register_fn, push_val/pop_val/top_val, tree_new
 *
 * Each test is standalone. Failures pinpoint exactly what to implement.
 */

#include <stdio.h>
#include <string.h>
#include "snobol4.h"

int pass = 0, fail = 0;

#define CHECK(desc, cond) \
    do { if (cond) { printf("  PASS: %s\n", desc); pass++; } \
         else      { printf("  FAIL: %s\n", desc); fail++; } } while(0)

/* =========================================================================
 * GAP 1: Pattern engine
 * ===================================================================== */

static void test_pattern_engine(void) {
    printf("\n[Gap 1: Pattern engine]\n");

    /* pat_lit + match_pattern */
    SnoVal lit = pat_lit("hello");
    CHECK("pat_lit matches 'hello'",      match_pattern(lit, "hello"));
    CHECK("pat_lit fails on 'world'",    !match_pattern(lit, "world"));
    CHECK("pat_lit matches inside 'say hello'", match_pattern(lit, "say hello"));

    /* pat_span */
    SnoVal sp = pat_span("abc");
    CHECK("pat_span('abc') matches 'aabbcc'", match_pattern(sp, "aabbcc"));
    CHECK("pat_span fails on 'xyz'",         !match_pattern(sp, "xyz"));

    /* pat_break_ */
    SnoVal brk = pat_break_(".");
    CHECK("pat_break('.') matches in 'foo.bar'", match_pattern(brk, "foo.bar"));

    /* pat_len */
    SnoVal l3 = pat_len(3);
    CHECK("pat_len(3) matches 'abc'",  match_pattern(l3, "abc"));
    CHECK("pat_len(3) matches in 'xabcy'", match_pattern(l3, "xabcy"));

    /* pat_pos + pat_rpos */
    SnoVal p0 = pat_pos(0);
    SnoVal rp0 = pat_rpos(0);
    SnoVal anchored = pat_cat(p0, pat_cat(pat_lit("ab"), rp0));
    CHECK("POS(0) 'ab' RPOS(0) matches 'ab'",   match_pattern(anchored, "ab"));
    CHECK("POS(0) 'ab' RPOS(0) fails on 'xab'", !match_pattern(anchored, "xab"));

    /* pat_cat + pat_alt */
    SnoVal cat = pat_cat(pat_lit("foo"), pat_lit("bar"));
    CHECK("cat 'foo''bar' matches 'foobar'",  match_pattern(cat, "foobar"));
    CHECK("cat 'foo''bar' fails on 'foobaz'", !match_pattern(cat, "foobaz"));

    SnoVal alt = pat_alt(pat_lit("cat"), pat_lit("dog"));
    CHECK("alt 'cat'|'dog' matches 'cat'", match_pattern(alt, "cat"));
    CHECK("alt 'cat'|'dog' matches 'dog'", match_pattern(alt, "dog"));
    CHECK("alt 'cat'|'dog' fails on 'bird'", !match_pattern(alt, "bird"));

    /* pat_epsilon — always matches */
    SnoVal eps = pat_epsilon();
    CHECK("epsilon always matches",  match_pattern(eps, "anything"));
    CHECK("epsilon matches empty",   match_pattern(eps, ""));

    /* pat_arbno */
    SnoVal star = pat_arbno(pat_lit("ab"));
    CHECK("arbno('ab') matches ''",      match_pattern(star, ""));
    CHECK("arbno('ab') matches 'ababab'", match_pattern(star, "ababab"));

    /* pat_any_cs — ANY(chars) */
    SnoVal any_vw = pat_any_cs("aeiou");
    CHECK("any('aeiou') matches 'a'", match_pattern(any_vw, "a"));
    CHECK("any('aeiou') matches 'e'", match_pattern(any_vw, "e"));
    CHECK("any('aeiou') fails on 'b'", !match_pattern(any_vw, "b"));

    /* pat_rtab */
    SnoVal rt2 = pat_rtab(2);
    CHECK("rtab(2) on 'hello' leaves 2 chars", match_pattern(rt2, "hello"));

    /* pat_ref — deferred variable reference */
    var_set("myPat", pat_lit("xyz"));
    SnoVal ref = pat_ref("myPat");
    CHECK("pat_ref('myPat') matches 'xyz'",   match_pattern(ref, "xyz"));
    CHECK("pat_ref('myPat') fails on 'abc'", !match_pattern(ref, "abc"));

    /* var_as_pattern — variable holding pattern */
    SnoVal vp = var_as_pattern(var_get("myPat"));
    CHECK("var_as_pattern from var works", match_pattern(vp, "xyz"));

    /* pat_assign_cond — conditional capture $.var */
    SnoVal cap_var = NULL_VAL;
    var_set("captured", NULL_VAL);
    SnoVal cap = pat_assign_cond(pat_span("0123456789"), var_get("captured"));
    /* Note: assign_cond stores into the variable on mtch */
    int ok = match_pattern(
        pat_cat(pat_pos(0), pat_cat(cap, pat_rpos(0))),
        "12345"
    );
    CHECK("pat_assign_cond captures digits", ok);

    /* match_and_replace */
    SnoVal subject = STR_VAL("hello world");
    int replaced = match_and_replace(&subject, pat_lit("world"), STR_VAL("there"));
    CHECK("match_and_replace replaces 'world' with 'there'", replaced);
    CHECK("match_and_replace result is 'hello there'",
          strcmp(to_str(subject), "hello there") == 0);

    /* pat_user_call — unknown pattern function dispatched via aply */
    /* (requires register_fn to be working) */
}

/* =========================================================================
 * GAP 2: Array/Table/Tree subscript via SnoVal
 * ===================================================================== */

static void test_collections(void) {
    printf("\n[Gap 2: Collections — array_create, subscript_get/set, table_new, tree_new]\n");

    /* array_create("1:4") */
    SnoVal arr = array_create(STR_VAL("1:4"));
    CHECK("array_create returns non-null", arr.type != SNULL);

    subscript_set(arr, INT_VAL(1), INT_VAL(18));
    subscript_set(arr, INT_VAL(2), INT_VAL(33));
    subscript_set(arr, INT_VAL(3), INT_VAL(36));
    subscript_set(arr, INT_VAL(4), INT_VAL(81));

    SnoVal v1 = subscript_get(arr, INT_VAL(1));
    SnoVal v4 = subscript_get(arr, INT_VAL(4));
    CHECK("arr[1] == 18", to_int(v1) == 18);
    CHECK("arr[4] == 81", to_int(v4) == 81);

    /* table_new */
    SnoVal tbl = TABLE_VAL(table_new());
    subscript_set(tbl, STR_VAL("key"), STR_VAL("value"));
    SnoVal got = subscript_get(tbl, STR_VAL("key"));
    CHECK("table set/get 'key'='value'", strcmp(to_str(got), "value") == 0);

    /* tree_new — the tree DATA type */
    SnoVal t = make_tree(STR_VAL("snoId"),
                              STR_VAL("hello"),
                              INT_VAL(0),
                              NULL_VAL);
    CHECK("tree_new returns non-null", t.type != SNULL);
    SnoVal ttype = field_get(t, "t");
    SnoVal tval  = field_get(t, "v");
    CHECK("tree t field = 'snoId'",  strcmp(to_str(ttype), "snoId") == 0);
    CHECK("tree v field = 'hello'",  strcmp(to_str(tval),  "hello") == 0);
}

/* =========================================================================
 * GAP 3: register_fn, push_val/pop_val/top_val
 * ===================================================================== */

static SnoVal _test_fn(SnoVal *args, int n) {
    if (n < 1) return INT_VAL(0);
    return add(args[0], INT_VAL(1));
}

static void test_registration(void) {
    printf("\n[Gap 3: register_fn + push_val/pop_val/top_val]\n");

    /* register_fn */
    register_fn("addOne", _test_fn, 1, 1);
    SnoVal result = aply("addOne", (SnoVal[]){INT_VAL(41)}, 1);
    CHECK("registered fn 'addOne(41)' returns 42", to_int(result) == 42);

    /* push_val / pop_val / top_val */
    SnoVal a = STR_VAL("alpha");
    SnoVal b = STR_VAL("beta");
    push_val(a);
    push_val(b);
    SnoVal top = top_val();
    CHECK("top_val() is 'beta'", strcmp(to_str(top), "beta") == 0);
    SnoVal popped = pop_val();
    CHECK("pop_val() returns 'beta'", strcmp(to_str(popped), "beta") == 0);
    SnoVal popped2 = pop_val();
    CHECK("second pop_val() returns 'alpha'", strcmp(to_str(popped2), "alpha") == 0);
}

/* =========================================================================
 * Main
 * ===================================================================== */

int main(void) {
    runtime_init();
    printf("=== Sprint 20 Gap Smoke Tests ===\n");

    test_pattern_engine();
    test_collections();
    test_registration();

    printf("\n================================================\n");
    printf("Results: %d pass, %d fail\n", pass, fail);
    if (fail == 0) printf("ALL PASS — gaps are closed.\n");
    return fail ? 1 : 0;
}
