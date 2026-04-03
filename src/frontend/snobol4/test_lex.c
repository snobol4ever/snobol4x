/*
 * test_lex.c — TDD harness for the SNOBOL4 lexer (lex.c / lex.l)
 *
 * Ports dotnet TestLexer tests to C, targeting the lex_open_str / sno_parse
 * interface. Run standalone; prints PASS/FAIL per test.
 *
 * Tests covered (matching dotnet numbering):
 *   Test_214  — label at column 1
 *   Test_218  — goto field extraction
 *   Test_231  — numeric literals (integer + real)
 *   Test_232  — string literals (single-quote, double-quote)
 *   Test_220  — unary/binary operators: + - * / %
 *   Test_221  — operators: ^ ! ** @ ~ $ . # | = ?
 *   Test_233  — punctuation: , ( ) [ ] < >
 *
 * Build:
 *   gcc -I . -I src -g -o test_lex \
 *       src/frontend/snobol4/test_lex.c \
 *       src/frontend/snobol4/lex.o \
 *       -lm
 * (run from one4all root)
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */

#include "lex.h"
#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── test infrastructure ─────────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define EXPECT_EQ_INT(name, got, want) do { \
    if ((got) == (want)) { \
        printf("  PASS %s: %d\n", name, (int)(got)); g_pass++; \
    } else { \
        printf("  FAIL %s: got %d want %d\n", name, (int)(got), (int)(want)); g_fail++; \
    } \
} while(0)

#define EXPECT_EQ_STR(name, got, want) do { \
    const char *g_ = (got); const char *w_ = (want); \
    if (g_ && w_ && strcmp(g_, w_) == 0) { \
        printf("  PASS %s: \"%s\"\n", name, g_); g_pass++; \
    } else { \
        printf("  FAIL %s: got \"%s\" want \"%s\"\n", name, g_ ? g_ : "(null)", w_ ? w_ : "(null)"); g_fail++; \
    } \
} while(0)

#define EXPECT_EQ_DOUBLE(name, got, want) do { \
    if (fabs((got) - (want)) < 1e-9) { \
        printf("  PASS %s: %g\n", name, (double)(got)); g_pass++; \
    } else { \
        printf("  FAIL %s: got %g want %g\n", name, (double)(got), (double)(want)); g_fail++; \
    } \
} while(0)

/* Lex a body string and return tokens in caller-supplied array. Returns count. */
static int lex_str_tokens(const char *src, Token *out, int max) {
    Lex lx;
    lex_open_str(&lx, src, (int)strlen(src), 1);
    int n = 0;
    while (n < max) {
        Token t = lex_next(&lx);
        out[n++] = t;
        if (t.kind == T_EOF || t.kind == T_ERR) break;
    }
    return n;
}

/* ── Test_214: label at column 1 ─────────────────────────────────────────── */
static void test_214(void) {
    printf("Test_214 — label at column 1\n");
    /*
     * Source line: "LOOP  OUTPUT = N"
     * Label "LOOP" is at col-1; body is "OUTPUT = N"
     * sno_parse produces a linked-list program; first stmt->label == "LOOP"
     */
    const char *src = "LOOP  OUTPUT = N\n     END\n";
    FILE *f = tmpfile(); fputs(src, f); rewind(f);
    sno_reset();
    Program *prog = sno_parse(f, "<t214>");
    fclose(f);

    if (!prog || prog->nstmts == 0 || !prog->head) {
        printf("  FAIL test_214: no program/stmts (nstmts=%d)\n",
               prog ? prog->nstmts : -1);
        g_fail++; return;
    }
    EXPECT_EQ_STR("label", prog->head->label, "LOOP");
}

/* ── Test_218: goto field ────────────────────────────────────────────────── */
static void test_218(void) {
    printf("Test_218 — goto field\n");
    /*
     * Source: "      X = Y  :S(DONE)F(ERR)"
     * parse_goto_field splits this into go->onsuccess="DONE", go->onfailure="ERR"
     */
    const char *src = "      X = Y  :S(DONE)F(ERR)\n     END\n";
    FILE *f = tmpfile(); fputs(src, f); rewind(f);
    sno_reset();
    Program *prog = sno_parse(f, "<t218>");
    fclose(f);

    if (!prog || prog->nstmts == 0 || !prog->head) {
        printf("  FAIL test_218: no program/stmts\n"); g_fail++; return;
    }
    STMT_t *s = prog->head;
    if (!s->go) {
        printf("  FAIL test_218: go is NULL\n"); g_fail++; return;
    }
    EXPECT_EQ_STR("onsuccess", s->go->onsuccess, "DONE");
    EXPECT_EQ_STR("onfailure", s->go->onfailure, "ERR");
}

/* ── Test_231: numeric literals ──────────────────────────────────────────── */
static void test_231(void) {
    printf("Test_231 — numeric literals\n");
    Token toks[16];
    int n;

    /* integer */
    n = lex_str_tokens("42", toks, 16);
    EXPECT_EQ_INT("int kind", toks[0].kind, T_INT);
    EXPECT_EQ_INT("int val",  (int)toks[0].ival, 42);

    /* negative integer (unary minus handled by parser; lexer gives T_INT) */
    n = lex_str_tokens("0", toks, 16);
    EXPECT_EQ_INT("zero kind", toks[0].kind, T_INT);
    EXPECT_EQ_INT("zero val",  (int)toks[0].ival, 0);

    /* real — decimal */
    n = lex_str_tokens("3.14", toks, 16);
    EXPECT_EQ_INT("real kind", toks[0].kind, T_REAL);
    EXPECT_EQ_DOUBLE("real val", toks[0].dval, 3.14);

    /* leading dot: .5 is NOT a real in SNOBOL4 — ELEMTB has no dot entry.
     * SPITBOL rejects .5 as syntax error (missing operand).
     * . is the naming operator; .5 = unary-dot applied to integer 5.
     * Lexer correctly emits T_DOT then T_INT(5). */
    n = lex_str_tokens(".5", toks, 16);
    EXPECT_EQ_INT("leading-dot is T_DOT", toks[0].kind, T_DOT);
    EXPECT_EQ_INT("leading-dot next is T_INT", toks[1].kind, T_INT);
    EXPECT_EQ_INT("leading-dot T_INT val",  (int)toks[1].ival, 5);

    /* real — exponent */
    n = lex_str_tokens("1E3", toks, 16);
    EXPECT_EQ_INT("exp kind",  toks[0].kind, T_REAL);
    EXPECT_EQ_DOUBLE("exp val", toks[0].dval, 1000.0);

    /* real — negative exponent */
    n = lex_str_tokens("2.5E-1", toks, 16);
    EXPECT_EQ_INT("negexp kind", toks[0].kind, T_REAL);
    EXPECT_EQ_DOUBLE("negexp val", toks[0].dval, 0.25);

    (void)n;
}

/* ── Test_232: string literals ───────────────────────────────────────────── */
static void test_232(void) {
    printf("Test_232 — string literals\n");
    Token toks[16];
    int n;

    /* single-quote string */
    n = lex_str_tokens("'hello'", toks, 16);
    EXPECT_EQ_INT("sq kind",  toks[0].kind, T_STR);
    EXPECT_EQ_STR("sq val",   toks[0].sval, "hello");

    /* double-quote string */
    n = lex_str_tokens("\"world\"", toks, 16);
    EXPECT_EQ_INT("dq kind",  toks[0].kind, T_STR);
    EXPECT_EQ_STR("dq val",   toks[0].sval, "world");

    /* empty string */
    n = lex_str_tokens("''", toks, 16);
    EXPECT_EQ_INT("empty kind", toks[0].kind, T_STR);
    EXPECT_EQ_STR("empty val",  toks[0].sval, "");

    /* 'it''s' is TWO tokens: 'it' then identifier s then 'empty'
     * Doubled-quote escape is NOT valid SNOBOL4 (SQLITB: FOR(SQUOTE) STOP).
     * SPITBOL rejects it as a syntax error. First token is just 'it'. */
    n = lex_str_tokens("'it''s'", toks, 16);
    EXPECT_EQ_INT("no-escape kind", toks[0].kind, T_STR);
    EXPECT_EQ_STR("no-escape val",  toks[0].sval, "it");

    (void)n;
}

/* ── Test_220: arithmetic operators ─────────────────────────────────────── */
static void test_220(void) {
    printf("Test_220 — arithmetic operators + - * / %%\n");
    Token toks[16];
    int n;

    n = lex_str_tokens("+ - * / %", toks, 16);
    /* expecting T_PLUS T_WS T_MINUS T_WS T_STAR T_WS T_SLASH T_WS T_PCT */
    /* find non-WS tokens */
    TokKind want[] = { T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT };
    int wi = 0;
    for (int i = 0; i < n && wi < 5; i++) {
        if (toks[i].kind == T_WS || toks[i].kind == T_EOF) continue;
        char name[32]; snprintf(name, sizeof(name), "op[%d]", wi);
        EXPECT_EQ_INT(name, toks[i].kind, want[wi]);
        wi++;
    }
    (void)n;
}

/* ── Test_221: other operators ───────────────────────────────────────────── */
static void test_221(void) {
    printf("Test_221 — operators ^ ! ** @ ~ $ . # | = ?\n");
    Token toks[32];

    /* ** must come before * in the lexer (longest match) */
    int n = lex_str_tokens("^ ! ** @ ~ $ . # | = ?", toks, 32);
    TokKind want[] = {
        T_CARET, T_BANG, T_STARSTAR,
        T_AT, T_TILDE, T_DOLLAR, T_DOT,
        T_HASH, T_PIPE, T_EQ, T_QMARK
    };
    int wi = 0;
    for (int i = 0; i < n && wi < 11; i++) {
        if (toks[i].kind == T_WS || toks[i].kind == T_EOF) continue;
        char nm[32]; snprintf(nm, sizeof(nm), "op[%d]", wi);
        EXPECT_EQ_INT(nm, toks[i].kind, want[wi]);
        wi++;
    }
    (void)n;
}

/* ── Test_233: punctuation , ( ) [ ] < > ────────────────────────────────── */
static void test_233(void) {
    printf("Test_233 — punctuation , ( ) [ ] < >\n");
    Token toks[32];
    int n = lex_str_tokens(", ( ) [ ] < >", toks, 32);
    TokKind want[] = {
        T_COMMA, T_LPAREN, T_RPAREN,
        T_LBRACKET, T_RBRACKET,
        T_LANGLE, T_RANGLE
    };
    int wi = 0;
    for (int i = 0; i < n && wi < 7; i++) {
        if (toks[i].kind == T_WS || toks[i].kind == T_EOF) continue;
        char nm[32]; snprintf(nm, sizeof(nm), "punct[%d]", wi);
        EXPECT_EQ_INT(nm, toks[i].kind, want[wi]);
        wi++;
    }
    (void)n;
}

/* ── additional: keyword (&ALPHABET etc.) ────────────────────────────────── */
static void test_keyword(void) {
    printf("Test_keyword — &ALPHABET &TRIM\n");
    Token toks[8];

    int n = lex_str_tokens("&ALPHABET", toks, 8);
    EXPECT_EQ_INT("kw kind",  toks[0].kind, T_KEYWORD);
    EXPECT_EQ_STR("kw sval",  toks[0].sval, "ALPHABET");

    n = lex_str_tokens("&TRIM", toks, 8);
    EXPECT_EQ_INT("trim kind", toks[0].kind, T_KEYWORD);
    EXPECT_EQ_STR("trim sval", toks[0].sval, "TRIM");
    (void)n;
}

/* ── additional: identifier ──────────────────────────────────────────────── */
static void test_ident(void) {
    printf("Test_ident — simple identifiers\n");
    Token toks[8];

    int n = lex_str_tokens("OUTPUT", toks, 8);
    EXPECT_EQ_INT("ident kind", toks[0].kind, T_IDENT);
    EXPECT_EQ_STR("ident sval", toks[0].sval, "OUTPUT");

    /* END keyword in body → T_END */
    n = lex_str_tokens("END", toks, 8);
    EXPECT_EQ_INT("end kind", toks[0].kind, T_END);
    (void)n;
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    printf("=== test_lex ===\n");
    test_214();
    test_218();
    test_231();
    test_232();
    test_220();
    test_221();
    test_233();
    test_keyword();
    test_ident();
    printf("=== %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
