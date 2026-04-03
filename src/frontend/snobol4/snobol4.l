%{
/*
 * snobol4.l - SNOBOL4 one-pass flex lexer
 *
 * One character stream. No line accumulation. No sub-buffers.
 * Include files handled by yypush/pop_buffer_state.
 *
 * Card format handled as rules:
 *   col-1 * ! | ;   -> comment: skip to newline
 *   col-1 -         -> CTL: INCLUDE/COPY handled; others skipped
 *   col-1 + or .    -> continuation: swallow newline, keep scanning body
 *   col-1 space/tab -> body continues (label was empty)
 *   col-1 other     -> label token, then body
 *
 * Start conditions:
 *   INITIAL   - col-1 character dispatch
 *   LABEL     - reading label identifier
 *   BODY      - statement body tokens
 *   GT        - goto field (raw text after ':')
 *   STR1      - single-quoted string
 *   STR2      - double-quoted string
 *   SKIP      - skipping to end of line (comment/CTL)
 *   INCL      - reading filename after -INCLUDE/-COPY
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "scrip_cc.h"
#include "snobol4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

void sno_error(int, const char *, ...);
char *inc_dirs[64];
int   n_inc = 0;

static int   lineno = 1;
static char  strbuf[65536];
static int   strpos;
static int   depth;   /* paren depth in BODY for ':' detection */

static Token mktok(TokKind k, const char *sv, long iv, double dv) {
    Token t; t.kind=k; t.sval=sv; t.ival=iv; t.dval=dv; t.lineno=lineno;
    return t;
}

%}

%option reentrant noyywrap never-interactive stack
%option extra-type="void *"

%x LABEL BODY GT STR1 STR2 SKIP INCL

DIGIT   [0-9]
ALPHA   [A-Za-z\x80-\xFF]
IDCONT  [A-Za-z0-9_.\x80-\xFF]
ALNUM   [A-Za-z0-9_]
EXP     [eE][+-]?{DIGIT}+
WS      [ \t]

%%

    /* ---- INITIAL: col-1 dispatch ---- */

<INITIAL>\n             { lineno++; /* blank line */ }
<INITIAL>[*!|;]         { BEGIN(SKIP); }
<INITIAL>[ \t]          { BEGIN(BODY); depth=0; }

<INITIAL>"-"            {
    /* CTL line: check for INCLUDE/COPY */
    BEGIN(INCL);
}

<INITIAL>[+.]           {
    /* continuation: discard, stay in BODY (don't emit STMT_END) */
    BEGIN(BODY);
}

<INITIAL>[^ \t\n*!|;+.\-] {
    /* col-1 non-space: start of label */
    strbuf[0] = yytext[0]; strpos = 1;
    BEGIN(LABEL);
}

    /* ---- LABEL: read rest of label ---- */

<LABEL>[^ \t\n]+        {
    int take = yyleng < (int)sizeof(strbuf)-strpos-1
               ? yyleng : (int)sizeof(strbuf)-strpos-1;
    memcpy(strbuf+strpos, yytext, take); strpos += take;
    strbuf[strpos] = '\0';
    const char *sv = intern(strbuf);
    BEGIN(BODY); depth = 0;
    return T_LABEL;   /* sval set via yyextra trick — see flex_lex_next */
}

<LABEL>[ \t]            {
    strbuf[strpos] = '\0';
    const char *sv = intern(strbuf);
    BEGIN(BODY); depth = 0;
    return T_LABEL;
}

<LABEL>\n               {
    /* label-only line */
    strbuf[strpos] = '\0';
    lineno++;
    BEGIN(INITIAL);
    return T_LABEL;
    /* STMT_END emitted next call via INITIAL newline path — actually
       parse.y handles label-only via T_STMT_END; emit it now */
}

    /* ---- BODY: statement body ---- */

<BODY>[ \t]+            { /* discard whitespace */ }

<BODY>\n                {
    lineno++; BEGIN(INITIAL);
    return T_STMT_END;
}

<BODY>";"               {
    /* inline semicolon: end statement, next char is col-1 of next stmt */
    depth = 0;
    return T_STMT_END;
}

<BODY>":"               {
    if (depth == 0) { BEGIN(GT); }
    /* else: colon inside parens not used in SNOBOL4 */
}

<BODY>&{ALPHA}{ALNUM}*  {
    strbuf[0]='\0'; strncat(strbuf, yytext+1, sizeof(strbuf)-1);
    return T_KEYWORD;
}
<BODY>"&"               { return T_AMP; }

<BODY>{ALPHA}{IDCONT}*  {
    strbuf[0]='\0'; strncat(strbuf, yytext, sizeof(strbuf)-1);
    return strcasecmp(strbuf,"END")==0 ? T_END : T_IDENT;
}

<BODY>{DIGIT}+"."{DIGIT}+{EXP}? |
<BODY>{DIGIT}+{EXP}             {
    strbuf[0]='\0'; strncat(strbuf, yytext, sizeof(strbuf)-1);
    return T_REAL;
}
<BODY>{DIGIT}+                  {
    strbuf[0]='\0'; strncat(strbuf, yytext, sizeof(strbuf)-1);
    return T_INT;
}

<BODY>\'                { strpos=0; BEGIN(STR1); }
<BODY>\"                { strpos=0; BEGIN(STR2); }

<BODY>"**"              { return T_STARSTAR; }
<BODY>"+"               { return T_PLUS;     }
<BODY>"-"               { return T_MINUS;    }
<BODY>"*"               { return T_STAR;     }
<BODY>"/"               { return T_SLASH;    }
<BODY>"%"               { return T_PCT;      }
<BODY>"^"               { return T_CARET;    }
<BODY>"!"               { return T_BANG;     }
<BODY>"@"               { return T_AT;       }
<BODY>"~"               { return T_TILDE;    }
<BODY>"$"               { return T_DOLLAR;   }
<BODY>"."               { return T_DOT;      }
<BODY>"#"               { return T_HASH;     }
<BODY>"|"               { return T_PIPE;     }
<BODY>"="               { return T_EQ;       }
<BODY>"?"               { return T_QMARK;    }
<BODY>","               { return T_COMMA;    }
<BODY>"("               { depth++; return T_LPAREN;   }
<BODY>")"               { depth--; return T_RPAREN;   }
<BODY>"["               { depth++; return T_LBRACKET; }
<BODY>"]"               { depth--; return T_RBRACKET; }
<BODY>"<"               { depth++; return T_LANGLE;   }
<BODY>">"               { depth--; return T_RANGLE;   }
<BODY>.                 { sno_error(lineno,"unexpected char '%s'",yytext); }

    /* ---- STR1: single-quoted string ---- */

<STR1>[^'\n]*           {
    int take = yyleng < (int)sizeof(strbuf)-strpos-1
               ? yyleng : (int)sizeof(strbuf)-strpos-1;
    memcpy(strbuf+strpos, yytext, take); strpos += take;
}
<STR1>\'\'              {
    /* escaped quote: '' inside single-quoted string */
    if (strpos < (int)sizeof(strbuf)-1) strbuf[strpos++] = '\'';
}
<STR1>\'                {
    strbuf[strpos] = '\0';
    BEGIN(BODY);
    return T_STR;
}
<STR1>\n                {
    sno_error(lineno,"unterminated string"); lineno++; BEGIN(BODY);
}

    /* ---- STR2: double-quoted string ---- */

<STR2>[^\"\n]*          {
    int take = yyleng < (int)sizeof(strbuf)-strpos-1
               ? yyleng : (int)sizeof(strbuf)-strpos-1;
    memcpy(strbuf+strpos, yytext, take); strpos += take;
}
<STR2>\"\"              {
    if (strpos < (int)sizeof(strbuf)-1) strbuf[strpos++] = '"';
}
<STR2>\"                {
    strbuf[strpos] = '\0';
    BEGIN(BODY);
    return T_STR;
}
<STR2>\n                {
    sno_error(lineno,"unterminated string"); lineno++; BEGIN(BODY);
}

    /* ---- GT: goto field ---- */

<GT>[ \t]+              { /* skip leading/trailing ws */ }
<GT>[^ \t\n;]+          {
    strbuf[0]='\0'; strncat(strbuf, yytext, sizeof(strbuf)-1);
    return T_GOTO;
}
<GT>";"                 {
    /* semicolon after goto: end statement, next is col-1 */
    depth = 0;
    BEGIN(BODY);   /* next char after ; is NOT col-1, it's inline */
    return T_STMT_END;
}
<GT>\n                  {
    lineno++; BEGIN(INITIAL);
    return T_STMT_END;
}

    /* ---- SKIP: comment or unknown CTL line ---- */

<SKIP>[^\n]*            { /* discard */ }
<SKIP>\n                { lineno++; BEGIN(INITIAL); }

    /* ---- INCL: -INCLUDE / -COPY filename ---- */

<INCL>[ \t]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][ \t]* |
<INCL>[ \t]*[Cc][Oo][Pp][Yy][ \t]*             { /* keyword consumed */ }

<INCL>\'[^'\n]*\'   |
<INCL>\"[^\"\n]*\"  {
    char iname[4096];
    int fi = yyleng-2; if(fi>=(int)sizeof iname) fi=(int)sizeof iname-1;
    memcpy(iname,yytext+1,fi); iname[fi]='\0';
    FILE *inc = fopen(iname,"r");
    if(!inc){char p[4096];for(int i=0;i<n_inc&&!inc;i++){snprintf(p,sizeof p,"%s/%s",inc_dirs[i],iname);inc=fopen(p,"r");}}
    if(!inc) sno_error(lineno,"cannot open include '%s'",iname);
    else yypush_buffer_state(yy_create_buffer(inc,YY_BUF_SIZE,yyscanner),yyscanner);
    BEGIN(INITIAL);
}
<INCL>[^ \t\n'\"]+  {
    char iname[4096];
    int fi=yyleng; if(fi>=(int)sizeof iname) fi=(int)sizeof iname-1;
    memcpy(iname,yytext,fi); iname[fi]='\0';
    FILE *inc = fopen(iname,"r");
    if(!inc){char p[4096];for(int i=0;i<n_inc&&!inc;i++){snprintf(p,sizeof p,"%s/%s",inc_dirs[i],iname);inc=fopen(p,"r");}}
    if(!inc) sno_error(lineno,"cannot open include '%s'",iname);
    else yypush_buffer_state(yy_create_buffer(inc,YY_BUF_SIZE,yyscanner),yyscanner);
    BEGIN(INITIAL);
}
<INCL>[^\n]*\n      { lineno++; BEGIN(INITIAL); /* no filename found, skip */ }

    /* ---- EOF: pop include stack or done ---- */

<<EOF>>             {
    if (YY_CURRENT_BUFFER) yypop_buffer_state(yyscanner);
    if (!YY_CURRENT_BUFFER) return T_EOF;
}

%%

/* ---- Public interface ---- */

void sno_error(int ln, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr,"snobol4:%d: error: ",ln);
    va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fputc('\n',stderr);
}

/* Lex wraps a single reentrant scanner + strbuf pointer for sval. */

void flex_lex_open(Lex *lx, FILE *f, const char *fname) {
    yyscan_t sc;
    yylex_init(&sc);
    yy_switch_to_buffer(yy_create_buffer(f, YY_BUF_SIZE, sc), sc);
    lineno = 1;
    lx->_scanner = sc;
    lx->_extra   = NULL;
    (void)fname;
}

Token flex_lex_next(Lex *lx) {
    yyscan_t sc = (yyscan_t)lx->_scanner;
    for (;;) {
        int r = yylex(sc);
        Token t;
        t.lineno = lineno;
        t.ival   = 0;
        t.dval   = 0.0;
        t.sval   = NULL;
        t.kind   = (TokKind)r;

        switch (r) {
        case T_EOF:
            t.kind = T_EOF; return t;

        case T_LABEL:
            t.sval = intern(strbuf);
            t.ival = strcasecmp(strbuf,"END")==0 ? 1 : 0;
            return t;

        case T_IDENT: case T_END: case T_KEYWORD:
        case T_GOTO:
            t.sval = intern(strbuf);
            return t;

        case T_STR:
            t.sval = intern(strbuf);
            return t;

        case T_INT:
            t.ival = atol(strbuf);
            return t;

        case T_REAL:
            t.sval = intern(strbuf);
            t.dval = atof(strbuf);
            return t;

        default:
            return t;
        }
    }
}

void flex_lex_destroy(Lex *lx) {
    if (lx->_scanner) {
        yylex_destroy((yyscan_t)lx->_scanner);
        lx->_scanner = NULL;
    }
}

void lex_open_str(Lex *lx, const char *s, int len, int ln) {
    yyscan_t sc;
    yylex_init(&sc);
    yy_scan_bytes(s, len, sc);
    lineno = ln;
    /* yy_push_state is static in this TU — valid to call from user section */
    yy_push_state(BODY, sc);
    lx->_scanner = sc;
    lx->_extra   = NULL;
}

Token lex_next(Lex *lx) {
    if (lx->peeked) { lx->peeked=0; return lx->peek; }
    return flex_lex_next(lx);
}
Token lex_peek(Lex *lx) {
    if (!lx->peeked) { lx->peek=lex_next(lx); lx->peeked=1; }
    return lx->peek;
}
int  lex_at_end(Lex *lx) { return lex_peek(lx).kind==T_EOF; }
void lex_destroy(Lex *lx) { flex_lex_destroy(lx); }

extern Program *parse_program_tokens(Lex *stream);

Program *sno_parse(FILE *f, const char *fname) {
    Lex lx; memset(&lx,0,sizeof lx);
    flex_lex_open(&lx,f,fname);
    Program *prog = parse_program_tokens(&lx);
    flex_lex_destroy(&lx);
    return prog;
}

void sno_reset(void) {}
