%{
/*
 * sno.y — SNOBOL4 bison grammar for snoc  (revised: unified expression grammar)
 *
 * The original grammar had 16 SR + 140 RR conflicts because it tried to split
 * value-expr and pattern-expr at the TOKEN level using a PAT_BUILTIN token class.
 * LALR(1) state merging made that approach structurally unfixable.
 *
 * Fix (from the JVM instaparse grammar):
 *   - ONE expression grammar for everything.  No pat_expr / pat_alt / pat_cat /
 *     pat_cap / pat_atom productions.
 *   - No PAT_BUILTIN token.  Pattern primitives (LEN, POS, ...) are plain IDENT.
 *   - Subject/pattern/replacement split resolved at the STATEMENT level by
 *     position, exactly as the emitter already expects.
 *   - STAR IDENT always E_DEREF(E_VAR): consuming only IDENT (not factor) in the
 *     STAR rule prevents *IDENT LPAREN from being absorbed as a function call.
 *   - arglist RR conflict fixed: only one empty production.
 *   - DOLLAR STR special case removed: falls through normal DOLLAR factor path.
 *   - (expr, expr, ...) grouping parens produce E_ALT chain (cnd in JVM grammar).
 */

#include "snoc.h"
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
%token  SGOTO FGOTO NEWLINE _

%type <stmt>  stmt
%type <expr>  expr term factor atom primary opt_repl opt_expr
%type <go>    opt_goto goto_clauses
%type <sval>  opt_label glabel
%type <sval>  gclause_s gclause_f gclause_u
%type <al>    arglist arglist_ne

%left  PIPE AMP
%left  PLUS MINUS
%left  STAR SLASH
%right STARSTAR CARET
%right UMINUS UDEREF
%left  LBRACKET
%left  _
%nonassoc SUBJ

%%

program : stmtlist ;

stmtlist
    : /* empty */
    | stmtlist line
    ;

line
    : stmt NEWLINE      {
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

stmt
    : LABEL
        { Stmt *s=stmt_new(); s->label=$1; s->lineno=lineno_stmt; $$=s; }
    | opt_label COLON goto_clauses
        { Stmt *s=stmt_new(); s->label=$1; s->go=(SnoGoto*)$3;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label term opt_goto %prec SUBJ
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->go=(SnoGoto*)$3; s->lineno=lineno_stmt; $$=s; }
    | opt_label term EQ opt_repl opt_goto %prec SUBJ
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->replacement=$4; s->go=(SnoGoto*)$5;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label term _ expr opt_goto %prec SUBJ
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->pattern=$4; s->go=(SnoGoto*)$5;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label term _ expr EQ opt_repl opt_goto %prec SUBJ
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->pattern=$4; s->replacement=$6; s->go=(SnoGoto*)$7;
          s->lineno=lineno_stmt; $$=s; }
    ;

opt_label : /* empty */ { $$=NULL; } | LABEL { $$=$1; } ;

opt_repl
    : /* empty */  { Expr *e=expr_new(E_NULL); $$=e; }
    | expr         { $$=$1; }
    ;

opt_goto : /* empty */ { $$=NULL; } | COLON goto_clauses { $$=$2; } ;

expr
    : term                          { $$=$1; }
    | expr _ term               { $$=binop(E_CONCAT,$1,$3); }
    | expr AMP term                 { $$=binop(E_REDUCE,$1,$3); }
    | expr PLUS  term               { $$=binop(E_ADD,$1,$3); }
    | expr MINUS term               { $$=binop(E_SUB,$1,$3); }
    | expr PIPE  term               { $$=binop(E_ALT,$1,$3); }
    | expr DOT   primary            { $$=binop(E_COND,$1,$3); }
    | expr DOLLAR primary           { $$=binop(E_IMM,$1,$3); }
    ;

term
    : factor                        { $$=$1; }
    | term STAR   factor            { $$=binop(E_MUL,$1,$3); }
    | term SLASH  factor            { $$=binop(E_DIV,$1,$3); }
    | term STARSTAR factor          { $$=binop(E_POW,$1,$3); }
    | term CARET  factor            { $$=binop(E_POW,$1,$3); }
    ;

factor
    : atom                          { $$=$1; }
    | MINUS  factor %prec UMINUS    { $$=binop(E_NEG,NULL,$2); }
    | PLUS   factor %prec UMINUS    { $$=$2; }
    | DOLLAR factor %prec UDEREF    { $$=binop(E_DEREF,NULL,$2); }
    | DOT    factor %prec UDEREF    { $$=binop(E_COND,NULL,$2); }
    | STAR IDENT    %prec UDEREF    {
        /* *X — deferred pattern ref.  Consuming IDENT (not factor) is the key:
         * *foo(bar) = concat(deref(foo), call(bar)), NOT call(*foo, bar). */
        Expr *v=expr_new(E_VAR); v->sval=$2;
        $$=binop(E_DEREF,v,NULL); }
    | AT IDENT                      { Expr *e=expr_new(E_AT); e->sval=$2; $$=e; }
    ;

atom
    : primary                                   { $$=$1; }
    | atom LBRACKET arglist RBRACKET            {
        AL *al=$3; Expr *e=expr_new(E_INDEX);
        e->left=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    ;

primary
    : STR       { Expr *e=expr_new(E_STR);  e->sval=$1; $$=e; }
    | INT       { Expr *e=expr_new(E_INT);  e->ival=$1; $$=e; }
    | REAL      { Expr *e=expr_new(E_REAL); e->dval=$1; $$=e; }
    | KEYWORD   { Expr *e=expr_new(E_KEYWORD); e->sval=$1; $$=e; }
    | IDENT LPAREN arglist RPAREN
        { AL *al=$3; Expr *e=expr_new(E_CALL);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | IDENT     { Expr *e=expr_new(E_VAR); e->sval=$1; $$=e; }
    | LPAREN expr RPAREN            { $$=$2; }
    | LPAREN expr COMMA arglist_ne RPAREN
        { /* (a, b, c) — alternation grouping, becomes E_ALT chain */
          AL *al=$4; Expr *e=$2;
          for (int i=0;i<al->n;i++) e=binop(E_ALT,e,al->a[i]);
          free(al->a); free(al); $$=e; }
    ;

arglist
    : /* empty */   { $$=al_new(); }
    | arglist_ne    { $$=$1; }
    ;

arglist_ne
    : opt_expr                      { AL *al=al_new(); al_push(al,$1); $$=al; }
    | arglist_ne COMMA opt_expr     { al_push($1,$3); $$=$1; }
    ;

opt_expr
    : expr          { $$=$1; }
    | IDENT EQ expr
        { Expr *e=expr_new(E_ASSIGN);
          e->left=expr_new(E_VAR); e->left->sval=$1; e->right=$3; $$=e; }
    ;

goto_clauses
    : gclause_s     { SnoGoto *g=sgoto_new(); g->onsuccess=$1; $$=g; }
    | gclause_f     { SnoGoto *g=sgoto_new(); g->onfailure=$1; $$=g; }
    | gclause_u     { SnoGoto *g=sgoto_new(); g->uncond=$1; $$=g; }
    | goto_clauses gclause_s { ((SnoGoto*)$1)->onsuccess=$2; $$=$1; }
    | goto_clauses gclause_f { ((SnoGoto*)$1)->onfailure=$2; $$=$1; }
    | goto_clauses gclause_u { ((SnoGoto*)$1)->uncond=$2; $$=$1; }
    ;

gclause_s : SGOTO  LPAREN glabel RPAREN { $$=$3; } ;
gclause_f : FGOTO  LPAREN glabel RPAREN { $$=$3; } ;
gclause_u : LPAREN glabel RPAREN        { $$=$2; } ;

glabel
    : IDENT         { $$=$1; }
    | KEYWORD       { $$=$1; }
    | DOLLAR IDENT  { char *buf=malloc(strlen($2)+3);
                      sprintf(buf,"$%s",$2); $$=buf; }
    ;

%%

void snoc_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    parsed_program = prog;
}
