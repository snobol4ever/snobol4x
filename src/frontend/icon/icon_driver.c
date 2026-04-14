/*
 * icon_driver.c -- Icon frontend pipeline driver
 *
 * icon_compile(source, filename) -> Program*
 *
 * Pipeline (FI-2: direct IR, no intermediate AST):
 *   icn_lex_init() -> IcnLexer
 *   icn_parse_file() -> Program*   (EXPR_t/STMT_t built inline)
 *
 * Authors: Claude Sonnet 4.6 (FI-2, 2026-04-14)
 */

#include "icon_driver.h"
#include "icon_lex.h"
#include "icon_parse.h"

#include <stdio.h>
#include <stdlib.h>

Program *icon_compile(const char *source, const char *filename) {
    if (!filename) filename = "<stdin>";

    IcnLexer lx;
    icn_lex_init(&lx, source);

    IcnParser parser;
    icn_parse_init(&parser, &lx);
    Program *prog = icn_parse_file(&parser);
    if (parser.had_error) {
        fprintf(stderr, "icon: parse error in %s: %s\n", filename, parser.errmsg);
        free(prog);
        return NULL;
    }
    return prog;
}
