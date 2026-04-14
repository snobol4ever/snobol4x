/*
 * raku_driver.h — Tiny-Raku compiler entry point
 *
 * raku_compile() parses .raku source and returns a Program*
 * with LANG_RAKU statements ready for --ir-run.
 *
 * Phase 1 (Rung 0-5): raku_eval_direct() interprets the AST
 * directly without going through IR — used for early rungs.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#ifndef RAKU_DRIVER_H
#define RAKU_DRIVER_H

#include "raku_ast.h"

/* Direct AST interpreter — Phase 1 (no IR).
 * Returns 0 on success, non-zero on error. */
int raku_eval_direct(RakuNode *program);

/* Parse src (null-terminated Raku source) and run it directly.
 * Used for .raku files before IR lowering is wired. */
int raku_run_string(const char *src);

/* Full pipeline: parse → lower → wrap → Program*.
 * Sets st->lang = LANG_RAKU on each STMT_t.
 * Returns NULL on parse failure. */
#include "../snobol4/scrip_cc.h"
Program *raku_compile(const char *src, const char *filename);

#endif /* RAKU_DRIVER_H */
