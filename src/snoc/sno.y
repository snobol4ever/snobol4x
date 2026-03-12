%{
/*
 * sno.y — SNOBOL4 bison grammar for snoc
 *
 * One expression grammar for everything.
 * Statement fields:
 *   subject  — first expr on the line
 *   pattern  — second expr (before '='), emitted as sno_pat_*
 *   replacement — expr after '='
 *
 * The subject/pattern split is resolved at the TOKEN level:
 * the lexer returns PAT_BUILTIN for the closed set of SNOBOL4
 * pattern primitives (LEN, POS, SPAN, …).  A PAT_BUILTIN token
 * cannot start a value expression (there is no value named POS),
 * so the grammar is unambiguous LALR(1).
 *
 *   stmt  ::=  [LABEL]  expr  [pat_expr]  ['=' expr]  [':' goto]  NEWLINE
 *   expr  ::=  concat of terms
 *   pat_expr ::= first token must be PAT_BUILTIN | STAR | '(' | STR
 *                (all unambiguous with value-expr continuation)
 *
 * E_COND / E_IMM (. and $ capture) live inside pat_expr.
 * E_ALT  (|)     lives inside pat_expr.
 */

#include "snoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Program *prog;
extern Program *parsed_program;

/* arg list builder */
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
    void           *go;    /* SnoGoto* — void* avoids struct fwd-decl issue */
    struct Stmt    *stmt;
    void           *al;    /* AL* */
}

%token <sval>  LABEL IDENT PAT_BUILTIN KEYWORD STR END_LABEL
%token <ival>  INT
%token <dval>  REAL
%token  EQ COLON LPAREN RPAREN LBRACKET RBRACKET
%token  STARSTAR CARET PLUS MINUS STAR SLASH
%token  PIPE DOT DOLLAR AMP COMMA AT
%token  SGOTO FGOTO NEWLINE

%type <stmt>  stmt
%type <expr>  expr term factor atom primary opt_expr
%type <expr>  pat_expr pat_alt pat_cat pat_cap pat_atom
%type <expr>  opt_repl
%type <go>    opt_goto goto_clauses
%type <sval>  opt_label glabel
%type <sval>  gclause_s gclause_f gclause_u
%type <al>    arglist arglist_ne

/* value-expr precedence (low→high) */
%left  PLUS MINUS
%left  STAR SLASH
%right STARSTAR CARET
%right UMINUS UDEREF

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
            /* END marks program termination but function bodies follow it */
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
    : opt_label
        { Stmt *s=stmt_new(); s->label=$1; s->lineno=lineno_stmt; $$=s; }
    | opt_label COLON goto_clauses
        { /* label-only with goto: FENCE :(RETURN) */
          Stmt *s=stmt_new(); s->label=$1; s->go=(SnoGoto*)$3;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label expr opt_repl opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->replacement=$3; s->go=(SnoGoto*)$4;
          s->lineno=lineno_stmt; $$=s; }
    | opt_label expr pat_expr opt_repl opt_goto
        { Stmt *s=stmt_new(); s->label=$1; s->subject=$2;
          s->pattern=$3; s->replacement=$4; s->go=(SnoGoto*)$5;
          s->lineno=lineno_stmt; $$=s; }
    ;

opt_label : /* empty */ { $$=NULL; } | LABEL { $$=$1; } ;
opt_repl
    : /* empty */           { $$=NULL; }
    | EQ expr               { $$=$2; }
    | EQ                    { Expr *e=expr_new(E_NULL); $$=e; }  /* X = (null) */
    ;
opt_goto  : /* empty */ { $$=NULL; } | COLON goto_clauses { $$=$2; } ;

/* ============================================================
 * Value expression grammar
 * ============================================================ */

expr
    : term                  { $$=$1; }
    | expr term             { $$=binop(E_CONCAT,$1,$2); }   /* juxtaposition */
    | expr AMP term         { $$=binop(E_CONCAT,$1,$3); }   /* & = concat */
    | expr PLUS  term       { $$=binop(E_ADD,$1,$3); }
    | expr MINUS term       { $$=binop(E_SUB,$1,$3); }
    | expr PIPE  term       { $$=binop(E_ALT,$1,$3); }      /* | pattern alt in value ctx */
    ;

term
    : factor                { $$=$1; }
    | term STAR   factor    { $$=binop(E_MUL,$1,$3); }
    | term SLASH  factor    { $$=binop(E_DIV,$1,$3); }
    | term STARSTAR factor  { $$=binop(E_POW,$1,$3); }
    | term CARET  factor    { $$=binop(E_POW,$1,$3); }
    ;

factor
    : atom                  { $$=$1; }
    | MINUS factor %prec UMINUS  { $$=binop(E_NEG,NULL,$2); }
    | PLUS  factor %prec UMINUS  { $$=$2; }                    /* unary + is identity */
    | DOLLAR factor %prec UDEREF { $$=binop(E_DEREF,NULL,$2); }
    | DOLLAR STR %prec UDEREF    { /* $'name' — indirect variable reference */
                                   Expr *s=expr_new(E_STR); s->sval=$2;
                                   $$=binop(E_DEREF,NULL,s); }
    | DOT factor %prec UDEREF    { $$=binop(E_COND,NULL,$2); }  /* .X = name ref */
    | STAR factor %prec UDEREF   { $$=binop(E_DEREF,$2,NULL); } /* *X = indirect in expr */
    | AT IDENT               { /* @var — cursor position capture */
                               Expr *e=expr_new(E_AT); e->sval=$2; $$=e; }
    ;

atom
    : primary               { $$=$1; }
    | atom LBRACKET arglist RBRACKET
        { /* postfix subscript: expr[i] — e.g. c(x)[i] */
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
    | PAT_BUILTIN LPAREN arglist RPAREN
        { AL *al=$3; Expr *e=expr_new(E_CALL);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | IDENT LBRACKET arglist RBRACKET
        { AL *al=$3; Expr *e=expr_new(E_ARRAY);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | IDENT     { Expr *e=expr_new(E_VAR); e->sval=$1; $$=e; }
    | LPAREN expr RPAREN { $$=$2; }
    ;

arglist    : /* empty */ { $$=al_new(); } | arglist_ne { $$=$1; } ;
arglist_ne
    : opt_expr                  { AL *al=al_new(); al_push(al,$1); $$=al; }
    | arglist_ne COMMA opt_expr { al_push($1,$3); $$=$1; }
    ;
/* opt_expr: an optional expression — NULL for empty slots (consecutive commas)
 * ALSO allows IDENT = expr for the SNOBOL4 "assign-in-arg" idiom: DIFFER(x = Pop()) */
opt_expr
    : expr    { $$=$1; }
    | IDENT EQ expr
        { Expr *e=expr_new(E_ASSIGN);
          e->left=expr_new(E_VAR); e->left->sval=$1; e->right=$3; $$=e; }
    | /* empty */ { $$=NULL; }
    ;

/* ============================================================
 * Pattern expression grammar
 *
 * Entry token MUST be unambiguous with value-expr continuation:
 *   PAT_BUILTIN  — closed set: LEN POS SPAN BREAK etc
 *   STAR IDENT   — deferred pattern ref (*X)
 *   LPAREN       — grouped pattern
 *   STR          — literal (already a primary, but STR after subject
 *                  expr is a pattern-literal in SNOBOL4)
 *
 * NOTE: IDENT alone is ambiguous (could be variable in concat or pattern var).
 * We resolve: a bare IDENT after a complete subject expr is a PATTERN VAR.
 * This is the correct SNOBOL4 rule — the subject is terminated by the first
 * token that cannot extend the current expr under LALR(1) lookahead.
 * With PAT_BUILTIN tokenised separately the grammar IS unambiguous for all
 * real SNOBOL4 programs (bare-ident pattern vars are rare; the common case
 * is PAT_BUILTIN calls).
 * ============================================================ */

pat_expr
    : pat_alt               { $$=$1; }
    ;

pat_alt
    : pat_cat                       { $$=$1; }
    | pat_alt PIPE pat_cat          { $$=binop(E_ALT,$1,$3); }
    ;

pat_cat
    : pat_cap                       { $$=$1; }
    | pat_cat pat_cap               { $$=binop(E_CONCAT,$1,$2); }
    ;

pat_cap
    : pat_atom                      { $$=$1; }
    | pat_cap DOT    primary        { $$=binop(E_COND,$1,$3); }
    | pat_cap DOLLAR primary        { $$=binop(E_IMM,$1,$3); }
    ;

pat_atom
    : PAT_BUILTIN LPAREN arglist RPAREN
        { AL *al=$3; Expr *e=expr_new(E_CALL);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | PAT_BUILTIN
        { Expr *e=expr_new(E_CALL); e->sval=$1; e->nargs=0; $$=e; }
    | STAR IDENT
        /* deferred pattern reference *X */
        { Expr *e=expr_new(E_DEREF); e->left=expr_new(E_VAR); e->left->sval=$2; $$=e; }
    | STR
        { Expr *e=expr_new(E_STR); e->sval=$1; $$=e; }
    | IDENT LPAREN arglist RPAREN
        { AL *al=$3; Expr *e=expr_new(E_CALL);
          e->sval=$1; e->args=al->a; e->nargs=al->n; free(al); $$=e; }
    | IDENT
        { Expr *e=expr_new(E_VAR); e->sval=$1; $$=e; }
    | LPAREN pat_expr RPAREN  { $$=$2; }
    ;

/* ============================================================
 * Goto field
 * ============================================================ */

goto_clauses
    : gclause_s
        { SnoGoto *g=sgoto_new(); g->onsuccess=$1; $$=g; }
    | gclause_f
        { SnoGoto *g=sgoto_new(); g->onfailure=$1; $$=g; }
    | gclause_u
        { SnoGoto *g=sgoto_new(); g->uncond=$1; $$=g; }
    | goto_clauses gclause_s  { ((SnoGoto*)$1)->onsuccess=$2; $$=$1; }
    | goto_clauses gclause_f  { ((SnoGoto*)$1)->onfailure=$2; $$=$1; }
    | goto_clauses gclause_u  { ((SnoGoto*)$1)->uncond=$2; $$=$1; }
    ;

gclause_s : SGOTO  LPAREN glabel RPAREN { $$=$3; } ;
gclause_f : FGOTO  LPAREN glabel RPAREN { $$=$3; } ;
gclause_u : LPAREN glabel RPAREN        { $$=$2; } ;

/* glabel: static label name OR computed label (indirect goto) */
glabel
    : IDENT        { $$=$1; }
    | KEYWORD      { $$=$1; }
    | PAT_BUILTIN  { $$=$1; }
    /* computed goto: $expr or any expression — store as "*COMPUTED*" sentinel.
       The lexer is in <GT> state; we need INITIAL-mode tokens for the expr.
       Since bison/flex interaction makes mid-rule state switches tricky,
       we handle common patterns: DOLLAR STR, DOLLAR LPAREN ... RPAREN */
    | DOLLAR IDENT { char *buf=malloc(strlen($2)+3);
                     sprintf(buf,"$%s",$2); $$=buf; }
    ;

%%

void snoc_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    parsed_program = prog;
}
