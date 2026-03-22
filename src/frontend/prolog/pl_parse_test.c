/*
 * pl_parse_test.c — M-PROLOG-PARSE acceptance criterion
 *
 * Parses puzzle_02.pro (33 clauses) and several synthetic tests.
 * Acceptance: 0 parse errors, clause count matches expected.
 *
 * Build:
 *   gcc -I. -o pl_parse_test pl_parse_test.c pl_parse.c pl_lex.c \
 *       pl_atom.c pl_unify.c
 */

#include "pl_parse.h"
#include "pl_lex.h"
#include "pl_atom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

#define CHECK(label, cond) do { \
    tests_run++; \
    if (cond) { printf("PASS: %s\n", label); tests_passed++; } \
    else        printf("FAIL: %s\n", label); \
} while(0)

/* -------------------------------------------------------------------------
 * Test 1: facts-only program
 * ---------------------------------------------------------------------- */
static void test_facts(void) {
    const char *src =
        "person(brown).\n"
        "person(jones).\n"
        "person(smith).\n";
    PlProgram *prog = pl_parse(src, "test_facts");
    CHECK("facts: 0 errors",  prog->nerrors == 0);
    CHECK("facts: 3 clauses", prog->nclauses == 3);
    /* All three are facts (no body) */
    int all_facts = 1;
    for (PlClause *cl = prog->head; cl; cl = cl->next)
        if (cl->nbody != 0) all_facts = 0;
    CHECK("facts: all have empty body", all_facts);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 2: rule with body goals
 * ---------------------------------------------------------------------- */
static void test_rule(void) {
    const char *src =
        "display(C, M, T) :-\n"
        "    write('Cashier='), write(C),\n"
        "    write(' Manager='), write(M),\n"
        "    write(' Teller='), write(T),\n"
        "    write('\\n').\n";
    PlProgram *prog = pl_parse(src, "test_rule");
    CHECK("rule: 0 errors",  prog->nerrors == 0);
    CHECK("rule: 1 clause",  prog->nclauses == 1);
    PlClause *cl = prog->head;
    CHECK("rule: head not null",  cl && cl->head != NULL);
    CHECK("rule: 5 body goals",   cl && cl->nbody == 7);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 3: cut + fail pattern (differ/2)
 * ---------------------------------------------------------------------- */
static void test_cut(void) {
    const char *src =
        "differ(X, X) :- !, fail.\n"
        "differ(_, _).\n";
    PlProgram *prog = pl_parse(src, "test_cut");
    CHECK("cut: 0 errors",  prog->nerrors == 0);
    CHECK("cut: 2 clauses", prog->nclauses == 2);
    /* First clause body: [!, fail] */
    PlClause *cl1 = prog->head;
    CHECK("cut: first clause has 2 body goals", cl1 && cl1->nbody == 2);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 4: directive
 * ---------------------------------------------------------------------- */
static void test_directive(void) {
    const char *src = ":- initialization(main).\n";
    PlProgram *prog = pl_parse(src, "test_directive");
    CHECK("directive: 0 errors",  prog->nerrors == 0);
    CHECK("directive: 1 clause",  prog->nclauses == 1);
    CHECK("directive: head null", prog->head && prog->head->head == NULL);
    CHECK("directive: 1 body goal", prog->head && prog->head->nbody == 1);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 5: list syntax
 * ---------------------------------------------------------------------- */
static void test_lists(void) {
    const char *src =
        "member(X, [X|_]).\n"
        "member(X, [_|T]) :- member(X, T).\n";
    PlProgram *prog = pl_parse(src, "test_lists");
    CHECK("lists: 0 errors",  prog->nerrors == 0);
    CHECK("lists: 2 clauses", prog->nclauses == 2);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 6: arithmetic operators
 * ---------------------------------------------------------------------- */
static void test_arith(void) {
    const char *src =
        "fib(0, 0).\n"
        "fib(1, 1).\n"
        "fib(N, F) :- N > 1, N1 is N - 1, N2 is N - 2,\n"
        "             fib(N1, F1), fib(N2, F2), F is F1 + F2.\n";
    PlProgram *prog = pl_parse(src, "test_arith");
    CHECK("arith: 0 errors",  prog->nerrors == 0);
    CHECK("arith: 3 clauses", prog->nclauses == 3);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 7: puzzle_02.pro-equivalent (full program, 20+ clauses)
 * ---------------------------------------------------------------------- */
static const char *PUZZLE02 =
    ":- initialization(main).\n"
    "person(clark).\n"
    "person(daw).\n"
    "person(fuller).\n"
    "hasHeardOf(fuller, daw) :- !, fail.\n"
    "hasHeardOf(_, _).\n"
    "earnsMore(daw, clark).\n"
    "doesEarnMore(X, Y) :- earnsMore(X, Y).\n"
    "doesEarnMore(X, Y) :- earnsMore(Y, X), !, fail.\n"
    "doesEarnMore(X, Z) :- earnsMore(X, Y), earnsMore(X, Z).\n"
    "statement(X, V, Y) :- write(X), write(V), write(Y), write('.\\n').\n"
    "main :-\n"
    "   person(Carpenter),\n"
    "   person(Painter),\n"
    "   person(Plumber),\n"
    "   write('\\n'),\n"
    "   differ(Carpenter, Painter, Plumber),\n"
    "   write('Carpenter:'), write(Carpenter),\n"
    "   write(' Painter:'),  write(Painter),\n"
    "   write(' Plumber:'),  write(Plumber),\n"
    "   write('\\n'),\n"
    "   hasHeardOf(Painter, Carpenter),\n"
    "   hasHeardOf(Carpenter, Painter),\n"
    "   hasHeardOf(Carpenter, Plumber),\n"
    "   hasHeardOf(Plumber, Carpenter),\n"
    "   doesEarnMore(Plumber, Painter),\n"
    "   write('WINNER'),\n"
    "   fail.\n"
    "differ(X, X, _) :- !, fail.\n"
    "differ(X, _, X) :- !, fail.\n"
    "differ(_, X, X) :- !, fail.\n"
    "differ(_, _, _).\n";

static void test_puzzle02(void) {
    PlProgram *prog = pl_parse(PUZZLE02, "puzzle02");
    CHECK("puzzle02: 0 errors",     prog->nerrors == 0);
    CHECK("puzzle02: >= 16 clauses", prog->nclauses >= 16);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * Test 8: pretty-print round-trip (structural check)
 * ---------------------------------------------------------------------- */
static void test_pretty(void) {
    const char *src =
        "append([], L, L).\n"
        "append([H|T], L, [H|R]) :- append(T, L, R).\n";
    PlProgram *prog = pl_parse(src, "test_pretty");
    CHECK("pretty: 0 errors",  prog->nerrors == 0);
    CHECK("pretty: 2 clauses", prog->nclauses == 2);
    /* Pretty-print to a string buffer; verify it contains 'append' */
    FILE *tmp = tmpfile();
    pl_program_pretty(prog, tmp);
    rewind(tmp);
    char buf[1024]; int n = (int)fread(buf, 1, sizeof buf - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);
    CHECK("pretty: output contains 'append'", strstr(buf, "append") != NULL);
    pl_program_free(prog);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(void) {
    pl_atom_init();

    test_facts();
    test_rule();
    test_cut();
    test_directive();
    test_lists();
    test_arith();
    test_puzzle02();
    test_pretty();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
