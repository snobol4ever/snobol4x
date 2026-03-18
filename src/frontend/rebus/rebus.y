%{
/*
 * rebus.y  —  Bison grammar for the Rebus language (Griswold TR 84-9)
 *
 * Grammar structure (top-down):
 *
 *   program   ::= decl* EOF
 *   decl      ::= function_decl | record_decl
 *
 *   record_decl   ::= 'record' IDENT '(' idlist ')'
 *   function_decl ::= 'function' IDENT '(' idlist ')'
 *                       ['local' idlist ';']
 *                       ['initial' stmt ';']
 *                       stmt*
 *                     'end'
 *
 *   stmt  ::= expr_stmt | if_stmt | unless_stmt | while_stmt | until_stmt
 *           | repeat_stmt | for_stmt | case_stmt
 *           | 'exit' | 'next' | 'fail' | 'stop'
 *           | 'return' [expr]
 *           | match_stmt | replace_stmt | repln_stmt
 *           | compound_stmt
 *
 *   expr  ::= assignment | comparison | concat | arith | unary | primary
 *
 * Semicolons are inserted by the lexer (virtual semicolons after certain
 * tokens at end-of-line).  The grammar uses ';' as the statement terminator.
 *
 * Operator precedence (low → high, matches Rebus/Icon):
 *   := :=: ||:= +:= -:=         (assignments, right-assoc)
 *   |                            (pattern alternation)
 *   || &                         (string/pattern concat)
 *   < <= > >= = ~= == ~== << <<= >> >>=  (comparisons)
 *   + -                          (addition, left-assoc)
 *   * / %                        (multiplication, left-assoc)
 *   ^ **                         (exponentiation, right-assoc)
 *   unary: - + ~ \ / ! @ $ .    (right-assoc, high)
 *
 * Pattern operators:
 *   expr ? pat            (match)
 *   expr ? pat <- expr    (replace)
 *   expr ?- pat           (replace with empty)
 *   pat . var             (conditional capture)
 *   pat $ var             (immediate capture)
 */

#include "rebus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RProgram *prog;
extern RProgram *rebus_parsed_program;
extern int       rebus_nerrors;

/* ---- helper: make binary expr ---- */
static RExpr *rbinop(REKind k, RExpr *l, RExpr *r, int lineno) {
    RExpr *e = rexpr_new(k, lineno);
    e->left  = l;
    e->right = r;
    return e;
}

/* ---- helpers for growing arrays ---- */
typedef struct { char **a; int n, cap; } SAL;  /* string array list */
static SAL *sal_new(void) {
    SAL *s = calloc(1, sizeof *s);
    s->cap = 4; s->a = malloc(4 * sizeof(char *));
    return s;
}
static void sal_push(SAL *s, char *v) {
    if (s->n >= s->cap) { s->cap *= 2; s->a = realloc(s->a, s->cap * sizeof(char *)); }
    s->a[s->n++] = v;
}

typedef struct { RExpr **a; int n, cap; } EAL;  /* expr array list */
static EAL *eal_new(void) {
    EAL *e = calloc(1, sizeof *e);
    e->cap = 4; e->a = malloc(4 * sizeof(RExpr *));
    return e;
}
static void eal_push(EAL *e, RExpr *v) {
    if (e->n >= e->cap) { e->cap *= 2; e->a = realloc(e->a, e->cap * sizeof(RExpr *)); }
    e->a[e->n++] = v;
}

typedef struct { RStmt **a; int n, cap; } STAL; /* stmt array list */
static STAL *stal_new(void) {
    STAL *s = calloc(1, sizeof *s);
    s->cap = 8; s->a = malloc(8 * sizeof(RStmt *));
    return s;
}
static void stal_push(STAL *s, RStmt *v) {
    if (s->n >= s->cap) { s->cap *= 2; s->a = realloc(s->a, s->cap * sizeof(RStmt *)); }
    s->a[s->n++] = v;
}

extern int  yylex(void);
extern void yyerror(const char *);
extern int  yylineno;
%}

%union {
    char       *sval;
    long        ival;
    double      dval;
    RExpr      *expr;
    RStmt      *stmt;
    RDecl      *decl;
    RCase      *rcase;
    void       *sal;    /* SAL* */
    void       *eal;    /* EAL* */
    void       *stal;   /* STAL* */
}

/* ---- tokens ---- */
%token <sval>  T_IDENT T_STR T_KEYWORD
%token <ival>  T_INT
%token <dval>  T_REAL

/* keywords */
%token T_CASE T_DEFAULT T_DO T_ELSE T_END T_EXIT T_FAIL
%token T_FOR T_FROM T_FUNCTION T_BY T_IF T_INITIAL T_LOCAL
%token T_NEXT T_OF T_RECORD T_REPEAT T_RETURN T_STOP
%token T_THEN T_TO T_UNLESS T_UNTIL T_WHILE

/* multi-char operators */
%token T_ASSIGN      /* :=   */
%token T_EXCHANGE    /* :=:  */
%token T_ADDASSIGN   /* +:=  */
%token T_SUBASSIGN   /* -:=  */
%token T_CATASSIGN   /* ||:= */
%token T_QUESTMINUS  /* ?-   */
%token T_ARROW       /* <-   */
%token T_STRCAT      /* ||   */
%token T_STARSTAR    /* **   */
%token T_NE          /* ~=   */
%token T_GE          /* >=   */
%token T_LE          /* <=   */
%token T_SEQ         /* ==   */
%token T_SNE         /* ~==  */
%token T_SGT         /* >>   */
%token T_SGE         /* >>=  */
%token T_SLT         /* <<   */
%token T_SLE         /* <<=  */
%token T_PLUSCOLON   /* +:   (substring range) */

/* ---- types ---- */
%type <decl>   decl function_decl record_decl
%type <stmt>   stmt stmt_body compound_stmt expr_as_stmt
%type <stmt>   if_stmt unless_stmt while_stmt until_stmt
%type <stmt>   repeat_stmt for_stmt case_stmt
%type <stmt>   stmt_list stmt_list_ne
%type <rcase>  caselist caseclause
%type <expr>   expr opt_expr
%type <expr>   assign_expr alt_expr cat_expr cmp_expr
%type <expr>   add_expr mul_expr pow_expr unary_expr postfix_expr primary
%type <expr>   pat_expr
%type <sal>    idlist_ne opt_idlist
%type <sal>    opt_locals opt_params
%type <eal>    arglist arglist_ne
%type <stmt>   opt_initial

%expect 1   /* one benign shift/reduce: opt_semi in record_decl vs decl_list ';' */

/* Dangling-else disambiguation: prefer shift (attach else to nearest if).
 * %nonassoc LOWER_THAN_ELSE gives the no-else rule lower precedence. */
%nonassoc LOWER_THAN_ELSE
%nonassoc T_ELSE
%right T_ASSIGN T_EXCHANGE T_ADDASSIGN T_SUBASSIGN T_CATASSIGN
%left  '|'
%left  T_STRCAT '&'
%left  '<' T_LE '>' T_GE '=' T_NE T_SEQ T_SNE T_SLT T_SLE T_SGT T_SGE
%left  '+' '-'
%left  '*' '/' '%'
%right T_STARSTAR '^'
%right UMINUS UPLUS UTILDE UBACK USLASH UBANG UAT UDOLLAR UDOT

%%

/* ================================================================
 * Top-level
 * ================================================================ */

program
    : decl_list             { /* rebus_parsed_program already set */ }
    ;

decl_list
    : /* empty */
    | decl_list decl        {
            if ($2) {
                $2->next = NULL;
                if (!prog->decls) prog->decls = $2;
                else {
                    RDecl *d = prog->decls;
                    while (d->next) d = d->next;
                    d->next = $2;
                }
                prog->ndecls++;
            }
        }
    | decl_list ';'         { /* empty statement at top level */ }
    | decl_list error ';'   { yyerrok; }
    ;

decl
    : function_decl         { $$ = $1; }
    | record_decl           { $$ = $1; }
    ;

/* opt_semi absorbs the semicolon that the lexer inserts after ')' at end-of-line
 * in function/record headers, and at end of control-structure headers. */
opt_semi
    : /* empty */
    | ';'
    ;

/* ================================================================
 * Record declaration:   record ident ( idlist )
 * ================================================================ */

record_decl
    : T_RECORD T_IDENT '(' opt_idlist ')' opt_semi
        {
            RDecl *d   = rdecl_new(RD_RECORD, yylineno);
            d->name    = $2;
            SAL *sl    = $4;
            d->fields  = sl->a;
            d->nfields = sl->n;
            free(sl);
            $$ = d;
        }
    ;

/* ================================================================
 * Function declaration:
 *   function ident ( params )
 *     [ local idlist ; ]
 *     [ initial stmt ; ]
 *     stmt*
 *   end
 * ================================================================ */

function_decl
    : T_FUNCTION T_IDENT '(' opt_params ')' opt_semi
        opt_locals
        opt_initial
        stmt_list
      T_END
        {
            RDecl *d    = rdecl_new(RD_FUNCTION, yylineno);
            d->name     = $2;
            SAL *ps     = (SAL*)$4;
            d->params   = ps->a;
            d->nparams  = ps->n;
            free(ps);
            SAL *ls     = (SAL*)$7;
            d->locals   = ls->a;
            d->nlocals  = ls->n;
            free(ls);
            d->initial  = $8;
            d->body     = $9;
            $$ = d;
        }
    ;

opt_params
    : /* empty */   { $$ = (void*)sal_new(); }
    | idlist_ne     { $$ = $1; }
    ;

opt_locals
    : /* empty */              { $$ = (void*)sal_new(); }
    | T_LOCAL idlist_ne ';'   { $$ = $2; }
    ;

opt_initial
    : /* empty */              { $$ = NULL; }
    | T_INITIAL compound_stmt               { $$ = $2; }
    | T_INITIAL stmt ';'                    { $$ = $2; }
    ;

/* A stmt_list returns the first stmt of a linked list (via ->next).
 * Both rules return RStmt* (typed as %type <stmt>). */
stmt_list
    : /* empty */   { $$ = NULL; }
    | stmt_list_ne  { $$ = $1; }
    ;

stmt_list_ne
    : stmt ';'                  { $$ = $1; }
    | compound_stmt             { $$ = $1; }
    | stmt_list_ne stmt ';'     {
            if (!$1) { $$ = $2; }
            else {
                RStmt *t = $1; while (t->next) t = t->next;
                t->next = $2; $$ = $1;
            }
        }
    | stmt_list_ne compound_stmt {
            if (!$1) { $$ = $2; }
            else {
                RStmt *t = $1; while (t->next) t = t->next;
                t->next = $2; $$ = $1;
            }
        }
    | stmt_list_ne error ';'    { yyerrok; $$ = $1; }
    ;

/* ================================================================
 * idlist helpers
 * ================================================================ */

idlist_ne
    : T_IDENT               { SAL *s = sal_new(); sal_push(s, $1); $$ = s; }
    | idlist_ne ',' T_IDENT { sal_push($1, $3); $$ = $1; }
    ;

opt_idlist
    : /* empty */   { $$ = sal_new(); }
    | idlist_ne     { $$ = $1; }
    ;

/* ================================================================
 * Statements
 * ================================================================ */

stmt
    : expr_as_stmt          { $$ = $1; }
    | if_stmt               { $$ = $1; }
    | unless_stmt           { $$ = $1; }
    | while_stmt            { $$ = $1; }
    | until_stmt            { $$ = $1; }
    | repeat_stmt           { $$ = $1; }
    | for_stmt              { $$ = $1; }
    | case_stmt             { $$ = $1; }
    | T_EXIT                { $$ = rstmt_new(RS_EXIT,   yylineno); }
    | T_NEXT                { $$ = rstmt_new(RS_NEXT,   yylineno); }
    | T_FAIL                { $$ = rstmt_new(RS_FAIL,   yylineno); }
    | T_STOP                { $$ = rstmt_new(RS_STOP,   yylineno); }
    | T_RETURN opt_expr     {
            RStmt *s = rstmt_new(RS_RETURN, yylineno);
            s->retval = $2; $$ = s;
        }
    | compound_stmt         { $$ = $1; }
    ;

/* An expression used as a statement; also handles match/replace/repln */
expr_as_stmt
    : expr                          {
            RStmt *s = rstmt_new(RS_EXPR, $1->lineno);
            s->expr = $1; $$ = s;
        }
    | expr '?' pat_expr             {
            RStmt *s = rstmt_new(RS_MATCH, $1->lineno);
            s->expr = $1; s->pat = $3; $$ = s;
        }
    | expr '?' pat_expr T_ARROW expr {
            RStmt *s = rstmt_new(RS_REPLACE, $1->lineno);
            s->expr = $1; s->pat = $3; s->repl = $5; $$ = s;
        }
    | expr T_QUESTMINUS pat_expr    {
            RStmt *s = rstmt_new(RS_REPLN, $1->lineno);
            s->expr = $1; s->pat = $3; $$ = s;
        }
    ;

/* ---- compound statement: { stmt ; stmt ; … } ---- */
compound_stmt
    : '{' stmt_list '}'     {
            /* collect the linked-list into a RS_COMPOUND */
            RStmt *s = rstmt_new(RS_COMPOUND, yylineno);
            /* count and store */
            int n = 0; RStmt *t = $2; while (t) { n++; t = t->next; }
            s->stmts  = malloc(n * sizeof(RStmt *));
            s->nstmts = n;
            t = $2;
            for (int i = 0; i < n; i++) { s->stmts[i] = t; t = t->next; }
            $$ = s;
        }
    ;

/* stmt_body is any stmt — compound_stmt is already a stmt production */
stmt_body
    : stmt          { $$ = $1; }
    ;

/* ---- if / unless ---- */
if_stmt
    : T_IF stmt T_THEN opt_semi stmt_body %prec LOWER_THAN_ELSE
        {
            RStmt *s = rstmt_new(RS_IF, yylineno);
            s->body  = $2;
            s->alt   = $5;
            s->repl  = NULL;
            $$ = s;
        }
    | T_IF stmt T_THEN opt_semi stmt_body T_ELSE opt_semi stmt_body
        {
            RStmt *s = rstmt_new(RS_IF, yylineno);
            s->body  = $2;
            s->alt   = $5;
            s->repl  = (RExpr*)$8;
            $$ = s;
        }
    ;

unless_stmt
    : T_UNLESS stmt T_THEN opt_semi stmt_body
        {
            RStmt *s = rstmt_new(RS_UNLESS, yylineno);
            s->body  = $2;
            s->alt   = $5;
            $$ = s;
        }
    ;

/* ---- while / until ---- */
while_stmt
    : T_WHILE stmt T_DO opt_semi stmt_body
        {
            RStmt *s = rstmt_new(RS_WHILE, yylineno);
            s->body  = $2;
            s->alt   = $5;
            $$ = s;
        }
    ;

until_stmt
    : T_UNTIL stmt T_DO opt_semi stmt_body
        {
            RStmt *s = rstmt_new(RS_UNTIL, yylineno);
            s->body  = $2;
            s->alt   = $5;
            $$ = s;
        }
    ;

/* ---- repeat ---- */
repeat_stmt
    : T_REPEAT opt_semi stmt_body
        {
            RStmt *s = rstmt_new(RS_REPEAT, yylineno);
            s->alt   = $3;
            $$ = s;
        }
    ;

/* ---- for ---- */
for_stmt
    : T_FOR T_IDENT T_FROM expr T_TO expr T_DO opt_semi stmt_body
        {
            RStmt *s    = rstmt_new(RS_FOR, yylineno);
            s->for_var  = $2;
            s->for_from = $4;
            s->for_to   = $6;
            s->for_by   = NULL;
            s->alt      = $9;
            $$ = s;
        }
    | T_FOR T_IDENT T_FROM expr T_TO expr T_BY expr T_DO opt_semi stmt_body
        {
            RStmt *s    = rstmt_new(RS_FOR, yylineno);
            s->for_var  = $2;
            s->for_from = $4;
            s->for_to   = $6;
            s->for_by   = $8;
            s->alt      = $11;
            $$ = s;
        }
    ;

/* ---- case ---- */
case_stmt
    : T_CASE expr T_OF '{' caselist '}'
        {
            RStmt *s      = rstmt_new(RS_CASE, yylineno);
            s->case_expr  = $2;
            s->cases      = $5;
            $$ = s;
        }
    ;

caselist
    : caseclause            { $$ = $1; }
    | caselist ';' caseclause {
            /* append $3 to end of $1 */
            RCase *c = $1; while (c->next) c = c->next;
            c->next = $3; $$ = $1;
        }
    | caselist ';'          { $$ = $1; }
    ;

caseclause
    : expr ':' stmt_body
        {
            RCase *c  = rcase_new(yylineno);
            c->guard  = $1;
            c->body   = $3;
            $$ = c;
        }
    | T_DEFAULT ':' stmt_body
        {
            RCase *c     = rcase_new(yylineno);
            c->is_default = 1;
            c->body       = $3;
            $$ = c;
        }
    ;

/* ================================================================
 * Expressions
 *
 * Rebus has:
 *   - Assignment (right-associative): := :=: +:= -:= ||:=
 *   - Pattern alternation: |
 *   - String/pattern concat: || (strcat), & (patcat)
 *   - Comparisons: = ~= < <= > >= == ~== << <<= >> >>=
 *   - Arithmetic: + - * / % ^ **
 *   - Unary: - + ~ \ / ! @ $ .
 *   - Postfix: f(args), a[i], a[i+:n]
 * ================================================================ */

expr
    : assign_expr           { $$ = $1; }
    ;

/* Assignment: right-recursive */
assign_expr
    : alt_expr                              { $$ = $1; }
    | alt_expr T_ASSIGN    assign_expr      { $$ = rbinop(RE_ASSIGN,    $1, $3, yylineno); }
    | alt_expr T_EXCHANGE  assign_expr      { $$ = rbinop(RE_EXCHANGE,  $1, $3, yylineno); }
    | alt_expr T_ADDASSIGN assign_expr      { $$ = rbinop(RE_ADDASSIGN, $1, $3, yylineno); }
    | alt_expr T_SUBASSIGN assign_expr      { $$ = rbinop(RE_SUBASSIGN, $1, $3, yylineno); }
    | alt_expr T_CATASSIGN assign_expr      { $$ = rbinop(RE_CATASSIGN, $1, $3, yylineno); }
    ;

/* Pattern alternation */
alt_expr
    : cat_expr                              { $$ = $1; }
    | alt_expr '|' cat_expr                 { $$ = rbinop(RE_ALT,    $1, $3, yylineno); }
    ;

/* Concatenation: || (string), & (pattern) */
cat_expr
    : cmp_expr                              { $$ = $1; }
    | cat_expr T_STRCAT cmp_expr            { $$ = rbinop(RE_STRCAT, $1, $3, yylineno); }
    | cat_expr '&' cmp_expr                 { $$ = rbinop(RE_PATCAT, $1, $3, yylineno); }
    ;

/* Comparisons */
cmp_expr
    : add_expr                              { $$ = $1; }
    | cmp_expr '='    add_expr              { $$ = rbinop(RE_EQ,  $1, $3, yylineno); }
    | cmp_expr T_NE   add_expr              { $$ = rbinop(RE_NE,  $1, $3, yylineno); }
    | cmp_expr '<'    add_expr              { $$ = rbinop(RE_LT,  $1, $3, yylineno); }
    | cmp_expr T_LE   add_expr              { $$ = rbinop(RE_LE,  $1, $3, yylineno); }
    | cmp_expr '>'    add_expr              { $$ = rbinop(RE_GT,  $1, $3, yylineno); }
    | cmp_expr T_GE   add_expr              { $$ = rbinop(RE_GE,  $1, $3, yylineno); }
    | cmp_expr T_SEQ  add_expr              { $$ = rbinop(RE_SEQ, $1, $3, yylineno); }
    | cmp_expr T_SNE  add_expr              { $$ = rbinop(RE_SNE, $1, $3, yylineno); }
    | cmp_expr T_SLT  add_expr              { $$ = rbinop(RE_SLT, $1, $3, yylineno); }
    | cmp_expr T_SLE  add_expr              { $$ = rbinop(RE_SLE, $1, $3, yylineno); }
    | cmp_expr T_SGT  add_expr              { $$ = rbinop(RE_SGT, $1, $3, yylineno); }
    | cmp_expr T_SGE  add_expr              { $$ = rbinop(RE_SGE, $1, $3, yylineno); }
    ;

/* Additive */
add_expr
    : mul_expr                              { $$ = $1; }
    | add_expr '+' mul_expr                 { $$ = rbinop(RE_ADD, $1, $3, yylineno); }
    | add_expr '-' mul_expr                 { $$ = rbinop(RE_SUB, $1, $3, yylineno); }
    ;

/* Multiplicative */
mul_expr
    : pow_expr                              { $$ = $1; }
    | mul_expr '*' pow_expr                 { $$ = rbinop(RE_MUL, $1, $3, yylineno); }
    | mul_expr '/' pow_expr                 { $$ = rbinop(RE_DIV, $1, $3, yylineno); }
    | mul_expr '%' pow_expr                 { $$ = rbinop(RE_MOD, $1, $3, yylineno); }
    ;

/* Exponentiation (right-associative) */
pow_expr
    : unary_expr                            { $$ = $1; }
    | unary_expr '^'       pow_expr         { $$ = rbinop(RE_POW, $1, $3, yylineno); }
    | unary_expr T_STARSTAR pow_expr        { $$ = rbinop(RE_POW, $1, $3, yylineno); }
    ;

/* Unary prefix operators */
unary_expr
    : postfix_expr                          { $$ = $1; }
    | '-' unary_expr %prec UMINUS           { $$ = rbinop(RE_NEG,   NULL, $2, yylineno); }
    | '+' unary_expr %prec UPLUS            { $$ = rbinop(RE_POS,   NULL, $2, yylineno); }
    | '~' unary_expr %prec UTILDE          { $$ = rbinop(RE_NOT,   NULL, $2, yylineno); }
    | '\\' unary_expr %prec UBACK          { $$ = rbinop(RE_NOT,   NULL, $2, yylineno); }
    | '/' unary_expr %prec USLASH          { $$ = rbinop(RE_VALUE, NULL, $2, yylineno); }
    | '!' unary_expr %prec UBANG           { $$ = rbinop(RE_BANG,  NULL, $2, yylineno); }
    | '@' T_IDENT %prec UAT                 {
            RExpr *e = rexpr_new(RE_CURSOR, yylineno);
            e->sval = $2; $$ = e;
        }
    | '$' unary_expr %prec UDOLLAR         { $$ = rbinop(RE_DEREF,  NULL, $2, yylineno); }
    | '.' unary_expr %prec UDOT            { $$ = rbinop(RE_COND,   NULL, $2, yylineno); }
    ;

/* Postfix: function call, subscript, substring */
postfix_expr
    : primary                               { $$ = $1; }
    | postfix_expr '(' arglist ')'
        {
            /* function call or pattern constructor */
            EAL *al = $3;
            /* If left is RE_VAR, convert to RE_CALL */
            RExpr *e = rexpr_new(RE_CALL, $1->lineno);
            if ($1->kind == RE_VAR) {
                e->sval = $1->sval; free($1);
            } else {
                /* complex callee: store in left */
                e->left = $1;
            }
            e->args  = al->a;
            e->nargs = al->n;
            free(al);
            $$ = e;
        }
    | postfix_expr '[' arglist ']'
        {
            EAL *al = $3;
            RExpr *e = rexpr_new(RE_SUB_IDX, $1->lineno);
            e->left  = $1;
            e->args  = al->a;
            e->nargs = al->n;
            free(al);
            $$ = e;
        }
    | postfix_expr '[' expr T_PLUSCOLON expr ']'
        {
            /* a[i +: n]  — substring starting at i, length n */
            RExpr *range = rexpr_new(RE_RANGE, $3->lineno);
            range->left  = $3;
            range->right = $5;
            RExpr *e = rexpr_new(RE_SUB_IDX, $1->lineno);
            e->left  = $1;
            e->args  = malloc(1 * sizeof(RExpr *));
            e->args[0] = range;
            e->nargs = 1;
            $$ = e;
        }
    /* Pattern capture operators (postfix on patterns) */
    | postfix_expr '.' primary
        {
            $$ = rbinop(RE_COND, $1, $3, yylineno);
        }
    | postfix_expr '$' primary
        {
            $$ = rbinop(RE_IMM, $1, $3, yylineno);
        }
    ;

/* Primary */
primary
    : T_STR         { RExpr *e = rexpr_new(RE_STR,     yylineno); e->sval = $1; $$ = e; }
    | T_INT         { RExpr *e = rexpr_new(RE_INT,     yylineno); e->ival = $1; $$ = e; }
    | T_REAL        { RExpr *e = rexpr_new(RE_REAL,    yylineno); e->dval = $1; $$ = e; }
    | T_KEYWORD     { RExpr *e = rexpr_new(RE_KEYWORD, yylineno); e->sval = $1; $$ = e; }
    | T_IDENT       { RExpr *e = rexpr_new(RE_VAR,     yylineno); e->sval = $1; $$ = e; }
    | '(' expr ')'  { $$ = $2; }
    ;

/* Pattern expression (used after ? — same as expr for now; the ?
 * operator distinguishes context at the statement level) */
pat_expr
    : expr      { $$ = $1; }
    ;

/* Optional expression */
opt_expr
    : /* empty */   { $$ = NULL; }
    | expr          { $$ = $1; }
    ;

/* Argument list */
arglist
    : /* empty */   { $$ = eal_new(); }
    | arglist_ne    { $$ = $1; }
    ;

arglist_ne
    : expr                      { EAL *al = eal_new(); eal_push(al, $1); $$ = al; }
    | arglist_ne ',' expr       { eal_push($1, $3); $$ = $1; }
    | arglist_ne ','            { eal_push($1, NULL); $$ = $1; }  /* trailing comma / empty slot */
    ;

%%

void rebus_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    rebus_parsed_program = prog;
}
