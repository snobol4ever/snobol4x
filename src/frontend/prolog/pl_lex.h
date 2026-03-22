#ifndef PL_LEX_H
#define PL_LEX_H
/*
 * pl_lex.h — Prolog lexer for snobol4x Prolog frontend
 *
 * Tokenises standard Edinburgh/ISO Prolog source.
 * Hand-rolled; no flex dependency.
 */

/* -------------------------------------------------------------------------
 * Token kinds
 * ---------------------------------------------------------------------- */
typedef enum {
    TK_EOF = 0,

    /* literals */
    TK_ATOM,        /* foo, 'quoted atom', []  */
    TK_VAR,         /* X, Foo, _Bar            */
    TK_ANON,        /* _                       */
    TK_INT,         /* 42                      */
    TK_FLOAT,       /* 3.14                    */
    TK_STRING,      /* "hello"  (char list)    */

    /* punctuation */
    TK_LPAREN,      /* (  */
    TK_RPAREN,      /* )  */
    TK_LBRACKET,    /* [  */
    TK_RBRACKET,    /* ]  */
    TK_PIPE,        /* |  */
    TK_COMMA,       /* ,  */
    TK_DOT,         /* .  followed by whitespace/EOF — clause terminator */
    TK_DOTDOT,      /* .. — not valid Prolog; reject gracefully */

    /* operators (returned as TK_OP; text in tok_text) */
    TK_OP,          /* :- | ?- | :  | =.. | = | \= | == | \== |
                       is | < | > | =< | >= | =:= | =\= |
                       + | - | * | // | / | mod | not | \+ | ! | ; */

    TK_NECK,        /* :- (clause neck — also TK_OP for operator table) */
    TK_QUERY,       /* ?-                                               */
    TK_CUT,         /* !                                                */
    TK_SEMI,        /* ;                                                */

    TK_ERROR        /* lexical error */
} TkKind;

/* -------------------------------------------------------------------------
 * Token
 * ---------------------------------------------------------------------- */
typedef struct {
    TkKind  kind;
    char   *text;   /* heap copy of token text (caller frees if needed) */
    long    ival;   /* TK_INT */
    double  fval;   /* TK_FLOAT */
    int     line;   /* 1-based source line                              */
} Token;

/* -------------------------------------------------------------------------
 * Lexer state
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *src;     /* full source string (NUL-terminated)  */
    int         pos;     /* current read position                 */
    int         line;    /* current line number (1-based)         */
    Token       peek;    /* one-token lookahead (filled by pl_lex_peek) */
    int         has_peek;
} Lexer;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/* Initialise lexer over a NUL-terminated source string. */
void lexer_init(Lexer *lx, const char *src);

/* Return (and consume) the next token. */
Token lexer_next(Lexer *lx);

/* Peek at the next token without consuming it. */
Token lexer_peek(Lexer *lx);

/* Consume the next token; assert kind matches (fatal on mismatch). */
Token lexer_expect(Lexer *lx, TkKind kind, const char *context);

/* Free text inside a token (safe to call on tokens without heap text). */
void token_free(Token *t);

/* Human-readable kind name (for error messages). */
const char *tk_name(TkKind kind);

#endif /* PL_LEX_H */
