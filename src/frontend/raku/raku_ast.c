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

RakuNode *raku_node_interp_str(char *s, int line) {
    RakuNode *n = alloc_node(RK_INTERP_STR, line);
    n->sval = s;  /* raw string with $var sigils intact; lowerer splits */
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
/* RK_GIVEN: topic in left; whens list in children; deflt (may be NULL) in right */
RakuNode *raku_node_given(RakuNode *topic, RakuList *whens, RakuNode *deflt, int line) {
    RakuNode *n = alloc_node(RK_GIVEN, line);
    n->left     = topic;
    n->children = whens;
    n->right    = deflt;  /* default block, or NULL */
    return n;
}
/* RK_WHEN: match value in left; body block in right */
RakuNode *raku_node_when(RakuNode *val, RakuNode *body, int line) {
    RakuNode *n = alloc_node(RK_WHEN, line);
    n->left  = val;
    n->right = body;
    return n;
}
/* RK_ARR_GET: @arr[$i] — array name in sval, index in left */
RakuNode *raku_node_arr_get(char *arrname, RakuNode *idx, int line) {
    RakuNode *n = alloc_node(RK_ARR_GET, line);
    n->sval = arrname;
    n->left = idx;
    return n;
}
/* RK_ARR_SET: @arr[$i] = val — array name in sval, index in left, value in right */
RakuNode *raku_node_arr_set(char *arrname, RakuNode *idx, RakuNode *val, int line) {
    RakuNode *n = alloc_node(RK_ARR_SET, line);
    n->sval  = arrname;
    n->left  = idx;
    n->right = val;
    return n;
}
/* RK_HASH_GET: %h<key> / %h{$k} — hash name in sval, key expr in left */
RakuNode *raku_node_hash_get(char *hashname, RakuNode *key, int line) {
    RakuNode *n = alloc_node(RK_HASH_GET, line);
    n->sval = hashname;
    n->left = key;
    return n;
}
/* RK_HASH_SET: %h<key>=val — hash name in sval, key in left, value in right */
RakuNode *raku_node_hash_set(char *hashname, RakuNode *key, RakuNode *val, int line) {
    RakuNode *n = alloc_node(RK_HASH_SET, line);
    n->sval  = hashname;
    n->left  = key;
    n->right = val;
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
