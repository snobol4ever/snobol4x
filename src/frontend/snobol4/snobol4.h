#ifndef LEX_H
#define LEX_H

#include <stdio.h>

/* Token kinds.
 * Whitespace is never returned — grey and white both suppressed by lex.l.
 * Unary vs binary distinction is resolved by grammar position in parse.y. */
typedef enum {
    T_IDENT, T_INT, T_REAL, T_STR, T_KEYWORD,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT,
    T_CARET, T_BANG, T_STARSTAR,
    T_AMP, T_AT, T_TILDE, T_DOLLAR, T_DOT,
    T_HASH, T_PIPE, T_EQ, T_QMARK,
    T_COMMA, T_LPAREN, T_RPAREN,
    T_LBRACKET, T_RBRACKET,
    T_LANGLE, T_RANGLE,
    T_COLON, T_SGOTO, T_FGOTO,
    T_END,
    /* Statement-structure tokens from the one-pass flex lexer */
    T_LABEL,     /* column-1 identifier; sval=name, ival=1 if END label */
    T_GOTO,      /* goto field string; sval=raw goto text                */
    T_STMT_END,  /* logical-line boundary                                */
    T_EOF, T_ERR,
    T_WS         /* never returned; kept so old references compile       */
} TokKind;

typedef struct {
    TokKind     kind;
    const char *sval;
    long        ival;
    double      dval;
    int         lineno;
} Token;

/* Lexer handle — wraps a flex scanner.
 * lex_open_str opens the flex scanner over an in-memory string via fmemopen.
 * There is one tokenizer: lex.l. */
typedef struct Lex {
    int    lineno;
    Token  peek;
    int    peeked;
    void  *_scanner;   /* yyscan_t  */
    void  *_extra;     /* FlexExtra* */
} Lex;

void  lex_open_str(Lex *lx, const char *s, int len, int lineno);
Token lex_next    (Lex *lx);
Token lex_peek    (Lex *lx);
int   lex_at_end  (Lex *lx);
void  lex_destroy (Lex *lx);

/* flex one-pass entry points (defined in lex.yy.c) */
void  flex_lex_open   (Lex *lx, FILE *f, const char *fname);
Token flex_lex_next   (Lex *lx);
void  flex_lex_destroy(Lex *lx);

/* SnoLine / LineArray — scrip-cc emitter path (not used by scrip-interp) */
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
