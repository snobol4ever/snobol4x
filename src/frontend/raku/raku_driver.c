/*
 * raku_driver.c — Tiny-Raku compiler pipeline driver
 *
 * FI-3: parse → Program* directly, no intermediate RakuNode AST.
 * raku_compile() calls raku_parse_string() which runs the Bison parser;
 * grammar actions build EXPR_t/STMT_t inline and populate raku_prog_result.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "raku_driver.h"
#include "../snobol4/scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>

/* Declared in raku.y %code / raku.tab.h */
extern Program *raku_prog_result;
extern Program *raku_parse_string(const char *src);   /* defined below — wraps yyparse */

/*============================================================
 * raku_compile — public entry point (scrip.c calls this)
 *
 * Signature unchanged from pre-FI-3.
 * Returns Program* with LANG_RAKU stmts, or NULL on parse error.
 *============================================================*/
Program *raku_compile(const char *src, const char *filename) {
    if (!filename) filename = "<stdin>";
    raku_prog_result = NULL;   /* reset global before each parse */
    Program *prog = raku_parse_string(src);
    if (!prog) {
        fprintf(stderr, "raku: parse error in %s\n", filename);
        return NULL;
    }
    return prog;
}
