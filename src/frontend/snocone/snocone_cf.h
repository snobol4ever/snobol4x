/*
 * sc_cf.h — Snocone control-flow lowering pass  (Sprint SC4-ASM)
 *
 * Replaces sc_driver.c's expression-only pipeline with a full clause-walking
 * pass that handles if/while/for/goto/procedure/return and emits labeled
 * SNOBOL4-style STMT_t nodes with goto fields for the ASM backend.
 */

#ifndef SNOCONE_CF_H
#define SNOCONE_CF_H

#include "scrip_cc.h"   /* Program, STMT_t, EXPR_t, expr_new, stmt_new, etc. */

/*
 * snocone_cf_compile(source, filename) → Program*
 *
 * Compiles a complete Snocone source string to a Program* using the
 * full control-flow lowering pass (if/while/for/procedure/goto/return).
 *
 * Replaces snocone_compile() from sc_driver.c for the ASM backend.
 * Returns the compiled Program* (never NULL; partial on error).
 */
Program *snocone_cf_compile(const char *source, const char *filename);

#endif /* SNOCONE_CF_H */
