/*
 * icon_parse.h — Tiny-ICON recursive-descent parser API
 *
 * Parses explicit-semicolon Icon (no auto-insertion).
 * Produces EXPR_t / STMT_t IR directly — no intermediate AST.
 *
 * FI-2: IcnNode/icon_ast eliminated (2026-04-14).
 */

#ifndef ICON_PARSE_H
#define ICON_PARSE_H

#include "icon_lex.h"
#include "../snobol4/scrip_cc.h"   /* Program, STMT_t, EXPR_t, LANG_ICN */

/* -------------------------------------------------------------------------
 * Parser state
 * -------------------------------------------------------------------------*/
typedef struct {
    IcnLexer   *lex;
    IcnToken    cur;        /* current (already consumed) token */
    IcnToken    peek;       /* one-token lookahead */
    int         had_error;
    char        errmsg[512];
} IcnParser;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Initialize parser from an already-initialized lexer */
void     icn_parse_init(IcnParser *p, IcnLexer *lex);

/* Parse a complete Icon source file directly to IR.
 * Returns Program* (caller owns) or NULL on parse error. */
Program *icn_parse_file(IcnParser *p);

/* Parse a single expression to EXPR_t (useful for unit tests) */
EXPR_t  *icn_parse_expr(IcnParser *p);

#endif /* ICON_PARSE_H */
