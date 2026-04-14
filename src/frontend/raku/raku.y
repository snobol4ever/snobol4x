%define api.prefix {raku_yy}

%code requires {
/*
 * raku.y — Tiny-Raku Bison grammar
 *
 * Phase 1 subset: literals, $scalar/@array vars, my, say, print,
 * arithmetic, string concat (~), comparisons, range (..), for,
 * if/else, while, sub, gather, take, return.
 *
 * Produces RakuNode* AST. raku_ast.h defines all node types.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
/* %code requires is emitted into raku.tab.h — makes RakuNode/RakuList
 * available to any file that includes the generated header. */
#include "raku_ast.h"
}

%{
#include "raku_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  raku_yylex(void);
extern int  raku_get_lineno(void);
void raku_yyerror(const char *msg) {
    fprintf(stderr, "raku parse error line %d: %s\n", raku_get_lineno(), msg);
}

/* Root of the parsed program — filled by the start rule */
RakuNode *raku_parse_result = NULL;
%}

%union {
    long       ival;
    double     dval;
    char      *sval;
    RakuNode  *node;
    RakuList  *list;
}

/* ── Tokens ──────────────────────────────────────────────────────────── */
%token <ival> LIT_INT
%token <dval> LIT_FLOAT
%token <sval> LIT_STR LIT_INTERP_STR
%token <sval> VAR_SCALAR VAR_ARRAY IDENT

%token KW_MY KW_SAY KW_PRINT KW_IF KW_ELSE KW_ELSIF KW_WHILE KW_FOR
%token KW_SUB KW_GATHER KW_TAKE KW_RETURN
%token KW_GIVEN KW_WHEN KW_DEFAULT

%token OP_RANGE OP_RANGE_EX   /* ..  ..^ */
%token OP_ARROW                /* ->  */
%token OP_EQ OP_NE OP_LE OP_GE /* == != <= >= */
%token OP_SEQ OP_SNE           /* eq  ne */
%token OP_AND OP_OR            /* &&  || */
%token OP_BIND                 /* :=  */
%token OP_DIV                  /* div */

/* ── Types for non-terminals ─────────────────────────────────────────── */
%type <node> program stmt expr atom range_expr cmp_expr add_expr
%type <node> mul_expr unary_expr postfix_expr call_expr block
%type <node> if_stmt while_stmt for_stmt sub_decl given_stmt
%type <list> stmt_list arg_list param_list when_list

/* ── Precedence (low → high) ─────────────────────────────────────────── */
%right '='  OP_BIND
%left  OP_OR
%left  OP_AND
%left  '!'
%left  OP_EQ OP_NE '<' '>' OP_LE OP_GE OP_SEQ OP_SNE
%left  OP_RANGE OP_RANGE_EX
%left  '~'
%left  '+' '-'
%left  '*' '/' '%' OP_DIV
%right UMINUS

%%

/* ── Top level ───────────────────────────────────────────────────────── */
program
    : stmt_list
        { raku_parse_result = raku_node_block($1, raku_get_lineno()); }
    ;

stmt_list
    : /* empty */          { $$ = raku_list_new(); }
    | stmt_list stmt       { $$ = raku_list_append($1, $2); }
    ;

/* ── Statements ──────────────────────────────────────────────────────── */
stmt
    : KW_MY VAR_SCALAR '=' expr ';'
        { $$ = raku_node_my_scalar($2, $4, raku_get_lineno()); }
    | KW_MY VAR_ARRAY '=' expr ';'
        { $$ = raku_node_my_array($2, $4, raku_get_lineno()); }
    | KW_SAY expr ';'
        { $$ = raku_node_say($2, raku_get_lineno()); }
    | KW_PRINT expr ';'
        { $$ = raku_node_print($2, raku_get_lineno()); }
    | KW_TAKE expr ';'
        { $$ = raku_node_take($2, raku_get_lineno()); }
    | KW_RETURN expr ';'
        { $$ = raku_node_return($2, raku_get_lineno()); }
    | KW_RETURN ';'
        { $$ = raku_node_return(NULL, raku_get_lineno()); }
    | VAR_SCALAR '=' expr ';'
        { $$ = raku_node_assign($1, $3, raku_get_lineno()); }
    | expr ';'
        { $$ = raku_node_expr_stmt($1, raku_get_lineno()); }
    | if_stmt          { $$ = $1; }
    | while_stmt       { $$ = $1; }
    | for_stmt         { $$ = $1; }
    | given_stmt       { $$ = $1; }
    | sub_decl         { $$ = $1; }
    ;

/* ── Control flow ────────────────────────────────────────────────────── */
if_stmt
    : KW_IF '(' expr ')' block
        { $$ = raku_node_if($3, $5, NULL, raku_get_lineno()); }
    | KW_IF '(' expr ')' block KW_ELSE block
        { $$ = raku_node_if($3, $5, $7, raku_get_lineno()); }
    | KW_IF '(' expr ')' block KW_ELSE if_stmt
        { $$ = raku_node_if($3, $5, $7, raku_get_lineno()); }
    ;

while_stmt
    : KW_WHILE '(' expr ')' block
        { $$ = raku_node_while($3, $5, raku_get_lineno()); }
    ;

/* for RANGE -> $var { body }
   for @arr  -> $var { body }  */
for_stmt
    : KW_FOR expr OP_ARROW VAR_SCALAR block
        { $$ = raku_node_for($2, $4, $5, raku_get_lineno()); }
    | KW_FOR expr block
        { $$ = raku_node_for($2, NULL, $3, raku_get_lineno()); }
    ;

/* given $x { when val { } ... default { } }
 * Scalar smart-match only: numeric == or string eq chosen by literal type. */
given_stmt
    : KW_GIVEN expr '{' when_list '}'
        { $$ = raku_node_given($2, $4, NULL, raku_get_lineno()); }
    | KW_GIVEN expr '{' when_list KW_DEFAULT block '}'
        { $$ = raku_node_given($2, $4, $6,   raku_get_lineno()); }
    ;

when_list
    : /* empty */               { $$ = raku_list_new(); }
    | when_list KW_WHEN expr block
        { $$ = raku_list_append($1, raku_node_when($3, $4, raku_get_lineno())); }
    ;

sub_decl
    : KW_SUB IDENT '(' param_list ')' block
        { $$ = raku_node_sub($2, $4, $6, raku_get_lineno()); }
    | KW_SUB IDENT '(' ')' block
        { $$ = raku_node_sub($2, raku_list_new(), $5, raku_get_lineno()); }
    ;

param_list
    : VAR_SCALAR                   { $$ = raku_list_append(raku_list_new(),
                                         raku_node_var_scalar($1, raku_get_lineno())); }
    | param_list ',' VAR_SCALAR    { $$ = raku_list_append($1,
                                         raku_node_var_scalar($3, raku_get_lineno())); }
    ;

/* ── Block: { stmt* } ────────────────────────────────────────────────── */
block
    : '{' stmt_list '}'
        { $$ = raku_node_block($2, raku_get_lineno()); }
    ;

/* ── Expressions (precedence climbing via grammar layers) ────────────── */
expr
    : VAR_SCALAR '=' expr          { $$ = raku_node_assign($1, $3, raku_get_lineno()); }
    | KW_GATHER block              { $$ = raku_node_gather($2, raku_get_lineno()); }
    | cmp_expr                     { $$ = $1; }
    ;

cmp_expr
    : cmp_expr OP_AND add_expr     { $$ = raku_node_binop(RK_AND, $1, $3, raku_get_lineno()); }
    | cmp_expr OP_OR  add_expr     { $$ = raku_node_binop(RK_OR,  $1, $3, raku_get_lineno()); }
    | add_expr OP_EQ  add_expr     { $$ = raku_node_binop(RK_EQ,  $1, $3, raku_get_lineno()); }
    | add_expr OP_NE  add_expr     { $$ = raku_node_binop(RK_NE,  $1, $3, raku_get_lineno()); }
    | add_expr '<'    add_expr     { $$ = raku_node_binop(RK_LT,  $1, $3, raku_get_lineno()); }
    | add_expr '>'    add_expr     { $$ = raku_node_binop(RK_GT,  $1, $3, raku_get_lineno()); }
    | add_expr OP_LE  add_expr     { $$ = raku_node_binop(RK_LE,  $1, $3, raku_get_lineno()); }
    | add_expr OP_GE  add_expr     { $$ = raku_node_binop(RK_GE,  $1, $3, raku_get_lineno()); }
    | add_expr OP_SEQ add_expr     { $$ = raku_node_binop(RK_SEQ, $1, $3, raku_get_lineno()); }
    | add_expr OP_SNE add_expr     { $$ = raku_node_binop(RK_SNE, $1, $3, raku_get_lineno()); }
    | range_expr                   { $$ = $1; }
    ;

range_expr
    : add_expr OP_RANGE    add_expr { $$ = raku_node_binop(RK_RANGE,    $1, $3, raku_get_lineno()); }
    | add_expr OP_RANGE_EX add_expr { $$ = raku_node_binop(RK_RANGE_EX, $1, $3, raku_get_lineno()); }
    | add_expr                      { $$ = $1; }
    ;

add_expr
    : add_expr '+' mul_expr        { $$ = raku_node_binop(RK_ADD,    $1, $3, raku_get_lineno()); }
    | add_expr '-' mul_expr        { $$ = raku_node_binop(RK_SUBTRACT,    $1, $3, raku_get_lineno()); }
    | add_expr '~' mul_expr        { $$ = raku_node_binop(RK_STRCAT, $1, $3, raku_get_lineno()); }
    | mul_expr                     { $$ = $1; }
    ;

mul_expr
    : mul_expr '*'     unary_expr  { $$ = raku_node_binop(RK_MUL, $1, $3, raku_get_lineno()); }
    | mul_expr '/'     unary_expr  { $$ = raku_node_binop(RK_DIV, $1, $3, raku_get_lineno()); }
    | mul_expr '%'     unary_expr  { $$ = raku_node_binop(RK_MOD, $1, $3, raku_get_lineno()); }
    | mul_expr OP_DIV  unary_expr  { $$ = raku_node_binop(RK_IDIV,$1, $3, raku_get_lineno()); }
    | unary_expr                   { $$ = $1; }
    ;

unary_expr
    : '-' unary_expr %prec UMINUS  { $$ = raku_node_unop(RK_NEG, $2, raku_get_lineno()); }
    | '!' unary_expr               { $$ = raku_node_unop(RK_NOT, $2, raku_get_lineno()); }
    | postfix_expr                 { $$ = $1; }
    ;

postfix_expr
    : call_expr                    { $$ = $1; }
    ;

call_expr
    : IDENT '(' arg_list ')'      { $$ = raku_node_call($1, $3, raku_get_lineno()); }
    | IDENT '(' ')'               { $$ = raku_node_call($1, raku_list_new(), raku_get_lineno()); }
    | atom                        { $$ = $1; }
    ;

arg_list
    : expr                        { $$ = raku_list_append(raku_list_new(), $1); }
    | arg_list ',' expr           { $$ = raku_list_append($1, $3); }
    ;

/* ── Atoms ───────────────────────────────────────────────────────────── */
atom
    : LIT_INT                     { $$ = raku_node_int($1,  raku_get_lineno()); }
    | LIT_FLOAT                   { $$ = raku_node_float($1, raku_get_lineno()); }
    | LIT_STR                     { $$ = raku_node_str($1,  raku_get_lineno()); }
    | LIT_INTERP_STR              { $$ = raku_node_interp_str($1, raku_get_lineno()); }
    | VAR_SCALAR                  { $$ = raku_node_var_scalar($1, raku_get_lineno()); }
    | VAR_ARRAY                   { $$ = raku_node_var_array($1,  raku_get_lineno()); }
    | IDENT                       { $$ = raku_node_ident($1, raku_get_lineno()); }
    | '(' expr ')'                { $$ = $2; }
    ;

%%
