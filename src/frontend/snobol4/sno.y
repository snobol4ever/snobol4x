%{
/*
 * sno.y — SNOBOL4 bison grammar for snoc
 *
 * Whitespace design (from beauty.sno S4_expression.sno):
 *
 *   __  — raw token: any run of spaces/tabs (lexer always emits this)
 *   __  — mandatory whitespace: one or more __ tokens
 *   _   — optional whitespace (gray): __ or empty
 *
 * Binary operators require __ on both sides:  expr __ OP __ term
 * Concat requires __:                          expr __ term
 * Unary operators need no leading space:       OP term
 * Inside parens/brackets _ (gray) is used:     LPAREN _ expr _ RPAREN
 *
 * Statement structure mirrors beauty.sno snoStmt:
 *   label __ subject __ pattern = replacement : goto
 *   The first __ separates label from subject,
 *   the second __ separates subject from pattern.
 *   Subject is a full expr (snoExpr14 level — unary prefix).
 *   Pattern is expr (snoExpr1 level — everything).
 *
 * This eliminates all lexer lookahead, bstack, PAT_BUILTIN, SUBJ tricks.
 * The grammar expresses what was previously smuggled into the lexer.
 */

#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Program *prog;
extern Program *parsed_program;

typedef struct { Expr **a; int n, cap; } AL;
static AL *al_new(void) {
    AL *al = calloc(1,sizeof *al); al->cap=4; al->a=malloc(4*sizeof(Expr*)); return al;
}
static void al_push(AL *al, Expr *e) {
    if (al->n >= al->cap) { al->cap*=2; al->a=realloc(al->a,al->cap*sizeof(Expr*)); }
    al->a[al->n++] = e;
}
static Expr *binop(EKind k, Expr *l, Expr *r) {
    Expr *e=expr_new(k); e->left=l; e->right=r; return e;
}

extern int  yylex(void);
extern void yyerror(const char *);
extern int  lineno_stmt;
%}

%union {
    char   *sval;
    long    ival;
    double  dval;
    struct Expr    *expr;
    void           *go;
    struct Stmt    *stmt;
    void           *al;
}

%token <sval>  LABEL IDENT KEYWORD STR END_LABEL
%token <ival>  INT
%token <dval>  REAL
%token  EQ COLON LPAREN RPAREN LBRACKET RBRACKET
%token  STARSTAR CARET PLUS MINUS STAR SLASH
%token  PIPE DOT DOLLAR AMP COMMA AT
%token  SGOTO FGOTO NEWLINE __

%type <stmt>  stmt
%type <expr>  expr concat_expr unary_expr postfix_expr primary
%type <expr>  opt_repl
%type <go>    opt_goto goto_clauses
%type <sval>  opt_label glabel
%type <sval>  gclause_s gclause_f gclause_u
%type <al>    arglist arglist_ne

/* No precedence declarations needed — the grammar structure itself
 * encodes precedence via the beauty.sno snoExpr level hierarchy.
 * __/__ distinction handles binary vs unary without tricks. */

%%

program : stmtlist ;

stmtlist
    : /* empty */
    | stmtlist line
    ;

line
    : stmt NEWLINE {
            if ($1) {
                $1->next = NULL;
                if (!prog->head) prog->head = prog->tail = $1;
                else { prog->tail->next = $1; prog->tail = $1; }
                prog->nstmts++;
            }
        }
    | END_LABEL NEWLINE {
            Stmt *s = stmt_new();
            s->label = strdup("END");
            s->is_end = 1;
            s->lineno = lineno_stmt;
            if (!prog->head) prog->head = prog->tail = s;
            else { prog->tail->next = s; prog->tail = s; }
            prog->nstmts++;
        }
    | NEWLINE           { }
    | error NEWLINE     { yyerrok; }
    ;


/* _ = optional whitespace / gray */
_  : /* empty */ | __ ;

/* ---------------------------------------------------------------
 * Statement
 *
 * Mirrors beauty.sno snoStmt:
 *   label __ subject FENCE(
 *     ε                             -- label/subject only
 *     __ pattern                    -- subject __ pattern
 *     __ pattern = replacement      -- with replacement
 *   ) opt_goto
 *
 * Subject is unary_expr (snoExpr14 — all unary prefix operators).
 * Pattern is expr (snoExpr1 level — everything including =).
 * The __ between subject and pattern is the SAME token class as
 * concat — but the grammar position disambiguates unambiguously:
 * after a unary_expr at statement level, __ either starts a pattern
 * (if followed by expr) or precedes = (replacement) or goto.
 * --------------------------------------------------------------- */

stmt
    : opt_label
        { Stmt *s=stmt_new(); s->label=$1; s->lineno=lineno_stmt; $$=s; }
    | opt_label COLON goto_clauses
        { Stmt *s=stmt_new(); s->label=$1; s->go=(SnoGoto*)$3;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label __ unary_expr opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$3;
          s->go=(SnoGoto*)$4; s->lineno=lineno_stmt; $$=s; }
    | opt_label __ unary_expr EQ opt_repl opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$3;
          s->replacement=$5; s->go=(SnoGoto*)$6;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label __ unary_expr __ expr opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$3;
          s->pattern=$5; s->go=(SnoGoto*)$6;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label __ unary_expr __ expr EQ opt_repl opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$3;
          s->pattern=$5; s->replacement=$7; s->go=(SnoGoto*)$8;
          s->lineno=lineno_stmt; $$=s; }
    ;

opt_label : /* empty */ { $$=NULL; } | LABEL { $$=$1; } ;

opt_repl
    : /* empty */  { Expr *e=expr_new(E_NULL); $$=e; }
    | __ expr      { $$=$2; }
    ;

opt_goto : /* empty */ { $$=NULL; } | _ COLON _ goto_clauses { $$=$4; } ;

/* ---------------------------------------------------------------
 * Expression hierarchy — mirrors beauty.sno snoExpr levels
 *
 * expr         = snoExpr1  (assignment =, pattern match ?)
 * concat_expr  = snoExpr4  (concat via __)
 * unary_expr   = snoExpr14 (all unary prefix operators)
 * postfix_expr = snoExpr15/16 (subscript [])
 * primary      = snoExpr17 (atoms, calls, parens)
 *
 * Binary operators all require __ on both sides (beauty.sno $'op' =
 * snoWhite op snoWhite).  This means PLUS/MINUS/etc. without
 * surrounding spaces can only be unary — no %prec needed.
 * --------------------------------------------------------------- */

/* snoExpr1: assignment and pattern-match (right-associative by recursion) */
expr
    : concat_expr                               { $$=$1; }
    | concat_expr __ EQ __ expr                 { $$=binop(E_ASSIGN,$1,$5); }
    | concat_expr __ PIPE __ expr               { $$=binop(E_ALT,$1,$5); }
    | concat_expr __ AMP __ expr                { $$=binop(E_REDUCE,$1,$5); }
    | concat_expr __ PLUS __ expr               { $$=binop(E_ADD,$1,$5); }
    | concat_expr __ MINUS __ expr              { $$=binop(E_SUB,$1,$5); }
    | concat_expr __ STAR __ expr               { $$=binop(E_MUL,$1,$5); }
    | concat_expr __ SLASH __ expr              { $$=binop(E_DIV,$1,$5); }
    | concat_expr __ CARET __ expr              { $$=binop(E_POW,$1,$5); }
    | concat_expr __ STARSTAR __ expr           { $$=binop(E_POW,$1,$5); }
    | concat_expr __ DOT __ expr                { $$=binop(E_COND,$1,$5); }
    | concat_expr __ DOLLAR __ expr             { $$=binop(E_IMM,$1,$5); }
    | concat_expr __ AT __ expr                 { $$=binop(E_AT,$1,$5); }
    ;

/* snoExpr4: concatenation — requires __ between terms */
concat_expr
    : unary_expr                                { $$=$1; }
    | concat_expr __ unary_expr                 { $$=binop(E_CONCAT,$1,$3); }
    ;

/* snoExpr14: unary prefix operators — no leading space required */
unary_expr
    : postfix_expr                              { $$=$1; }
    | PLUS   unary_expr                         { $$=$2; }
    | MINUS  unary_expr                         { $$=binop(E_NEG,NULL,$2); }
    | STAR   unary_expr                         { $$=binop(E_DEREF,NULL,$2); }
    | DOLLAR unary_expr                         { $$=binop(E_DEREF,NULL,$2); }
    | DOT    unary_expr                         { $$=binop(E_COND,NULL,$2); }
    | AT     unary_expr                         { Expr *e=expr_new(E_AT); e->right=$2; $$=e; }
    | PIPE   unary_expr                         { $$=binop(E_ALT,NULL,$2); }
    | CARET  unary_expr                         { $$=binop(E_POW,NULL,$2); }
    | AMP    unary_expr                         { $$=binop(E_REDUCE,NULL,$2); }
    | SLASH  unary_expr                         { $$=binop(E_DIV,NULL,$2); }
    ;

/* snoExpr15/16: postfix subscript */
postfix_expr
    : primary                                   { $$=$1; }
    | postfix_expr LBRACKET _ arglist _ RBRACKET {
        AL *al=$4; Expr *e=expr_new(E_INDEX);
        e->left=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    ;

/* snoExpr17: atoms */
primary
    : STR       { Expr *e=expr_new(E_STR);  e->sval=$1; $$=e; }
    | INT       { Expr *e=expr_new(E_INT);  e->ival=$1; $$=e; }
    | REAL      { Expr *e=expr_new(E_REAL); e->dval=$1; $$=e; }
    | KEYWORD   { Expr *e=expr_new(E_KEYWORD); e->sval=$1; $$=e; }
    | IDENT LPAREN _ arglist _ RPAREN
        { AL *al=$4; Expr *e=expr_new(E_CALL);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | IDENT     { Expr *e=expr_new(E_VAR); e->sval=$1; $$=e; }
    | LPAREN _ expr _ RPAREN               { $$=$3; }
    | LPAREN _ expr COMMA _ arglist_ne _ RPAREN
        { /* (a, b, c) — alternation grouping */
          AL *al=$6; Expr *e=$3;
          for (int i=0;i<al->n;i++) e=binop(E_ALT,e,al->a[i]);
          free(al->a); free(al); $$=e; }
    ;

arglist
    : /* empty */   { $$=al_new(); }
    | arglist_ne    { $$=$1; }
    ;

arglist_ne
    : expr                          { AL *al=al_new(); al_push(al,$1); $$=al; }
    | arglist_ne _ COMMA _ expr     { al_push($1,$5); $$=$1; }
    ;

goto_clauses
    : gclause_s     { SnoGoto *g=sgoto_new(); g->onsuccess=$1; $$=g; }
    | gclause_f     { SnoGoto *g=sgoto_new(); g->onfailure=$1; $$=g; }
    | gclause_u     { SnoGoto *g=sgoto_new(); g->uncond=$1; $$=g; }
    | goto_clauses gclause_s { ((SnoGoto*)$1)->onsuccess=$2; $$=$1; }
    | goto_clauses gclause_f { ((SnoGoto*)$1)->onfailure=$2; $$=$1; }
    | goto_clauses gclause_u { ((SnoGoto*)$1)->uncond=$2; $$=$1; }
    ;

gclause_s : SGOTO  _ LPAREN _ glabel _ RPAREN { $$=$5; } ;
gclause_f : FGOTO  _ LPAREN _ glabel _ RPAREN { $$=$5; } ;
gclause_u :        _ LPAREN _ glabel _ RPAREN { $$=$4; } ;

glabel
    : IDENT         { $$=$1; }
    | KEYWORD       { $$=$1; }
    | DOLLAR IDENT  { char *buf=malloc(strlen($2)+3);
                      sprintf(buf,"$%s",$2); $$=buf; }
    ;

%%

void sno_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    parsed_program = prog;
}
