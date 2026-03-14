/*
 * lex.c - hand-rolled SNOBOL4 lexer for sno2c
 *
 * Pass 1 - join_file(): join continuation lines, split into SnoLine fields.
 * Pass 2 - lex_next() tokenises a single field string.
 * T_WS is the binary-vs-unary discriminator.
 */

#include "sno2c.h"
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

/* ── include dirs ───────────────────────────────────────────────────────── */

#define MAX_INC 64
static char *inc_dirs[MAX_INC];
static int   n_inc = 0;

void snoc_add_include_dir(const char *d) {
    if (n_inc < MAX_INC-1) inc_dirs[n_inc++] = strdup(d);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pass 1 - source line joining and field splitting
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Dynamic array of SnoLine */

static void la_push(LineArray *la, SnoLine sl) {
    if (la->n >= la->cap) {
        la->cap = la->cap ? la->cap*2 : 64;
        la->a   = realloc(la->a, la->cap * sizeof *la->a);
    }
    la->a[la->n++] = sl;
}

/* Find ':' starting goto field or ';' ending statement.
   Returns i for ':', -(i+1) for ';', -1 if neither found. */
static int find_goto_colon(const char *s, int len) {
    int depth=0; int inq=0; char qch=0;
    for (int i=0; i<len; i++) {
        char c = s[i];
        if (inq) { if (c==qch) inq=0; continue; }
        if (c=='\'' || c=='"') { inq=1; qch=c; continue; }
        if (c=='(') { depth++; continue; }
        if (c==')') { depth--; continue; }
        if (c==';' && depth==0) return -(i+1); /* semicolon = end of stmt */
        if (c==':' && depth==0) return i;
    }
    return -1;
}

/* Split buf[0..len) into (label, body, goto_str). Caller owns the strings. */
static SnoLine split_line(const char *buf, int len, int lineno) {
    SnoLine sl = {0};
    sl.lineno = lineno;

    if (len == 0) return sl;  /* blank logical line */

    /* Detect label: line starts with non-space/tab */
    int pos = 0;
    if (buf[0] != ' ' && buf[0] != '\t') {
        /* label extends to first whitespace or end */
        int lend = 0;
        while (lend < len && buf[lend] != ' ' && buf[lend] != '\t') lend++;
        sl.label = intern_n(buf, lend);
        if (strcasecmp(sl.label, "END") == 0) sl.is_end = 1;
        pos = lend;
        /* skip whitespace after label */
        while (pos < len && (buf[pos]==' ' || buf[pos]=='\t')) pos++;
    }

    /* Remaining: body + optional goto */
    const char *rest = buf + pos;
    int rlen = len - pos;

    if (rlen <= 0) return sl;

    int ci = find_goto_colon(rest, rlen);
    if (ci >= 0) {
        /* colon found: body is rest[0..ci), goto is rest[ci+1..rlen) */
        int blen = ci;
        while (blen > 0 && (rest[blen-1]==' ' || rest[blen-1]=='\t')) blen--;
        if (blen > 0) sl.body = intern_n(rest, blen);

        const char *gs = rest + ci + 1;
        int glen = rlen - ci - 1;
        while (glen > 0 && (gs[0]==' ' || gs[0]=='\t')) { gs++; glen--; }
        while (glen > 0 && (gs[glen-1]==' ' || gs[glen-1]=='\t')) glen--;
        /* strip any trailing ;* inline comment from goto field */
        for (int k=0; k<glen; k++) {
            if (gs[k]==';') { glen=k; break; }
        }
        while (glen > 0 && (gs[glen-1]==' ' || gs[glen-1]=='\t')) glen--;
        if (glen > 0) sl.goto_str = intern_n(gs, glen);
    } else if (ci < 0 && ci != -1) {
        /* semicolon found: truncate body there, no goto */
        int semi_pos = (-ci) - 1;
        int blen = semi_pos;
        while (blen > 0 && (rest[blen-1]==' ' || rest[blen-1]=='\t')) blen--;
        if (blen > 0) sl.body = intern_n(rest, blen);
    } else {
        /* no goto, no semicolon */
        int blen = rlen;
        while (blen > 0 && (rest[blen-1]==' ' || rest[blen-1]=='\t')) blen--;
        if (blen > 0) sl.body = intern_n(rest, blen);
    }

    return sl;
}

/* Recursive -INCLUDE handler */
static void join_file(FILE *fp, const char *fname, LineArray *out);

static void open_include(const char *iname, const char *fromfile, LineArray *out) {
    FILE *f = fopen(iname, "r");
    if (!f) {
        char path[4096];
        for (int i=0; i<n_inc && !f; i++) {
            snprintf(path, sizeof path, "%s/%s", inc_dirs[i], iname);
            f = fopen(path, "r");
        }
    }
    if (!f) {
        fprintf(stderr, "%s: cannot open include '%s'\n", fromfile, iname);
        snoc_nerrors++;
        return;
    }
    join_file(f, iname, out);
    fclose(f);
}

static void join_file(FILE *fp, const char *fname, LineArray *out) {
    char raw[65536], logical[65536];
    int  llen = 0, lineno = 0, logical_lineno = 0;

#define FLUSH() do { \
    if (!llen) break; \
    logical[llen] = '\0'; \
    SnoLine sl = split_line(logical, llen, logical_lineno); \
    la_push(out, sl); \
    llen = 0; \
} while(0)

    while (fgets(raw, sizeof raw, fp)) {
        lineno++;
        int n = (int)strlen(raw);
        while (n > 0 && (raw[n-1]=='\n' || raw[n-1]=='\r')) n--;
        raw[n] = '\0';

        if (n == 0) { FLUSH(); continue; }

        char c1 = raw[0];

        /* Comment or control-line types that terminate logical line */
        if (c1=='*' || c1=='!' || c1=='#' || c1=='|') { FLUSH(); continue; }

        /* ';' inline comment — treat as logical line terminator */
        if (c1==';') { FLUSH(); continue; }

        /* Control line '-' */
        if (c1 == '-') {
            FLUSH();
            int p = 1;
            while (p<n && (raw[p]==' '||raw[p]=='\t')) p++;
            if (strncasecmp(raw+p, "INCLUDE", 7)==0) {
                p += 7;
                while (p<n && (raw[p]==' '||raw[p]=='\t')) p++;
                char iname[4096]; int fi=0;
                if (p<n && (raw[p]=='\''||raw[p]=='"')) {
                    char q=raw[p++];
                    while (p<n && raw[p]!=q) iname[fi++]=raw[p++];
                } else {
                    while (p<n && raw[p]!=' '&&raw[p]!='\t') iname[fi++]=raw[p++];
                }
                iname[fi]='\0';
                open_include(iname, fname, out);
            }
            /* other control lines silently dropped */
            continue;
        }

        /* Continuation line */
        if (c1=='+' || c1=='.') {
            /* strip trailing ws from current logical line */
            while (llen>0 && (logical[llen-1]==' '||logical[llen-1]=='\t')) llen--;
            /* append one space + content from col 2 */
            int src=1;
            while (src<n && (raw[src]==' '||raw[src]=='\t')) src++;
            int copy = n - src;
            if (llen + copy + 2 < (int)sizeof logical) {
                logical[llen++] = ' ';
                memcpy(logical+llen, raw+src, copy);
                llen += copy;
            }
            continue;
        }

        /* Ordinary line — flush previous logical line, start new */
        FLUSH();
        logical_lineno = lineno;
        if (n < (int)sizeof logical) { memcpy(logical, raw, n); llen = n; }
    }
    FLUSH();
#undef FLUSH
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pass 2 — tokeniser over a single field string
 * ═══════════════════════════════════════════════════════════════════════════ */



void lex_open_str(Lex *lx, const char *s, int len, int lineno) {
    lx->src    = s;
    lx->pos    = 0;
    lx->len    = len;
    lx->lineno = lineno;
    lx->peeked = 0;
}

static Token make_tok(TokKind k, const char *sv, long iv, double dv, int ln) {
    Token t; t.kind=k; t.sval=sv; t.ival=iv; t.dval=dv; t.lineno=ln; return t;
}

/* Raw next token — no peek buffering */
static Token raw_next(Lex *lx) {
    const char *s = lx->src;
    int pos = lx->pos, len = lx->len, ln = lx->lineno;

    if (pos >= len) return make_tok(T_EOF, NULL, 0, 0, ln);

    /* Mandatory whitespace */
    if (s[pos]==' ' || s[pos]=='\t') {
        while (pos<len && (s[pos]==' '||s[pos]=='\t')) pos++;
        lx->pos = pos;
        return make_tok(T_WS, NULL, 0, 0, ln);
    }

    /* Single-quoted string */
    if (s[pos]=='\'') {
        int start=pos+1;
        pos++;
        while (pos<len && s[pos]!='\'') pos++;
        const char *sv = intern_n(s+start, pos-start);
        if (pos<len) pos++;  /* consume closing quote */
        lx->pos = pos;
        return make_tok(T_STR, sv, 0, 0, ln);
    }

    /* Double-quoted string */
    if (s[pos]=='"') {
        int start=pos+1;
        pos++;
        while (pos<len && s[pos]!='"') pos++;
        const char *sv = intern_n(s+start, pos-start);
        if (pos<len) pos++;
        lx->pos = pos;
        return make_tok(T_STR, sv, 0, 0, ln);
    }

    /* Number — integer or real */
    if (isdigit((unsigned char)s[pos])) {
        int start=pos;
        while (pos<len && isdigit((unsigned char)s[pos])) pos++;
        if (pos<len && s[pos]=='.' && pos+1<len && isdigit((unsigned char)s[pos+1])) {
            pos++;
            while (pos<len && isdigit((unsigned char)s[pos])) pos++;
            /* optional exponent */
            if (pos<len && (s[pos]=='e'||s[pos]=='E')) {
                pos++;
                if (pos<len && (s[pos]=='+'||s[pos]=='-')) pos++;
                while (pos<len && isdigit((unsigned char)s[pos])) pos++;
            }
            char buf[64]; int blen=pos-start<63?pos-start:63;
            memcpy(buf,s+start,blen); buf[blen]='\0';
            lx->pos=pos;
            return make_tok(T_REAL, NULL, 0, atof(buf), ln);
        }
        /* integer — optional exponent */
        if (pos<len && (s[pos]=='e'||s[pos]=='E')) {
            pos++;
            if (pos<len && (s[pos]=='+'||s[pos]=='-')) pos++;
            while (pos<len && isdigit((unsigned char)s[pos])) pos++;
            char buf[64]; int blen=pos-start<63?pos-start:63;
            memcpy(buf,s+start,blen); buf[blen]='\0';
            lx->pos=pos;
            return make_tok(T_REAL, NULL, 0, atof(buf), ln);
        }
        char buf[64]; int blen=pos-start<63?pos-start:63;
        memcpy(buf,s+start,blen); buf[blen]='\0';
        lx->pos=pos;
        return make_tok(T_INT, NULL, atol(buf), 0, ln);
    }

    /* Keyword variable &IDENT */
    if (s[pos]=='&') {
        pos++;
        int start=pos;
        while (pos<len && (isalnum((unsigned char)s[pos])||s[pos]=='_')) pos++;
        if (pos>start) {
            lx->pos=pos;
            return make_tok(T_KEYWORD, intern_n(s+start, pos-start), 0, 0, ln);
        }
        /* bare & */
        lx->pos=pos;
        return make_tok(T_AMP, NULL, 0, 0, ln);
    }

    /* Identifier */
    if (isalpha((unsigned char)s[pos]) || (unsigned char)s[pos]>=0x80) {
        int start=pos;
        while (pos<len && (isalnum((unsigned char)s[pos])||
               s[pos]=='_'||s[pos]=='.'||(unsigned char)s[pos]>=0x80)) pos++;
        char *sv = intern_n(s+start, pos-start);
        lx->pos=pos;
        if (strcasecmp(sv,"END")==0) return make_tok(T_END, sv, 0, 0, ln);
        return make_tok(T_IDENT, sv, 0, 0, ln);
    }

    /* Two-character operators */
    if (pos+1<len && s[pos]=='*' && s[pos+1]=='*') {
        lx->pos=pos+2; return make_tok(T_STARSTAR, NULL, 0, 0, ln);
    }

    /* Goto field single-letter prefixes S( F( — only meaningful in goto ctx,
       but the goto parser checks them; here we just lex the characters.
       S/F alone as identifiers are lexed normally via the IDENT path above. */

    /* Single-character operators */
    lx->pos = pos+1;
    switch (s[pos]) {
        case '+': return make_tok(T_PLUS,     NULL,0,0,ln);
        case '-': return make_tok(T_MINUS,    NULL,0,0,ln);
        case '*': return make_tok(T_STAR,     NULL,0,0,ln);
        case '/': return make_tok(T_SLASH,    NULL,0,0,ln);
        case '%': return make_tok(T_PCT,      NULL,0,0,ln);
        case '^': return make_tok(T_CARET,    NULL,0,0,ln);
        case '!': return make_tok(T_BANG,     NULL,0,0,ln);
        case '@': return make_tok(T_AT,       NULL,0,0,ln);
        case '~': return make_tok(T_TILDE,    NULL,0,0,ln);
        case '$': return make_tok(T_DOLLAR,   NULL,0,0,ln);
        case '.': return make_tok(T_DOT,      NULL,0,0,ln);
        case '#': return make_tok(T_HASH,     NULL,0,0,ln);
        case '|': return make_tok(T_PIPE,     NULL,0,0,ln);
        case '=': return make_tok(T_EQ,       NULL,0,0,ln);
        case '?': return make_tok(T_QMARK,    NULL,0,0,ln);
        case ',': return make_tok(T_COMMA,    NULL,0,0,ln);
        case '(': return make_tok(T_LPAREN,   NULL,0,0,ln);
        case ')': return make_tok(T_RPAREN,   NULL,0,0,ln);
        case '[': return make_tok(T_LBRACKET, NULL,0,0,ln);
        case ']': return make_tok(T_RBRACKET, NULL,0,0,ln);
        case '<': return make_tok(T_LANGLE,   NULL,0,0,ln);
        case '>': return make_tok(T_RANGLE,   NULL,0,0,ln);
        default:
            snoc_error(ln, "unexpected character '%c'", s[pos]);
            return make_tok(T_ERR, NULL, 0, 0, ln);
    }
}

Token lex_next(Lex *lx) {
    if (lx->peeked) { lx->peeked=0; return lx->peek; }
    return raw_next(lx);
}

Token lex_peek(Lex *lx) {
    if (!lx->peeked) { lx->peek=raw_next(lx); lx->peeked=1; }
    return lx->peek;
}

int lex_at_end(Lex *lx) {
    return lex_peek(lx).kind == T_EOF;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public parse entry point  (called by main.c via sno2c.h)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Defined in parse.c */
extern Program *parse_program(LineArray *lines);

Program *snoc_parse(FILE *f, const char *filename) {
    yyfilename = (char *)filename;

    LineArray lines = {0};
    join_file(f, filename, &lines);

    if (getenv("SNOC_DEBUG")) {
        fprintf(stderr, "=== %d logical lines ===\n", lines.n);
        for (int i=0; i<lines.n && i<10; i++) {
            SnoLine *sl = &lines.a[i];
            fprintf(stderr, "  [%d] label=%s body=%s goto=%s\n",
                    sl->lineno,
                    sl->label    ? sl->label    : "(none)",
                    sl->body     ? sl->body     : "(none)",
                    sl->goto_str ? sl->goto_str : "(none)");
        }
        fprintf(stderr, "===\n");
    }

    return parse_program(&lines);
}
