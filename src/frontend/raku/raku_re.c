/*============================================================= raku_re.c ====
 * Raku regex engine — table-driven NFA (Thompson construction)
 *
 * PHASE 1  (RK-32): compile /pattern/ → Nfa_state[] table; report state count.
 * PHASE 2  (RK-33): table-driven NFA simulation for --ir-run  (raku_nfa_match).
 * PHASE 3  (future): BB lifter — each Nfa_state → BB box for --sm-run/--jit-run
 *                    and m:g/pat/ generator (RK-37) and grammar rules (RK-40+).
 *
 * Supported syntax (RK-32):
 *   literals    a b c ...
 *   .           any character
 *   \d \w \s    digit / word / whitespace
 *   \D \W \S    negated classes
 *   [cls]       character class  (ranges a-z, negated [^...])
 *   ^  $        anchors
 *   *  +  ?     quantifiers (greedy)
 *   |           alternation
 *   ( )         grouping
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 *==========================================================================*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "raku_re.h"

/*------------------------------------------------------------------------
 * Character-class bitmap  (256 bits = 32 bytes)
 *----------------------------------------------------------------------*/
static void cc_set(Raku_cc *cc, unsigned char c) { cc->bits[c>>3] |= (1u << (c&7)); }
static void cc_setrange(Raku_cc *cc, unsigned char lo, unsigned char hi) {
    for (unsigned c = lo; c <= hi; c++) cc_set(cc, (unsigned char)c);
}
static void cc_invert(Raku_cc *cc) {
    for (int i = 0; i < 32; i++) cc->bits[i] ^= 0xFFu;
}
int raku_cc_test(const Raku_cc *cc, unsigned char c) { return (cc->bits[c>>3] >> (c&7)) & 1; }

static void cc_fill_digit(Raku_cc *cc)  { cc_setrange(cc,'0','9'); }
static void cc_fill_word(Raku_cc *cc)   { cc_setrange(cc,'a','z'); cc_setrange(cc,'A','Z');
                                           cc_setrange(cc,'0','9'); cc_set(cc,'_'); }
static void cc_fill_space(Raku_cc *cc)  { cc_set(cc,' '); cc_set(cc,'\t'); cc_set(cc,'\n');
                                           cc_set(cc,'\r'); cc_set(cc,'\f'); cc_set(cc,'\v'); }

/*========================================================================
 * NFA state array (growable)
 *======================================================================*/
#define NFA_INIT_CAP 64

struct Raku_nfa {
    Nfa_state  *states;
    int         n;        /* used */
    int         cap;
    int         start;    /* index of start state */
    int         accept;   /* index of accept state */
};

static int nfa_alloc(Raku_nfa *nfa) {
    if (nfa->n >= nfa->cap) {
        nfa->cap *= 2;
        nfa->states = realloc(nfa->states, (size_t)nfa->cap * sizeof(Nfa_state));
    }
    int id = nfa->n++;
    memset(&nfa->states[id], 0, sizeof(Nfa_state));
    nfa->states[id].id   = id;
    nfa->states[id].out1 = NFA_NULL;
    nfa->states[id].out2 = NFA_NULL;
    nfa->states[id].kind = NK_EPS;
    return id;
}

/* Helper: new state with kind + two successors */
static int nfa_state(Raku_nfa *nfa, Nfa_kind kind, int out1, int out2) {
    int id = nfa_alloc(nfa);
    nfa->states[id].kind = kind;
    nfa->states[id].out1 = out1;
    nfa->states[id].out2 = out2;
    return id;
}

/*========================================================================
 * Parser state — recursive descent over the pattern string
 *======================================================================*/
typedef struct {
    const char *pat;   /* full pattern */
    int         pos;   /* current position */
    int         len;
    Raku_nfa   *nfa;
    char        errbuf[128];
    int         ok;
} Re_parser;

/* forward declarations */
static int parse_alt(Re_parser *p, int *out_start, int *out_accept);
static int parse_concat(Re_parser *p, int *out_start, int *out_accept);
static int parse_quantified(Re_parser *p, int *out_start, int *out_accept);
static int parse_atom(Re_parser *p, int *out_start, int *out_accept);

static char peek(Re_parser *p)       { return p->pos < p->len ? p->pat[p->pos] : '\0'; }
static char consume(Re_parser *p)    { return p->pos < p->len ? p->pat[p->pos++] : '\0'; }
static int  at_end(Re_parser *p)     { return p->pos >= p->len; }
static void re_err(Re_parser *p, const char *msg) {
    if (p->ok) { snprintf(p->errbuf, sizeof p->errbuf, "%s", msg); p->ok = 0; }
}

/*------------------------------------------------------------------------
 * parse_charclass — parse [...]  (cursor after opening '[')
 * Fills a NK_CLASS state; returns its id.
 *----------------------------------------------------------------------*/
static int parse_charclass(Re_parser *p) {
    int id = nfa_alloc(p->nfa);
    Nfa_state *s = &p->nfa->states[id];
    s->kind = NK_CLASS;
    s->out1 = NFA_NULL; s->out2 = NFA_NULL;
    int negate = 0;
    if (peek(p) == '^') { negate = 1; consume(p); }
    /* must read at least one item before ] terminates */
    int first = 1;
    while (!at_end(p)) {
        char c = peek(p);
        if (c == ']' && !first) { consume(p); break; }
        first = 0;
        consume(p);
        if (c == '\\') {
            if (at_end(p)) { re_err(p,"truncated escape in []"); return id; }
            char esc = consume(p);
            switch (esc) {
                case 'd': cc_fill_digit(&s->cc); break;
                case 'D': { Raku_cc t={0}; cc_fill_digit(&t); cc_invert(&t);
                             for(int i=0;i<32;i++) s->cc.bits[i]|=t.bits[i]; } break;
                case 'w': cc_fill_word(&s->cc); break;
                case 'W': { Raku_cc t={0}; cc_fill_word(&t); cc_invert(&t);
                             for(int i=0;i<32;i++) s->cc.bits[i]|=t.bits[i]; } break;
                case 's': cc_fill_space(&s->cc); break;
                case 'S': { Raku_cc t={0}; cc_fill_space(&t); cc_invert(&t);
                             for(int i=0;i<32;i++) s->cc.bits[i]|=t.bits[i]; } break;
                default:  cc_set(&s->cc,(unsigned char)esc); break;
            }
        } else {
            /* range check */
            if (peek(p)=='-' && p->pos+1 < p->len && p->pat[p->pos+1]!=']') {
                consume(p); /* eat '-' */
                char hi = consume(p);
                if ((unsigned char)c <= (unsigned char)hi)
                    cc_setrange(&s->cc,(unsigned char)c,(unsigned char)hi);
                else
                    re_err(p,"invalid char class range");
            } else {
                cc_set(&s->cc,(unsigned char)c);
            }
        }
    }
    if (negate) cc_invert(&s->cc);
    return id;
}

/*------------------------------------------------------------------------
 * parse_atom — literal, ., \d etc, [], ^, $, (group)
 * Returns fragment: (*out_start, *out_accept).
 * out_accept is a "dangling" state whose out1 must be patched by caller.
 *----------------------------------------------------------------------*/
static int parse_atom(Re_parser *p, int *out_start, int *out_accept) {
    if (at_end(p)) { re_err(p,"unexpected end of pattern"); return 0; }
    char c = peek(p);

    if (c == '(') {
        consume(p);
        if (!parse_alt(p, out_start, out_accept)) return 0;
        if (peek(p) != ')') { re_err(p,"missing ')'"); return 0; }
        consume(p);
        return 1;
    }
    if (c == '[') {
        consume(p);
        int id = parse_charclass(p);
        if (!p->ok) return 0;
        *out_start = id;
        *out_accept = id;  /* out1 dangling */
        return 1;
    }
    if (c == '^') {
        consume(p);
        int id = nfa_state(p->nfa, NK_ANCHOR_BOL, NFA_NULL, NFA_NULL);
        *out_start = *out_accept = id;
        return 1;
    }
    if (c == '$') {
        consume(p);
        int id = nfa_state(p->nfa, NK_ANCHOR_EOL, NFA_NULL, NFA_NULL);
        *out_start = *out_accept = id;
        return 1;
    }
    if (c == '.') {
        consume(p);
        int id = nfa_state(p->nfa, NK_ANY, NFA_NULL, NFA_NULL);
        *out_start = *out_accept = id;
        return 1;
    }
    if (c == '\\') {
        consume(p);
        if (at_end(p)) { re_err(p,"truncated escape"); return 0; }
        char esc = consume(p);
        int id = nfa_alloc(p->nfa);
        Nfa_state *s = &p->nfa->states[id];
        s->out1 = NFA_NULL; s->out2 = NFA_NULL;
        switch (esc) {
            case 'd': s->kind=NK_CLASS; cc_fill_digit(&s->cc);  break;
            case 'D': s->kind=NK_CLASS; cc_fill_digit(&s->cc); cc_invert(&s->cc); break;
            case 'w': s->kind=NK_CLASS; cc_fill_word(&s->cc);   break;
            case 'W': s->kind=NK_CLASS; cc_fill_word(&s->cc);  cc_invert(&s->cc); break;
            case 's': s->kind=NK_CLASS; cc_fill_space(&s->cc);  break;
            case 'S': s->kind=NK_CLASS; cc_fill_space(&s->cc); cc_invert(&s->cc); break;
            default:  s->kind=NK_CHAR; s->ch=(unsigned char)esc; break;
        }
        *out_start = *out_accept = id;
        return 1;
    }
    /* literal character — reject meta characters that shouldn't reach here */
    if (c == ')' || c == '|' || c == '*' || c == '+' || c == '?') {
        re_err(p,"unexpected meta character");
        return 0;
    }
    consume(p);
    int id = nfa_state(p->nfa, NK_CHAR, NFA_NULL, NFA_NULL);
    p->nfa->states[id].ch = (unsigned char)c;
    *out_start = *out_accept = id;
    return 1;
}

/*------------------------------------------------------------------------
 * parse_quantified — atom followed by optional * + ?
 *----------------------------------------------------------------------*/
static int parse_quantified(Re_parser *p, int *out_start, int *out_accept) {
    int a_start, a_acc;
    if (!parse_atom(p, &a_start, &a_acc)) return 0;

    char q = peek(p);
    if (q == '*' || q == '+' || q == '?') {
        consume(p);
        Raku_nfa *nfa = p->nfa;

        if (q == '*') {
            /* split → (atom_start | accept_new);  atom_acc.out1 → split */
            int split = nfa_alloc(nfa);
            int acc   = nfa_alloc(nfa);
            nfa->states[split].kind = NK_SPLIT;
            nfa->states[split].out1 = a_start;
            nfa->states[split].out2 = acc;
            nfa->states[a_acc].out1 = split;  /* loop back */
            nfa->states[acc].kind   = NK_EPS;
            nfa->states[acc].out1   = NFA_NULL;
            nfa->states[acc].out2   = NFA_NULL;
            *out_start = split; *out_accept = acc;
        } else if (q == '+') {
            /* atom then split → (atom_start | acc) */
            int split = nfa_alloc(nfa);
            int acc   = nfa_alloc(nfa);
            nfa->states[split].kind = NK_SPLIT;
            nfa->states[split].out1 = a_start;
            nfa->states[split].out2 = acc;
            nfa->states[a_acc].out1 = split;
            nfa->states[acc].kind   = NK_EPS;
            nfa->states[acc].out1   = NFA_NULL;
            nfa->states[acc].out2   = NFA_NULL;
            *out_start = a_start; *out_accept = acc;
        } else { /* ? */
            int split = nfa_alloc(nfa);
            int acc   = nfa_alloc(nfa);
            nfa->states[split].kind = NK_SPLIT;
            nfa->states[split].out1 = a_start;
            nfa->states[split].out2 = acc;
            nfa->states[a_acc].out1 = acc;
            nfa->states[acc].kind   = NK_EPS;
            nfa->states[acc].out1   = NFA_NULL;
            nfa->states[acc].out2   = NFA_NULL;
            *out_start = split; *out_accept = acc;
        }
    } else {
        *out_start = a_start; *out_accept = a_acc;
    }
    return 1;
}

/*------------------------------------------------------------------------
 * parse_concat — sequence of quantified atoms
 *----------------------------------------------------------------------*/
static int parse_concat(Re_parser *p, int *out_start, int *out_accept) {
    int started = 0;
    int c_start = NFA_NULL, c_acc = NFA_NULL;

    while (!at_end(p) && peek(p) != '|' && peek(p) != ')') {
        int q_start, q_acc;
        if (!parse_quantified(p, &q_start, &q_acc)) return 0;
        if (!started) {
            c_start = q_start; c_acc = q_acc; started = 1;
        } else {
            /* chain: previous accept → new start via epsilon */
            p->nfa->states[c_acc].out1 = q_start;
            c_acc = q_acc;
        }
    }
    if (!started) {
        /* empty concat — epsilon fragment */
        int id = nfa_state(p->nfa, NK_EPS, NFA_NULL, NFA_NULL);
        c_start = c_acc = id;
    }
    *out_start = c_start; *out_accept = c_acc;
    return 1;
}

/*------------------------------------------------------------------------
 * parse_alt — e1 | e2 | ...
 *----------------------------------------------------------------------*/
static int parse_alt(Re_parser *p, int *out_start, int *out_accept) {
    int l_start, l_acc;
    if (!parse_concat(p, &l_start, &l_acc)) return 0;

    while (peek(p) == '|') {
        consume(p);
        int r_start, r_acc;
        if (!parse_concat(p, &r_start, &r_acc)) return 0;

        Raku_nfa *nfa = p->nfa;
        int split = nfa_alloc(nfa);
        int join  = nfa_alloc(nfa);
        nfa->states[split].kind = NK_SPLIT;
        nfa->states[split].out1 = l_start;
        nfa->states[split].out2 = r_start;
        nfa->states[l_acc].out1 = join;
        nfa->states[r_acc].out1 = join;
        nfa->states[join].kind  = NK_EPS;
        nfa->states[join].out1  = NFA_NULL;
        nfa->states[join].out2  = NFA_NULL;
        l_start = split; l_acc = join;
    }
    *out_start = l_start; *out_accept = l_acc;
    return 1;
}

/*========================================================================
 * Public: raku_nfa_build
 *======================================================================*/
Raku_nfa *raku_nfa_build(const char *pattern) {
    Raku_nfa *nfa = malloc(sizeof *nfa);
    nfa->cap    = NFA_INIT_CAP;
    nfa->n      = 0;
    nfa->states = malloc((size_t)nfa->cap * sizeof(Nfa_state));
    nfa->start  = NFA_NULL;
    nfa->accept = NFA_NULL;

    Re_parser p;
    p.pat = pattern;
    p.pos = 0;
    p.len = (int)strlen(pattern);
    p.nfa = nfa;
    p.ok  = 1;
    p.errbuf[0] = '\0';

    int frag_start, frag_acc;
    if (!parse_alt(&p, &frag_start, &frag_acc) || !p.ok) {
        fprintf(stderr, "raku_re: compile error: %s\n", p.errbuf);
        raku_nfa_free(nfa);
        return NULL;
    }

    /* append accept state, patch dangling out1 of fragment tail */
    int acc = nfa_state(nfa, NK_ACCEPT, NFA_NULL, NFA_NULL);
    nfa->states[frag_acc].out1 = acc;
    nfa->start  = frag_start;
    nfa->accept = acc;
    return nfa;
}

int raku_nfa_state_count(const Raku_nfa *nfa) { return nfa ? nfa->n : 0; }
Nfa_state *raku_nfa_states(Raku_nfa *nfa)     { return nfa ? nfa->states : NULL; }

void raku_nfa_free(Raku_nfa *nfa) {
    if (!nfa) return;
    free(nfa->states);
    free(nfa);
}

/*========================================================================
 * Table-driven NFA simulation  (RK-33 — Thompson parallel active sets)
 *
 * Uses two bitsets: cur (active states this step) and nxt (next step).
 * Epsilon-closure computed inline via a small DFS stack.
 *======================================================================*/
#define MAX_STATES 512

typedef struct { int ids[MAX_STATES]; int n; } State_set;

/* ss_add: epsilon-closure walker.
 * pos/slen needed so BOL/EOL anchors fire only when position condition holds. */
static void ss_add(State_set *ss, const Raku_nfa *nfa, int id, char *visited,
                   int pos, int slen) {
    if (id == NFA_NULL || visited[id]) return;
    visited[id] = 1;
    Nfa_state *s = &nfa->states[id];
    switch (s->kind) {
        case NK_EPS:
            ss_add(ss, nfa, s->out1, visited, pos, slen); break;
        case NK_SPLIT:
            ss_add(ss, nfa, s->out1, visited, pos, slen);
            ss_add(ss, nfa, s->out2, visited, pos, slen); break;
        case NK_ANCHOR_BOL:
            if (pos == 0) ss_add(ss, nfa, s->out1, visited, pos, slen); break;
        case NK_ANCHOR_EOL:
            if (pos == slen) ss_add(ss, nfa, s->out1, visited, pos, slen); break;
        default:
            ss->ids[ss->n++] = id; break;
    }
}

static State_set eps_closure(const Raku_nfa *nfa, int start, int pos, int slen) {
    State_set ss; ss.n = 0;
    char visited[MAX_STATES]; memset(visited, 0, (size_t)nfa->n);
    ss_add(&ss, nfa, start, visited, pos, slen);
    return ss;
}

/* raku_nfa_match: returns 1 if pattern matches anywhere in subject (unanchored
 * unless ^ is in the pattern), 0 if no match. */
int raku_nfa_match(const Raku_nfa *nfa, const char *subject) {
    if (!nfa || !subject) return 0;
    if (nfa->n > MAX_STATES) { fprintf(stderr,"raku_re: NFA too large\n"); return 0; }

    int slen = (int)strlen(subject);
    int anchored_bol = (nfa->states[nfa->start].kind == NK_ANCHOR_BOL);

    /* try match starting at each position */
    for (int start_pos = 0; start_pos <= slen; start_pos++) {
        State_set cur = eps_closure(nfa, nfa->start, start_pos, slen);
        int pos = start_pos;
        int matched = 0;

        while (1) {
            /* check for accept in current set */
            for (int i = 0; i < cur.n; i++) {
                if (nfa->states[cur.ids[i]].kind == NK_ACCEPT) { matched = 1; break; }
            }
            if (matched) return 1;
            if (pos >= slen) break;

            unsigned char ch = (unsigned char)subject[pos];
            State_set nxt; nxt.n = 0;
            char visited[MAX_STATES]; memset(visited, 0, (size_t)nfa->n);

            for (int i = 0; i < cur.n; i++) {
                Nfa_state *s = &nfa->states[cur.ids[i]];
                int advance = 0;
                switch (s->kind) {
                    case NK_CHAR:  advance = (s->ch == ch); break;
                    case NK_ANY:   advance = (ch != '\n');  break;
                    case NK_CLASS: advance = raku_cc_test(&s->cc, ch); break;
                    default: break;
                }
                if (advance) ss_add(&nxt, nfa, s->out1, visited, pos+1, slen);
            }
            cur = nxt;
            pos++;
            if (cur.n == 0) break;
        }
        if (matched) return 1;
        if (anchored_bol) break;  /* ^ means only try pos 0 */
    }
    return 0;
}
