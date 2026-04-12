/*
 * sc_driver.h — Snocone frontend pipeline driver  (Sprint SC3)
 *
 * snocone_compile(src, filename) → Program*
 *
 * Runs the full Snocone pipeline on a NUL-terminated source string:
 *
 *   snocone_lex()  → ScTokenArray
 *   per-stmt: snocone_parse() → ScParseResult (postfix tokens)
 *   snocone_lower() on combined postfix stream → ScLowerResult → Program*
 *
 * The per-stmt split is identical to the pipeline() helper that was
 * proven in test/frontend/snocone/sc_lower_test.c (50/50 PASS).
 *
 * Returns NULL on lex/parse/lower error (errors already printed to stderr).
 * The returned Program* is heap-allocated; caller does NOT free it (it
 * lives until process exit, consistent with snoc_parse() convention).
 */

#ifndef SNOCONE_DRIVER_H
#define SNOCONE_DRIVER_H

#include "../snobol4/scrip_cc.h"   /* Program */

/*
 * snocone_compile(source, filename)
 *   source   — complete NUL-terminated Snocone source text
 *   filename — used in error messages (may be NULL → "<stdin>")
 *
 * Returns the compiled Program*, or NULL on error.
 */
Program *snocone_compile(const char *source, const char *filename);

#endif /* SNOCONE_DRIVER_H */
