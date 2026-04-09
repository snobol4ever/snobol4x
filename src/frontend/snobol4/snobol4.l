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
#include "snobol4.tab.h"
#define T_EOF SNOBOL4_EOF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

void sno_error(int, const char *, ...);
char *inc_dirs[64];
int   n_inc = 0;
int   sno_nerrors = 0;

static int   lineno = 1;
static char  strbuf[65536];
static int   strpos;


static Token mktok(int k, const char *sv, long iv, double dv) {
    Token t; t.kind=k; t.sval=sv; t.ival=iv; t.dval=dv; t.lineno=lineno;
    return t;
}

%}

%option reentrant noyywrap never-interactive stack
%option extra-type="void *"

%x LABEL LABEL_DONE BODY BODY_START GT STR1 STR2 SKIP INCL

DIGIT   [0-9]
ALPHA   [A-Za-z\x80-\xFF]
IDCONT  [A-Za-z0-9_.\x80-\xFF]
ALNUM   [A-Za-z0-9_]
EXP     [eE][+-]?{DIGIT}+
WS      [ \t]
CONT    \n[+.][ \t]*
W       ({WS}|{CONT})+
G       ({WS}|{CONT})*

%%

    /* ---- INITIAL: col-1 dispatch ---- */

<INITIAL>\n             { lineno++; /* blank line */ }
<INITIAL>[*!|;]         { BEGIN(SKIP); }
<INITIAL>[ \t]          { BEGIN(BODY_START); }

<INITIAL>"-"[Ii][Nn][Cc][Ll][Uu][Dd][Ee][ \t]+'[^'\n]+'[^\n]*\n  {
    /* -include 'filename' — single-quoted, whole line consumed */
    char *q1 = strchr(yytext,'\''); char *q2 = strrchr(yytext,'\'');
    lineno++;
    if(q1 && q2 && q2>q1){
        char iname[4096]; int fi=(int)(q2-q1-1);
        if(fi>=(int)sizeof iname)fi=(int)sizeof iname-1;
        memcpy(iname,q1+1,fi);iname[fi]='\0';
        char rpath[4096]; rpath[0]='\0';
        FILE *inc=fopen(iname,"r");
        if(inc){snprintf(rpath,sizeof rpath,"%s",iname);}
        if(!inc){char p[4096];for(int i=0;i<n_inc&&!inc;i++){snprintf(p,sizeof p,"%s/%s",inc_dirs[i],iname);if((inc=fopen(p,"r")))snprintf(rpath,sizeof rpath,"%s",p);}}
        if(!inc)sno_error(lineno,"cannot open include '%s'",iname);
        else {
            /* add resolved file's directory so its own -INCLUDEs resolve */
            char *sl=strrchr(rpath,'/');
            if(sl){char *d=strndup(rpath,(size_t)(sl-rpath));sno_add_include_dir(d);}
            yypush_buffer_state(yy_create_buffer(inc,YY_BUF_SIZE,yyscanner),yyscanner);
        }
    }
    /* stay in INITIAL — next char is first char of included file */
}
<INITIAL>"-"[Ii][Nn][Cc][Ll][Uu][Dd][Ee][ \t]+\"[^\"\n]+\"[^\n]*\n  {
    /* -include "filename" — double-quoted */
    char *q1 = strchr(yytext,'"'); char *q2 = strrchr(yytext,'"');
    lineno++;
    if(q1 && q2 && q2>q1){
        char iname[4096]; int fi=(int)(q2-q1-1);
        if(fi>=(int)sizeof iname)fi=(int)sizeof iname-1;
        memcpy(iname,q1+1,fi);iname[fi]='\0';
        char rpath[4096]; rpath[0]='\0';
        FILE *inc=fopen(iname,"r");
        if(inc){snprintf(rpath,sizeof rpath,"%s",iname);}
        if(!inc){char p[4096];for(int i=0;i<n_inc&&!inc;i++){snprintf(p,sizeof p,"%s/%s",inc_dirs[i],iname);if((inc=fopen(p,"r")))snprintf(rpath,sizeof rpath,"%s",p);}}
        if(!inc)sno_error(lineno,"cannot open include '%s'",iname);
        else {
            char *sl=strrchr(rpath,'/');
            if(sl){char *d=strndup(rpath,(size_t)(sl-rpath));sno_add_include_dir(d);}
            yypush_buffer_state(yy_create_buffer(inc,YY_BUF_SIZE,yyscanner),yyscanner);
        }
    }
}
<INITIAL>"-"[Cc][Oo][Pp][Yy][ \t]+'[^'\n]+'[^\n]*\n  {
    /* -copy 'filename' — single-quoted */
    char *q1 = strchr(yytext,'\''); char *q2 = strrchr(yytext,'\'');
    lineno++;
    if(q1 && q2 && q2>q1){
        char iname[4096]; int fi=(int)(q2-q1-1);
        if(fi>=(int)sizeof iname)fi=(int)sizeof iname-1;
        memcpy(iname,q1+1,fi);iname[fi]='\0';
        char rpath[4096]; rpath[0]='\0';
        FILE *inc=fopen(iname,"r");
        if(inc){snprintf(rpath,sizeof rpath,"%s",iname);}
        if(!inc){char p[4096];for(int i=0;i<n_inc&&!inc;i++){snprintf(p,sizeof p,"%s/%s",inc_dirs[i],iname);if((inc=fopen(p,"r")))snprintf(rpath,sizeof rpath,"%s",p);}}
        if(!inc)sno_error(lineno,"cannot open include '%s'",iname);
        else {
            char *sl=strrchr(rpath,'/');
            if(sl){char *d=strndup(rpath,(size_t)(sl-rpath));sno_add_include_dir(d);}
            yypush_buffer_state(yy_create_buffer(inc,YY_BUF_SIZE,yyscanner),yyscanner);
        }
    }
}
<INITIAL>"-"[^\n]*\n    {
    /* other CTL line (not include/copy): skip whole line */
    lineno++;
}

<INITIAL>[+.]           {
    /* continuation: discard, stay in BODY (don't emit STMT_END) */
    BEGIN(BODY_START);
}

<INITIAL>[^ \t\n*!|;+.\-] {
    /* col-1 non-space: start of label */
    strbuf[0] = yytext[0]; strpos = 1;
    BEGIN(LABEL);
}

    /* ---- LABEL: IDCONT chars only — stops at first non-label char ---- */

<LABEL>[A-Za-z0-9_.\x80-\xFF]+  {
    int take = yyleng < (int)sizeof(strbuf)-strpos-1
               ? yyleng : (int)sizeof(strbuf)-strpos-1;
    memcpy(strbuf+strpos, yytext, take); strpos += take;
}

<LABEL>[ \t]            {
    strbuf[strpos] = '\0';
    BEGIN(BODY_START);
    return T_LABEL;
}

<LABEL>\n               {
    strbuf[strpos] = '\0';
    lineno++;
    BEGIN(LABEL_DONE);
    return T_LABEL;
    /* LABEL_DONE emits T_STMT_END before any body on the next line,
     * preventing the next line's body from being absorbed into this stmt. */
}

    /* ---- LABEL_DONE: after a label-only line (label ended with \n) ----
     * Must emit T_STMT_END, then re-dispatch at col-1 for the next line. */
<LABEL_DONE>[ \t]          { BEGIN(INITIAL); return T_STMT_END; }
<LABEL_DONE>\n             { lineno++; BEGIN(INITIAL); return T_STMT_END; }
<LABEL_DONE>[*!|;]         { yyless(0); BEGIN(INITIAL); return T_STMT_END; }
<LABEL_DONE>[^ \t\n*!|;]  { yyless(0); BEGIN(INITIAL); return T_STMT_END; }
<LABEL_DONE><<EOF>>        { BEGIN(INITIAL); return T_STMT_END; }

<LABEL>.                {
    /* non-IDCONT char immediately after label (no space) — put back, emit label */
    yyless(0);
    strbuf[strpos] = '\0';
    BEGIN(BODY_START);
    return T_LABEL;
}


    /* ---- BODY_START: leading whitespace before first atom — never T_CONCAT ---- */

<BODY_START>[ \t]+      { /* discard leading whitespace — not concatenation */ }
<BODY_START>\n          { lineno++; BEGIN(INITIAL); return T_STMT_END; }
<BODY_START>";"         { BEGIN(INITIAL); return T_STMT_END; }
<BODY_START>.           { yyless(0); BEGIN(BODY); }

    /* ---- BODY: statement body ----
     * W (White) = mandatory whitespace + optional continuations (\n[+.] ws*)
     * G (Gray)  = optional whitespace + optional continuations
     * Design mirrors beauty.sno: every symbol has W or G on each side.
     * T_CONCAT is the ONLY token returned for whitespace alone. */

    /* Add G to definitions if not present — defined above as ({WS}|{CONT})* */

<BODY>\n               { lineno++; BEGIN(INITIAL); return T_STMT_END; }
<BODY>";"              { BEGIN(INITIAL); return T_STMT_END; }
<BODY>{W}";"           { BEGIN(INITIAL); return T_STMT_END; }
<BODY>{G}":"  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; BEGIN(GT); }

<BODY>&{ALPHA}{ALNUM}* { strbuf[0]='\0'; strncat(strbuf,yytext+1,sizeof(strbuf)-1); return T_KEYWORD; }

    /* T_CONCAT: White before atom-starting char — the only whitespace-only token */
<BODY>{W}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_CONCAT; }

    /* Binary ops: White OP White — mandatory space distinguishes from unary */
<BODY>{W}"**"{W}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_EXPONENTIATION;  }
<BODY>{W}"^"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_EXPONENTIATION;  }
<BODY>{W}"!"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_EXPONENTIATION;  }
<BODY>{W}"+"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_ADDITION;        }
<BODY>{W}"-"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_SUBTRACTION;     }
<BODY>{W}"*"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_MULTIPLICATION;  }
<BODY>{W}"/"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_DIVISION;        }
<BODY>{W}"$"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_IMMEDIATE_ASSIGN;}
<BODY>{W}"."{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_COND_ASSIGN;     }
<BODY>{W}"|"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_ALTERNATION;     }
<BODY>{W}"="{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_ASSIGNMENT;      }
<BODY>{W}"="       { return T_ASSIGNMENT; }  /* = at EOL with no trailing space — DYN-63 */
<BODY>{W}"?"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_MATCH;           }
<BODY>{W}"&"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_AMPERSAND;       }
<BODY>{W}"@"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_AT_SIGN;         }
<BODY>{W}"#"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_POUND;           }
<BODY>{W}"%"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_PERCENT;         }
<BODY>{W}"~"{W}   { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_TILDE;           }

    /* Structural: open absorbs trailing Gray; close absorbs leading Gray */
<BODY>"("{G}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_LPAREN; }
<BODY>"["{G}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_LBRACK; }
<BODY>"<"{G}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_LANGLE; }
<BODY>{G}")"  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_RPAREN; }
<BODY>{G}"]"  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_RBRACK; }
<BODY>{G}">"  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=='\n') lineno++; return T_RANGLE; }
<BODY>{G}","{G}  { for(int _i=0;_i<yyleng;_i++) if(yytext[_i]=="\n") lineno++; return T_COMMA; }

    /* Atoms: arrive bare (T_CONCAT or Gray already consumed leading space) */
<BODY>{ALPHA}{IDCONT}*/"("  { strbuf[0]='\0'; strncat(strbuf,yytext,sizeof(strbuf)-1); return T_FUNCTION; }
<BODY>{ALPHA}{IDCONT}*      { strbuf[0]='\0'; strncat(strbuf,yytext,sizeof(strbuf)-1); return strcasecmp(strbuf,"END")==0?T_END:T_IDENT; }
<BODY>{DIGIT}+"."{DIGIT}+{EXP}? |
<BODY>{DIGIT}+{EXP}         { strbuf[0]='\0'; strncat(strbuf,yytext,sizeof(strbuf)-1); return T_REAL; }
<BODY>{DIGIT}+                { strbuf[0]='\0'; strncat(strbuf,yytext,sizeof(strbuf)-1); return T_INT;  }
<BODY>\'                       { strpos=0; BEGIN(STR1); }
<BODY>\"                       { strpos=0; BEGIN(STR2); }

    /* Unary ops: arrive bare (no leading space) */
<BODY>"**"   { return T_EXPONENTIATION;  }
<BODY>"^"    { return T_EXPONENTIATION;  }
<BODY>"+"    { return T_UN_PLUS;         }
<BODY>"-"    { return T_UN_MINUS;        }
<BODY>"*"    { return T_UN_ASTERISK;     }
<BODY>"/"    { return T_UN_SLASH;        }
<BODY>"%"    { return T_UN_PERCENT;      }
<BODY>"@"    { return T_UN_AT_SIGN;      }
<BODY>"~"    { return T_UN_TILDE;        }
<BODY>"$"    { return T_UN_DOLLAR_SIGN;  }
<BODY>"."    { return T_UN_PERIOD;       }
<BODY>"!"    { return T_UN_EXCLAMATION;  }
<BODY>"#"    { return T_UN_POUND;        }
<BODY>"|"    { return T_UN_VERTICAL_BAR; }
<BODY>"="    { return T_UN_EQUAL;        }
<BODY>"?"    { return T_UN_QUESTION_MARK;}
<BODY>"&"    { return T_UN_AMPERSAND;    }

<BODY>.      { sno_error(lineno,"unexpected char '%s'",yytext); }


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

    /* ---- GT: goto field — proper tokens, no T_GOTO blob ---- */
    /* DYN-59: T_GOTO eliminated. GT state emits real tokens so the grammar
     * can parse opt_goto with real BNF rules instead of re-lexing a blob.
     * Tokens emitted from GT: T_GOTO_S, T_GOTO_F, T_IDENT, T_FUNCTION,
     * T_LPAREN, T_RPAREN, T_LANGLE, T_RANGLE, T_STR, T_STMT_END. */

<GT>[ \t]+              { return T_CONCAT; } /* concat between atoms in $(expr) */
<GT>[Ss]/"("            { return T_GOTO_S; }
<GT>[Ss]/"<"            { return T_GOTO_S; }
<GT>[Ff]/"("            { return T_GOTO_F; }
<GT>[Ff]/"<"            { return T_GOTO_F; }
<GT>"("                 { return T_GOTO_LPAREN; }
<GT>")"                 { return T_GOTO_RPAREN; }
<GT>"<"                 { return T_GOTO_LPAREN; }
<GT>">"                 { return T_GOTO_RPAREN; }
<GT>"$"                 { return T_UN_DOLLAR_SIGN; }
<GT>[A-Za-z][A-Za-z0-9_.]* {
    strbuf[0]='\0'; strncat(strbuf, yytext, sizeof(strbuf)-1);
    return T_IDENT;
}
<GT>\'[^\'\n]*\'    {
    /* quoted label: strip quotes */
    int len = yyleng - 2;
    if (len < 0) len = 0;
    if (len >= (int)sizeof(strbuf)) len = sizeof(strbuf)-1;
    memcpy(strbuf, yytext+1, len); strbuf[len]='\0';
    return T_STR;
}
<GT>";"                 { BEGIN(INITIAL); return T_STMT_END; }
<GT>\n                  { lineno++; BEGIN(INITIAL); return T_STMT_END; }
<GT>.                   { /* ignore unknown chars in goto field */ }

    /* ---- SKIP: comment or unknown CTL line ---- */

<SKIP>[^\n]*            { /* discard */ }
<SKIP>\n                { lineno++; BEGIN(INITIAL); }

    /* ---- INCL state retired: include handled directly in INITIAL ---- */
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
    sno_nerrors++;
}

void sno_add_include_dir(const char *d) {
    if (n_inc < 64) inc_dirs[n_inc++] = (char *)d;
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
        t.kind   = r;

        switch (r) {
        case T_EOF:
            t.kind = T_EOF; return t;

        case T_LABEL:
            t.sval = intern(strbuf);
            t.ival = strcasecmp(strbuf,"END")==0 ? 1 : 0;
            return t;

        case T_IDENT: case T_END: case T_KEYWORD:
        case T_FUNCTION:
        case T_GOTO_S: case T_GOTO_F:
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

void sno_reset(void) { sno_nerrors = 0; n_inc = 0; }
