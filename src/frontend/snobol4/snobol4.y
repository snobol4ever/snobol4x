%code requires {
#include "scrip_cc.h"
#include "snobol4.h"
}
%code {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct { Program *prog; EXPR_t **result; } PP;
static Lex     *g_lx;
static void     sno4_stmt_commit_go(void*,Token,EXPR_t*,EXPR_t*,int,EXPR_t*,SnoGoto*);
static void     fixup_val(EXPR_t*);
static int      is_pat(EXPR_t*);
static EXPR_t  *parse_expr(Lex*);
}
%define api.prefix {snobol4_}
%define api.pure full
%parse-param { void *yyparse_param }
%union { EXPR_t *expr; Token tok; SnoGoto *go; }

/* Atoms */
%token <tok> T_IDENT T_FUNCTION T_KEYWORD T_END T_INT T_REAL T_STR
/* Statement structure */
%token <tok> T_LABEL T_GOTO_S T_GOTO_F T_GOTO_LPAREN T_GOTO_RPAREN T_STMT_END
/* Binary operators (WHITE op WHITE) */
%token T_ASSIGNMENT T_MATCH T_ALTERNATION T_ADDITION T_SUBTRACTION
%token T_MULTIPLICATION T_DIVISION T_EXPONENTIATION
%token T_IMMEDIATE_ASSIGN T_COND_ASSIGN
%token T_AMPERSAND T_AT_SIGN T_POUND T_PERCENT T_TILDE
/* Unary operators (no leading space) — named per SPITBOL Chapter 15 */
%token T_UN_AT_SIGN T_UN_TILDE T_UN_QUESTION_MARK T_UN_AMPERSAND
%token T_UN_PLUS T_UN_MINUS T_UN_ASTERISK T_UN_DOLLAR_SIGN T_UN_PERIOD
%token T_UN_EXCLAMATION T_UN_PERCENT T_UN_SLASH T_UN_POUND
%token T_UN_EQUAL T_UN_VERTICAL_BAR
/* Structural */
%token T_CONCAT T_COMMA T_LPAREN T_RPAREN T_LBRACK T_RBRACK T_LANGLE T_RANGLE

%type <expr> expr0 expr2 expr3 expr4 expr5 expr6 expr7 expr8
%type <expr> expr9 expr10 expr11 expr12 expr13 expr14 expr15 expr17
%type <expr> exprlist exprlist_ne opt_subject opt_pattern opt_repl
%type <tok>  opt_label
%type <go>   opt_goto goto_label_expr

%%
top        : program                                                                                { }
           ;
program    : program stmt | stmt                                                                    ;
stmt       : opt_label opt_subject opt_repl opt_goto T_STMT_END                      { sno4_stmt_commit_go(yyparse_param,$1,$2,NULL,($3!=NULL),$3,$4); }
           | opt_label expr2 T_MATCH opt_pattern opt_repl opt_goto T_STMT_END        { EXPR_t*sc=expr_binary(E_SCAN,$2,$4); sno4_stmt_commit_go(yyparse_param,$1,sc,NULL,($5!=NULL),$5,$6); }
           ;
opt_label  : T_LABEL                                                                              { $$=$1; }
           | /* empty */                                                                           { $$.sval=NULL;$$.ival=0;$$.lineno=0;$$.kind=0; }
           ;
opt_subject: expr3                                                                                { $$=$1; }
           | /* empty */                                                                           { $$=NULL; }
           ;
opt_pattern: expr3                                                                                 { $$=$1; }
           | /* empty */                                                                           { $$=NULL; }
           ;
opt_repl   : T_ASSIGNMENT expr0                                                                   { $$=$2; }
           | T_ASSIGNMENT                                                                          { EXPR_t*e=expr_new(E_QLIT);e->sval=strdup("");$$=e; }
           | /* empty */                                                                           { $$=NULL; }
           ;

/* goto_label_expr: T_GOTO_LPAREN/RPAREN are exclusive to GT state — no S/R conflict */
goto_label_expr
           : T_GOTO_LPAREN T_IDENT T_GOTO_RPAREN              { $$=sgoto_new(); $$->uncond=strdup($2.sval); }
           | T_GOTO_LPAREN T_END T_GOTO_RPAREN                { $$=sgoto_new(); $$->uncond=strdup($2.sval); }
           | T_GOTO_LPAREN T_FUNCTION T_GOTO_RPAREN           { $$=sgoto_new(); $$->uncond=strdup($2.sval); }
           | T_GOTO_LPAREN T_UN_DOLLAR_SIGN T_IDENT T_GOTO_RPAREN { $$=sgoto_new(); char buf[512]; snprintf(buf,sizeof buf,"$%s",$3.sval); $$->uncond=strdup(buf); }
           ;

opt_goto   : goto_label_expr                                  { $$=$1; }
           | T_GOTO_S goto_label_expr T_GOTO_F goto_label_expr {
               $$=sgoto_new();
               $$->onsuccess=$2->uncond; free($2);
               $$->onfailure=$4->uncond; free($4);
             }
           | T_GOTO_S goto_label_expr                         { $$=sgoto_new(); $$->onsuccess=$2->uncond; free($2); }
           | T_GOTO_F goto_label_expr T_GOTO_S goto_label_expr {
               $$=sgoto_new();
               $$->onfailure=$2->uncond; free($2);
               $$->onsuccess=$4->uncond; free($4);
             }
           | T_GOTO_F goto_label_expr                         { $$=sgoto_new(); $$->onfailure=$2->uncond; free($2); }
           | /* empty */                                       { $$=NULL; }
           ;

/* Expression grammar — levels match beauty.sno Expr0–Expr17 and SPITBOL manual priorities */
expr0      : expr2 T_ASSIGNMENT expr0                                                             { $$=expr_binary(E_ASSIGN,          $1,$3); }
           | expr2 T_MATCH      expr0                                                             { $$=expr_binary(E_SCAN,            $1,$3); }
           | expr2                                                                                 { $$=$1; }
           ;
expr2      : expr2 T_AMPERSAND  expr3                                                             { $$=expr_binary(E_OPSYN,           $1,$3); }
           | expr3                                                                                 { $$=$1; }
           ;
expr3      : expr3 T_ALTERNATION expr4                                                            { if($1->kind==E_ALT){expr_add_child($1,$3);$$=$1;}else{EXPR_t*a=expr_new(E_ALT);expr_add_child(a,$1);expr_add_child(a,$3);$$=a;} }
           | expr4                                                                                 { $$=$1; }
           ;
expr4      : expr4 T_CONCAT expr5                                                                           { if($1->kind==E_SEQ){expr_add_child($1,$3);$$=$1;}else{EXPR_t*s=expr_new(E_SEQ);expr_add_child(s,$1);expr_add_child(s,$3);$$=s;} }
           | expr5                                                                                 { $$=$1; }
           ;
expr5      : expr5 T_AT_SIGN    expr6                                                             { $$=expr_binary(E_OPSYN,           $1,$3); }
           | expr6                                                                                 { $$=$1; }
           ;
expr6      : expr6 T_ADDITION   expr7                                                             { $$=expr_binary(E_ADD,             $1,$3); }
           | expr6 T_SUBTRACTION expr7                                                            { $$=expr_binary(E_SUB,             $1,$3); }
           | expr7                                                                                 { $$=$1; }
           ;
expr7      : expr7 T_POUND      expr8                                                             { $$=expr_binary(E_MUL,             $1,$3); }
           | expr8                                                                                 { $$=$1; }
           ;
expr8      : expr8 T_DIVISION   expr9                                                             { $$=expr_binary(E_DIV,             $1,$3); }
           | expr9                                                                                 { $$=$1; }
           ;
expr9      : expr9 T_MULTIPLICATION expr10                                                        { $$=expr_binary(E_MUL,             $1,$3); }
           | expr10                                                                                { $$=$1; }
           ;
expr10     : expr10 T_PERCENT   expr11                                                            { $$=expr_binary(E_DIV,             $1,$3); }
           | expr11                                                                                { $$=$1; }
           ;
expr11     : expr12 T_EXPONENTIATION expr11                                                       { $$=expr_binary(E_POW,             $1,$3); }
           | expr12                                                                                { $$=$1; }
           ;
expr12     : expr12 T_IMMEDIATE_ASSIGN expr13                                                     { $$=expr_binary(E_CAPT_IMMED_ASGN,$1,$3); }
           | expr12 T_COND_ASSIGN      expr13                                                     { $$=expr_binary(E_CAPT_COND_ASGN, $1,$3); }
           | expr13                                                                                { $$=$1; }
           ;
expr13     : expr14 T_TILDE     expr13                                                            { $$=expr_binary(E_CAPT_COND_ASGN, $1,$3); }
           | expr14                                                                                { $$=$1; }
           ;
expr14     : T_UN_AT_SIGN      expr14                                                             { $$=expr_unary(E_CAPT_CURSOR,     $2); }
           | T_UN_TILDE        expr14                                                             { $$=expr_unary(E_INDIRECT,        $2); }
           | T_UN_QUESTION_MARK expr14                                                            { $$=expr_unary(E_INTERROGATE,     $2); }
           | T_UN_AMPERSAND    expr14                                                             { $$=expr_unary(E_OPSYN,           $2); }
           | T_UN_PLUS         expr14                                                             { $$=expr_unary(E_PLS,             $2); }
           | T_UN_MINUS        expr14                                                             { $$=expr_unary(E_MNS,             $2); }
           | T_UN_ASTERISK     expr14                                                             { $$=expr_unary(E_DEFER,           $2); }
           | T_UN_DOLLAR_SIGN  expr14                                                             { $$=expr_unary(E_INDIRECT,        $2); }
           | T_UN_PERIOD       expr14                                                             { $$=expr_unary(E_NAME,            $2); }
           | T_UN_EXCLAMATION  expr14                                                             { $$=expr_unary(E_POW,             $2); }  /* user-definable */
           | T_UN_PERCENT      expr14                                                             { $$=expr_unary(E_DIV,             $2); }  /* user-definable */
           | T_UN_SLASH        expr14                                                             { $$=expr_unary(E_DIV,             $2); }  /* user-definable */
           | T_UN_POUND        expr14                                                             { $$=expr_unary(E_MUL,             $2); }  /* user-definable */
           | T_UN_EQUAL        expr14                                                             { $$=expr_unary(E_ASSIGN,          $2); }  /* user-definable */
           | T_UN_VERTICAL_BAR expr14                                                             { $$=expr_unary(E_ALT,             $2); }  /* user-definable */
           | expr15                                                                                { $$=$1; }
           ;
expr15     : expr15 T_LBRACK exprlist T_RBRACK                                                  { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,$1);for(int j=0;j<$3->nchildren;j++)expr_add_child(i,$3->children[j]);free($3->children);free($3);$$=i; }
           | expr15 T_LANGLE exprlist T_RANGLE                                                  { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,$1);for(int j=0;j<$3->nchildren;j++)expr_add_child(i,$3->children[j]);free($3->children);free($3);$$=i; }
           | expr17                                                                                { $$=$1; }
           ;
exprlist   : exprlist_ne                                                                           { $$=$1; }
           | /* empty */                                                                           { $$=expr_new(E_NUL); }
           ;
exprlist_ne: exprlist_ne T_COMMA expr0                                                            { expr_add_child($1,$3);$$=$1; }
           | exprlist_ne T_COMMA                                                                  { expr_add_child($1,expr_new(E_NUL));$$=$1; }
           | expr0                                                                                 { EXPR_t*l=expr_new(E_NUL);expr_add_child(l,$1);$$=l; }
           ;
expr17     : T_LPAREN expr0 T_RPAREN                                                            { $$=$2; }
           | T_LPAREN expr0 T_COMMA exprlist_ne T_RPAREN                                       { EXPR_t*a=expr_new(E_ALT);expr_add_child(a,$2);for(int i=0;i<$4->nchildren;i++)expr_add_child(a,$4->children[i]);free($4->children);free($4);$$=a; }
           | T_LPAREN T_RPAREN                                                                  { $$=expr_new(E_NUL); }
           | T_FUNCTION T_LPAREN exprlist T_RPAREN                                             { EXPR_t*e=expr_new(E_FNC);e->sval=(char*)$1.sval;for(int i=0;i<$3->nchildren;i++)expr_add_child(e,$3->children[i]);free($3->children);free($3);$$=e; }
           | T_IDENT                                                                              { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)$1.sval;$$=e; }
           | T_END                                                                                { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)$1.sval;$$=e; }
           | T_KEYWORD                                                                            { EXPR_t*e=expr_new(E_KEYWORD);e->sval=(char*)$1.sval;$$=e; }
           | T_STR                                                                                { EXPR_t*e=expr_new(E_QLIT);   e->sval=(char*)$1.sval;$$=e; }
           | T_INT                                                                                { EXPR_t*e=expr_new(E_ILIT);   e->ival=$1.ival;$$=e; }
           | T_REAL                                                                               { EXPR_t*e=expr_new(E_FLIT);   e->dval=$1.dval;$$=e; }
           ;
%%
int snobol4_lex(YYSTYPE *yylval_param, void *yyparse_param) {
    (void)yyparse_param; Token t=lex_next(g_lx); yylval_param->tok=t;
    return t.kind;
}
void snobol4_error(void *p,const char *msg){(void)p;sno_error(g_lx?g_lx->lineno:0,"parse error: %s",msg);}
static void fixup_val(EXPR_t *e){ (void)e; /* SNOBOL4: no-op — E_SEQ never converted to E_CAT; runtime handles both */ }
static int is_pat(EXPR_t *e){
    if(!e) return 0;
    switch(e->kind){case E_ARB:case E_ARBNO:case E_CAPT_COND_ASGN:case E_CAPT_IMMED_ASGN:case E_CAPT_CURSOR:case E_DEFER:return 1;default:break;}
    for(int i=0;i<e->nchildren;i++) if(is_pat(e->children[i])) return 1;
    return 0;
}
static void sno4_stmt_commit_go(void*,Token,EXPR_t*,EXPR_t*,int,EXPR_t*,SnoGoto*);
/* DYN-59: sno4_stmt_commit_go — takes SnoGoto* directly from grammar.
 * Replaces sno4_stmt_commit + goto_field()/goto_label() re-lexing. */
static void sno4_stmt_commit_go(void *param,Token lbl,EXPR_t *subj,EXPR_t *pat,int has_eq,EXPR_t *repl,SnoGoto *go){
    PP *pp=(PP*)param;
    if(lbl.sval&&strcasecmp(lbl.sval,"EXPORT")==0){
        if(subj&&subj->kind==E_VAR&&subj->sval){
            ExportEntry*e=calloc(1,sizeof*e);e->name=strdup(subj->sval);
            for(char*p=e->name;*p;p++)*p=(char)toupper((unsigned char)*p);
            e->next=pp->prog->exports;pp->prog->exports=e;
        } return;
    }
    if(lbl.sval&&strcasecmp(lbl.sval,"IMPORT")==0){
        ImportEntry*e=calloc(1,sizeof*e);const char*n=subj&&subj->sval?subj->sval:"";
        char*dot1=strchr(n,'.');
        if(dot1){char*dot2=strchr(dot1+1,'.');
            if(dot2){e->lang=strndup(n,(size_t)(dot1-n));e->name=strndup(dot1+1,(size_t)(dot2-dot1-1));e->method=strdup(dot2+1);}
            else{e->lang=strdup("");e->name=strndup(n,(size_t)(dot1-n));e->method=strdup(dot1+1);}}
        else{e->lang=strdup("");e->name=strdup(n);e->method=strdup(n);}
        e->next=pp->prog->imports;pp->prog->imports=e;return;
    }
    STMT_t *s=stmt_new();s->lineno=lbl.lineno;
    if(lbl.sval){s->label=strdup(lbl.sval);s->is_end=lbl.ival||(strcasecmp(lbl.sval,"END")==0);}
    /* S=PR split: E_SCAN(subj, pat) from "X ? PAT" binary match operator */
    if(!pat && subj && subj->kind==E_SCAN && subj->nchildren==2) {
        EXPR_t *orig = subj;
        subj = orig->children[0];
        pat  = orig->children[1];
    }
    /* S=PR split: if subj is E_SEQ with first child a bare name, split into
     * subject=first_child, pattern=rest. Grammar puts everything in opt_subject. */
    if(!pat && subj && (subj->kind==E_SEQ) && subj->nchildren>=2) {
        EXPR_t *first = subj->children[0];
        if(first->kind==E_VAR || first->kind==E_KEYWORD) {
            int nc = subj->nchildren - 1;
            EXPR_t *rest;
            if(nc == 1) {
                rest = subj->children[1];
            } else {
                rest = expr_new(E_SEQ);
                for(int i=1;i<subj->nchildren;i++) expr_add_child(rest,subj->children[i]);
            }
            subj = first;
            pat  = rest;
        }
    }
    s->subject=subj; s->pattern=pat;
    if(s->subject) fixup_val(s->subject);
    if(has_eq){s->has_eq=1;s->replacement=repl;if(repl&&!is_pat(repl))fixup_val(repl);}
    s->go=go;
    if(!pp->prog->head) pp->prog->head=pp->prog->tail=s; else{pp->prog->tail->next=s;pp->prog->tail=s;}
    pp->prog->nstmts++;
}
static EXPR_t *parse_expr(Lex *lx){
    Program *prog=calloc(1,sizeof*prog);PP p={prog,NULL};g_lx=lx;snobol4_parse(&p);
    return prog->head?prog->head->subject:NULL;
}
Program *parse_program_tokens(Lex *stream){
    Program *prog=calloc(1,sizeof*prog);PP p={prog,NULL};g_lx=stream;snobol4_parse(&p);return prog;
}
Program *parse_program(LineArray *lines){(void)lines;return calloc(1,sizeof(Program));}
EXPR_t *parse_expr_from_str(const char *src){
    if(!src||!*src) return NULL;Lex lx={0};lex_open_str(&lx,src,(int)strlen(src),0);return parse_expr(&lx);
}
