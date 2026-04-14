/*
 * raku_ast.c — Tiny-Raku AST node constructors
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "raku_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────────── */
static RakuNode *alloc_node(RakuKind kind, int line) {
    RakuNode *n = calloc(1, sizeof *n);
    if (!n) { fprintf(stderr, "raku_ast: out of memory\n"); exit(1); }
    n->kind   = kind;
    n->lineno = line;
    return n;
}

/* ── List ────────────────────────────────────────────────────────────── */
RakuList *raku_list_new(void) {
    RakuList *l = calloc(1, sizeof *l);
    if (!l) { fprintf(stderr, "raku_ast: out of memory\n"); exit(1); }
    return l;
}

RakuList *raku_list_append(RakuList *lst, RakuNode *node) {
    if (lst->count >= lst->cap) {
        lst->cap  = lst->cap ? lst->cap * 2 : 8;
        lst->items = realloc(lst->items, lst->cap * sizeof(RakuNode*));
    }
    lst->items[lst->count++] = node;
    return lst;
}

/* ── Literals ────────────────────────────────────────────────────────── */
RakuNode *raku_node_int(long v, int line) {
    RakuNode *n = alloc_node(RK_INT, line);
    n->ival = v;
    return n;
}
RakuNode *raku_node_float(double v, int line) {
    RakuNode *n = alloc_node(RK_FLOAT, line);
    n->dval = v;
    return n;
}
RakuNode *raku_node_str(char *s, int line) {
    RakuNode *n = alloc_node(RK_STR, line);
    n->sval = s;  /* already strdup'd by lexer */
    return n;
}

/* ── Variables ───────────────────────────────────────────────────────── */
RakuNode *raku_node_var_scalar(char *name, int line) {
    RakuNode *n = alloc_node(RK_VAR_SCALAR, line);
    n->sval = name;
    return n;
}
RakuNode *raku_node_var_array(char *name, int line) {
    RakuNode *n = alloc_node(RK_VAR_ARRAY, line);
    n->sval = name;
    return n;
}
RakuNode *raku_node_ident(char *name, int line) {
    RakuNode *n = alloc_node(RK_IDENT, line);
    n->sval = name;
    return n;
}

/* ── Operators ───────────────────────────────────────────────────────── */
RakuNode *raku_node_binop(RakuKind op, RakuNode *l, RakuNode *r, int line) {
    RakuNode *n = alloc_node(op, line);
    n->left  = l;
    n->right = r;
    return n;
}
RakuNode *raku_node_unop(RakuKind op, RakuNode *operand, int line) {
    RakuNode *n = alloc_node(op, line);
    n->left = operand;
    return n;
}

/* ── Statements ──────────────────────────────────────────────────────── */
RakuNode *raku_node_my_scalar(char *name, RakuNode *init, int line) {
    RakuNode *n = alloc_node(RK_MY_SCALAR, line);
    n->sval = name;
    n->left = init;
    return n;
}
RakuNode *raku_node_my_array(char *name, RakuNode *init, int line) {
    RakuNode *n = alloc_node(RK_MY_ARRAY, line);
    n->sval = name;
    n->left = init;
    return n;
}
RakuNode *raku_node_assign(char *name, RakuNode *val, int line) {
    RakuNode *n = alloc_node(RK_ASSIGN, line);
    n->sval = name;
    n->left = val;
    return n;
}
RakuNode *raku_node_say(RakuNode *expr, int line) {
    RakuNode *n = alloc_node(RK_SAY, line);
    n->left = expr;
    return n;
}
RakuNode *raku_node_print(RakuNode *expr, int line) {
    RakuNode *n = alloc_node(RK_PRINT, line);
    n->left = expr;
    return n;
}
RakuNode *raku_node_take(RakuNode *expr, int line) {
    RakuNode *n = alloc_node(RK_TAKE, line);
    n->left = expr;
    return n;
}
RakuNode *raku_node_return(RakuNode *expr, int line) {
    RakuNode *n = alloc_node(RK_RETURN, line);
    n->left = expr;   /* may be NULL */
    return n;
}
RakuNode *raku_node_expr_stmt(RakuNode *expr, int line) {
    RakuNode *n = alloc_node(RK_EXPR_STMT, line);
    n->left = expr;
    return n;
}

/* ── Compound ────────────────────────────────────────────────────────── */
RakuNode *raku_node_block(RakuList *stmts, int line) {
    RakuNode *n = alloc_node(RK_BLOCK, line);
    n->children = stmts;
    return n;
}
RakuNode *raku_node_if(RakuNode *cond, RakuNode *then_blk,
                        RakuNode *else_blk, int line) {
    RakuNode *n = alloc_node(RK_IF, line);
    n->left  = cond;
    n->right = then_blk;
    n->extra = else_blk;   /* NULL if no else */
    return n;
}
RakuNode *raku_node_while(RakuNode *cond, RakuNode *body, int line) {
    RakuNode *n = alloc_node(RK_WHILE, line);
    n->left  = cond;
    n->right = body;
    return n;
}
RakuNode *raku_node_for(RakuNode *iter, char *var, RakuNode *body, int line) {
    RakuNode *n = alloc_node(RK_FOR, line);
    n->left  = iter;
    n->sval  = var;    /* "$var" name, or NULL to use $_ */
    n->extra = body;
    return n;
}
RakuNode *raku_node_sub(char *name, RakuList *params, RakuNode *body, int line) {
    RakuNode *n = alloc_node(RK_SUBDEF, line);
    n->sval     = name;
    n->children = params;
    n->right    = body;
    return n;
}
RakuNode *raku_node_call(char *name, RakuList *args, int line) {
    RakuNode *n = alloc_node(RK_CALL, line);
    n->sval     = name;
    n->children = args;
    return n;
}
RakuNode *raku_node_gather(RakuNode *body, int line) {
    RakuNode *n = alloc_node(RK_GATHER, line);
    n->left = body;
    return n;
}

/* ── Parse entry (sets up flex buffer and calls yyparse) ─────────────── */
extern int  raku_yyparse(void);
extern void raku_yy_scan_string(const char *);
extern void raku_yy_delete_buffer(void *);

RakuNode *raku_parse_string(const char *src) {
    raku_parse_result = NULL;
    raku_yy_scan_string(src);
    raku_yyparse();
    return raku_parse_result;
}
