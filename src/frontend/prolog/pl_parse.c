/*
 * pl_parse.c — recursive-descent Prolog parser
 *
 * Grammar (simplified Edinburgh/ISO Prolog):
 *
 *   program    ::= clause* EOF
 *   clause     ::= directive | fact | rule
 *   directive  ::= :- term .
 *   fact       ::= head .
 *   rule       ::= head :- body .
 *   head       ::= term
 *   body       ::= goal (, goal)*
 *   goal       ::= term
 *   term       ::= primary (op primary)*   -- operator precedence handled
 *   primary    ::= atom | var | number | '(' term ')' | list
 *                | atom '(' args ')'        -- compound
 *   list       ::= '[' ']' | '[' term (',' term)* ('|' term)? ']'
 *
 * Operator precedence is handled with a simple Pratt / precedence-climbing
 * parser.  We handle the operators needed for the Prolog corpus:
 *   xfx 700: = \= == \== is < > =< >= =:= =\= =..
 *   xfy 200: ^
 *   yfx 400: * // / mod
 *   yfx 500: + -
 *   fy  900: \+ not
 *   xfx 1200: :- ?-  (only at clause level)
 *   xfy 1100: ;
 *   xfy 1000: ,
 *
 * Variables within a clause share a name→Term* map so that all
 * occurrences of the same variable name point to the same Term node.
 * Slot indices are assigned in order of first appearance.
 */

#include "pl_parse.h"
#include "pl_lex.h"
#include "pl_atom.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Variable scope — per-clause name→Term* mapping
 * ======================================================================= */
#define MAX_VARS 256

typedef struct {
    char *name;
    Term *term;
} VarEntry;

typedef struct {
    VarEntry entries[MAX_VARS];
    int      count;
    int      next_slot;
} VarScope;

static void scope_reset(VarScope *sc) { sc->count = 0; sc->next_slot = 0; }

static Term *scope_get(VarScope *sc, const char *name) {
    for (int i = 0; i < sc->count; i++)
        if (strcmp(sc->entries[i].name, name) == 0)
            return sc->entries[i].term;
    /* New variable */
    if (sc->count >= MAX_VARS) {
        fprintf(stderr, "too many variables in one clause\n");
        return term_new_var(-1);
    }
    Term *v = term_new_var(sc->next_slot++);
    sc->entries[sc->count].name = strdup(name);
    sc->entries[sc->count].term = v;
    sc->count++;
    return v;
}

/* =========================================================================
 * Parser state
 * ======================================================================= */
typedef struct {
    Lexer      lx;
    VarScope   sc;
    const char *filename;
    int         nerrors;
} Parser;

static void perror_at(Parser *p, int line, const char *msg) {
    fprintf(stderr, "%s:%d: parse error: %s\n", p->filename, line, msg);
    p->nerrors++;
}

/* =========================================================================
 * Operator table
 * ======================================================================= */
typedef enum { ASSOC_NONE, ASSOC_LEFT, ASSOC_RIGHT } Assoc;

typedef struct { const char *name; int prec; Assoc assoc; } OpEntry;

/* Binary operators in ascending precedence order */
static const OpEntry BIN_OPS[] = {
    { ",",    1000, ASSOC_RIGHT },
    { ";",    1100, ASSOC_RIGHT },
    { "->",    900, ASSOC_RIGHT },
    { "=",     700, ASSOC_NONE  },
    { "\\=",   700, ASSOC_NONE  },
    { "==",    700, ASSOC_NONE  },
    { "\\==",  700, ASSOC_NONE  },
    { "is",    700, ASSOC_NONE  },
    { "<",     700, ASSOC_NONE  },
    { ">",     700, ASSOC_NONE  },
    { "=<",    700, ASSOC_NONE  },
    { ">=",    700, ASSOC_NONE  },
    { "=:=",   700, ASSOC_NONE  },
    { "=\\=",  700, ASSOC_NONE  },
    { "=..",   700, ASSOC_NONE  },
    { "+",     500, ASSOC_LEFT  },
    { "-",     500, ASSOC_LEFT  },
    { "*",     400, ASSOC_LEFT  },
    { "/",     400, ASSOC_LEFT  },
    { "//",    400, ASSOC_LEFT  },
    { "mod",   400, ASSOC_LEFT  },
    { "^",     200, ASSOC_RIGHT },
    { NULL,    0,   ASSOC_NONE  }
};

static const OpEntry *find_binop(const char *name) {
    for (const OpEntry *op = BIN_OPS; op->name; op++)
        if (strcmp(op->name, name) == 0) return op;
    return NULL;
}

/* =========================================================================
 * Forward declarations
 * ======================================================================= */
static Term *parse_term(Parser *p, int max_prec);
static Term *parse_primary(Parser *p);

/* =========================================================================
 * List parsing:  [ ] | [ t, t, ... | tail ]
 * ======================================================================= */
static Term *parse_list(Parser *p) {
    /* '[' already consumed; peek tells us if empty */
    Token tk = lexer_peek(&p->lx);
    if (tk.kind == TK_RBRACKET) {
        lexer_next(&p->lx); /* consume ] */
        return term_new_atom(ATOM_NIL);
    }

    /* Build list spine: cons cells using '.' functor */
    Term *head = parse_term(p, 999); /* below comma precedence */
    if (!head) return term_new_atom(ATOM_NIL);

    tk = lexer_peek(&p->lx);
    Term *tail = NULL;

    if (tk.kind == TK_COMMA) {
        lexer_next(&p->lx);
        tail = parse_list(p); /* recurse for rest */
    } else if (tk.kind == TK_PIPE) {
        lexer_next(&p->lx);
        tail = parse_term(p, 999);
        tk = lexer_peek(&p->lx);
        if (tk.kind == TK_RBRACKET) lexer_next(&p->lx);
        else perror_at(p, tk.line, "expected ] after list tail");
    } else if (tk.kind == TK_RBRACKET) {
        lexer_next(&p->lx);
        tail = term_new_atom(ATOM_NIL);
    } else {
        perror_at(p, tk.line, "expected , | ] in list");
        tail = term_new_atom(ATOM_NIL);
    }

    Term *args[2] = { head, tail };
    return term_new_compound(ATOM_DOT, 2, args);
}

/* =========================================================================
 * Argument list:  term (, term)*
 * ======================================================================= */
static int parse_args(Parser *p, Term ***args_out) {
    int cap = 8, n = 0;
    Term **args = malloc(cap * sizeof(Term *));

    for (;;) {
        if (n >= cap) { cap *= 2; args = realloc(args, cap * sizeof(Term *)); }
        Term *t = parse_term(p, 999); /* below comma */
        if (!t) break;
        args[n++] = t;
        Token tk = lexer_peek(&p->lx);
        if (tk.kind != TK_COMMA) break;
        lexer_next(&p->lx); /* consume comma */
    }

    *args_out = args;
    return n;
}

/* =========================================================================
 * parse_primary — atom, var, number, compound, parens, list
 * ======================================================================= */
static Term *parse_primary(Parser *p) {
    Token tk = lexer_next(&p->lx);

    switch (tk.kind) {
        case TK_VAR:
            return scope_get(&p->sc, tk.text);

        case TK_ANON:
            return term_new_var(-1); /* fresh anonymous each time */

        case TK_INT: {
            Term *t = term_new_int(tk.ival);
            return t;
        }
        case TK_FLOAT: {
            Term *t = term_new_float(tk.fval);
            return t;
        }

        case TK_STRING: {
            /* Treat "str" as an atom for now (no char-list lowering yet) */
            int id = pl_atom_intern(tk.text);
            return term_new_atom(id);
        }

        case TK_ATOM: {
            /* Check if followed by '(' -> compound */
            Token pk = lexer_peek(&p->lx);
            if (pk.kind == TK_LPAREN) {
                lexer_next(&p->lx); /* consume ( */
                Term **args = NULL;
                int nargs = parse_args(p, &args);
                Token rp = lexer_peek(&p->lx);
                if (rp.kind == TK_RPAREN) lexer_next(&p->lx);
                else perror_at(p, rp.line, "expected ) after args");
                int fid = pl_atom_intern(tk.text);
                Term *t = term_new_compound(fid, nargs, args);
                free(args);
                return t;
            }
            /* Plain atom */
            int id = pl_atom_intern(tk.text);
            return term_new_atom(id);
        }

        case TK_CUT:
            return term_new_atom(ATOM_CUT);

        case TK_LPAREN: {
            Term *t = parse_term(p, 1200);
            Token rp = lexer_peek(&p->lx);
            if (rp.kind == TK_RPAREN) lexer_next(&p->lx);
            else perror_at(p, rp.line, "expected )");
            return t;
        }

        case TK_LBRACKET:
            return parse_list(p);

        case TK_OP: {
            /* Prefix operator: \+ or - or not */
            if (strcmp(tk.text, "\\+") == 0 || strcmp(tk.text, "not") == 0) {
                Term *arg = parse_term(p, 900);
                int fid = pl_atom_intern(tk.text);
                Term *args[1] = { arg };
                return term_new_compound(fid, 1, args);
            }
            if (strcmp(tk.text, "-") == 0 && (lexer_peek(&p->lx).kind == TK_INT ||
                                               lexer_peek(&p->lx).kind == TK_FLOAT)) {
                Token num = lexer_next(&p->lx);
                if (num.kind == TK_INT)   return term_new_int(-num.ival);
                if (num.kind == TK_FLOAT) return term_new_float(-num.fval);
            }
            /* Fallthrough: treat as atom */
            int id = pl_atom_intern(tk.text);
            return term_new_atom(id);
        }

        case TK_NECK:
        case TK_QUERY: {
            /* Shouldn't appear inside a term — error */
            perror_at(p, tk.line, "unexpected :- in term position");
            return NULL;
        }

        default:
            perror_at(p, tk.line, "unexpected token in term");
            return NULL;
    }
}

/* =========================================================================
 * parse_term — precedence-climbing binary operator parser
 * ======================================================================= */
static Term *parse_term(Parser *p, int max_prec) {
    Term *lhs = parse_primary(p);
    if (!lhs) return NULL;

    for (;;) {
        Token pk = lexer_peek(&p->lx);
        const char *optext = NULL;

        if (pk.kind == TK_OP)   optext = pk.text;
        else if (pk.kind == TK_ATOM) optext = pk.text; /* mod, is, etc */
        else if (pk.kind == TK_COMMA && max_prec >= 1000) optext = ",";
        else if (pk.kind == TK_SEMI  && max_prec >= 1100) optext = ";";
        else break;

        const OpEntry *op = optext ? find_binop(optext) : NULL;
        if (!op || op->prec > max_prec) break;

        lexer_next(&p->lx); /* consume operator token */

        int rprec = (op->assoc == ASSOC_LEFT) ? op->prec - 1 : op->prec;
        Term *rhs = parse_term(p, rprec);
        if (!rhs) break;

        int fid = pl_atom_intern(op->name);
        Term *args[2] = { lhs, rhs };
        lhs = term_new_compound(fid, 2, args);
    }

    return lhs;
}

/* =========================================================================
 * Flatten conjunction  ','(A,B) -> A, B, ...
 * ======================================================================= */
static int count_conj(Term *t) {
    t = term_deref(t);
    if (!t) return 0;
    int comma_id = pl_atom_intern(",");
    if (t->tag == TT_COMPOUND && t->compound.functor == comma_id && t->compound.arity == 2)
        return count_conj(t->compound.args[0]) + count_conj(t->compound.args[1]);
    return 1;
}

static int flatten_conj(Term *t, Term **buf, int idx) {
    t = term_deref(t);
    if (!t) return idx;
    int comma_id = pl_atom_intern(",");
    if (t->tag == TT_COMPOUND && t->compound.functor == comma_id && t->compound.arity == 2) {
        idx = flatten_conj(t->compound.args[0], buf, idx);
        idx = flatten_conj(t->compound.args[1], buf, idx);
        return idx;
    }
    buf[idx++] = t;
    return idx;
}

/* =========================================================================
 * parse_clause — one clause or directive
 * ======================================================================= */
static PlClause *parse_clause(Parser *p) {
    scope_reset(&p->sc);

    Token pk = lexer_peek(&p->lx);
    if (pk.kind == TK_EOF) return NULL;

    PlClause *cl = calloc(1, sizeof(PlClause));
    cl->lineno = pk.line;

    /* Directive:  :- goal . */
    if (pk.kind == TK_NECK) {
        lexer_next(&p->lx); /* consume :- */
        Term *goal = parse_term(p, 999);
        Token dot = lexer_next(&p->lx);
        if (dot.kind != TK_DOT)
            perror_at(p, dot.line, "expected . after directive");
        cl->head  = NULL;
        cl->body  = malloc(sizeof(Term *));
        cl->body[0] = goal;
        cl->nbody = 1;
        return cl;
    }

    /* Fact or rule: head [:-  body] . */
    Term *head = parse_term(p, 999);
    cl->head = head;

    pk = lexer_peek(&p->lx);
    if (pk.kind == TK_NECK) {
        lexer_next(&p->lx); /* consume :- */
        Term *body_term = parse_term(p, 1200);
        int n = count_conj(body_term);
        cl->body  = malloc((n ? n : 1) * sizeof(Term *));
        cl->nbody = flatten_conj(body_term, cl->body, 0);
    } else {
        cl->body  = NULL;
        cl->nbody = 0;
    }

    Token dot = lexer_next(&p->lx);
    if (dot.kind != TK_DOT)
        perror_at(p, dot.line, "expected . at end of clause");

    return cl;
}

/* =========================================================================
 * pl_parse — public entry point
 * ======================================================================= */
PlProgram *pl_parse(const char *src, const char *filename) {
    pl_atom_init(); /* idempotent — safe to call multiple times */

    Parser p;
    lexer_init(&p.lx, src);
    p.filename = filename ? filename : "<input>";
    p.nerrors  = 0;
    scope_reset(&p.sc);

    PlProgram *prog = calloc(1, sizeof(PlProgram));

    for (;;) {
        Token pk = lexer_peek(&p.lx);
        if (pk.kind == TK_EOF) break;
        if (pk.kind == TK_ERROR) {
            fprintf(stderr, "%s:%d: lex error: %s\n",
                    p.filename, pk.line, pk.text);
            p.nerrors++;
            lexer_next(&p.lx); /* skip error token */
            continue;
        }

        PlClause *cl = parse_clause(&p);
        if (!cl) break;

        /* Append to program */
        if (!prog->head) prog->head = cl;
        else             prog->tail->next = cl;
        prog->tail = cl;
        prog->nclauses++;
    }

    prog->nerrors = p.nerrors;
    return prog;
}

/* =========================================================================
 * term_pretty — recursive pretty-printer
 * ======================================================================= */
void term_pretty(Term *t, FILE *out) {
    t = term_deref(t);
    if (!t) { fprintf(out, "<null>"); return; }

    switch (t->tag) {
        case TT_ATOM: {
            const char *n = pl_atom_name(t->atom_id);
            if (!n) n = "?";
            /* Quote if needed */
            int needs_quote = !islower((unsigned char)n[0]) && n[0] != '[';
            for (const char *c = n; *c && !needs_quote; c++)
                if (!isalnum((unsigned char)*c) && *c != '_') needs_quote = 1;
            if (needs_quote && strcmp(n,"[]") != 0 && strcmp(n,"!")!=0 &&
                strcmp(n,",")!=0 && strcmp(n,".")!=0)
                fprintf(out, "'%s'", n);
            else
                fprintf(out, "%s", n);
            break;
        }
        case TT_VAR:
            fprintf(out, "_V%d", t->var_slot < 0 ? 0 : t->var_slot);
            break;
        case TT_INT:
            fprintf(out, "%ld", t->ival);
            break;
        case TT_FLOAT:
            fprintf(out, "%g", t->fval);
            break;
        case TT_COMPOUND: {
            const char *fn = pl_atom_name(t->compound.functor);
            if (!fn) fn = "?";
            /* List notation */
            if (t->compound.functor == ATOM_DOT && t->compound.arity == 2) {
                fprintf(out, "[");
                term_pretty(t->compound.args[0], out);
                Term *tail = term_deref(t->compound.args[1]);
                while (tail && tail->tag == TT_COMPOUND &&
                       tail->compound.functor == ATOM_DOT && tail->compound.arity == 2) {
                    fprintf(out, ",");
                    term_pretty(tail->compound.args[0], out);
                    tail = term_deref(tail->compound.args[1]);
                }
                if (tail && !(tail->tag == TT_ATOM && tail->atom_id == ATOM_NIL)) {
                    fprintf(out, "|");
                    term_pretty(tail, out);
                }
                fprintf(out, "]");
                break;
            }
            /* Infix binary operators */
            if (t->compound.arity == 2 && find_binop(fn)) {
                fprintf(out, "(");
                term_pretty(t->compound.args[0], out);
                fprintf(out, " %s ", fn);
                term_pretty(t->compound.args[1], out);
                fprintf(out, ")");
                break;
            }
            /* Regular compound */
            fprintf(out, "%s(", fn);
            for (int i = 0; i < t->compound.arity; i++) {
                if (i) fprintf(out, ",");
                term_pretty(t->compound.args[i], out);
            }
            fprintf(out, ")");
            break;
        }
        case TT_REF:
            term_pretty(t->ref, out);
            break;
    }
}

/* =========================================================================
 * pl_program_pretty — print all clauses
 * ======================================================================= */
void pl_program_pretty(PlProgram *prog, FILE *out) {
    for (PlClause *cl = prog->head; cl; cl = cl->next) {
        if (!cl->head) {
            /* directive */
            fprintf(out, ":- ");
            if (cl->nbody > 0) term_pretty(cl->body[0], out);
            fprintf(out, ".\n");
            continue;
        }
        term_pretty(cl->head, out);
        if (cl->nbody > 0) {
            fprintf(out, " :-\n");
            for (int i = 0; i < cl->nbody; i++) {
                fprintf(out, "    ");
                term_pretty(cl->body[i], out);
                if (i + 1 < cl->nbody) fprintf(out, ",\n");
            }
        }
        fprintf(out, ".\n");
    }
}

/* =========================================================================
 * pl_program_free
 * ======================================================================= */
void pl_program_free(PlProgram *prog) {
    PlClause *cl = prog->head;
    while (cl) {
        PlClause *next = cl->next;
        free(cl->body);
        /* Terms are arena-like; skip deep free for now */
        free(cl);
        cl = next;
    }
    free(prog);
}
