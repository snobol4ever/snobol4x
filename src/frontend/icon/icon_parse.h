/*
 * icon_parse.h — Tiny-ICON recursive-descent parser API
 *
 * Parses explicit-semicolon Icon (no auto-insertion).
 * Produces an IcnNode* AST.
 */

#ifndef ICON_PARSE_H
#define ICON_PARSE_H

#include "icon_lex.h"
#include "icon_ast.h"

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
void icn_parse_init(IcnParser *p, IcnLexer *lex);

/* Parse a complete Icon source file.
 * Returns a list of top-level IcnNode* (ICN_PROC / ICN_GLOBAL).
 * Returns NULL on parse error.
 * Caller frees via icn_node_free(). */
IcnNode **icn_parse_file(IcnParser *p, int *out_count);

/* Parse a single expression (useful for unit tests) */
IcnNode  *icn_parse_expr(IcnParser *p);

#endif /* ICON_PARSE_H */
