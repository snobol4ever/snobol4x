/*
 * emit_x64.h — x64 backend public interface
 *
 * Public entry point for the x64/Snocone lowering pass.
 */

#ifndef EMIT_X64_H
#define EMIT_X64_H

#include "scrip_cc.h"   /* Program, STMT_t, EXPR_t */

/*
 * emit_x64_snocone_compile(source, filename) → Program*
 *
 * Compiles a complete Snocone source string to a Program* (flat STMT_t list
 * with labels and goto fields — same IR as SNOBOL4/Icon/Prolog frontends).
 *
 * Handles: if/else, while, do-while, for, goto, go to, break, continue,
 * procedure, struct, return/freturn/nreturn, expression statements.
 *
 * break/continue lower to unconditional gotos — no new IR node types.
 * All NASM emission is handled by the shared emit_x64.c.
 *
 * Returns the compiled Program* (never NULL; partial on error).
 */
Program *emit_x64_snocone_compile(const char *source, const char *filename);

#endif /* EMIT_X64_H */
