%{
/*
 * raku.l — Tiny-Raku flex lexer
 *
 * Subset: integer/float/string literals, $scalar/@array sigils,
 * keywords, arithmetic, string concat (~), comparisons, ranges (..),
 * logic, pointy block (->), semicolons required.
 *
 * Single-line comments: # to end of line.
 * Double-quoted strings: flat literal (no interpolation in Phase 1).
 * Single-quoted strings: literal.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "raku_ast.h"
#include "raku.tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char raku_strbuf[65536];
static int  raku_strpos;

void raku_lex_error(const char *msg) {
    fprintf(stderr, "raku lex error line %d: %s\n", raku_yylineno, msg);
}
int raku_get_lineno(void) { return raku_yylineno; }
%}

%option noyywrap noinput nounput
%option prefix="raku_yy"
%option yylineno

%x STR_DQ STR_SQ

DIGIT    [0-9]
ALPHA    [A-Za-z_]
ALNUM    [A-Za-z0-9_]
EXP      [eE][+-]?{DIGIT}+
WS       [ \t\r]

%%

{WS}+           { /* skip whitespace */ }
\n              { /* raku_yylineno auto-incremented by %option yylineno */ }
"#"[^\n]*       { /* single-line comment */ }

    /* ── Keywords ───────────────────────────────────────────────── */
"my"            { return KW_MY; }
"say"           { return KW_SAY; }
"print"         { return KW_PRINT; }
"if"            { return KW_IF; }
"else"          { return KW_ELSE; }
"elsif"         { return KW_ELSIF; }
"while"         { return KW_WHILE; }
"for"           { return KW_FOR; }
"given"         { return KW_GIVEN; }
"when"          { return KW_WHEN; }
"default"       { return KW_DEFAULT; }
"sub"           { return KW_SUB; }
"gather"        { return KW_GATHER; }
"take"          { return KW_TAKE; }
"return"        { return KW_RETURN; }
"True"          { raku_yylval.ival = 1; return LIT_INT; }
"False"         { raku_yylval.ival = 0; return LIT_INT; }
"eq"            { return OP_SEQ; }
"ne"            { return OP_SNE; }
"div"           { return OP_DIV; }

    /* ── Operators (longest first) ───────────────────────────────── */
"..^"           { return OP_RANGE_EX; }
".."            { return OP_RANGE; }
"->"            { return OP_ARROW; }
"=="            { return OP_EQ; }
"!="            { return OP_NE; }
"<="            { return OP_LE; }
">="            { return OP_GE; }
"&&"            { return OP_AND; }
"||"            { return OP_OR; }
":="            { return OP_BIND; }
"+"             { return '+'; }
"-"             { return '-'; }
"*"             { return '*'; }
"/"             { return '/'; }
"%"             { return '%'; }
"~"             { return '~'; }
"<"             { return '<'; }
">"             { return '>'; }
"!"             { return '!'; }
"="             { return '='; }

    /* ── Punctuation ─────────────────────────────────────────────── */
"{"             { return '{'; }
"}"             { return '}'; }
"("             { return '('; }
")"             { return ')'; }
"["             { return '['; }
"]"             { return ']'; }
";"             { return ';'; }
","             { return ','; }

    /* ── Numeric literals ────────────────────────────────────────── */
{DIGIT}+"."{DIGIT}+{EXP}?  { raku_yylval.dval = atof(yytext); return LIT_FLOAT; }
{DIGIT}+{EXP}               { raku_yylval.dval = atof(yytext); return LIT_FLOAT; }
{DIGIT}+                    { raku_yylval.ival = atol(yytext); return LIT_INT; }

    /* ── Sigil variables ─────────────────────────────────────────── */
"$_"            { raku_yylval.sval = strdup("$_"); return VAR_SCALAR; }
"$"{ALPHA}{ALNUM}*  { raku_yylval.sval = strdup(yytext); return VAR_SCALAR; }
"@"{ALPHA}{ALNUM}*  { raku_yylval.sval = strdup(yytext); return VAR_ARRAY; }

    /* ── Identifiers (bare words — sub names, builtins) ─────────── */
{ALPHA}{ALNUM}* { raku_yylval.sval = strdup(yytext); return IDENT; }

    /* ── Double-quoted string: flat literal or interpolated ──────── */
    /* If the buffer contains '$', emit LIT_INTERP_STR (RK-12).     */
    /* The lowerer splits on $var boundaries into an E_CAT chain.    */
"\""            { raku_strpos = 0; BEGIN(STR_DQ); }
<STR_DQ>"\"" {
    raku_strbuf[raku_strpos] = '\0';
    raku_yylval.sval = strdup(raku_strbuf);
    BEGIN(INITIAL);
    if (strchr(raku_strbuf, '$') != NULL)
        return LIT_INTERP_STR;
    return LIT_STR;
}
<STR_DQ>"\\n"  { raku_strbuf[raku_strpos++] = '\n'; }
<STR_DQ>"\\t"  { raku_strbuf[raku_strpos++] = '\t'; }
<STR_DQ>"\\\\" { raku_strbuf[raku_strpos++] = '\\'; }
<STR_DQ>"\\\"" { raku_strbuf[raku_strpos++] = '"'; }
<STR_DQ>\n     { raku_strbuf[raku_strpos++] = '\n'; }
<STR_DQ>.      { raku_strbuf[raku_strpos++] = yytext[0]; }

    /* ── Single-quoted string (strictly literal) ─────────────────── */
"'"             { raku_strpos = 0; BEGIN(STR_SQ); }
<STR_SQ>"'"    {
    raku_strbuf[raku_strpos] = '\0';
    raku_yylval.sval = strdup(raku_strbuf);
    BEGIN(INITIAL);
    return LIT_STR;
}
<STR_SQ>"\\'"  { raku_strbuf[raku_strpos++] = '\''; }
<STR_SQ>"\\\\" { raku_strbuf[raku_strpos++] = '\\'; }
<STR_SQ>\n     { raku_strbuf[raku_strpos++] = '\n'; }
<STR_SQ>.      { raku_strbuf[raku_strpos++] = yytext[0]; }

.               {
    char buf[64];
    snprintf(buf, sizeof buf, "unexpected char '%c'", yytext[0]);
    raku_lex_error(buf);
}

%%
