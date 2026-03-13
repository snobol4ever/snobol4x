%{
/*
 * rebus.l  —  Flex lexer for the Rebus language (Griswold TR 84-9)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rebus.h"
#include "rebus.tab.h"

int   rebus_nerrors  = 0;
char *rebus_filename = (char *)"<stdin>";

void rebus_error(int lineno, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", rebus_filename, lineno);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    rebus_nerrors++;
}

static char *upcase(const char *s) {
    char *p = strdup(s);
    for (int i = 0; p[i]; i++) p[i] = (char)toupper((unsigned char)p[i]);
    return p;
}

typedef struct { const char *word; int tok; } KW;
static KW kwtab[] = {
    {"CASE",T_CASE},{"DEFAULT",T_DEFAULT},{"DO",T_DO},{"ELSE",T_ELSE},
    {"END",T_END},{"EXIT",T_EXIT},{"FAIL",T_FAIL},{"FOR",T_FOR},
    {"FROM",T_FROM},{"FUNCTION",T_FUNCTION},{"BY",T_BY},{"IF",T_IF},
    {"INITIAL",T_INITIAL},{"LOCAL",T_LOCAL},{"NEXT",T_NEXT},{"OF",T_OF},
    {"PROCEDURE",T_FUNCTION},{"RECORD",T_RECORD},{"REPEAT",T_REPEAT},
    {"RETURN",T_RETURN},{"STOP",T_STOP},{"THEN",T_THEN},{"TO",T_TO},
    {"UNLESS",T_UNLESS},{"UNTIL",T_UNTIL},{"WHILE",T_WHILE},{NULL,0}
};
static int lookup_kw(const char *u) {
    for (int i=0; kwtab[i].word; i++)
        if (strcmp(kwtab[i].word,u)==0) return kwtab[i].tok;
    return 0;
}

#define RBUF_MAX (4<<20)
static char  *rbuf = NULL;
static size_t rlen = 0;
static size_t rpos = 0;

#define YY_INPUT(buf,result,max_size) \
    do { size_t _n=(rlen-rpos<(size_t)(max_size))?(rlen-rpos):(size_t)(max_size); \
         if(!_n){(result)=YY_NULL;} \
         else{memcpy((buf),rbuf+rpos,_n);rpos+=_n;(result)=(int)_n;} } while(0)

static void load_file(FILE *fp) {
    char line[65536];
    while (fgets(line,sizeof line,fp)) {
        int n=(int)strlen(line);
        if (rlen+n+2<RBUF_MAX) { memcpy(rbuf+rlen,line,n); rlen+=n; }
    }
    rbuf[rlen]='\0';
}

/* next_is_continuation: scan rbuf for the start of line (yylineno+1)
 * and check if it begins with a continuation keyword (else/do/then).
 * We can't use rpos because flex may have pre-buffered the whole file. */
static int next_is_continuation(void) {
    /* Find the start of yylineno+1 by counting newlines in rbuf */
    int target = yylineno;  /* yylineno is already incremented past \n */
    size_t p = 0;
    int line = 1;
    while (p < rlen && line < target) {
        if (rbuf[p] == '\n') line++;
        p++;
    }
    /* p now at start of target line; skip whitespace */
    while (p < rlen && (rbuf[p] == ' ' || rbuf[p] == '\t')) p++;
    /* skip comment lines */
    while (p < rlen && rbuf[p] == '#') {
        while (p < rlen && rbuf[p] != '\n') p++;
        if (p < rlen) p++;
        while (p < rlen && (rbuf[p] == ' ' || rbuf[p] == '\t')) p++;
    }
    static const char *kws[] = { "else", "do", "then", NULL };
    for (int i = 0; kws[i]; i++) {
        size_t klen = strlen(kws[i]);
        if (p + klen <= rlen && strncasecmp(rbuf + p, kws[i], klen) == 0) {
            char after = (p + klen < rlen) ? rbuf[p + klen] : '\0';
            if (!isalnum((unsigned char)after) && after != '_') return 1;
        }
    }
    return 0;
}

static int last_tok=0;
static int needs_semi(int tok) {
    switch(tok) {
        case T_IDENT: case T_STR: case T_INT: case T_REAL: case T_KEYWORD:
        case ')': case ']': case '}':
        case T_END: case T_EXIT: case T_NEXT: case T_FAIL: case T_STOP:
        case T_RETURN:
            return 1;
        default: return 0;
    }
}
%}

%option noyywrap never-interactive yylineno

ALPHA   [A-Za-z_\x80-\xff]
IDENT   {ALPHA}([A-Za-z0-9_.\x80-\xff])*
DIGIT   [0-9]
WS      [ \t]+

%%

{WS}            { }
"#"[^\n]*       { }
\n              { if(needs_semi(last_tok)&&!next_is_continuation()){last_tok=';';return ';';} }

\'[^\']*\'      { yylval.sval=rebus_intern_n(yytext+1,yyleng-2); last_tok=T_STR; return T_STR; }
\"[^\"]*\"      { yylval.sval=rebus_intern_n(yytext+1,yyleng-2); last_tok=T_STR; return T_STR; }

{DIGIT}+"."{DIGIT}*([eE][+-]?{DIGIT}+)?  { yylval.dval=atof(yytext); last_tok=T_REAL; return T_REAL; }
{DIGIT}+[eE][+-]?{DIGIT}+                { yylval.dval=atof(yytext); last_tok=T_REAL; return T_REAL; }
{DIGIT}+        { yylval.ival=atol(yytext); last_tok=T_INT; return T_INT; }

"&"{IDENT}      { yylval.sval=upcase(yytext+1); last_tok=T_KEYWORD; return T_KEYWORD; }
"&"             { last_tok='&'; return '&'; }

{IDENT}         { char *up=upcase(yytext); int kw=lookup_kw(up);
                  if(kw){free(up);last_tok=kw;return kw;}
                  yylval.sval=up; last_tok=T_IDENT; return T_IDENT; }

":=:"           { last_tok=T_EXCHANGE;   return T_EXCHANGE;  }
"||:="          { last_tok=T_CATASSIGN;  return T_CATASSIGN; }
"+:="           { last_tok=T_ADDASSIGN;  return T_ADDASSIGN; }
"-:="           { last_tok=T_SUBASSIGN;  return T_SUBASSIGN; }
":="            { last_tok=T_ASSIGN;     return T_ASSIGN;    }
"?-"            { last_tok=T_QUESTMINUS; return T_QUESTMINUS;}
"<-"            { last_tok=T_ARROW;      return T_ARROW;     }
"||"            { last_tok=T_STRCAT;     return T_STRCAT;    }
"**"            { last_tok=T_STARSTAR;   return T_STARSTAR;  }
"~=="           { last_tok=T_SNE;        return T_SNE;       }
"~="            { last_tok=T_NE;         return T_NE;        }
">>="           { last_tok=T_SGE;        return T_SGE;       }
"<<="           { last_tok=T_SLE;        return T_SLE;       }
">>"            { last_tok=T_SGT;        return T_SGT;       }
"<<"            { last_tok=T_SLT;        return T_SLT;       }
">="            { last_tok=T_GE;         return T_GE;        }
"<="            { last_tok=T_LE;         return T_LE;        }
"=="            { last_tok=T_SEQ;        return T_SEQ;       }
"+:"            { last_tok=T_PLUSCOLON;  return T_PLUSCOLON; }

"("  { last_tok='('; return '('; }
")"  { last_tok=')'; return ')'; }
"["  { last_tok='['; return '['; }
"]"  { last_tok=']'; return ']'; }
"{"  { last_tok='{'; return '{'; }
"}"  { last_tok='}'; return '}'; }
","  { last_tok=','; return ','; }
";"  { last_tok=';'; return ';'; }
":"  { last_tok=':'; return ':'; }
"?"  { last_tok='?'; return '?'; }
"="  { last_tok='='; return '='; }
"+"  { last_tok='+'; return '+'; }
"-"  { last_tok='-'; return '-'; }
"*"  { last_tok='*'; return '*'; }
"/"  { last_tok='/'; return '/'; }
"%"  { last_tok='%'; return '%'; }
"^"  { last_tok='^'; return '^'; }
"|"  { last_tok='|'; return '|'; }
"."  { last_tok='.'; return '.'; }
"$"  { last_tok='$'; return '$'; }
"@"  { last_tok='@'; return '@'; }
"~"  { last_tok='~'; return '~'; }
"<"  { last_tok='<'; return '<'; }
">"  { last_tok='>'; return '>'; }
"!"  { last_tok='!'; return '!'; }
"\\" { last_tok='\\'; return '\\'; }

.    { rebus_error(yylineno,"unexpected character '%s'",yytext); }

%%

void yyerror(const char *msg) { rebus_error(yylineno,"%s",msg); }

extern int   yyparse(void);
extern void  rebus_parse_init(void);
RProgram    *rebus_parsed_program = NULL;

RProgram *rebus_parse(FILE *f, const char *filename) {
    rebus_filename=(char *)filename;
    last_tok=0;
    rbuf=malloc(RBUF_MAX);
    if(!rbuf){fprintf(stderr,"rebus: out of memory\n");exit(1);}
    rlen=0; rpos=0;
    load_file(f);
    yy_switch_to_buffer(yy_create_buffer(NULL,YY_BUF_SIZE));
    rebus_parse_init();
    yyparse();
    free(rbuf); rbuf=NULL;
    return rebus_parsed_program;
}
