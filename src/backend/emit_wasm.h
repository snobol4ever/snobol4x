/*
 * emit_wasm.h — Shared WASM emitter API for sibling frontend emitters
 *
 * emit_wasm.c owns the string literal table and output stream.
 * Prolog (emit_wasm_prolog.c) and Icon (emit_wasm_icon.c) call these
 * helpers to intern atom/string literals and share the same data segment.
 *
 * Usage in a sibling emitter:
 *   #include "emit_wasm.h"
 *   ...
 *   emit_wasm_set_out(my_out_file);         // share output stream
 *   int idx = emit_wasm_strlit_intern("hello");
 *   int off = emit_wasm_strlit_abs(idx);
 *   int len = emit_wasm_strlit_len(idx);
 *   emit_wasm_data_segment();               // emit (data ...) block
 *
 * Created: PW-2 M-PW-HELLO (2026-03-30)
 */

#ifndef EMIT_WASM_H
#define EMIT_WASM_H

#include <stdio.h>

/* Share the output stream — call before any W() output from a sibling emitter.
 *
 * ⚠ SIBLING EMITTER CONTRACT (IW-12):
 *   Every sibling entry point (emit_wasm_icon_file, prolog_emit_wasm, etc.)
 *   MUST call emit_wasm_set_out(out) in addition to its own set_out.
 *   emit_wasm.c's W() macro uses the shared wasm_out; helpers like
 *   emit_wasm_data_segment() will SIGSEGV if wasm_out is NULL.
 *
 *   Correct pattern (Icon example):
 *     emit_wasm_icon_set_out(out);   // sets icon_wasm_out → WI() macro
 *     emit_wasm_set_out(out);        // sets wasm_out      → W() macro in emit_wasm.c
 */
void emit_wasm_set_out(FILE *f);

/* Intern a string literal into the shared string table.
 * Returns the index; safe to call multiple times with same string (deduped). */
int  emit_wasm_strlit_intern(const char *s);

/* Absolute memory offset of string literal at index idx (STR_DATA_BASE + offset) */
int  emit_wasm_strlit_abs(int idx);

/* Byte length of string literal at index idx */
int  emit_wasm_strlit_len(int idx);

/* Emit the (data ...) segment for all interned literals.
 * Called once from the sibling emitter's module preamble. */
void emit_wasm_data_segment(void);

/* Reset the string table (called at start of each emit pass) */
void emit_wasm_strlit_reset(void);

/* Number of interned string literals (used to emit per-literal globals) */
int  emit_wasm_strlit_count(void);

/* Emit the 10 base "sno" namespace imports shared by SNOBOL4 and Icon:
 *   memory (npages pages), sno_output_str/int/flush, sno_str_alloc/concat/eq/
 *   sno_str_to_int/sno_int_to_str/sno_float_to_str, sno_pow.
 * npages: 2 for SNOBOL4 (output+heap), 3 for Icon (adds gen-state page).
 * Caller appends any additional frontend-specific imports after this call. */
void emit_wasm_runtime_imports_sno_base(FILE *out, int npages, const char *page_comment);


#include "../ir/ir.h"  /* EXPR_t */

/* WasmTy — value-type tag returned by emit_wasm_expr */
typedef enum { TY_STR = 0, TY_INT = 1, TY_FLOAT = 2 } WasmTy;

/* Emit a WASM expression for an IR node into the current output stream.
 * Shared by SNOBOL4, Icon, and Prolog WASM emitters for arithmetic/literals.
 * Returns the WasmTy of the emitted value. */
WasmTy emit_wasm_expr(const EXPR_t *e);

/* ── Icon × WASM emitter (formerly emit_wasm_icon.h) ─────────────────────── */
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
int emit_wasm_icon_node(const EXPR_t *n, FILE *out);

/* True if kind is an ICN_* node handled by this emitter. */
int is_icon_node(int kind);

/* Top-level file emitter — called from main.c for -icn -wasm (M-IW-A01).
 * Emits a complete .wat module for an array of ICN_PROC nodes. */
void emit_wasm_icon_file(EXPR_t **procs, int count, FILE *out,
                          const char *filename);

#endif /* EMIT_WASM_H */
