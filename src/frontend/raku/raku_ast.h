/*
 * raku_ast.h — Tiny-Raku AST node definitions
 *
 * All nodes are RakuNode*. Node kind is RakuKind enum.
 * Lists of nodes use RakuList (dynamic array).
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#ifndef RAKU_AST_H
#define RAKU_AST_H

#include <stddef.h>

/* ── Node kinds ──────────────────────────────────────────────────────── */
typedef enum {
    /* Literals */
    RK_INT, RK_FLOAT, RK_STR, RK_INTERP_STR,    /* Variables */
    RK_VAR_SCALAR, RK_VAR_ARRAY, RK_IDENT,
    /* Binary operators */
    RK_ADD, RK_SUBTRACT, RK_MUL, RK_DIV, RK_MOD, RK_IDIV,
    RK_STRCAT,                          /* ~ */
    RK_EQ, RK_NE, RK_LT, RK_GT, RK_LE, RK_GE,
    RK_SEQ, RK_SNE,                     /* eq ne */
    RK_AND, RK_OR,
    RK_RANGE, RK_RANGE_EX,              /* ..  ..^ */
    /* Unary operators */
    RK_NEG, RK_NOT,
    /* Statements */
    RK_MY_SCALAR, RK_MY_ARRAY,          /* my $x = e  /  my @a = e */
    RK_ASSIGN,                          /* $x = e */
    RK_SAY, RK_PRINT,                   /* say / print */
    RK_TAKE,                            /* take expr */
    RK_RETURN,                          /* return expr? */
    RK_EXPR_STMT,                       /* expr; */
    /* Compound */
    RK_BLOCK,                           /* { stmt* } */
    RK_IF,                              /* if (cond) block else? */
    RK_WHILE,                           /* while (cond) block */
    RK_FOR,                             /* for range/arr -> $v block */
    RK_SUBDEF,                          /* sub name(params) block */
    RK_GIVEN,                           /* given $x { when_list } */
    RK_WHEN,                            /* when val { block } */
    RK_DEFAULT,                         /* default { block } */
    RK_CALL,                            /* name(args) */
    RK_GATHER,                          /* gather { block } */
} RakuKind;

/* ── Forward declarations ────────────────────────────────────────────── */
typedef struct RakuNode RakuNode;
typedef struct RakuList RakuList;

/* ── Dynamic list of RakuNode* ───────────────────────────────────────── */
struct RakuList {
    RakuNode **items;
    int        count;
    int        cap;
};

/* ── AST node ────────────────────────────────────────────────────────── */
struct RakuNode {
    RakuKind  kind;
    int       lineno;
    /* payload — which fields are used depends on kind */
    long      ival;          /* RK_INT */
    double    dval;          /* RK_FLOAT */
    char     *sval;          /* RK_STR, RK_VAR_SCALAR, RK_VAR_ARRAY, RK_IDENT,
                                RK_MY_SCALAR, RK_MY_ARRAY, RK_ASSIGN: var name,
                                RK_SUB: sub name, RK_CALL: func name,
                                RK_FOR: pointy-block var name (NULL means use $_) */
    RakuNode *left;          /* binary/unary lhs; RK_IF cond; RK_WHILE cond;
                                RK_FOR iterable; RK_GATHER body;
                                RK_MY_SCALAR/ARRAY and RK_ASSIGN: the rhs expr;
                                RK_SAY/PRINT/TAKE/RETURN: the expr (RETURN may be NULL) */
    RakuNode *right;         /* binary rhs; RK_IF then-block */
    RakuNode *extra;         /* RK_IF else-block; RK_FOR body */
    RakuList *children;      /* RK_BLOCK stmts; RK_CALL args;
                                RK_SUB: params (body is in ->right) */
};

/* ── List API ────────────────────────────────────────────────────────── */
RakuList *raku_list_new(void);
RakuList *raku_list_append(RakuList *lst, RakuNode *node);

/* ── Node constructors ───────────────────────────────────────────────── */
RakuNode *raku_node_int(long v, int line);
RakuNode *raku_node_float(double v, int line);
RakuNode *raku_node_str(char *s, int line);
RakuNode *raku_node_interp_str(char *s, int line); /* double-quoted with $var interpolation */
RakuNode *raku_node_var_scalar(char *name, int line);
RakuNode *raku_node_var_array(char *name, int line);
RakuNode *raku_node_ident(char *name, int line);

RakuNode *raku_node_binop(RakuKind op, RakuNode *l, RakuNode *r, int line);
RakuNode *raku_node_unop(RakuKind op, RakuNode *operand, int line);

RakuNode *raku_node_my_scalar(char *name, RakuNode *init, int line);
RakuNode *raku_node_my_array(char *name, RakuNode *init, int line);
RakuNode *raku_node_assign(char *name, RakuNode *val, int line);
RakuNode *raku_node_say(RakuNode *expr, int line);
RakuNode *raku_node_print(RakuNode *expr, int line);
RakuNode *raku_node_take(RakuNode *expr, int line);
RakuNode *raku_node_return(RakuNode *expr, int line);
RakuNode *raku_node_expr_stmt(RakuNode *expr, int line);

RakuNode *raku_node_block(RakuList *stmts, int line);
RakuNode *raku_node_if(RakuNode *cond, RakuNode *then_blk,
                        RakuNode *else_blk, int line);
RakuNode *raku_node_while(RakuNode *cond, RakuNode *body, int line);
RakuNode *raku_node_for(RakuNode *iter, char *var, RakuNode *body, int line);
RakuNode *raku_node_sub(char *name, RakuList *params, RakuNode *body, int line);
RakuNode *raku_node_call(char *name, RakuList *args, int line);
RakuNode *raku_node_gather(RakuNode *body, int line);
RakuNode *raku_node_given(RakuNode *topic, RakuList *whens, RakuNode *deflt, int line);
RakuNode *raku_node_when(RakuNode *val, RakuNode *body, int line);

/* ── Parse entry point ───────────────────────────────────────────────── */
/* Call after setting up yyin or a string buffer.
 * Returns the program root (RK_BLOCK of top-level stmts), or NULL on error. */
RakuNode *raku_parse_string(const char *src);

/* Global set by the bison start rule */
extern RakuNode *raku_parse_result;

#endif /* RAKU_AST_H */
