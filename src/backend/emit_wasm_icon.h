#ifndef EMIT_WASM_ICON_H
#define EMIT_WASM_ICON_H
/*
 * emit_wasm_icon.h — public interface for Icon × WASM emitter
 *
 * Called from emit_wasm.c when it encounters an ICN_* node.
 * All other frontends (SNOBOL4, Prolog) are unaffected.
 */
#include "icon_ast.h"
#include <stdio.h>

/* Set the output file handle (called before any emit function). */
void emit_wasm_icon_set_out(FILE *f);

/* Emit WAT (global …) declarations for all per-node value globals.
 * Call once, before the (func …) section. */
void emit_wasm_icon_globals(FILE *out);
void emit_wasm_icon_str_globals(FILE *out);   /* M-IW-A02: string literal (off,len) globals */

/* Dispatch: emit WAT for one ICN_* node and its sub-tree.
 * Returns 1 if handled, 0 if unknown kind. */
int emit_wasm_icon_node(const IcnNode *n, FILE *out);

/* True if kind is an ICN_* node handled by this emitter. */
int is_icon_node(int kind);

/* Top-level file emitter — called from main.c for -icn -wasm (M-IW-A01).
 * Emits a complete .wat module for an array of ICN_PROC nodes. */
void emit_wasm_icon_file(IcnNode **procs, int count, FILE *out,
                          const char *filename);

#endif /* EMIT_WASM_ICON_H */
