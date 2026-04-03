#ifndef LEX_H
#define LEX_H

#include <stdio.h>

/* Token kinds */
typedef enum {
    T_IDENT, T_INT, T_REAL, T_STR, T_KEYWORD,
    T_WS,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT,
    T_CARET, T_BANG, T_STARSTAR,
    T_AMP, T_AT, T_TILDE, T_DOLLAR, T_DOT,
    T_HASH, T_PIPE, T_EQ, T_QMARK,
    T_COMMA, T_LPAREN, T_RPAREN,
    T_LBRACKET, T_RBRACKET,
    T_LANGLE, T_RANGLE,
    T_COLON, T_SGOTO, T_FGOTO,
    T_END,
    /* Statement-structure tokens emitted by the one-pass lexer */
    T_LABEL,     /* column-1 identifier; sval=name, ival=1 if END */
    T_GOTO,      /* goto field string; sval=goto_str               */
    T_STMT_END,  /* end of one logical statement                    */
    T_EOF, T_ERR
} TokKind;

typedef struct {
    TokKind     kind;
    const char *sval;
    long        ival;
    double      dval;
    int         lineno;
} Token;

/* Lexer state.  src==NULL → drain token queue (main program stream).
 * src!=NULL  → tokenise a string directly (goto-field parsing only). */
typedef struct Lex {
    const char *src;
    int         pos;
    int         len;
    int         lineno;
    Token       peek;
    int         peeked;
    /* M-LEX-1: flex scanner state (NULL when using queue path) */
    void       *_scanner;  /* yyscan_t — opaque flex handle */
    void       *_extra;    /* FlexExtra* — token scratch storage */
} Lex;

void  lex_open_str(Lex *lx, const char *s, int len, int lineno);
Token lex_next(Lex *lx);
Token lex_peek(Lex *lx);
int   lex_at_end(Lex *lx);

/* M-LEX-1: flex one-pass lexer — called by lex.c, defined in lex.yy.c */
void  flex_lex_open   (Lex *lx, FILE *f, const char *fname);
Token flex_lex_next   (Lex *lx);
void  flex_lex_destroy(Lex *lx);

/* Checkpoint for speculative lookahead */
typedef struct { int pos; Token peek; int peeked; } LexMark;
static inline LexMark lex_mark(Lex *lx)             { LexMark m; m.pos=lx->pos; m.peek=lx->peek; m.peeked=lx->peeked; return m; }
static inline void    lex_restore(Lex *lx, LexMark m){ lx->pos=m.pos; lx->peek=m.peek; lx->peeked=m.peeked; }

/* SnoLine / LineArray kept for scrip-cc emitter path (not used by scrip-interp) */
typedef struct SnoLine {
    char *label;
    char *body;
    char *goto_str;
    int   lineno;
    int   is_end;
} SnoLine;

typedef struct {
    SnoLine *a;
    int      n, cap;
} LineArray;

#endif /* LEX_H */
