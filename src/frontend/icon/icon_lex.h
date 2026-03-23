/*
 * icon_lex.h — Tiny-ICON lexer types
 *
 * Hand-rolled lexer. No auto-semicolon insertion (deliberate deviation from
 * standard Icon). All expression sequences require explicit ';'.
 *
 * Token names match ttoken.h from icon-master where applicable.
 */

#ifndef ICON_LEX_H
#define ICON_LEX_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Token kinds
 * -------------------------------------------------------------------------*/
typedef enum {
    /* Sentinels */
    TK_EOF = 0,
    TK_ERROR,

    /* Literals */
    TK_INT,        /* integer literal: 42  0377  0xff */
    TK_REAL,       /* real literal: 3.14  1e-5 */
    TK_STRING,     /* string literal: "hello" */
    TK_CSET,       /* cset literal: 'abc' */
    TK_IDENT,      /* identifier / keyword */

    /* Arithmetic */
    TK_PLUS,       /* + */
    TK_MINUS,      /* - */
    TK_STAR,       /* * */
    TK_SLASH,      /* / */
    TK_MOD,        /* % */
    TK_CARET,      /* ^ (exponentiation) */

    /* Numeric relational */
    TK_LT,         /* < */
    TK_LE,         /* <= */
    TK_GT,         /* > */
    TK_GE,         /* >= */
    TK_EQ,         /* = */
    TK_NEQ,        /* ~= */

    /* String relational */
    TK_SLT,        /* << */
    TK_SLE,        /* <<= */
    TK_SGT,        /* >> */
    TK_SGE,        /* >>= */
    TK_SEQ,        /* == */
    TK_SNE,        /* ~== */

    /* String concat */
    TK_CONCAT,     /* || */
    TK_LCONCAT,    /* ||| (list concat) */

    /* Assignment family */
    TK_ASSIGN,     /* := */
    TK_SWAP,       /* :=: */
    TK_REVASSIGN,  /* <- */
    TK_AUGPLUS,    /* +:= */
    TK_AUGMINUS,   /* -:= */
    TK_AUGSTAR,    /* *:= */
    TK_AUGSLASH,   /* /:= */
    TK_AUGMOD,     /* %:= */
    TK_AUGCONCAT,  /* ||:= */

    /* Misc operators */
    TK_AND,        /* & (conjunction) */
    TK_BAR,        /* | (alternation) */
    TK_BACKSLASH,  /* \ (limitation / complement) */
    TK_BANG,       /* ! (generate) */
    TK_QMARK,      /* ? (random / string scan) */
    TK_AT,         /* @ (co-expression activate / cursor) */
    TK_TILDE,      /* ~ (complement prefix) */
    TK_DOT,        /* . */

    /* Keywords (reserved words) */
    TK_TO,         /* to */
    TK_BY,         /* by */
    TK_EVERY,      /* every */
    TK_DO,         /* do */
    TK_IF,         /* if */
    TK_THEN,       /* then */
    TK_ELSE,       /* else */
    TK_WHILE,      /* while */
    TK_UNTIL,      /* until */
    TK_REPEAT,     /* repeat */
    TK_RETURN,     /* return */
    TK_SUSPEND,    /* suspend */
    TK_FAIL,       /* fail */
    TK_BREAK,      /* break */
    TK_NEXT,       /* next */
    TK_NOT,        /* not */
    TK_PROCEDURE,  /* procedure */
    TK_END,        /* end */
    TK_GLOBAL,     /* global */
    TK_LOCAL,      /* local */
    TK_STATIC,     /* static */
    TK_RECORD,     /* record */
    TK_LINK,       /* link */
    TK_INVOCABLE,  /* invocable */
    TK_CASE,       /* case */
    TK_OF,         /* of */
    TK_DEFAULT,    /* default */
    TK_CREATE,     /* create */
    TK_INITIAL,    /* initial */

    /* Punctuation */
    TK_LPAREN,     /* ( */
    TK_RPAREN,     /* ) */
    TK_LBRACE,     /* { */
    TK_RBRACE,     /* } */
    TK_LBRACK,     /* [ */
    TK_RBRACK,     /* ] */
    TK_COMMA,      /* , */
    TK_SEMICOL,    /* ; */
    TK_COLON,      /* : */

    TK_COUNT       /* sentinel: number of token kinds */
} IcnTkKind;

/* -------------------------------------------------------------------------
 * Token value (union for literal payloads)
 * -------------------------------------------------------------------------*/
typedef union {
    long   ival;   /* TK_INT */
    double fval;   /* TK_REAL */
    struct {
        char  *data;
        size_t len;
    } sval;        /* TK_STRING, TK_CSET, TK_IDENT */
} IcnTkVal;

/* -------------------------------------------------------------------------
 * Token struct
 * -------------------------------------------------------------------------*/
typedef struct {
    IcnTkKind kind;
    IcnTkVal  val;
    int       line;   /* 1-based source line */
    int       col;    /* 1-based column */
} IcnToken;

/* -------------------------------------------------------------------------
 * Lexer state
 * -------------------------------------------------------------------------*/
typedef struct {
    const char *src;      /* full source text */
    size_t      src_len;
    size_t      pos;      /* current position in src */
    int         line;
    int         col;
    char        errmsg[256];
    int         had_error;
} IcnLexer;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Initialize lexer over null-terminated source text */
void icn_lex_init(IcnLexer *lex, const char *src);

/* Return next token. Caller owns any string/ident data (not freed by lexer).
 * Returns TK_EOF repeatedly after end of input. */
IcnToken icn_lex_next(IcnLexer *lex);

/* Peek at next token without consuming it.
 * NOTE: peeked string pointers become invalid after next call to icn_lex_next. */
IcnToken icn_lex_peek(IcnLexer *lex);

/* Human-readable token kind name (for diagnostics / test output) */
const char *icn_tk_name(IcnTkKind kind);

#endif /* ICON_LEX_H */
