/*
 * snocone_lex.h — Snocone lexer public API
 *
 * Ported from snobol4jvm/src/SNOBOL4clojure/snocone.clj (Clojure reference).
 * Token kind names match the JVM KIND-* constants exactly (SC_ prefix here).
 *
 * Source model (Snocone language spec / snocone.clj):
 *   - Comments: # to end of line; not in token stream
 *   - Continuation: newline suppressed when last non-ws char is one of:
 *       @ $ % ^ & * ( - + = [ < > | ~ , ? :
 *   - Semicolons terminate statements within a line (same as newline)
 *   - Strings: delimited by matching ' or "
 *   - Numbers: integer or real (optional exponent eEdD+/-); leading-dot (.5) legal
 *   - Identifiers: [A-Za-z_][A-Za-z0-9_]*  reclassified to keyword if match
 *   - Operators: longest-match from op-entries table
 */

#ifndef SNOCONE_LEX_H
#define SNOCONE_LEX_H

#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Token kinds  (matches JVM KIND-* enum order)
 * ------------------------------------------------------------------------- */
typedef enum {
    /* Literals */
    SNOCONE_INTEGER,
    SNOCONE_REAL,
    SNOCONE_STRING,
    SNOCONE_IDENT,

    /* Keywords -- matched as identifiers, then reclassified */
    SNOCONE_KW_IF,
    SNOCONE_KW_ELSE,
    SNOCONE_KW_WHILE,
    SNOCONE_KW_DO,
    SNOCONE_KW_FOR,
    SNOCONE_KW_RETURN,
    SNOCONE_KW_FRETURN,
    SNOCONE_KW_NRETURN,
    SNOCONE_KW_GO,
    SNOCONE_KW_TO,
    SNOCONE_KW_PROCEDURE,
    SNOCONE_KW_STRUCT,

    /* Punctuation */
    SNOCONE_LPAREN,
    SNOCONE_RPAREN,
    SNOCONE_LBRACE,
    SNOCONE_RBRACE,
    SNOCONE_LBRACKET,
    SNOCONE_RBRACKET,
    SNOCONE_COMMA,
    SNOCONE_SEMICOLON,
    SNOCONE_COLON,

    /* Binary operators (precedence low->high, from bconv table in snocone.sc) */
    SNOCONE_ASSIGN,       /* =    prec 1/2  -> SNOBOL4 =          */
    SNOCONE_QUESTION,     /* ?    prec 2    -> SNOBOL4 ?           */
    SNOCONE_PIPE,         /* |    prec 3    -> SNOBOL4 |           */
    SNOCONE_OR,           /* ||   prec 4    -> SNOBOL4 (a,b)       */
    SNOCONE_CONCAT,       /* &&   prec 5    -> SNOBOL4 blank       */
    SNOCONE_EQ,           /* ==   prec 6    -> EQ(a,b)             */
    SNOCONE_NE,           /* !=   prec 6    -> NE(a,b)             */
    SNOCONE_LT,           /* <    prec 6    -> LT(a,b)             */
    SNOCONE_GT,           /* >    prec 6    -> GT(a,b)             */
    SNOCONE_LE,           /* <=   prec 6    -> LE(a,b)             */
    SNOCONE_GE,           /* >=   prec 6    -> GE(a,b)             */
    SNOCONE_STR_IDENT,    /* ::   prec 6    -> IDENT(a,b)          */
    SNOCONE_STR_DIFFER,   /* :!:  prec 6    -> DIFFER(a,b)         */
    SNOCONE_STR_LT,       /* :<:  prec 6    -> LLT(a,b)            */
    SNOCONE_STR_GT,       /* :>:  prec 6    -> LGT(a,b)            */
    SNOCONE_STR_LE,       /* :<=: prec 6    -> LLE(a,b)            */
    SNOCONE_STR_GE,       /* :>=: prec 6    -> LGE(a,b)            */
    SNOCONE_STR_EQ,       /* :==: prec 6    -> LEQ(a,b)            */
    SNOCONE_STR_NE,       /* :!=: prec 6    -> LNE(a,b)            */
    SNOCONE_PLUS,         /* +    prec 7                           */
    SNOCONE_MINUS,        /* -    prec 7                           */
    SNOCONE_SLASH,        /* /    prec 8                           */
    SNOCONE_STAR,         /* *    prec 8                           */
    SNOCONE_PERCENT,      /* %    prec 8    -> REMDR(a,b)          */
    SNOCONE_CARET,        /* ^ ** prec 9/10 right-assoc -> **      */
    SNOCONE_PERIOD,       /* .    prec 10   -> SNOBOL4 .           */
    SNOCONE_DOLLAR,       /* $    prec 10   -> SNOBOL4 $           */

    /* Unary-only operators */
    SNOCONE_AT,           /* @                                     */
    SNOCONE_AMPERSAND,    /* &                                     */
    SNOCONE_TILDE,        /* ~ logical negation                    */

    /* Compound assignment operators (SC-1 C-style extensions) */
    SNOCONE_PLUS_ASSIGN,  /* +=  -> x = x + rhs                   */
    SNOCONE_MINUS_ASSIGN, /* -=  -> x = x - rhs                   */
    SNOCONE_STAR_ASSIGN,  /* *=  -> x = x * rhs                   */
    SNOCONE_SLASH_ASSIGN, /* /=  -> x = x / rhs                   */
    SNOCONE_PERCENT_ASSIGN,/* %= -> x = x % rhs                   */
    SNOCONE_CARET_ASSIGN, /* ^=  -> x = x ^ rhs                   */

    /* Keywords added after original spec (SC-1) — before UNKNOWN to avoid
     * collision with SNOCONE_CALL/ARRAY_REF synthetic tokens in snocone_parse.h */
    SNOCONE_KW_THEN,      /* "then"     — optional after if(cond)  */
    SNOCONE_KW_GOTO,      /* "goto"     — C-style one-word form    */
    SNOCONE_KW_BREAK,     /* "break"    — exit innermost loop      */
    SNOCONE_KW_CONTINUE,  /* "continue" — next iteration           */

    /* Synthetic */
    SNOCONE_NEWLINE,      /* logical end-of-statement              */
    SNOCONE_EOF,
    SNOCONE_UNKNOWN
} SnoconeKind;

/* ---------------------------------------------------------------------------
 * Token struct
 * ------------------------------------------------------------------------- */
typedef struct {
    SnoconeKind  kind;
    char   *text;   /* heap-allocated, NUL-terminated verbatim source text */
    int     line;   /* 1-based physical line where token began              */
} SnoconeToken;

/* ---------------------------------------------------------------------------
 * Token array (returned by snocone_lex)
 * ------------------------------------------------------------------------- */
typedef struct {
    SnoconeToken *tokens;
    int      count;   /* includes the trailing SNOCONE_EOF */
} ScTokenArray;

/* ---------------------------------------------------------------------------
 * Public API
 *
 * snocone_lex(source)
 *   Tokenise a complete Snocone source string.
 *   Returns a heap-allocated ScTokenArray terminated by SNOCONE_EOF.
 *   Caller must free with sc_tokens_free().
 *
 * sc_tokens_free(arr)
 *   Free all memory owned by the token array.
 *
 * sc_kind_name(kind)
 *   Return a static string name for debugging/testing.
 * ------------------------------------------------------------------------- */
ScTokenArray snocone_lex(const char *source);
void         sc_tokens_free(ScTokenArray *arr);
const char  *sc_kind_name(SnoconeKind kind);

#endif /* SNOCONE_LEX_H */
