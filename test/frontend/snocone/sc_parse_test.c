/*
 * sc_parse_test.c -- sc_parse() unit tests  (Sprint SC1 / M-SNOC-PARSE)
 *
 * Mirrors snobol4dotnet/TestSnobol4/TestSnocone/TestSnoconeParser.cs exactly.
 * Each section corresponds to a C# test class section.
 *
 * Quick-check trigger (M-SNOC-PARSE):
 *   gcc -I src/frontend/snocone -o /tmp/sc_parse_test \
 *       test/frontend/snocone/sc_parse_test.c \
 *       src/frontend/snocone/sc_lex.c src/frontend/snocone/sc_parse.c
 *   /tmp/sc_parse_test
 *   # output: NNN/NNN PASS
 */

#include "sc_lex.h"
#include "sc_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Test harness
 * ------------------------------------------------------------------------- */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s (line %d)\n", name, __LINE__); } \
} while(0)

/* Parse src (stripping NEWLINE/EOF), return ScParseResult */
static ScParseResult parse_src(const char *src) {
    ScTokenArray arr = sc_lex(src);
    /* collect non-NEWLINE non-EOF tokens */
    ScToken *filtered = malloc((size_t)arr.count * sizeof(ScToken));
    int n = 0;
    for (int i = 0; i < arr.count; i++) {
        ScKind k = arr.tokens[i].kind;
        if (k != SC_NEWLINE && k != SC_EOF)
            filtered[n++] = arr.tokens[i];
    }
    ScParseResult r = sc_parse(filtered, n);
    free(filtered);
    sc_tokens_free(&arr);
    return r;
}

/* Return array of kinds from parsing src */
static void kinds_of(const char *src, ScKind *out, int *count_out) {
    ScParseResult r = parse_src(src);
    for (int i = 0; i < r.count; i++) out[i] = r.tokens[i].kind;
    *count_out = r.count;
    sc_parse_free(&r);
}

/* Check kinds match expected array */
static int kinds_eq(const char *src, const ScKind *expected, int n) {
    ScKind got[64];
    int got_n = 0;
    kinds_of(src, got, &got_n);
    if (got_n != n) return 0;
    for (int i = 0; i < n; i++)
        if (got[i] != expected[i]) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Section 1 — Single operands pass through unchanged
 * ------------------------------------------------------------------------- */
static void test_single_operands(void) {
    { ScKind ex[] = {SC_IDENT};   CHECK(kinds_eq("x",    ex,1), "Single_Identifier"); }
    { ScKind ex[] = {SC_INTEGER}; CHECK(kinds_eq("42",   ex,1), "Single_Integer");    }
    { ScKind ex[] = {SC_STRING};  CHECK(kinds_eq("'hi'", ex,1), "Single_String");     }
    { ScKind ex[] = {SC_REAL};    CHECK(kinds_eq("3.14", ex,1), "Single_Real");       }
}

/* ---------------------------------------------------------------------------
 * Section 2 — Simple binary: a b op
 * ------------------------------------------------------------------------- */
static void test_simple_binary(void) {
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS};
      CHECK(kinds_eq("a + b", ex, 3), "Binary_Add"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_ASSIGN};
      CHECK(kinds_eq("x = y", ex, 3), "Binary_Assign"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_CONCAT};
      CHECK(kinds_eq("a && b", ex, 3), "Binary_Concat"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_OR};
      CHECK(kinds_eq("a || b", ex, 3), "Binary_Or"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_MINUS};
      CHECK(kinds_eq("a - b", ex, 3), "Binary_Minus"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STAR};
      CHECK(kinds_eq("a * b", ex, 3), "Binary_Star"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_SLASH};
      CHECK(kinds_eq("a / b", ex, 3), "Binary_Slash"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PERCENT};
      CHECK(kinds_eq("a % b", ex, 3), "Binary_Percent"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_CARET};
      CHECK(kinds_eq("a ^ b", ex, 3), "Binary_Caret"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PERIOD};
      CHECK(kinds_eq("a . b", ex, 3), "Binary_Period"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_DOLLAR};
      CHECK(kinds_eq("a $ b", ex, 3), "Binary_Dollar"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PIPE};
      CHECK(kinds_eq("a | b", ex, 3), "Binary_Pipe"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_QUESTION};
      CHECK(kinds_eq("a ? b", ex, 3), "Binary_Question"); }
}

/* ---------------------------------------------------------------------------
 * Section 3 — Precedence: higher-prec operators reduce first
 * ------------------------------------------------------------------------- */
static void test_precedence(void) {
    /* a + b * c  ->  a b c * + */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_STAR, SC_PLUS};
      CHECK(kinds_eq("a + b * c", ex, 5), "Prec_MulBeforeAdd"); }

    /* a == b + c  ->  a b c + == */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_PLUS, SC_EQ};
      CHECK(kinds_eq("a == b + c", ex, 5), "Prec_AddBeforeCompare"); }

    /* a || b && c  ->  a b c && || */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_CONCAT, SC_OR};
      CHECK(kinds_eq("a || b && c", ex, 5), "Prec_ConcatBeforeOr"); }

    /* a + b . c  ->  a b c . + */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_PERIOD, SC_PLUS};
      CHECK(kinds_eq("a + b . c", ex, 5), "Prec_DotHigherThanAdd"); }

    /* a * b ^ c  ->  a b c ^ * */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_CARET, SC_STAR};
      CHECK(kinds_eq("a * b ^ c", ex, 5), "Prec_CaretHigherThanMul"); }

    /* x = a == b  ->  x a b == = */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_EQ, SC_ASSIGN};
      CHECK(kinds_eq("x = a == b", ex, 5), "Prec_CompareBeforeAssign"); }

    /* a + b - c  ->  a b + c -   (both prec 7, left-assoc) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS, SC_IDENT, SC_MINUS};
      CHECK(kinds_eq("a + b - c", ex, 5), "Prec_AddSubSameLevel"); }

    /* a * b / c  ->  a b * c /   (both prec 8) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STAR, SC_IDENT, SC_SLASH};
      CHECK(kinds_eq("a * b / c", ex, 5), "Prec_MulDivSameLevel"); }

    /* a == b != c  ->  a b == c !=  (both prec 6, left) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_EQ, SC_IDENT, SC_NE};
      CHECK(kinds_eq("a == b != c", ex, 5), "Prec_EqNeSameLevel"); }

    /* all string compare ops at prec 6 */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_EQ};
      CHECK(kinds_eq("a :==: b", ex, 3), "Prec_StrEq"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_NE};
      CHECK(kinds_eq("a :!=: b", ex, 3), "Prec_StrNe"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_LT};
      CHECK(kinds_eq("a :<: b", ex, 3), "Prec_StrLt"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_GT};
      CHECK(kinds_eq("a :>: b", ex, 3), "Prec_StrGt"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_LE};
      CHECK(kinds_eq("a :<=: b", ex, 3), "Prec_StrLe"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_GE};
      CHECK(kinds_eq("a :>=: b", ex, 3), "Prec_StrGe"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_IDENT};
      CHECK(kinds_eq("a :: b", ex, 3), "Prec_StrIdent"); }
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STR_DIFFER};
      CHECK(kinds_eq("a :!: b", ex, 3), "Prec_StrDiffer"); }

    /* == same precedence as :==: — left-assoc resolution */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_EQ, SC_IDENT, SC_STR_EQ};
      CHECK(kinds_eq("a == b :==: c", ex, 5), "Prec_NumericAndStringCompareSamePrec"); }
}

/* ---------------------------------------------------------------------------
 * Section 4 — Associativity
 * ------------------------------------------------------------------------- */
static void test_associativity(void) {
    /* left: a + b + c  ->  a b + c + */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS, SC_IDENT, SC_PLUS};
      CHECK(kinds_eq("a + b + c", ex, 5), "Assoc_Add_Left"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_STAR, SC_IDENT, SC_STAR};
      CHECK(kinds_eq("a * b * c", ex, 5), "Assoc_Mul_Left"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_CONCAT, SC_IDENT, SC_CONCAT};
      CHECK(kinds_eq("a && b && c", ex, 5), "Assoc_Concat_Left"); }

    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_OR, SC_IDENT, SC_OR};
      CHECK(kinds_eq("a || b || c", ex, 5), "Assoc_Or_Left"); }

    /* right: a ^ b ^ c  ->  a b c ^ ^  (lp=9 < rp=10) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_CARET, SC_CARET};
      CHECK(kinds_eq("a ^ b ^ c", ex, 5), "Assoc_Caret_Right"); }

    /* right: a = b = c  ->  a b c = =  (lp=1 < rp=2) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_ASSIGN, SC_ASSIGN};
      CHECK(kinds_eq("a = b = c", ex, 5), "Assoc_Assign_Right"); }

    /* right: a ^ b ^ c ^ d  ->  a b c d ^ ^ ^ */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_IDENT,
                     SC_CARET, SC_CARET, SC_CARET};
      CHECK(kinds_eq("a ^ b ^ c ^ d", ex, 7), "Assoc_Caret_Right_Chain"); }
}

/* ---------------------------------------------------------------------------
 * Section 5 — Unary operators
 * ------------------------------------------------------------------------- */
static void test_unary(void) {
    /* -x  ->  x unary- */
    {
        ScParseResult r = parse_src("-x");
        CHECK(r.count == 2 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_MINUS &&
              r.tokens[1].is_unary == 1,
              "Unary_Minus");
        sc_parse_free(&r);
    }

    /* ~x  ->  x unary~ */
    {
        ScParseResult r = parse_src("~x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_TILDE &&
              r.tokens[1].is_unary == 1,
              "Unary_Tilde");
        sc_parse_free(&r);
    }

    /* *p  ->  p unary* */
    {
        ScParseResult r = parse_src("*p");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_STAR &&
              r.tokens[1].is_unary == 1,
              "Unary_Star_UnevaluatedPattern");
        sc_parse_free(&r);
    }

    /* +x  ->  x unary+ */
    {
        ScParseResult r = parse_src("+x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_PLUS &&
              r.tokens[1].is_unary == 1,
              "Unary_Plus");
        sc_parse_free(&r);
    }

    /* @x  ->  x unary@ */
    {
        ScParseResult r = parse_src("@x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_AT &&
              r.tokens[1].is_unary == 1,
              "Unary_At");
        sc_parse_free(&r);
    }

    /* &x  ->  x unary& */
    {
        ScParseResult r = parse_src("&x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_AMPERSAND &&
              r.tokens[1].is_unary == 1,
              "Unary_Ampersand");
        sc_parse_free(&r);
    }

    /* $x  ->  x unary$ */
    {
        ScParseResult r = parse_src("$x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_DOLLAR &&
              r.tokens[1].is_unary == 1,
              "Unary_Dollar");
        sc_parse_free(&r);
    }

    /* .x  ->  x unary. */
    {
        ScParseResult r = parse_src(".x");
        CHECK(r.count == 2 &&
              r.tokens[1].kind == SC_PERIOD &&
              r.tokens[1].is_unary == 1,
              "Unary_Period");
        sc_parse_free(&r);
    }

    /* a + -b  ->  a b unary- + */
    {
        ScParseResult r = parse_src("a + -b");
        CHECK(r.count == 4 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_IDENT &&
              r.tokens[2].kind == SC_MINUS && r.tokens[2].is_unary == 1 &&
              r.tokens[3].kind == SC_PLUS  && r.tokens[3].is_unary == 0,
              "Unary_BindsTighterThanBinary");
        sc_parse_free(&r);
    }

    /* --x  ->  x unary- unary- */
    {
        ScParseResult r = parse_src("--x");
        CHECK(r.count == 3 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_MINUS && r.tokens[1].is_unary == 1 &&
              r.tokens[2].kind == SC_MINUS && r.tokens[2].is_unary == 1,
              "Unary_Double_Minus");
        sc_parse_free(&r);
    }

    /* -a * b  ->  a unary- b *  (unary binds tighter than *) */
    {
        ScParseResult r = parse_src("-a * b");
        CHECK(r.count == 4 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_MINUS && r.tokens[1].is_unary == 1 &&
              r.tokens[2].kind == SC_IDENT &&
              r.tokens[3].kind == SC_STAR  && r.tokens[3].is_unary == 0,
              "Unary_NegBeforeMul");
        sc_parse_free(&r);
    }
}

/* ---------------------------------------------------------------------------
 * Section 6 — Parentheses override precedence
 * ------------------------------------------------------------------------- */
static void test_parens(void) {
    /* (a + b) * c  ->  a b + c * */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS, SC_IDENT, SC_STAR};
      CHECK(kinds_eq("(a + b) * c", ex, 5), "Parens_OverridePrecedence"); }

    /* (a + (b * c))  ->  a b c * + */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_STAR, SC_PLUS};
      CHECK(kinds_eq("(a + (b * c))", ex, 5), "Parens_Nested"); }

    /* (a)  ->  a */
    { ScKind ex[] = {SC_IDENT};
      CHECK(kinds_eq("(a)", ex, 1), "Parens_Single"); }

    /* ((a + b)) * c  ->  a b + c * */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS, SC_IDENT, SC_STAR};
      CHECK(kinds_eq("((a + b)) * c", ex, 5), "Parens_DoubleNested"); }

    /* a * (b + c) + d  ->  a b c + * d + */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_PLUS, SC_STAR, SC_IDENT, SC_PLUS};
      CHECK(kinds_eq("a * (b + c) + d", ex, 7), "Parens_MidExpr"); }
}

/* ---------------------------------------------------------------------------
 * Section 7 — Function calls and array refs
 * ------------------------------------------------------------------------- */
static void test_calls(void) {
    /* f()  ->  f CALL(0) */
    {
        ScParseResult r = parse_src("f()");
        CHECK(r.count == 2 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_CALL  &&
              r.tokens[1].arg_count == 0,
              "FunctionCall_NoArgs");
        sc_parse_free(&r);
    }

    /* f(x)  ->  f x CALL(1) */
    {
        ScParseResult r = parse_src("f(x)");
        CHECK(r.count == 3 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_IDENT &&
              r.tokens[2].kind == SC_CALL  &&
              r.tokens[2].arg_count == 1,
              "FunctionCall_OneArg");
        sc_parse_free(&r);
    }

    /* f(x, y)  ->  f x y CALL(2) */
    {
        ScParseResult r = parse_src("f(x, y)");
        CHECK(r.count == 4 &&
              r.tokens[3].kind == SC_CALL &&
              r.tokens[3].arg_count == 2,
              "FunctionCall_TwoArgs");
        sc_parse_free(&r);
    }

    /* f(x, y, z)  ->  f x y z CALL(3) */
    {
        ScParseResult r = parse_src("f(x, y, z)");
        CHECK(r.count == 5 &&
              r.tokens[4].kind == SC_CALL &&
              r.tokens[4].arg_count == 3,
              "FunctionCall_ThreeArgs");
        sc_parse_free(&r);
    }

    /* arr[i]  ->  arr i ARRAY_REF(1) */
    {
        ScParseResult r = parse_src("arr[i]");
        CHECK(r.count == 3 &&
              r.tokens[0].kind == SC_IDENT     &&
              r.tokens[1].kind == SC_IDENT     &&
              r.tokens[2].kind == SC_ARRAY_REF &&
              r.tokens[2].arg_count == 1,
              "ArrayRef_OneIndex");
        sc_parse_free(&r);
    }

    /* arr[i, j]  ->  arr i j ARRAY_REF(2) */
    {
        ScParseResult r = parse_src("arr[i, j]");
        CHECK(r.count == 4 &&
              r.tokens[3].kind == SC_ARRAY_REF &&
              r.tokens[3].arg_count == 2,
              "ArrayRef_TwoIndices");
        sc_parse_free(&r);
    }

    /* f(a + b)  ->  f a b + CALL(1) */
    {
        ScParseResult r = parse_src("f(a + b)");
        CHECK(r.count == 5 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_IDENT &&
              r.tokens[2].kind == SC_IDENT &&
              r.tokens[3].kind == SC_PLUS  &&
              r.tokens[4].kind == SC_CALL  &&
              r.tokens[4].arg_count == 1,
              "FunctionCall_ArgIsExpression");
        sc_parse_free(&r);
    }

    /* f(a, b + c)  ->  f a b c + CALL(2) */
    {
        ScParseResult r = parse_src("f(a, b + c)");
        CHECK(r.count == 6 &&
              r.tokens[5].kind == SC_CALL &&
              r.tokens[5].arg_count == 2,
              "FunctionCall_SecondArgIsExpression");
        sc_parse_free(&r);
    }

    /* f(g(x))  ->  f g x CALL(1) CALL(1) */
    {
        ScParseResult r = parse_src("f(g(x))");
        CHECK(r.count == 5 &&
              r.tokens[3].kind == SC_CALL && r.tokens[3].arg_count == 1 &&
              r.tokens[4].kind == SC_CALL && r.tokens[4].arg_count == 1,
              "FunctionCall_Nested");
        sc_parse_free(&r);
    }
}

/* ---------------------------------------------------------------------------
 * Section 8 — dotck: leading-dot float rewritten
 * ------------------------------------------------------------------------- */
static void test_dotck(void) {
    /* .5  ->  SC_REAL with text "0.5" */
    {
        ScParseResult r = parse_src(".5");
        CHECK(r.count == 1 &&
              r.tokens[0].kind == SC_REAL &&
              strcmp(r.tokens[0].text, "0.5") == 0,
              "Dotck_LeadingDot_Rewritten");
        sc_parse_free(&r);
    }

    /* 3.14  ->  unchanged */
    {
        ScParseResult r = parse_src("3.14");
        CHECK(r.count == 1 &&
              r.tokens[0].kind == SC_REAL &&
              strcmp(r.tokens[0].text, "3.14") == 0,
              "Dotck_NormalFloat_Unchanged");
        sc_parse_free(&r);
    }

    /* .5 + x  ->  "0.5" x + */
    {
        ScParseResult r = parse_src(".5 + x");
        CHECK(r.count == 3 &&
              r.tokens[0].kind == SC_REAL &&
              strcmp(r.tokens[0].text, "0.5") == 0 &&
              r.tokens[2].kind == SC_PLUS,
              "Dotck_InExpression");
        sc_parse_free(&r);
    }
}

/* ---------------------------------------------------------------------------
 * Section 9 — Complex / integration expressions
 * ------------------------------------------------------------------------- */
static void test_complex(void) {
    /* x = a + b * c  ->  x a b c * + = */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_IDENT,
                     SC_STAR, SC_PLUS, SC_ASSIGN};
      CHECK(kinds_eq("x = a + b * c", ex, 7), "Complex_AssignArith"); }

    /* (a + b) * (c - d)  ->  a b + c d - * */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PLUS,
                     SC_IDENT, SC_IDENT, SC_MINUS, SC_STAR};
      CHECK(kinds_eq("(a + b) * (c - d)", ex, 7), "Complex_TwoParenGroups"); }

    /* -f(x)  ->  f x CALL(1) unary- */
    {
        ScParseResult r = parse_src("-f(x)");
        CHECK(r.count == 4 &&
              r.tokens[0].kind == SC_IDENT &&
              r.tokens[1].kind == SC_IDENT &&
              r.tokens[2].kind == SC_CALL  && r.tokens[2].arg_count == 1 &&
              r.tokens[3].kind == SC_MINUS && r.tokens[3].is_unary == 1,
              "Complex_NegatedCall");
        sc_parse_free(&r);
    }

    /* a . b $ c  ->  a b . c $  (both prec 10 left-assoc) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_PERIOD, SC_IDENT, SC_DOLLAR};
      CHECK(kinds_eq("a . b $ c", ex, 5), "Complex_DotDollarLeftAssoc"); }

    /* f(a+b, g(c))  ->  f a b + g c CALL(1) CALL(2) */
    {
        ScParseResult r = parse_src("f(a+b, g(c))");
        CHECK(r.count == 8 &&
              r.tokens[7].kind == SC_CALL && r.tokens[7].arg_count == 2,
              "Complex_CallWithCallArg");
        sc_parse_free(&r);
    }

    /* a || b | c  ->  a b || c |  (|| prec4, | prec3) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_OR, SC_IDENT, SC_PIPE};
      CHECK(kinds_eq("a || b | c", ex, 5), "Complex_OrVsPipe"); }

    /* x = a == b  ->  x a b == =  (assign right-assoc prec 1/2) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_EQ, SC_ASSIGN};
      CHECK(kinds_eq("x = a == b", ex, 5), "Complex_AssignOfComparison"); }

    /* a + b . c * d  ->  a b c . d * + (. prec10 > * prec8 > + prec7) */
    { ScKind ex[] = {SC_IDENT, SC_IDENT, SC_IDENT, SC_PERIOD,
                     SC_IDENT, SC_STAR, SC_PLUS};
      CHECK(kinds_eq("a + b . c * d", ex, 7), "Complex_DotPeriodHighest"); }
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
    test_single_operands();
    test_simple_binary();
    test_precedence();
    test_associativity();
    test_unary();
    test_parens();
    test_calls();
    test_dotck();
    test_complex();

    int total = g_pass + g_fail;
    printf("%d/%d %s\n", g_pass, total, g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
