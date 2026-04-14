/*
 * raku_driver.h — Tiny-Raku compiler entry point
 *
 * FI-3: direct IR, no intermediate RakuNode AST.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#ifndef RAKU_DRIVER_H
#define RAKU_DRIVER_H

#include "../snobol4/scrip_cc.h"   /* Program */

/* Full pipeline: parse → Program* directly (no lower step).
 * Sets st->lang = LANG_RAKU on each STMT_t.
 * Returns NULL on parse failure. */
Program *raku_compile(const char *src, const char *filename);

#endif /* RAKU_DRIVER_H */
