%{
/*
 * lex.l — SNOBOL4 one-pass character-stream lexer for scrip-interp
 *
 * One pure pipeline: character stream → lex.l → tokens → parse.c → IR
 * No preprocessing step. Include depth handled via yypush/pop_buffer_state.
 * Continuation lines joined in-scanner. Goto field scanned in GT condition.
 *
 * Card format (CARDTB, CSNOBOL4 equ.h / snobol4.c NEWCRD):
 *   col-1 * ! | ; or empty → CMT comment, discard
 *   col-1 -                → CTL control line (INCLUDE/COPY + others silently skipped)
 *   col-1 + or .           → CNT continuation, join to previous logical line
 *   col-1 anything else    → NEW normal statement
 *
 * Token contract to parse.c (unchanged):
 *   T_LABEL    sval=name ival=1-if-END
 *   T_WS       whitespace field separator
 *   T_GOTO     sval=raw-goto-text
 *   T_STMT_END end of logical statement
 *   T_IDENT T_INT T_REAL T_STR T_KEYWORD + all operators
 *
 * Architecture:
 *   INITIAL state drives line-at-a-time card dispatch.
 *   Logical lines are accumulated in ex->logical[] handling continuations.
 *   When a complete logical line is ready it is parsed by
 *   emit_logical_to_pending() into ex->pending[].
 *   flex_lex_next() drains pending[] before calling yylex() again.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */

#include "scrip_cc.h"
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct {
    /* pending token mini-queue */
    Token   pending[64];
    int     phead, ptail;
    /* logical-line accumulation */
    char    logical[65536];
    int     llen;
    int     logical_lineno;
    /* scratch for current token */
    Token   tok;
} FlexExtra;

#define EX ((FlexExtra*)yyextra)

/* extern symbols from lex.c / scrip_cc */
extern char       *inc_dirs[];
extern int         n_inc;
void   sno_error(int, const char *, ...);
/* intern/intern_n provided as static inline by scrip_cc.h — no extern needed */

static Token make_tok(TokKind k, const char *sv, long iv, double dv, int ln) {
    Token t; t.kind=k; t.sval=sv; t.ival=iv; t.dval=dv; t.lineno=ln; return t;
}
static void pq_push(FlexExtra *ex, Token t) {
    ex->pending[ex->ptail % 64] = t; ex->ptail++;
}
static int pq_empty(FlexExtra *ex) { return ex->phead == ex->ptail; }
static Token pq_pop(FlexExtra *ex) { return ex->pending[ex->phead++ % 64]; }

/* forward */
static void emit_logical_to_pending(FlexExtra *ex);
static void emit_stmt_seg(FlexExtra *ex, const char *s, int len, int ln, int first);
static void tokenise_body_pending(FlexExtra *ex, const char *s, int len, int ln);

%}

%option reentrant noyywrap never-interactive
%option stack yylineno
%option extra-type="FlexExtra *"

%x BODY
%x GT_STATE

DIGIT   [0-9]
ALPHA   [A-Za-z\x80-\xFF]
ALNUM   [A-Za-z0-9_]
IDCONT  [A-Za-z0-9_.\x80-\xFF]
EXP     [eE][+-]?{DIGIT}+

%%

    /* ── comment lines ───────────────────────────────────────── */
^[ \t]*\n                       { /* blank */
    if (EX->llen) { emit_logical_to_pending(EX); }
}
^[*!|;][^\n]*\n                 { /* comment */
    if (EX->llen) { emit_logical_to_pending(EX); }
}

    /* ── -INCLUDE / -COPY ────────────────────────────────────── */
^-[ \t]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][ \t]* |
^-[ \t]*[Cc][Oo][Pp][Yy][ \t]*             {
    if (EX->llen) { emit_logical_to_pending(EX); }
    char iname[4096]; int fi = 0;
    int c = input(yyscanner);
    if (c == '\'' || c == '"') {
        int q = c; c = input(yyscanner);
        while (c != EOF && c != q && c != '\n') {
            if (fi < 4095) iname[fi++] = (char)c;
            c = input(yyscanner);
        }
    } else {
        while (c != EOF && c != '\n' && c != ' ' && c != '\t') {
            if (fi < 4095) iname[fi++] = (char)c;
            c = input(yyscanner);
        }
    }
    iname[fi] = '\0';
    while (c != EOF && c != '\n') c = input(yyscanner);
    if (fi > 0) {
        FILE *inc = fopen(iname, "r");
        if (!inc) {
            char path[4096];
            for (int i = 0; i < n_inc && !inc; i++) {
                snprintf(path, sizeof path, "%s/%s", inc_dirs[i], iname);
                inc = fopen(path, "r");
            }
        }
        if (!inc)
            sno_error(yylineno, "cannot open include '%s'", iname);
        else
            yypush_buffer_state(yy_create_buffer(inc, YY_BUF_SIZE, yyscanner),
                                yyscanner);
    }
}

    /* ── other CTL lines: silently skip ─────────────────────── */
^-[^\n]*\n                      {
    if (EX->llen) { emit_logical_to_pending(EX); }
}

    /* ── continuation: col-1 + or . ─────────────────────────── */
^[+.]                           {
    /* trim trailing whitespace from accumulated logical line */
    while (EX->llen > 0 &&
           (EX->logical[EX->llen-1]==' '||EX->logical[EX->llen-1]=='\t'))
        EX->llen--;
    /* append space then rest of line */
    int c = input(yyscanner);
    while (c == ' ' || c == '\t') c = input(yyscanner);
    if (EX->llen < (int)sizeof(EX->logical)-1)
        EX->logical[EX->llen++] = ' ';
    while (c != '\n' && c != EOF) {
        if (EX->llen < (int)sizeof(EX->logical)-1)
            EX->logical[EX->llen++] = (char)c;
        c = input(yyscanner);
    }
}

    /* ── normal statement line ───────────────────────────────── */
^[^\n]                          {
    /* flush previous logical line */
    if (EX->llen) { emit_logical_to_pending(EX); }
    EX->logical_lineno = yylineno;
    EX->logical[0] = yytext[0];
    EX->llen = 1;
    /* read rest of line */
    int c = input(yyscanner);
    while (c != '\n' && c != EOF) {
        if (EX->llen < (int)sizeof(EX->logical)-1)
            EX->logical[EX->llen++] = (char)c;
        c = input(yyscanner);
    }
}

    /* ── EOF: flush last logical line, pop include stack ─────── */
<<EOF>>                         {
    if (EX->llen) { emit_logical_to_pending(EX); }
    int _ln = yyget_lineno(yyscanner);
    if (YY_CURRENT_BUFFER) yypop_buffer_state(yyscanner);
    if (!YY_CURRENT_BUFFER) {
        pq_push(EX, make_tok(T_EOF, NULL, 0, 0, _ln));
        return 0;
    }
}

    /* ── catch stray newlines in INITIAL (shouldn't happen) ─── */
\n                              { }

%%

/* ── emit_logical_to_pending ─────────────────────────────────────────────
 * Parse one accumulated logical line into the pending queue.
 * Mirrors old emit_logical() but writes to pending[] instead of Q.
 */
static void emit_logical_to_pending(FlexExtra *ex) {
    const char *s   = ex->logical;
    int         len = ex->llen;
    int         ln  = ex->logical_lineno;
    ex->llen = 0;
    emit_stmt_seg(ex, s, len, ln, 1);
}

static void emit_stmt_seg(FlexExtra *ex,
                           const char *s, int len, int ln, int first) {
    int pos = 0;

    /* label — first segment only, col-1 non-space */
    if (first && len > 0 && s[0] != ' ' && s[0] != '\t') {
        int le = 0;
        while (le < len && s[le] != ' ' && s[le] != '\t') le++;
        const char *lbl = intern_n(s, le);
        pq_push(ex, make_tok(T_LABEL, lbl, strcasecmp(lbl,"END")==0, 0, ln));
        pos = le;
        while (pos < len && (s[pos]==' '||s[pos]=='\t')) pos++;
    }

    const char *rest = s + pos;
    int rlen = len - pos;

    /* find ':' or ';' outside strings/parens */
    int body_len = rlen, semi_pos = -1;
    const char *goto_str = NULL;
    {
        int depth=0, inq=0; char qch=0;
        for (int i = 0; i < rlen; i++) {
            char c = rest[i];
            if (inq) { if (c==qch) inq=0; continue; }
            if (c=='\''||c=='"') { inq=1; qch=c; continue; }
            if (c=='(') { depth++; continue; }
            if (c==')') { depth--; continue; }
            if (c==';' && depth==0) { semi_pos=i; body_len=i; break; }
            if (c==':' && depth==0) {
                body_len = i;
                while (body_len>0 &&
                       (rest[body_len-1]==' '||rest[body_len-1]=='\t'))
                    body_len--;
                const char *gs = rest+i+1; int gl = rlen-i-1;
                while (gl>0 && (gs[0]==' '||gs[0]=='\t')) { gs++; gl--; }
                for (int k=0; k<gl; k++) if (gs[k]==';') { gl=k; break; }
                while (gl>0 && (gs[gl-1]==' '||gs[gl-1]=='\t')) gl--;
                if (gl > 0) goto_str = intern_n(gs, gl);
                break;
            }
        }
    }
    while (body_len>0 && (rest[body_len-1]==' '||rest[body_len-1]=='\t'))
        body_len--;

    if (body_len > 0) tokenise_body_pending(ex, rest, body_len, ln);
    if (goto_str)     pq_push(ex, make_tok(T_GOTO, goto_str, 0, 0, ln));
    pq_push(ex, make_tok(T_STMT_END, NULL, 0, 0, ln));

    /* recurse for semicolon-separated statements */
    if (semi_pos >= 0) {
        const char *next = rest+semi_pos+1;
        int nlen = rlen-semi_pos-1;
        while (nlen>0 && (*next==' '||*next=='\t')) { next++; nlen--; }
        if (nlen>0 && next[0]!='*')
            emit_stmt_seg(ex, next, nlen, ln, 0);
    }
}

static void tokenise_body_pending(FlexExtra *ex,
                                   const char *s, int len, int ln) {
    int pos = 0;
    while (pos < len) {
        unsigned char ch = (unsigned char)s[pos];
        /* whitespace */
        if (ch==' '||ch=='\t') {
            while (pos<len&&((unsigned char)s[pos]==' '||(unsigned char)s[pos]=='\t')) pos++;
            pq_push(ex, make_tok(T_WS,NULL,0,0,ln)); continue;
        }
        /* single-quoted string */
        if (ch=='\'') {
            int st=pos+1; pos++;
            while (pos<len&&s[pos]!='\'') pos++;
            pq_push(ex, make_tok(T_STR,intern_n(s+st,pos-st),0,0,ln));
            if (pos<len) pos++;
            continue;
        }
        /* double-quoted string */
        if (ch=='"') {
            int st=pos+1; pos++;
            while (pos<len&&s[pos]!='"') pos++;
            pq_push(ex, make_tok(T_STR,intern_n(s+st,pos-st),0,0,ln));
            if (pos<len) pos++;
            continue;
        }
        /* number */
        if (isdigit(ch)) {
            int st=pos;
            while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
            if (pos<len&&s[pos]=='.'&&pos+1<len&&isdigit((unsigned char)s[pos+1])) {
                pos++;
                while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
                    pos++; if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
                    while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                }
                char b[64]; int bl=pos-st<63?pos-st:63;
                memcpy(b,s+st,bl); b[bl]='\0';
                pq_push(ex, make_tok(T_REAL,intern_n(s+st,pos-st),0,atof(b),ln));
                continue;
            }
            if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
                pos++; if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
                while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                char b[64]; int bl=pos-st<63?pos-st:63;
                memcpy(b,s+st,bl); b[bl]='\0';
                pq_push(ex, make_tok(T_REAL,intern_n(s+st,pos-st),0,atof(b),ln));
                continue;
            }
            char b[64]; int bl=pos-st<63?pos-st:63;
            memcpy(b,s+st,bl); b[bl]='\0';
            pq_push(ex, make_tok(T_INT,NULL,atol(b),0,ln)); continue;
        }
        /* &KEYWORD */
        if (ch=='&') {
            pos++;
            int st=pos;
            while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_')) pos++;
            if (pos>st) { pq_push(ex, make_tok(T_KEYWORD,intern_n(s+st,pos-st),0,0,ln)); continue; }
            pq_push(ex, make_tok(T_AMP,NULL,0,0,ln)); continue;
        }
        /* identifier */
        if (isalpha(ch)||ch>=0x80) {
            int st=pos;
            while (pos<len&&(isalnum((unsigned char)s[pos])||
                   s[pos]=='_'||s[pos]=='.'||(unsigned char)s[pos]>=0x80)) pos++;
            const char *sv=intern_n(s+st,pos-st);
            if (strcasecmp(sv,"END")==0) { pq_push(ex, make_tok(T_END,sv,0,0,ln)); continue; }
            pq_push(ex, make_tok(T_IDENT,sv,0,0,ln)); continue;
        }
        /* ** before * */
        if (ch=='*'&&pos+1<len&&s[pos+1]=='*') {
            pos+=2; pq_push(ex, make_tok(T_STARSTAR,NULL,0,0,ln)); continue;
        }
        /* single-char ops */
        TokKind k;
        switch(ch) {
            case '+': k=T_PLUS;     break; case '-': k=T_MINUS;    break;
            case '*': k=T_STAR;     break; case '/': k=T_SLASH;    break;
            case '%': k=T_PCT;      break; case '^': k=T_CARET;    break;
            case '!': k=T_BANG;     break; case '@': k=T_AT;       break;
            case '~': k=T_TILDE;    break; case '$': k=T_DOLLAR;   break;
            case '.': k=T_DOT;      break; case '#': k=T_HASH;     break;
            case '|': k=T_PIPE;     break; case '=': k=T_EQ;       break;
            case '?': k=T_QMARK;    break; case ',': k=T_COMMA;    break;
            case '(': k=T_LPAREN;   break; case ')': k=T_RPAREN;   break;
            case '[': k=T_LBRACKET; break; case ']': k=T_RBRACKET; break;
            case '<': k=T_LANGLE;   break; case '>': k=T_RANGLE;   break;
            default:
                sno_error(ln,"unexpected char '%c'",ch); pos++; continue;
        }
        pos++;
        pq_push(ex, make_tok(k,NULL,0,0,ln));
    }
}

/* ── Public interface ────────────────────────────────────────────────────── */

void flex_lex_open(Lex *lx, FILE *f, const char *fname) {
    FlexExtra *ex = calloc(1, sizeof(FlexExtra));
    ex->logical_lineno = 1;
    yyscan_t sc;
    yylex_init_extra(ex, &sc);
    yy_switch_to_buffer(yy_create_buffer(f, YY_BUF_SIZE, sc), sc);
    lx->_scanner = sc;
    lx->_extra   = ex;
    (void)fname;
}

Token flex_lex_next(Lex *lx) {
    FlexExtra *ex = (FlexExtra *)lx->_extra;
    yyscan_t   sc = (yyscan_t)lx->_scanner;
    for (;;) {
        if (!pq_empty(ex)) return pq_pop(ex);
        int r = yylex(sc);
        if (r == 0) {
            if (!pq_empty(ex)) return pq_pop(ex);
            return make_tok(T_EOF, NULL, 0, 0, yyget_lineno(sc));
        }
        /* yylex returned a direct token kind from BODY/GT rules (future) */
        Token t = ex->tok;
        t.kind   = (TokKind)r;
        t.lineno = yyget_lineno(sc);
        return t;
    }
}

void flex_lex_destroy(Lex *lx) {
    if (lx->_scanner) {
        yylex_destroy((yyscan_t)lx->_scanner);
        free(lx->_extra);
        lx->_scanner = NULL;
        lx->_extra   = NULL;
    }
}

/* ── Storage for externs declared above ─────────────────────────────────── */

char *inc_dirs[64];
int   n_inc = 0;

/* intern/intern_n: static inline in scrip_cc.h — no definition needed here */

void sno_error(int lineno, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "snobol4:%d: error: ", lineno);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

/* ── lex_open_str: str-slice path for body/goto sub-parsing in parse.c ─── */

void lex_open_str(Lex *lx, const char *s, int len, int lineno) {
    memset(lx, 0, sizeof *lx);
    lx->src = s; lx->pos = 0; lx->len = len; lx->lineno = lineno;
}

/* raw_next_str: minimal tokeniser for in-memory body/goto strings */
static Token raw_next_str(Lex *lx) {
    const char *s = lx->src;
    int pos = lx->pos, len = lx->len, ln = lx->lineno;
#define EMIT(k,sv,iv,dv) do { lx->pos=pos; return (Token){k,sv,iv,dv,ln}; } while(0)
    if (pos >= len) EMIT(T_EOF, NULL, 0, 0.0);
    if (s[pos]==' '||s[pos]=='\t') {
        while (pos<len&&(s[pos]==' '||s[pos]=='\t')) pos++;
        EMIT(T_WS," ",0,0.0);
    }
    if (s[pos]=='\''||s[pos]=='"') {
        char q=s[pos++]; int st=pos;
        while (pos<len&&s[pos]!=q) pos++;
        const char *sv=intern_n(s+st,pos-st);
        if (pos<len) pos++;
        EMIT(T_STR,sv,0,0.0);
    }
    if (isdigit((unsigned char)s[pos])) {
        int st=pos,real=0;
        while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
        if (pos<len&&s[pos]=='.'){real=1;pos++;while(pos<len&&isdigit((unsigned char)s[pos]))pos++;}
        if (pos<len&&(s[pos]=='e'||s[pos]=='E')){real=1;pos++;
            if(pos<len&&(s[pos]=='+'||s[pos]=='-'))pos++;
            while(pos<len&&isdigit((unsigned char)s[pos]))pos++;}
        const char *sv=intern_n(s+st,pos-st);
        if (real) EMIT(T_REAL,sv,0,atof(sv)); else EMIT(T_INT,sv,atol(sv),0.0);
    }
    if (s[pos]=='&') {
        int st=++pos;
        while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_')) pos++;
        EMIT(T_KEYWORD,intern_n(s+st,pos-st),0,0.0);
    }
    if (isalpha((unsigned char)s[pos])||s[pos]=='_') {
        int st=pos;
        while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_'||s[pos]=='.')) pos++;
        const char *sv=intern_n(s+st,pos-st);
        if (strcmp(sv,"END")==0) EMIT(T_END,sv,0,0.0);
        EMIT(T_IDENT,sv,0,0.0);
    }
    char c=s[pos++];
    if (c=='*'&&pos<len&&s[pos]=='*'){pos++;EMIT(T_STARSTAR,"**",0,0.0);}
    if (c==':'&&pos<len&&(s[pos]=='S'||s[pos]=='s')){pos++;EMIT(T_SGOTO,":S",0,0.0);}
    if (c==':'&&pos<len&&(s[pos]=='F'||s[pos]=='f')){pos++;EMIT(T_FGOTO,":F",0,0.0);}
    static const struct{char c;TokKind k;} ops[]={
        {'+',T_PLUS},{'-',T_MINUS},{'*',T_STAR},{'/',T_SLASH},{'%',T_PCT},
        {'^',T_CARET},{'!',T_BANG},{'&',T_AMP},{'@',T_AT},{'~',T_TILDE},
        {'$',T_DOLLAR},{'.', T_DOT},{'#',T_HASH},{'|',T_PIPE},{'=',T_EQ},
        {'?',T_QMARK},{',',T_COMMA},{'(',T_LPAREN},{')',T_RPAREN},
        {'[',T_LBRACKET},{']',T_RBRACKET},{'<',T_LANGLE},{'>',T_RANGLE},
        {':',T_COLON},{0,T_ERR}};
    for (int i=0;ops[i].c;i++) if (ops[i].c==c) EMIT(ops[i].k,intern_n(&c,1),0,0.0);
    EMIT(T_ERR,NULL,0,0.0);
#undef EMIT
}

/* ── lex_next / lex_peek / lex_at_end ───────────────────────────────────── */

Token lex_next(Lex *lx) {
    if (lx->peeked) { lx->peeked=0; return lx->peek; }
    if (lx->src)       return raw_next_str(lx);
    if (lx->_scanner)  return flex_lex_next(lx);
    return (Token){T_EOF,NULL,0,0.0,lx->lineno};
}

Token lex_peek(Lex *lx) {
    if (!lx->peeked) {
        lx->peek = lx->src      ? raw_next_str(lx) :
                   lx->_scanner ? flex_lex_next(lx) :
                   (Token){T_EOF,NULL,0,0.0,lx->lineno};
        lx->peeked=1;
    }
    return lx->peek;
}

int lex_at_end(Lex *lx) { return lex_peek(lx).kind==T_EOF; }

/* ── sno_parse ──────────────────────────────────────────────────────────── */

extern Program *parse_program_tokens(Lex *stream);

Program *sno_parse(FILE *f, const char *filename) {
    Lex lx; memset(&lx,0,sizeof lx);
    flex_lex_open(&lx, f, filename);
    Program *prog = parse_program_tokens(&lx);
    flex_lex_destroy(&lx);
    return prog;
}

/* sno_reset: no-op in one-pass path — no global queue state to clear */
void sno_reset(void) {}
