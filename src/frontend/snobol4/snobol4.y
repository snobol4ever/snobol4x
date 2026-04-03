/*
 * parse.y — SNOBOL4 expression parser (M-PARSE-1)
 *
 * Zero shift/reduce conflicts.  The lexer resolves binary vs unary:
 *   " + "  (space op space)  → TK_PLUS   (binary)
 *   "+X"   (op then non-sp)  → TK_PLUS  (unary)
 * So every operator appears in exactly one grammar position.
 *
 * Level map (beauty.sno Expr0–Expr17):
 *   expr0   = ?     right-assoc  assign/scan
 *   expr2   &       left-assoc   opsyn
 *   expr3   |       n-ary        alternation
 *   expr4   concat  n-ary        juxtaposition
 *   expr5   @       left-assoc   cursor-capture (binary)
 *   expr6   + -     left-assoc   add/sub
 *   expr7   #       left-assoc   user-binary
 *   expr8   /       left-assoc   div
 *   expr9   *       left-assoc   mul
 *   expr10  %       left-assoc   user-binary
 *   expr11  ^ ! **  right-assoc  exponent
 *   expr12  $ .     left-assoc   immediate/conditional capture (binary)
 *   expr13  ~       right-assoc  conditional capture (binary)
 *   expr14          unary prefix
 *   expr15  [] <>   postfix      subscript
 *   expr17          atom
 */

%{
#include "scrip_cc.h"
#include "snobol4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { Lex *lx; EXPR_t **result; } ParseParm;
static Lex *g_lx;
%}

%define api.prefix {snobol4_}
%define api.pure full
%parse-param { void *yyparse_param }

%union { EXPR_t *expr; Token tok; }

%token <tok> TK_IDENT TK_END TK_INT TK_REAL TK_STR TK_KEYWORD
%token TK_PLUS TK_MINUS TK_STAR TK_SLASH TK_PCT TK_CARET TK_BANG TK_STARSTAR
%token TK_AMP TK_AT TK_TILDE TK_DOLLAR TK_DOT TK_HASH TK_PIPE TK_EQ TK_QMARK
%token TK_COMMA TK_LPAREN TK_RPAREN TK_LBRACKET TK_RBRACKET TK_LANGLE TK_RANGLE

%type <expr> top expr expr0 expr2 expr3 expr4 expr5 expr6 expr7 expr8
%type <expr> expr9 expr10 expr11 expr12 expr13 expr14 expr15 expr17
%type <expr> exprlist exprlist_ne

%%

top : expr        { *(((ParseParm*)yyparse_param)->result) = $1; }
    | /* empty */ { *(((ParseParm*)yyparse_param)->result) = NULL; }
    ;

expr : expr0 { $$ = $1; } ;

/* right-assoc: = ? */
expr0
    : expr2 TK_EQ    expr0  { $$ = expr_binary(E_ASSIGN,         $1, $3); }
    | expr2 TK_QMARK expr0  { $$ = expr_binary(E_CAPT_COND_ASGN, $1, $3); }
    | expr2                  { $$ = $1; }
    ;

/* left-assoc: & */
expr2
    : expr2 TK_AMP expr3  { $$ = expr_binary(E_OPSYN, $1, $3); }
    | expr3               { $$ = $1; }
    ;

/* n-ary: | */
expr3
    : expr3 TK_PIPE expr4
        {
            if ($1->kind==E_ALT) { expr_add_child($1,$3); $$=$1; }
            else { EXPR_t*a=expr_new(E_ALT); expr_add_child(a,$1); expr_add_child(a,$3); $$=a; }
        }
    | expr4  { $$ = $1; }
    ;

/* n-ary concat: juxtaposition — no separator token */
expr4
    : expr4 expr5
        {
            if ($1->kind==E_SEQ) { expr_add_child($1,$2); $$=$1; }
            else { EXPR_t*s=expr_new(E_SEQ); expr_add_child(s,$1); expr_add_child(s,$2); $$=s; }
        }
    | expr5  { $$ = $1; }
    ;

/* left-assoc: @ (binary cursor-capture) */
expr5
    : expr5 TK_AT expr6  { $$ = expr_binary(E_CAPT_CURSOR, $1, $3); }
    | expr6              { $$ = $1; }
    ;

/* left-assoc: + - */
expr6
    : expr6 TK_PLUS  expr7  { $$ = expr_binary(E_ADD, $1, $3); }
    | expr6 TK_MINUS expr7  { $$ = expr_binary(E_SUB, $1, $3); }
    | expr7                 { $$ = $1; }
    ;

/* left-assoc: # */
expr7
    : expr7 TK_HASH expr8  { $$ = expr_binary(E_MUL, $1, $3); }
    | expr8                { $$ = $1; }
    ;

/* left-assoc: / */
expr8
    : expr8 TK_SLASH expr9  { $$ = expr_binary(E_DIV, $1, $3); }
    | expr9                 { $$ = $1; }
    ;

/* left-assoc: * */
expr9
    : expr9 TK_STAR expr10  { $$ = expr_binary(E_MUL, $1, $3); }
    | expr10                { $$ = $1; }
    ;

/* left-assoc: % */
expr10
    : expr10 TK_PCT expr11  { $$ = expr_binary(E_DIV, $1, $3); }
    | expr11                { $$ = $1; }
    ;

/* right-assoc: ^ ! ** */
expr11
    : expr12 TK_CARET    expr11  { $$ = expr_binary(E_POW, $1, $3); }
    | expr12 TK_BANG     expr11  { $$ = expr_binary(E_POW, $1, $3); }
    | expr12 TK_STARSTAR expr11  { $$ = expr_binary(E_POW, $1, $3); }
    | expr12                     { $$ = $1; }
    ;

/* left-assoc: $ . (binary capture) */
expr12
    : expr12 TK_DOLLAR expr13  { $$ = expr_binary(E_CAPT_IMMED_ASGN, $1, $3); }
    | expr12 TK_DOT    expr13  { $$ = expr_binary(E_CAPT_COND_ASGN,  $1, $3); }
    | expr13                   { $$ = $1; }
    ;

/* right-assoc: ~ (binary cond-capture) */
expr13
    : expr14 TK_TILDE expr13  { $$ = expr_binary(E_CAPT_COND_ASGN, $1, $3); }
    | expr14                  { $$ = $1; }
    ;

/* unary prefix — all use U-variants, no ambiguity with binary levels */
expr14
    : TK_AT     expr14  { $$ = expr_unary(E_CAPT_CURSOR,  $2); }
    | TK_TILDE  expr14  { $$ = expr_unary(E_INDIRECT,     $2); }
    | TK_QMARK  expr14  { $$ = expr_unary(E_INTERROGATE,  $2); }
    | TK_AMP    expr14  { $$ = expr_unary(E_OPSYN,        $2); }
    | TK_PLUS   expr14  { $$ = expr_unary(E_PLS,          $2); }
    | TK_MINUS  expr14  { $$ = expr_unary(E_MNS,          $2); }
    | TK_STAR   expr14  { $$ = expr_unary(E_DEFER,        $2); }
    | TK_DOLLAR expr14  { $$ = expr_unary(E_INDIRECT,     $2); }
    | TK_DOT    expr14  { $$ = expr_unary(E_NAME,         $2); }
    | TK_BANG   expr14  { $$ = expr_unary(E_POW,          $2); }
    | TK_PCT    expr14  { $$ = expr_unary(E_DIV,          $2); }
    | TK_SLASH  expr14  { $$ = expr_unary(E_DIV,          $2); }
    | TK_HASH   expr14  { $$ = expr_unary(E_MUL,          $2); }
    | TK_EQ     expr14  { $$ = expr_unary(E_ASSIGN,       $2); }
    | TK_PIPE   expr14  { $$ = expr_unary(E_ALT,          $2); }
    | TK_CARET  expr14  { $$ = expr_unary(E_POW,          $2); }
    | TK_STARSTAR expr14 { $$ = expr_unary(E_DEFER,       $2); }
    | expr15             { $$ = $1; }
    ;

/* postfix subscript */
expr15
    : expr15 TK_LBRACKET exprlist TK_RBRACKET
        { EXPR_t*idx=expr_new(E_IDX); expr_add_child(idx,$1);
          for(int i=0;i<$3->nchildren;i++) expr_add_child(idx,$3->children[i]);
          free($3->children); free($3); $$=idx; }
    | expr15 TK_LANGLE exprlist TK_RANGLE
        { EXPR_t*idx=expr_new(E_IDX); expr_add_child(idx,$1);
          for(int i=0;i<$3->nchildren;i++) expr_add_child(idx,$3->children[i]);
          free($3->children); free($3); $$=idx; }
    | expr17  { $$ = $1; }
    ;

/* comma-separated list — returns scratch E_NUL node with children */
exprlist
    : exprlist_ne   { $$ = $1; }
    | /* empty */   { $$ = expr_new(E_NUL); }
    ;
exprlist_ne
    : exprlist_ne TK_COMMA expr0  { expr_add_child($1,$3); $$=$1; }
    | exprlist_ne TK_COMMA        { expr_add_child($1,expr_new(E_NUL)); $$=$1; }
    | expr0  { EXPR_t*l=expr_new(E_NUL); expr_add_child(l,$1); $$=l; }
    ;

/* atoms */
expr17
    : TK_LPAREN expr0 TK_RPAREN                        { $$ = $2; }
    | TK_LPAREN expr0 TK_COMMA exprlist_ne TK_RPAREN
        { EXPR_t*a=expr_new(E_ALT); expr_add_child(a,$2);
          for(int i=0;i<$4->nchildren;i++) expr_add_child(a,$4->children[i]);
          free($4->children); free($4); $$=a; }
    | TK_LPAREN TK_RPAREN                              { $$ = expr_new(E_NUL); }
    | TK_IDENT TK_LPAREN exprlist TK_RPAREN
        { EXPR_t*e=expr_new(E_FNC); e->sval=(char*)$1.sval;
          for(int i=0;i<$3->nchildren;i++) expr_add_child(e,$3->children[i]);
          free($3->children); free($3); $$=e; }
    | TK_END TK_LPAREN exprlist TK_RPAREN
        { EXPR_t*e=expr_new(E_FNC); e->sval=(char*)$1.sval;
          for(int i=0;i<$3->nchildren;i++) expr_add_child(e,$3->children[i]);
          free($3->children); free($3); $$=e; }
    | TK_IDENT    { EXPR_t*e=expr_new(E_VAR);     e->sval=(char*)$1.sval; $$=e; }
    | TK_END      { EXPR_t*e=expr_new(E_VAR);     e->sval=(char*)$1.sval; $$=e; }
    | TK_KEYWORD  { EXPR_t*e=expr_new(E_KEYWORD); e->sval=(char*)$1.sval; $$=e; }
    | TK_STR      { EXPR_t*e=expr_new(E_QLIT);    e->sval=(char*)$1.sval; $$=e; }
    | TK_INT      { EXPR_t*e=expr_new(E_ILIT);    e->ival=$1.ival;         $$=e; }
    | TK_REAL     { EXPR_t*e=expr_new(E_FLIT);    e->dval=$1.dval;         $$=e; }
    ;

%%

int snobol4_lex(YYSTYPE *yylval_param, void *yyparse_param) {
    (void)yyparse_param;
    Token t = lex_next(g_lx);
    yylval_param->tok = t;
    switch (t.kind) {
        case T_IDENT:     return TK_IDENT;
        case T_END:       return TK_END;
        case T_INT:       return TK_INT;
        case T_REAL:      return TK_REAL;
        case T_STR:       return TK_STR;
        case T_KEYWORD:   return TK_KEYWORD;
        /* binary */
        case T_PLUS:      return TK_PLUS;
        case T_MINUS:     return TK_MINUS;
        case T_STAR:      return TK_STAR;
        case T_SLASH:     return TK_SLASH;
        case T_PCT:       return TK_PCT;
        case T_CARET:     return TK_CARET;
        case T_BANG:      return TK_BANG;
        case T_STARSTAR:  return TK_STARSTAR;
        case T_AMP:       return TK_AMP;
        case T_AT:        return TK_AT;
        case T_TILDE:     return TK_TILDE;
        case T_DOLLAR:    return TK_DOLLAR;
        case T_DOT:       return TK_DOT;
        case T_HASH:      return TK_HASH;
        case T_PIPE:      return TK_PIPE;
        case T_EQ:        return TK_EQ;
        case T_QMARK:     return TK_QMARK;
        /* unary */
        /* structural */
        case T_COMMA:     return TK_COMMA;
        case T_LPAREN:    return TK_LPAREN;
        case T_RPAREN:    return TK_RPAREN;
        case T_LBRACKET:  return TK_LBRACKET;
        case T_RBRACKET:  return TK_RBRACKET;
        case T_LANGLE:    return TK_LANGLE;
        case T_RANGLE:    return TK_RANGLE;
        default:          return 0;
    }
}

void snobol4_error(void *param, const char *msg) {
    (void)param;
    sno_error(g_lx ? g_lx->lineno : 0, "parse error: %s", msg);
}

static EXPR_t *parse_expr_lx(Lex *lx) {
    EXPR_t *result = NULL;
    ParseParm p = { lx, &result };
    g_lx = lx;
    snobol4_parse(&p);
    return result;
}

static void fixup_val_tree(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_SEQ) e->kind = E_CAT;
    for (int i=0; i<e->nchildren; i++) fixup_val_tree(e->children[i]);
}

static int repl_is_pat_tree(EXPR_t *e) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ARB: case E_ARBNO:
        case E_CAPT_COND_ASGN: case E_CAPT_IMMED_ASGN:
        case E_CAPT_CURSOR: case E_DEFER: return 1;
        default: break;
    }
    for (int i=0; i<e->nchildren; i++)
        if (repl_is_pat_tree(e->children[i])) return 1;
    return 0;
}

static char *parse_goto_label(Lex *lx) {
    Token t = lex_peek(lx);
    TokKind open=t.kind, close;
    if      (open==T_LPAREN) close=T_RPAREN;
    else if (open==T_LANGLE) close=T_RANGLE;
    else return NULL;
    lex_next(lx); t=lex_peek(lx);
    char *label=NULL;
    if (t.kind==T_IDENT||t.kind==T_KEYWORD||t.kind==T_END) {
        lex_next(lx); label=(char*)t.sval;
    } else if (t.kind==T_DOLLAR) {
        lex_next(lx);
        if (lex_peek(lx).kind==T_LPAREN) {
            /* Reconstruct $(expr) text from token svals */
            int depth=1; lex_next(lx);
            char ebuf[512]; int epos=0;
            while (!lex_at_end(lx)&&depth>0) {
                Token tok=lex_next(lx);
                if (tok.kind==T_LPAREN) depth++;
                else if (tok.kind==T_RPAREN){depth--;if(!depth) break;}
                char tmp[64]; int tlen=tok_to_chars(&tok,tmp,sizeof tmp);
                if(epos+tlen<(int)sizeof(ebuf)-1){memcpy(ebuf+epos,tmp,tlen);epos+=tlen;}
            }
            ebuf[epos]='\0';
            char *buf=malloc(12+epos+1);
            memcpy(buf,"$COMPUTED:",10); memcpy(buf+10,ebuf,epos); buf[10+epos]='\0';
            label=buf;
        } else if (lex_peek(lx).kind==T_STR) {
            Token n2=lex_next(lx); const char *lit=n2.sval?n2.sval:"";
            char *buf=malloc(12+strlen(lit)+4); sprintf(buf,"$COMPUTED:'%s'",lit); label=buf;
        } else {
            Token n2=lex_next(lx); char buf[512];
            snprintf(buf,sizeof buf,"$%s",n2.sval?n2.sval:"?"); label=strdup(buf);
        }
    } else if (t.kind==T_LPAREN) {
        int depth=1; lex_next(lx);
        while (!lex_at_end(lx)&&depth>0){Token tok=lex_next(lx);if(tok.kind==T_LPAREN)depth++;else if(tok.kind==T_RPAREN)depth--;}
        label=strdup("$COMPUTED");
    }
    if (lex_peek(lx).kind==close) lex_next(lx);
    return label;
}

static SnoGoto *parse_goto_field(const char *goto_str, int lineno) {
    if (!goto_str||!*goto_str) return NULL;
    Lex lx={0}; lex_open_str(&lx,goto_str,(int)strlen(goto_str),lineno);
    SnoGoto *g=sgoto_new();
    while (!lex_at_end(&lx)) {
        Token t=lex_peek(&lx);
        if (t.kind==T_IDENT&&t.sval) {
            if (strcasecmp(t.sval,"S")==0){lex_next(&lx);g->onsuccess=parse_goto_label(&lx);continue;}
            if (strcasecmp(t.sval,"F")==0){lex_next(&lx);g->onfailure=parse_goto_label(&lx);continue;}
        }
        if (t.kind==T_LPAREN||t.kind==T_LANGLE){g->uncond=parse_goto_label(&lx);continue;}
        sno_error(lineno,"unexpected token in goto field"); lex_next(&lx);
    }
    if (!g->onsuccess&&!g->onfailure&&!g->uncond){free(g);return NULL;}
    return g;
}

int tok_to_chars(Token *tk, char *buf, int bufsz) {
    int n=0;
#define OUT(c) do{if(n<bufsz-1)buf[n++]=(c);}while(0)
    switch(tk->kind){
        case T_IDENT: case T_END:
            if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-1){memcpy(buf+n,tk->sval,l);n+=l;}}break;
        case T_KEYWORD:
            OUT('&');if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-2){memcpy(buf+n,tk->sval,l);n+=l;}}break;
        case T_STR:{const char*sv=tk->sval?tk->sval:"";char q=strchr(sv,'\'')?'"':'\'';
            OUT(q);int l=(int)strlen(sv);if(l<bufsz-n-2){memcpy(buf+n,sv,l);n+=l;}OUT(q);break;}
        case T_INT:  n+=snprintf(buf+n,bufsz-n,"%ld",tk->ival);break;
        case T_REAL:
            if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-1){memcpy(buf+n,tk->sval,l);n+=l;}}
            else n+=snprintf(buf+n,bufsz-n,"%g",tk->dval);break;
        /* binary ops get surrounding spaces to preserve spacing for re-lex */
        case T_PLUS:     OUT(' ');OUT('+');OUT(' ');break;
        case T_MINUS:    OUT(' ');OUT('-');OUT(' ');break;
        case T_STAR:     OUT(' ');OUT('*');OUT(' ');break;
        case T_SLASH:    OUT(' ');OUT('/');OUT(' ');break;
        case T_PCT:      OUT(' ');OUT('%');OUT(' ');break;
        case T_CARET:    OUT(' ');OUT('^');OUT(' ');break;
        case T_BANG:     OUT(' ');OUT('!');OUT(' ');break;
        case T_STARSTAR: OUT(' ');OUT('*');OUT('*');OUT(' ');break;
        case T_AMP:      OUT(' ');OUT('&');OUT(' ');break;
        case T_AT:       OUT(' ');OUT('@');OUT(' ');break;
        case T_TILDE:    OUT(' ');OUT('~');OUT(' ');break;
        case T_DOLLAR:   OUT(' ');OUT('$');OUT(' ');break;
        case T_DOT:      OUT(' ');OUT('.');OUT(' ');break;
        case T_HASH:     OUT(' ');OUT('#');OUT(' ');break;
        case T_PIPE:     OUT(' ');OUT('|');OUT(' ');break;
        case T_EQ:       OUT(' ');OUT('=');OUT(' ');break;
        case T_QMARK:    OUT(' ');OUT('?');OUT(' ');break;
        /* unary ops — no surrounding spaces */
        case T_COMMA:    OUT(',');break;
        case T_LPAREN:   OUT('(');break; case T_RPAREN:   OUT(')');break;
        case T_LBRACKET: OUT('[');break; case T_RBRACKET: OUT(']');break;
        case T_LANGLE:   OUT('<');break; case T_RANGLE:   OUT('>');break;
        default:break;
    }
#undef OUT
    buf[n]='\0'; return n;
}

Program *parse_program_tokens(Lex *stream) {
    Program *prog=calloc(1,sizeof*prog);
    while (1) {
        Token t=lex_peek(stream);
        if (t.kind==T_EOF) break;
        char *label=NULL,*goto_str=NULL;
        int is_end=0,lineno=t.lineno;
        if (t.kind==T_LABEL){
            lex_next(stream); label=(char*)t.sval; is_end=(int)t.ival; lineno=t.lineno;
            t=lex_peek(stream);
        }
        if (t.kind==T_STMT_END){
            lex_next(stream);
            if(label||is_end){
                STMT_t*s2=stmt_new(); s2->lineno=lineno;
                if(label) s2->label=strdup(label); s2->is_end=is_end;
                if(!prog->head) prog->head=prog->tail=s2; else{prog->tail->next=s2;prog->tail=s2;}
                prog->nstmts++;
            }
            continue;
        }
        if (t.kind==T_EOF){
            if(label||is_end){
                STMT_t*s2=stmt_new(); s2->lineno=lineno;
                if(label) s2->label=strdup(label); s2->is_end=is_end;
                if(!prog->head) prog->head=prog->tail=s2; else{prog->tail->next=s2;prog->tail=s2;}
                prog->nstmts++;
            }
            break;
        }
        char bbuf[8192]; int bpos=0;
        while(1){
            t=lex_peek(stream);
            if(t.kind==T_GOTO||t.kind==T_STMT_END||t.kind==T_EOF) break;
            char tmp[256]; int tlen=tok_to_chars(&t,tmp,sizeof tmp);
            if(bpos+tlen<8190){memcpy(bbuf+bpos,tmp,tlen);bpos+=tlen;}
            lex_next(stream);
        }
        bbuf[bpos]='\0';
        if(lex_peek(stream).kind==T_GOTO){goto_str=(char*)lex_peek(stream).sval;lex_next(stream);}
        if(lex_peek(stream).kind==T_STMT_END) lex_next(stream);

        if(label&&strcasecmp(label,"EXPORT")==0){
            Lex blx={0}; lex_open_str(&blx,bbuf,bpos,lineno);
            Token nt=lex_peek(&blx);
            if((nt.kind==T_IDENT||nt.kind==T_END)&&nt.sval){
                ExportEntry*e=calloc(1,sizeof*e); e->name=strdup(nt.sval);
                for(char*p=e->name;*p;p++) *p=(char)toupper((unsigned char)*p);
                e->next=prog->exports; prog->exports=e;
            }
            continue;
        }
        if(label&&strcasecmp(label,"IMPORT")==0){
            ImportEntry*e=calloc(1,sizeof*e);
            char*dot1=strchr(bbuf,'.');
            if(dot1){char*dot2=strchr(dot1+1,'.');
                if(dot2){e->lang=strndup(bbuf,(size_t)(dot1-bbuf));e->name=strndup(dot1+1,(size_t)(dot2-dot1-1));e->method=strdup(dot2+1);}
                else{e->lang=strdup("");e->name=strndup(bbuf,(size_t)(dot1-bbuf));e->method=strdup(dot1+1);}}
            else{e->lang=strdup("");e->name=strdup(bbuf);e->method=strdup(bbuf);}
            e->next=prog->imports; prog->imports=e; continue;
        }

        STMT_t*s=stmt_new(); s->lineno=lineno;
        if(label) s->label=strdup(label);
        s->go=parse_goto_field(goto_str,lineno);
        if(is_end){if(!s->label) s->label=strdup("END"); s->is_end=1;}
        else if(bpos>0){
            /* Split body at top-level '=' (depth 0) to find:
             *   subject [pattern] = replacement   or   subject = replacement
             * Find the last top-level '=' that is not inside parens/brackets.
             * Everything before is subject [pattern]; everything after is replacement.
             * Then split subject/pattern: subject ends at first top-level non-unary
             * token after an atom — handled by parse_expr_lx consuming expr14 only.
             * Simpler: run parse_expr_lx on full body up to '='; the yacc grammar
             * will consume subject+pattern as an expr naturally since '=' at top
             * level is the assign operator — but we don't want that.
             *
             * Correct split: scan bbuf for top-level '=' character.
             */
            int eq_pos = -1, depth = 0;
            for(int i=0;i<bpos;i++){
                char c=bbuf[i];
                if(c=='('||c=='['||c=='<') depth++;
                else if(c==')'||c==']'||c=='>') depth--;
                else if(c=='='&&depth==0){ eq_pos=i; break; }
            }
            if(eq_pos>=0){
                /* subject+pattern before '=', replacement after */
                char subpat[8192]; int splen=eq_pos;
                while(splen>0&&(bbuf[splen-1]==' '||bbuf[splen-1]=='\t')) splen--;
                memcpy(subpat,bbuf,splen); subpat[splen]='\0';
                char repl[8192]; int rlen=bpos-eq_pos-1;
                while(rlen>0&&(bbuf[eq_pos+1]==' '||bbuf[eq_pos+1]=='\t')){eq_pos++;rlen--;}
                if(rlen>0) memcpy(repl,bbuf+eq_pos+1,rlen); repl[rlen>0?rlen:0]='\0';
                s->has_eq=1;
                /* parse subject+pattern: subject is expr14-level atom,
                 * remainder is pattern (expr3-level).  Run full parser —
                 * the grammar will parse it as a concat (E_SEQ) of subject and pattern. */
                Lex blx={0}; lex_open_str(&blx,subpat,splen,lineno);
                EXPR_t *sp=parse_expr_lx(&blx);
                lex_destroy(&blx);
                /* If sp is E_SEQ, first child is subject, rest form pattern */
                if(sp && sp->kind==E_SEQ && sp->nchildren>=2){
                    s->subject=sp->children[0];
                    if(sp->nchildren==2){ s->pattern=sp->children[1]; }
                    else {
                        EXPR_t *seq=expr_new(E_SEQ);
                        for(int i=1;i<sp->nchildren;i++) expr_add_child(seq,sp->children[i]);
                        s->pattern=seq;
                    }
                    free(sp->children); free(sp);
                } else {
                    s->subject=sp;
                }
                fixup_val_tree(s->subject);
                if(rlen>0){
                    Lex rlx={0}; lex_open_str(&rlx,repl,rlen,lineno);
                    s->replacement=parse_expr_lx(&rlx);
                    lex_destroy(&rlx);
                    if(s->replacement&&!repl_is_pat_tree(s->replacement))
                        fixup_val_tree(s->replacement);
                }
            } else {
                /* No '=': subject [pattern] only */
                Lex blx={0}; lex_open_str(&blx,bbuf,bpos,lineno);
                EXPR_t *sp=parse_expr_lx(&blx);
                lex_destroy(&blx);
                if(sp && sp->kind==E_SEQ && sp->nchildren>=2){
                    s->subject=sp->children[0];
                    if(sp->nchildren==2){ s->pattern=sp->children[1]; }
                    else {
                        EXPR_t *seq=expr_new(E_SEQ);
                        for(int i=1;i<sp->nchildren;i++) expr_add_child(seq,sp->children[i]);
                        s->pattern=seq;
                    }
                    free(sp->children); free(sp);
                } else {
                    s->subject=sp;
                }
                fixup_val_tree(s->subject);
            }
        }
        s->next=NULL;
        if(!prog->head) prog->head=prog->tail=s; else{prog->tail->next=s;prog->tail=s;}
        prog->nstmts++;
    }
    return prog;
}

Program *parse_program(LineArray *lines) {
    (void)lines;
    fprintf(stderr,"parse_program(LineArray*): not used with one-pass lexer\n");
    return calloc(1,sizeof(Program));
}

EXPR_t *parse_expr_from_str(const char *src) {
    if(!src||!*src) return NULL;
    Lex lx={0}; lex_open_str(&lx,src,(int)strlen(src),0);
    return parse_expr_lx(&lx);
}
