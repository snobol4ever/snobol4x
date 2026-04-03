/*
 * lex.c — one-pass SNOBOL4 lexer for scrip-cc
 *
 * Single pass over source: reads lines, joins continuations, strips comments,
 * tokenises body inline, emits a flat token stream via a queue.
 * The parser calls lex_next()/lex_peek() on a Lex with src=NULL to drain
 * the queue.  No SnoLine, no LineArray, no split_line, no FLUSH macro.
 *
 * Synthetic tokens marking statement structure:
 *   T_LABEL    — col-1 identifier (label), sval=name, ival=is_end
 *   T_GOTO     — goto field string, sval=goto_str
 *   T_STMT_END — end of one logical statement
 *
 * T_WS is still emitted inside the body as the subject/pattern/replacement
 * field separator (unchanged contract with parse.c).
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */

#include "scrip_cc.h"
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ── error ──────────────────────────────────────────────────────────────── */

int   snoc_nerrors = 0;
char *yyfilename   = (char *)"<stdin>";

void snoc_error(int lineno, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", yyfilename, lineno);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    snoc_nerrors++;
}

/* ── include dirs ────────────────────────────────────────────────────────── */

#define MAX_INC 64
static char *inc_dirs[MAX_INC];
static int   n_inc = 0;

void snoc_add_include_dir(const char *d) {
    if (n_inc < MAX_INC-1) inc_dirs[n_inc++] = strdup(d);
}

void snoc_reset(void) {
    snoc_nerrors = 0;
    yyfilename   = (char *)"<stdin>";
    for (int i = 0; i < n_inc; i++) { free(inc_dirs[i]); inc_dirs[i] = NULL; }
    n_inc = 0;
}

/* ── token queue ─────────────────────────────────────────────────────────── */

#define TOKQ_CAP 16384
static struct { Token buf[TOKQ_CAP]; int head, tail; } Q;

static void q_push(TokKind k, const char *sv, long iv, double dv, int ln) {
    Token t; t.kind=k; t.sval=sv; t.ival=iv; t.dval=dv; t.lineno=ln;
    Q.buf[Q.tail % TOKQ_CAP] = t;
    Q.tail++;
}
static int  q_empty(void) { return Q.head == Q.tail; }
static Token q_pop(void) {
    if (q_empty()) { Token t={T_EOF,NULL,0,0,0}; return t; }
    return Q.buf[Q.head++ % TOKQ_CAP];
}
static Token q_peek(void) {
    if (q_empty()) { Token t={T_EOF,NULL,0,0,0}; return t; }
    return Q.buf[Q.head % TOKQ_CAP];
}

/* ── find goto colon ─────────────────────────────────────────────────────── */
/* Returns offset of ':', -(offset+1) for ';', -1 if neither. */
static int find_goto_colon(const char *s, int len) {
    int depth=0, inq=0; char qch=0;
    for (int i=0; i<len; i++) {
        char c=s[i];
        if (inq) { if (c==qch) inq=0; continue; }
        if (c=='\''||c=='"') { inq=1; qch=c; continue; }
        if (c=='(') { depth++; continue; }
        if (c==')') { depth--; continue; }
        if (c==';'&&depth==0) return -(i+1);
        if (c==':'&&depth==0) return i;
    }
    return -1;
}

/* ── tokenise a body string into the queue ───────────────────────────────── */
static void tokenise_body(const char *s, int len, int ln) {
    int pos=0;
    while (pos<len) {
        /* whitespace */
        if (s[pos]==' '||s[pos]=='\t') {
            while (pos<len&&(s[pos]==' '||s[pos]=='\t')) pos++;
            q_push(T_WS,NULL,0,0,ln); continue;
        }
        /* single-quoted string */
        if (s[pos]=='\'') {
            int st=pos+1; pos++;
            while (pos<len&&s[pos]!='\'') pos++;
            const char *sv=intern_n(s+st,pos-st);
            if (pos<len) pos++;
            q_push(T_STR,sv,0,0,ln); continue;
        }
        /* double-quoted string */
        if (s[pos]=='"') {
            int st=pos+1; pos++;
            while (pos<len&&s[pos]!='"') pos++;
            const char *sv=intern_n(s+st,pos-st);
            if (pos<len) pos++;
            q_push(T_STR,sv,0,0,ln); continue;
        }
        /* number */
        if (isdigit((unsigned char)s[pos])) {
            int st=pos;
            while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
            if (pos<len&&s[pos]=='.'&&pos+1<len&&isdigit((unsigned char)s[pos+1])) {
                pos++;
                while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
                    pos++;
                    if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
                    while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                }
                char b[64]; int bl=pos-st<63?pos-st:63;
                memcpy(b,s+st,bl); b[bl]='\0';
                q_push(T_REAL,intern_n(s+st,pos-st),0,atof(b),ln); continue;
            }
            if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
                pos++;
                if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
                while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
                char b[64]; int bl=pos-st<63?pos-st:63;
                memcpy(b,s+st,bl); b[bl]='\0';
                q_push(T_REAL,intern_n(s+st,pos-st),0,atof(b),ln); continue;
            }
            char b[64]; int bl=pos-st<63?pos-st:63;
            memcpy(b,s+st,bl); b[bl]='\0';
            q_push(T_INT,NULL,atol(b),0,ln); continue;
        }
        /* &KEYWORD */
        if (s[pos]=='&') {
            pos++;
            int st=pos;
            while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_')) pos++;
            if (pos>st) { q_push(T_KEYWORD,intern_n(s+st,pos-st),0,0,ln); continue; }
            q_push(T_AMP,NULL,0,0,ln); continue;
        }
        /* identifier */
        if (isalpha((unsigned char)s[pos])||(unsigned char)s[pos]>=0x80) {
            int st=pos;
            while (pos<len&&(isalnum((unsigned char)s[pos])||
                   s[pos]=='_'||s[pos]=='.'||(unsigned char)s[pos]>=0x80)) pos++;
            char *sv=intern_n(s+st,pos-st);
            if (strcasecmp(sv,"END")==0) { q_push(T_END,sv,0,0,ln); continue; }
            q_push(T_IDENT,sv,0,0,ln); continue;
        }
        /* ** */
        if (pos+1<len&&s[pos]=='*'&&s[pos+1]=='*') {
            pos+=2; q_push(T_STARSTAR,NULL,0,0,ln); continue;
        }
        /* single-char ops */
        char c=s[pos++];
        switch(c) {
            case '+': q_push(T_PLUS,    NULL,0,0,ln); break;
            case '-': q_push(T_MINUS,   NULL,0,0,ln); break;
            case '*': q_push(T_STAR,    NULL,0,0,ln); break;
            case '/': q_push(T_SLASH,   NULL,0,0,ln); break;
            case '%': q_push(T_PCT,     NULL,0,0,ln); break;
            case '^': q_push(T_CARET,   NULL,0,0,ln); break;
            case '!': q_push(T_BANG,    NULL,0,0,ln); break;
            case '@': q_push(T_AT,      NULL,0,0,ln); break;
            case '~': q_push(T_TILDE,   NULL,0,0,ln); break;
            case '$': q_push(T_DOLLAR,  NULL,0,0,ln); break;
            case '.': q_push(T_DOT,     NULL,0,0,ln); break;
            case '#': q_push(T_HASH,    NULL,0,0,ln); break;
            case '|': q_push(T_PIPE,    NULL,0,0,ln); break;
            case '=': q_push(T_EQ,      NULL,0,0,ln); break;
            case '?': q_push(T_QMARK,   NULL,0,0,ln); break;
            case ',': q_push(T_COMMA,   NULL,0,0,ln); break;
            case '(': q_push(T_LPAREN,  NULL,0,0,ln); break;
            case ')': q_push(T_RPAREN,  NULL,0,0,ln); break;
            case '[': q_push(T_LBRACKET,NULL,0,0,ln); break;
            case ']': q_push(T_RBRACKET,NULL,0,0,ln); break;
            case '<': q_push(T_LANGLE,  NULL,0,0,ln); break;
            case '>': q_push(T_RANGLE,  NULL,0,0,ln); break;
            default:  snoc_error(ln,"unexpected character '%c'",c); break;
        }
    }
}

/* ── emit one logical line as tokens ─────────────────────────────────────── */
static void emit_logical(const char *line, int len, int ln) {
    const char *src=line; int src_len=len;
    int first_seg=1;

    while (src_len>=0) {
        /* find semicolon multi-stmt split */
        int ci=find_goto_colon(src,src_len);
        int semi_end=(ci<-1)?((-ci)-1):-1;
        int piece_len=(semi_end<0)?src_len:semi_end;

        /* advance to next segment */
        const char *next_src=src+piece_len+(semi_end>=0?1:0);
        int next_len=-1;
        if (semi_end>=0) {
            while (*next_src==' '||*next_src=='\t') next_src++;
            if (*next_src!='*'&&*next_src!='\0')
                next_len=(int)strlen(next_src);
        }

        const char *seg=src; int seg_len=piece_len;
        int pos=0;

        /* label — only on first segment; non-first are body-only */
        if (first_seg && seg_len>0&&seg[0]!=' '&&seg[0]!='\t') {
            int le=0;
            while (le<seg_len&&seg[le]!=' '&&seg[le]!='\t') le++;
            char *lbl=intern_n(seg,le);
            q_push(T_LABEL,lbl,(strcasecmp(lbl,"END")==0),0,ln);
            pos=le;
            while (pos<seg_len&&(seg[pos]==' '||seg[pos]=='\t')) pos++;
        }

        /* body + goto */
        const char *rest=seg+pos; int rlen=seg_len-pos;
        if (rlen>0) {
            int gci=find_goto_colon(rest,rlen);
            int body_len;
            const char *goto_tok=NULL; int goto_len=0;
            if (gci>=0) {
                body_len=gci;
                while (body_len>0&&(rest[body_len-1]==' '||rest[body_len-1]=='\t')) body_len--;
                const char *gs=rest+gci+1; int glen=rlen-gci-1;
                while (glen>0&&(gs[0]==' '||gs[0]=='\t')) { gs++; glen--; }
                while (glen>0&&(gs[glen-1]==' '||gs[glen-1]=='\t')) glen--;
                for (int k=0;k<glen;k++) { if (gs[k]==';') { glen=k; break; } }
                while (glen>0&&(gs[glen-1]==' '||gs[glen-1]=='\t')) glen--;
                if (glen>0) { goto_tok=intern_n(gs,glen); goto_len=glen; }
            } else if (gci<-1) {
                body_len=(-gci)-1;
                while (body_len>0&&(rest[body_len-1]==' '||rest[body_len-1]=='\t')) body_len--;
            } else {
                body_len=rlen;
                while (body_len>0&&(rest[body_len-1]==' '||rest[body_len-1]=='\t')) body_len--;
            }
            /* body tokens first, then T_GOTO */
            if (body_len>0) tokenise_body(rest,body_len,ln);
            if (goto_tok) q_push(T_GOTO,goto_tok,0,0,ln);
        }

        q_push(T_STMT_END,NULL,0,0,ln);

        first_seg=0;
        if (next_len<0) break;
        src=next_src; src_len=next_len;
    }
}

/* ── file processing ─────────────────────────────────────────────────────── */

static void process_file(FILE *fp, const char *fname);

static void open_include(const char *iname, const char *fromfile) {
    FILE *f=fopen(iname,"r");
    if (!f) {
        char path[4096];
        for (int i=0;i<n_inc&&!f;i++) {
            snprintf(path,sizeof path,"%s/%s",inc_dirs[i],iname);
            f=fopen(path,"r");
        }
    }
    if (!f) {
        fprintf(stderr,"%s: cannot open include '%s'\n",fromfile,iname);
        snoc_nerrors++; return;
    }
    process_file(f,iname);
    fclose(f);
}

static void process_file(FILE *fp, const char *fname) {
    char raw[65536], logical[65536];
    int llen=0, lineno=0, logical_lineno=0;

    while (fgets(raw,sizeof raw,fp)) {
        lineno++;
        int n=(int)strlen(raw);
        while (n>0&&(raw[n-1]=='\n'||raw[n-1]=='\r')) n--;
        raw[n]='\0';

        char c1=n?raw[0]:'\0';

        if (n==0||c1=='*'||c1=='!'||c1=='#'||c1=='|'||c1==';') {
            if (llen) { emit_logical(logical,llen,logical_lineno); llen=0; }
            continue;
        }

        if (c1=='-') {
            if (llen) { emit_logical(logical,llen,logical_lineno); llen=0; }
            int p=1;
            while (p<n&&(raw[p]==' '||raw[p]=='\t')) p++;
            if (strncasecmp(raw+p,"INCLUDE",7)==0) {
                p+=7;
                while (p<n&&(raw[p]==' '||raw[p]=='\t')) p++;
                char iname[4096]; int fi=0;
                if (p<n&&(raw[p]=='\''||raw[p]=='"')) {
                    char q=raw[p++];
                    while (p<n&&raw[p]!=q) iname[fi++]=raw[p++];
                } else {
                    while (p<n&&raw[p]!=' '&&raw[p]!='\t') iname[fi++]=raw[p++];
                }
                iname[fi]='\0';
                if (fi>0) open_include(iname,fname);
            }
            continue;
        }

        if (c1=='+'||c1=='.') {
            while (llen>0&&(logical[llen-1]==' '||logical[llen-1]=='\t')) llen--;
            int s=1; while (s<n&&(raw[s]==' '||raw[s]=='\t')) s++;
            int copy=n-s;
            if (llen+copy+2<(int)sizeof logical) {
                logical[llen++]=' ';
                memcpy(logical+llen,raw+s,copy); llen+=copy;
            }
            continue;
        }

        /* ordinary line */
        if (llen) { emit_logical(logical,llen,logical_lineno); llen=0; }
        logical_lineno=lineno;
        if (n<(int)sizeof logical) { memcpy(logical,raw,n); llen=n; }
    }
    if (llen) { emit_logical(logical,llen,logical_lineno); llen=0; }
}

/* ── public lex interface ────────────────────────────────────────────────── */

void lex_open_str(Lex *lx, const char *s, int len, int lineno) {
    lx->src=s; lx->pos=0; lx->len=len; lx->lineno=lineno; lx->peeked=0;
}

/* Full tokeniser over a string — used by lex_open_str path (body re-parsing, goto parsing) */
static Token raw_next_str(Lex *lx) {
    const char *s=lx->src; int pos=lx->pos, len=lx->len, ln=lx->lineno;
    if (pos>=len) { lx->pos=pos; Token t={T_EOF,NULL,0,0,ln}; return t; }

    /* whitespace → T_WS */
    if (s[pos]==' '||s[pos]=='\t') {
        while (pos<len&&(s[pos]==' '||s[pos]=='\t')) pos++;
        lx->pos=pos; Token t={T_WS,NULL,0,0,ln}; return t;
    }
    /* single-quoted string */
    if (s[pos]=='\'') {
        int st=pos+1; pos++;
        while (pos<len&&s[pos]!='\'') pos++;
        const char *sv=intern_n(s+st,pos-st);
        if (pos<len) pos++;
        lx->pos=pos; Token t={T_STR,sv,0,0,ln}; return t;
    }
    /* double-quoted string */
    if (s[pos]=='"') {
        int st=pos+1; pos++;
        while (pos<len&&s[pos]!='"') pos++;
        const char *sv=intern_n(s+st,pos-st);
        if (pos<len) pos++;
        lx->pos=pos; Token t={T_STR,sv,0,0,ln}; return t;
    }
    /* number */
    if (isdigit((unsigned char)s[pos])) {
        int st=pos;
        while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
        if (pos<len&&s[pos]=='.'&&pos+1<len&&isdigit((unsigned char)s[pos+1])) {
            pos++;
            while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
            if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
                pos++; if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
                while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
            }
            char b[64]; int bl=pos-st<63?pos-st:63; memcpy(b,s+st,bl); b[bl]='\0';
            lx->pos=pos; Token t={T_REAL,NULL,0,atof(b),ln}; return t;
        }
        if (pos<len&&(s[pos]=='e'||s[pos]=='E')) {
            pos++; if (pos<len&&(s[pos]=='+'||s[pos]=='-')) pos++;
            while (pos<len&&isdigit((unsigned char)s[pos])) pos++;
            char b[64]; int bl=pos-st<63?pos-st:63; memcpy(b,s+st,bl); b[bl]='\0';
            lx->pos=pos; Token t={T_REAL,NULL,0,atof(b),ln}; return t;
        }
        char b[64]; int bl=pos-st<63?pos-st:63; memcpy(b,s+st,bl); b[bl]='\0';
        lx->pos=pos; Token t={T_INT,NULL,atol(b),0,ln}; return t;
    }
    /* &KEYWORD */
    if (s[pos]=='&') {
        pos++;
        int st=pos;
        while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_')) pos++;
        if (pos>st) { lx->pos=pos; Token t={T_KEYWORD,intern_n(s+st,pos-st),0,0,ln}; return t; }
        lx->pos=pos; Token t={T_AMP,NULL,0,0,ln}; return t;
    }
    /* identifier */
    if (isalpha((unsigned char)s[pos])||(unsigned char)s[pos]>=0x80) {
        int st=pos;
        while (pos<len&&(isalnum((unsigned char)s[pos])||s[pos]=='_'||
               s[pos]=='.'||(unsigned char)s[pos]>=0x80)) pos++;
        char *sv=intern_n(s+st,pos-st);
        lx->pos=pos;
        if (strcasecmp(sv,"END")==0) { Token t={T_END,sv,0,0,ln}; return t; }
        Token t={T_IDENT,sv,0,0,ln}; return t;
    }
    /* ** */
    if (pos+1<len&&s[pos]=='*'&&s[pos+1]=='*') {
        lx->pos=pos+2; Token t={T_STARSTAR,NULL,0,0,ln}; return t;
    }
    /* single-char ops */
    char c=s[pos]; lx->pos=pos+1;
    Token t; t.sval=NULL; t.ival=0; t.dval=0; t.lineno=ln;
    switch(c) {
        case '+': t.kind=T_PLUS;     break; case '-': t.kind=T_MINUS;    break;
        case '*': t.kind=T_STAR;     break; case '/': t.kind=T_SLASH;    break;
        case '%': t.kind=T_PCT;      break; case '^': t.kind=T_CARET;    break;
        case '!': t.kind=T_BANG;     break; case '@': t.kind=T_AT;       break;
        case '~': t.kind=T_TILDE;    break; case '$': t.kind=T_DOLLAR;   break;
        case '.': t.kind=T_DOT;      break; case '#': t.kind=T_HASH;     break;
        case '|': t.kind=T_PIPE;     break; case '=': t.kind=T_EQ;       break;
        case '?': t.kind=T_QMARK;    break; case ',': t.kind=T_COMMA;    break;
        case '(': t.kind=T_LPAREN;   break; case ')': t.kind=T_RPAREN;   break;
        case '[': t.kind=T_LBRACKET; break; case ']': t.kind=T_RBRACKET; break;
        case '<': t.kind=T_LANGLE;   break; case '>': t.kind=T_RANGLE;   break;
        default:  snoc_error(ln,"unexpected character '%c'",c); t.kind=T_ERR; break;
    }
    return t;
}

Token lex_next(Lex *lx) {
    if (lx->src) {
        if (lx->peeked) { lx->peeked=0; return lx->peek; }
        return raw_next_str(lx);
    }
    if (lx->peeked) { lx->peeked=0; q_pop(); return lx->peek; }
    return q_pop();
}

Token lex_peek(Lex *lx) {
    if (!lx->peeked) {
        lx->peek = lx->src ? raw_next_str(lx) : q_peek();
        lx->peeked=1;
    }
    return lx->peek;
}

int lex_at_end(Lex *lx) { return lex_peek(lx).kind==T_EOF; }

/* ── parse entry point ───────────────────────────────────────────────────── */

extern Program *parse_program(LineArray *lines);  /* old interface — replaced below */
extern Program *parse_program_tokens(Lex *lx);    /* new token-stream interface */

Program *snoc_parse(FILE *f, const char *filename) {
    yyfilename=(char*)filename;
    Lex lx={0};
    flex_lex_open(&lx, f, filename);
    Program *prog = parse_program_tokens(&lx);
    flex_lex_destroy(&lx);
    return prog;
}
