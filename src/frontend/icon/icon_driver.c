/*
 * icon_driver.c -- Icon frontend pipeline driver
 *
 * icon_compile(source, filename) -> Program*
 *
 * Pipeline:
 *   icn_lex_init() -> IcnLexer
 *   icn_parse_file() -> IcnNode**
 *   icon_lower_file() -> EXPR_t**
 *   wrap each EXPR_t* as a STMT_t subject -> Program*
 */

#include "icon_driver.h"
#include "icon_lex.h"
#include "icon_parse.h"
#include "icon_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Program *icon_compile(const char *source, const char *filename) {
    if (!filename) filename = "<stdin>";

    /* Lex */
    IcnLexer lx;
    icn_lex_init(&lx, source);

    /* Parse */
    IcnParser parser;
    icn_parse_init(&parser, &lx);
    int count = 0;
    IcnNode **procs = icn_parse_file(&parser, &count);
    if (parser.had_error) {
        fprintf(stderr, "icon: parse error in %s: %s\n", filename, parser.errmsg);
        return NULL;
    }

    /* Lower IcnNode** -> EXPR_t** */
    int lcount = 0;
    EXPR_t **lowered = icon_lower_file(procs, count, &lcount);

    /* Wrap each EXPR_t* as a STMT_t subject -> Program* */
    Program *prog = calloc(1, sizeof(Program));
    for (int i = 0; i < lcount; i++) {
        if (!lowered[i]) continue;
        STMT_t *st = calloc(1, sizeof(STMT_t));
        st->subject = lowered[i];
        st->lineno  = 0;
        if (!prog->head) prog->head = prog->tail = st;
        else           { prog->tail->next = st; prog->tail = st; }
        prog->nstmts++;
    }

    free(lowered);
    for (int i = 0; i < count; i++) icn_node_free(procs[i]);
    free(procs);

    return prog;
}
