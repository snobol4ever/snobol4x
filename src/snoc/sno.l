%{
/*
 * sno.l — SNOBOL4 flex lexer for snoc
 *
 * join_file() reads the raw source, strips comments, joins continuations,
 * handles -INCLUDE, and prefixes label-bearing lines with \x01 (LABEL_MARK).
 * Flex sees a clean one-statement-per-line buffer via YY_INPUT.
 *
 * Space is returned as a _ token — it is the concat operator.
 * IDENT immediately followed by '(' has no space between, so it is
 * unambiguous as a function call vs concat-with-group.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "snoc.h"
#include "sno.tab.h"

/* ---- error ---- */
int   snoc_nerrors  = 0;
char *yyfilename    = (char *)"<stdin>";
int   lineno_stmt   = 1;

void snoc_error(int lineno, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", yyfilename, lineno);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    snoc_nerrors++;
}

/* ---- include dirs ---- */
#define MAX_INC_DIRS 64
static char *inc_dirs[MAX_INC_DIRS];
static int   n_inc_dirs = 0;

void snoc_add_include_dir(const char *d) {
    if (n_inc_dirs < MAX_INC_DIRS-1) inc_dirs[n_inc_dirs++] = strdup(d);
}

/* ---- joined source buffer fed to flex via YY_INPUT ---- */
#define JBUF_MAX (8<<20)
static char  *jbuf = NULL;
static size_t jlen = 0;
static size_t jpos = 0;

#define YY_INPUT(buf, result, max_size) \
    do { \
        size_t _n = (jlen - jpos < (size_t)(max_size)) \
                    ? jlen - jpos : (size_t)(max_size); \
        if (!_n) { (result) = YY_NULL; } \
        else { memcpy((buf), jbuf+jpos, _n); jpos += _n; (result) = (int)_n; } \
    } while(0)

#define LABEL_MARK '\x01'

/* ---- join_file: assemble jbuf from fp ---- */
static void join_file(FILE *fp, const char *fname);

static void jbuf_append(const char *s, int n) {
    if (jlen + n + 1 < JBUF_MAX) { memcpy(jbuf+jlen, s, n); jlen += n; }
}

static void join_file(FILE *fp, const char *fname) {
    char raw[65536], logical[65536];
    int  llen = 0;

    /* flush_logical: write logical line to jbuf with LABEL_MARK if needed */
    #define FLUSH() do { \
        if (!llen) break; \
        logical[llen] = '\0'; \
        if (logical[0] != ' ' && logical[0] != '\t') \
            jbuf[jlen++] = LABEL_MARK; \
        jbuf_append(logical, llen); \
        jbuf[jlen++] = '\n'; \
        llen = 0; \
    } while(0)

    while (fgets(raw, sizeof raw, fp)) {
        int n = (int)strlen(raw);
        while (n > 0 && (raw[n-1]=='\n'||raw[n-1]=='\r')) n--;
        raw[n] = '\0';
        if (n == 0) { FLUSH(); continue; }

        char c1 = raw[0];

        if (c1=='*'||c1=='#'||c1=='!'||c1=='|'||c1==';') {
            FLUSH(); continue;
        }

        if (c1 == '-') {
            FLUSH();
            /* -INCLUDE */
            int p = 1;
            while (p<n && (raw[p]==' '||raw[p]=='\t')) p++;
            if (strncasecmp(raw+p, "INCLUDE", 7) == 0) {
                p += 7;
                while (p<n && (raw[p]==' '||raw[p]=='\t')) p++;
                char iname[4096]; int fi = 0;
                if (p<n && (raw[p]=='\''||raw[p]=='"')) {
                    char q = raw[p++];
                    while (p<n && raw[p]!=q) iname[fi++] = raw[p++];
                } else {
                    while (p<n && raw[p]!=' '&&raw[p]!='\t') iname[fi++] = raw[p++];
                }
                iname[fi] = '\0';
                FILE *inc = fopen(iname, "r");
                if (!inc) {
                    char path[4096];
                    for (int i = 0; i < n_inc_dirs && !inc; i++) {
                        snprintf(path, sizeof path, "%s/%s", inc_dirs[i], iname);
                        inc = fopen(path, "r");
                    }
                }
                if (!inc) {
                    fprintf(stderr, "%s: cannot open include '%s'\n", fname, iname);
                    snoc_nerrors++;
                } else { join_file(inc, iname); fclose(inc); }
            }
            continue;  /* other control cards: skip */
        }

        if (c1=='+' || c1=='.') {
            /* continuation: strip trailing WS from logical, append from col 2 */
            while (llen>0 && (logical[llen-1]==' '||logical[llen-1]=='\t')) llen--;
            int src = 1;
            while (src<n && (raw[src]==' '||raw[src]=='\t')) src++;
            int copy = n - src;
            if (llen + copy + 2 < (int)sizeof logical) {
                logical[llen++] = ' ';
                memcpy(logical+llen, raw+src, copy);
                llen += copy;
            }
            continue;
        }

        /* normal line */
        FLUSH();
        if (n < (int)sizeof logical) { memcpy(logical, raw, n); llen = n; }
    }
    FLUSH();
    #undef FLUSH
}

%}

%option noyywrap never-interactive yylineno
%x GT

ALPHA   [A-Za-z\x80-\xff]
/* IDENT in expression context: standard SNOBOL4 identifier characters */
IDENT   {ALPHA}([A-Za-z0-9._/\x80-\xff])*
/* SLABEL: special label — any printable non-whitespace char sequence starting with alpha,
 * but occurring ONLY after \x01 (LABEL_MARK injected by join_file).
 * This allows labels like pp_:() pp_:<> pp_! ss_# ss_$ pp_, pp_| pp_[] etc. */
SLABEL_CONT  [^ \t\n\x01]+
DIGIT   [0-9]
WS      [ \t]+

%%

    /* ---- label marker injected by join_file ---- */
    /* A SNOBOL4 label starts with \x01 and runs to the first whitespace.
     * beauty.sno uses labels like: pp_:() pp_:<> pp_! ss_# ss_,0 pp_[] etc.
     * We must NOT tokenise operators inside the label name. */
\x01[^ \t\n]+   {
                    char *lab = intern(yytext+1);
                    if (strcasecmp(lab,"END") == 0) { jpos=jlen; yylval.sval=lab; return END_LABEL; }
                    lineno_stmt = yylineno;
                    yylval.sval = lab; return LABEL;
                }

{WS}/[A-Za-z0-9'\"(+\-*/$^@&.~]  { return _; }
{WS}            { /* space before operator or EOL — skip */ }
\n              { BEGIN(INITIAL); lineno_stmt = yylineno; return NEWLINE; }

    /* ---- strings ---- */
\'[^\']*\'      { yylval.sval = intern_n(yytext+1, yyleng-2); return STR; }
\"[^\"]*\"      { yylval.sval = intern_n(yytext+1, yyleng-2); return STR; }

    /* ---- numbers ---- */
{DIGIT}+"."{DIGIT}*([eE][+-]?{DIGIT}+)?  { yylval.dval=atof(yytext); return REAL; }
{DIGIT}+[eE][+-]?{DIGIT}+                { yylval.dval=atof(yytext); return REAL; }
{DIGIT}+        { yylval.ival = atol(yytext); return INT; }

    /* ---- keyword reference: &IDENT ---- */
"&"{IDENT}      { yylval.sval = intern(yytext+1); return KEYWORD; }
"&"             { yylval.sval = intern("&"); return AMP; }

    /* ---- identifiers ---- */
{IDENT}/"("     { yylval.sval = intern(yytext); return IDENT; }
{IDENT}         { yylval.sval = intern(yytext); return IDENT; }

    /* ---- operators ---- */
"**"            { return STARSTAR; }
"~"             { return DOT; }        /* ~ is alias for . */
":"             { BEGIN(GT); return COLON; }
"="             { return EQ; }
"("             { return LPAREN; }
")"             { return RPAREN; }
"["             { return LBRACKET; }
"]"             { return RBRACKET; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return STAR; }
"/"             { return SLASH; }
"^"             { return CARET; }
"|"             { return PIPE; }
"."             { return DOT; }
"$"             { return DOLLAR; }
","             { return COMMA; }
"@"             { return AT; }
";"[^\n]*       { return NEWLINE; }  /* ; terminates stmt; rest of line is discarded */

.               { snoc_error(yylineno, "unexpected char '%s'", yytext); }

    /* ---- goto field ---- */
<GT>{WS}        { }
<GT>[Ss]        { return SGOTO; }
<GT>[Ff]        { return FGOTO; }
<GT>"("|"<"     { return LPAREN; }
<GT>")"|">"     { return RPAREN; }
<GT>"$""("      {
                    /* computed goto: $(...) — swallow until matching ')' */
                    int depth=1; int c;
                    while (depth>0 && (c=input())!=EOF) {
                        if (c=='(') depth++;
                        else if (c==')') depth--;
                    }
                    yylval.sval = intern("$COMPUTED");
                    return IDENT;
                }
<GT>"$"\'[^\']*\'  {
                    /* computed goto: $'label' — label is the string content */
                    /* yytext is: $'label'  → extract between quotes */
                    char *q = strchr(yytext, '\'');
                    int n = (int)(strchr(q+1,'\'') - (q+1));
                    yylval.sval = intern_n(q+1, n);
                    return IDENT;
                }
<GT>"$"         { return DOLLAR; }
<GT>\'[^\']*\'  { yylval.sval=intern_n(yytext+1,yyleng-2); return STR; }
<GT>\"[^\"]*\"  { yylval.sval=intern_n(yytext+1,yyleng-2); return STR; }
<GT>{DIGIT}+    { yylval.ival=atol(yytext); return INT; }
<GT>"&"{IDENT}  { yylval.sval=intern(yytext+1); return KEYWORD; }
    /* Label names in goto field may contain special chars like ():!#$%&|^~?@,[]<>
     * e.g. :(pp_:()) :($'pp_,1') :(pp_[]) etc.
     * Match as IDENT: anything non-whitespace, non-paren, non-semicolon, non-newline. */
<GT>[^ \t\n(),;><]+  { yylval.sval=intern(yytext); return IDENT; }
<GT>","         { return COMMA; }
<GT>";"[^\n]*   { BEGIN(INITIAL); return NEWLINE; }  /* ; ends stmt, rest of line discarded */
<GT>\n          { BEGIN(INITIAL); lineno_stmt=yylineno; return NEWLINE; }

%%

/* ---- yyerror ---- */
void yyerror(const char *msg) { snoc_error(yylineno, "%s", msg); }

/* ---- parse entry ---- */
extern int  yyparse(void);
extern void snoc_parse_init(void);
Program *parsed_program = NULL;

Program *snoc_parse(FILE *f, const char *filename) {
    yyfilename  = (char *)filename;
    lineno_stmt = 1;

    jbuf = malloc(JBUF_MAX);
    if (!jbuf) { fprintf(stderr,"snoc: out of memory\n"); exit(1); }
    jlen = 0; jpos = 0;

    join_file(f, filename);
    jbuf[jlen] = '\0';

    /* DEBUG: dump first 10 lines of jbuf */
    if (getenv("SNOC_DEBUG")) {
        int lc=0; size_t i=0;
        fprintf(stderr,"=== JBUF first 10 lines ===\n");
        while (i<jlen && lc<10) {
            fputc(jbuf[i]==LABEL_MARK?'@':jbuf[i], stderr);
            if (jbuf[i]=='\n') lc++;
            i++;
        }
        fprintf(stderr,"===\n");
    }

    yy_switch_to_buffer(yy_create_buffer(NULL, YY_BUF_SIZE));

    snoc_parse_init();
    yyparse();

    free(jbuf); jbuf = NULL;
    return parsed_program;
}

