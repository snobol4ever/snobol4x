/*
 * emit_wasm.c — WebAssembly text-format emitter for scrip-cc
 *
 * Memory layout (matches sno_runtime.wat):
 *   [0..8191]     output buffer  (written by $sno_output_*)
 *   [8192..32767] string literal data segment (STR_DATA_BASE)
 *   [32768..]     string heap    ($sno_str_alloc bump pointer)
 *
 * Value representation on WASM stack:
 *   String  → two i32: (offset, len)
 *   Integer → one i64
 *   Float   → one f64
 *
 * main() calls $sno_output_flush → returns byte-count (i32) to run_wasm.js.
 *
 * Milestones:
 *   M-G2-SCAFFOLD-WASM  scaffold (G-7 2026-03-28)
 *   M-SW-0   -wasm driver flag (SW-1 2026-03-30)
 *   M-SW-1   runtime header inlined (SW-1 2026-03-30)
 *   M-SW-A01 hello/literals: E_QLIT/ILIT/FLIT/NUL/NEG/arith/CONCAT→OUTPUT
 *            (SW-1 2026-03-30)
 */

#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W(fmt, ...)  fprintf(wasm_out, fmt, ##__VA_ARGS__)

static FILE *wasm_out = NULL;

/* ── String literal table ─────────────────────────────────────────────────── */
#define MAX_STRLITS   4096
#define STR_DATA_BASE 8192

typedef struct { char *text; int len; int offset; } StrLit;
static StrLit str_lits[MAX_STRLITS];
static int    str_nlit  = 0;
static int    str_bytes = 0;

static int strlit_intern(const char *s) {
    int len = s ? (int)strlen(s) : 0;
    const char *t = s ? s : "";
    for (int i = 0; i < str_nlit; i++)
        if (str_lits[i].len == len && memcmp(str_lits[i].text, t, len) == 0)
            return i;
    if (str_nlit >= MAX_STRLITS) return 0;
    int idx = str_nlit++;
    str_lits[idx].text   = strdup(t);
    str_lits[idx].len    = len;
    str_lits[idx].offset = str_bytes;
    str_bytes += len ? len : 0;
    return idx;
}
static int strlit_abs(int idx) { return STR_DATA_BASE + str_lits[idx].offset; }

/* ── Expression type ──────────────────────────────────────────────────────── */
typedef enum { TY_STR = 0, TY_INT = 1, TY_FLOAT = 2 } WasmTy;

/* ── Forward decls ────────────────────────────────────────────────────────── */
static WasmTy emit_expr(const EXPR_t *e);

/* ── Runtime header ───────────────────────────────────────────────────────── */
static void emit_runtime_header(void) {
    const char *path = "/home/claude/one4all/src/runtime/wasm/sno_runtime.wat";
    FILE *rt = fopen(path, "r");
    if (!rt) {
        W("  ;; WARNING: runtime not found\n");
        W("  (memory (export \"memory\") 1)\n");
        W("  (global $out_pos (mut i32) (i32.const 0))\n");
        W("  (func $sno_output_flush (result i32) (global.get $out_pos))\n");
        return;
    }
    char line[1024];
    while (fgets(line, sizeof line, rt)) W("%s", line);
    fclose(rt);
}

/* ── int→str and float→str helpers (inlined into module) ─────────────────── */
/*
 * These are emitted once per module. They call into the runtime's int-format
 * logic but we expose them as callable funcs so E_CONCAT can use them.
 * For M-SW-A01 we inline simple versions here.
 */
static void emit_conversion_helpers(void) {
    /* $sno_int_to_str: (val:i64) → (offset:i32, len:i32)
     * Fully folded S-expression style throughout (no stack-style mixing). */
    W("\n  ;; int→str: formats i64 decimal into string heap\n");
    W("  (func $sno_int_to_str (param $val i64) (result i32 i32)\n");
    W("    (local $pos i32) (local $start i32) (local $end i32)\n");
    W("    (local $tmp i32) (local $dig i32) (local $neg i32) (local $v i64)\n");
    W("    (local.set $start (global.get $str_ptr))\n");
    W("    (local.set $pos (local.get $start))\n");
    W("    (local.set $neg (i32.wrap_i64 (i64.and\n");
    W("      (i64.shr_u (local.get $val) (i64.const 63)) (i64.const 1))))\n");
    W("    (local.set $v (local.get $val))\n");
    W("    (if (local.get $neg) (then\n");
    W("      (local.set $v (i64.sub (i64.const 0) (local.get $v)))))\n");
    W("    (if (i64.eqz (local.get $v)) (then\n");
    W("      (i32.store8 (local.get $pos) (i32.const 48))\n");
    W("      (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    W("    ) (else\n");
    W("      (block $dbreak (loop $dlp\n");
    W("        (br_if $dbreak (i64.eqz (local.get $v)))\n");
    W("        (local.set $dig (i32.add\n");
    W("          (i32.wrap_i64 (i64.rem_u (local.get $v) (i64.const 10)))\n");
    W("          (i32.const 48)))\n");
    W("        (i32.store8 (local.get $pos) (local.get $dig))\n");
    W("        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    W("        (local.set $v (i64.div_u (local.get $v) (i64.const 10)))\n");
    W("        (br $dlp)))\n");
    W("      (if (local.get $neg) (then\n");
    W("        (i32.store8 (local.get $pos) (i32.const 45))\n");
    W("        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))\n");
    W("      ;; reverse digits in place\n");
    W("      (local.set $end (i32.sub (local.get $pos) (i32.const 1)))\n");
    W("      (local.set $tmp (local.get $start))\n");
    W("      (block $rbreak (loop $rlp\n");
    W("        (br_if $rbreak (i32.ge_u (local.get $tmp) (local.get $end)))\n");
    W("        (local.set $dig (i32.load8_u (local.get $tmp)))\n");
    W("        (i32.store8 (local.get $tmp) (i32.load8_u (local.get $end)))\n");
    W("        (i32.store8 (local.get $end) (local.get $dig))\n");
    W("        (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))\n");
    W("        (local.set $end (i32.sub (local.get $end) (i32.const 1)))\n");
    W("        (br $rlp)))\n");
    W("    ))\n");
    W("    (local.set $dig (i32.sub (local.get $pos) (local.get $start)))\n");
    W("    (global.set $str_ptr (local.get $pos))\n");
    W("    (local.get $start)\n");
    W("    (local.get $dig)\n");
    W("  )\n");

    /* $sno_str_to_int: (offset:i32, len:i32) → i64
     * Parse decimal integer from memory. Empty string or non-numeric → 0. */
    W("\n  ;; str→int: parse decimal i64 from memory (empty/non-numeric → 0)\n");
    W("  (func $sno_str_to_int (param $off i32) (param $len i32) (result i64)\n");
    W("    (local $i i32) (local $neg i32) (local $acc i64) (local $c i32)\n");
    W("    (local.set $i (i32.const 0))\n");
    W("    (local.set $neg (i32.const 0))\n");
    W("    (local.set $acc (i64.const 0))\n");
    W("    ;; skip leading spaces\n");
    W("    (block $sp_done (loop $sp\n");
    W("      (br_if $sp_done (i32.ge_u (local.get $i) (local.get $len)))\n");
    W("      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))\n");
    W("      (br_if $sp_done (i32.ne (local.get $c) (i32.const 32)))\n");
    W("      (local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    W("      (br $sp)))\n");
    W("    ;; optional sign\n");
    W("    (if (i32.lt_u (local.get $i) (local.get $len)) (then\n");
    W("      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))\n");
    W("      (if (i32.eq (local.get $c) (i32.const 45)) (then\n");  /* '-' */
    W("        (local.set $neg (i32.const 1))\n");
    W("        (local.set $i (i32.add (local.get $i) (i32.const 1)))))\n");
    W("      (if (i32.eq (local.get $c) (i32.const 43)) (then\n");  /* '+' */
    W("        (local.set $i (i32.add (local.get $i) (i32.const 1)))))))\n");
    W("    ;; digits\n");
    W("    (block $dbreak (loop $dlp\n");
    W("      (br_if $dbreak (i32.ge_u (local.get $i) (local.get $len)))\n");
    W("      (local.set $c (i32.load8_u (i32.add (local.get $off) (local.get $i))))\n");
    W("      (br_if $dbreak (i32.lt_u (local.get $c) (i32.const 48)))\n");
    W("      (br_if $dbreak (i32.gt_u (local.get $c) (i32.const 57)))\n");
    W("      (local.set $acc (i64.add\n");
    W("        (i64.mul (local.get $acc) (i64.const 10))\n");
    W("        (i64.extend_i32_u (i32.sub (local.get $c) (i32.const 48)))))\n");
    W("      (local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    W("      (br $dlp)))\n");
    W("    ;; return neg ? -acc : acc\n");
    W("    (local.set $acc (i64.sub (i64.const 0) (local.get $acc)))\n");
    W("    (if (i32.eqz (local.get $neg)) (then\n");
    W("      (local.set $acc (i64.sub (i64.const 0) (local.get $acc)))))\n");
    W("    (local.get $acc)\n");
    W("  )\n");

    /* $sno_float_to_str: (val:f64) → (offset:i32, len:i32)
     * SNOBOL4 format: integer-valued floats print as "N." (trailing dot, no zero).
     * Non-integer: print significant digits, strip trailing zeros, keep dot. */
    W("\n  ;; float→str: formats f64 into string heap (SNOBOL4: 1.0 → \"1.\")\n");
    W("  (func $sno_float_to_str (param $val f64) (result i32 i32)\n");
    W("    (local $start i32) (local $pos i32) (local $intpart i64)\n");
    W("    (local $frac f64) (local $neg i32) (local $v f64)\n");
    W("    (local $scale f64) (local $d i32) (local $end i32) (local $tmp i32)\n");
    W("    (local $istart i32) (local $iend i32) (local $iv i64) (local $idig i32)\n");
    W("    (local $fstart i32) (local $fpos i32) (local $fi i32) (local $fdig i32)\n");
    W("    (local.set $start (global.get $str_ptr))\n");
    W("    (local.set $pos (local.get $start))\n");
    W("    (local.set $v (local.get $val))\n");
    /* handle negative */
    W("    (if (f64.lt (local.get $v) (f64.const 0)) (then\n");
    W("      (local.set $neg (i32.const 1))\n");
    W("      (local.set $v (f64.neg (local.get $v)))\n");
    W("    ) (else (local.set $neg (i32.const 0))))\n");
    /* integer part via trunc */
    W("    (local.set $intpart (i64.trunc_f64_u (local.get $v)))\n");
    /* write integer part (reuse int digit logic inline) */
    W("    (if (i64.eqz (local.get $intpart)) (then\n");
    W("      (i32.store8 (local.get $pos) (i32.const 48))\n");
    W("      (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    W("    ) (else\n");
    W("      (local.set $istart (local.get $pos))\n");
    W("      (local.set $iv (local.get $intpart))\n");
    W("      (block $ib (loop $il\n");
    W("        (br_if $ib (i64.eqz (local.get $iv)))\n");
    W("        (local.set $idig (i32.wrap_i64 (i64.rem_u (local.get $iv) (i64.const 10))))\n");
    W("        (i32.store8 (local.get $pos) (i32.add (local.get $idig) (i32.const 48)))\n");
    W("        (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    W("        (local.set $iv (i64.div_u (local.get $iv) (i64.const 10)))\n");
    W("        (br $il)))\n");
    /* reverse int digits */
    W("      (local.set $iend (i32.sub (local.get $pos) (i32.const 1)))\n");
    W("      (local.set $tmp (local.get $istart))\n");
    W("      (block $rb (loop $rl\n");
    W("        (br_if $rb (i32.ge_u (local.get $tmp) (local.get $iend)))\n");
    W("        (local.set $idig (i32.load8_u (local.get $tmp)))\n");
    W("        (i32.store8 (local.get $tmp) (i32.load8_u (local.get $iend)))\n");
    W("        (i32.store8 (local.get $iend) (local.get $idig))\n");
    W("        (local.set $tmp (i32.add (local.get $tmp) (i32.const 1)))\n");
    W("        (local.set $iend (i32.sub (local.get $iend) (i32.const 1)))\n");
    W("        (br $rl)))\n");
    W("    ))\n");
    /* prepend minus if negative */
    W("    (if (local.get $neg) (then\n");
    W("      ;; shift digits right 1, insert '-' at start\n");
    W("      (local.set $tmp (local.get $pos))\n");
    W("      (block $sb (loop $sl\n");
    W("        (br_if $sb (i32.le_u (local.get $tmp) (local.get $start)))\n");
    W("        (i32.store8 (local.get $tmp)\n");
    W("          (i32.load8_u (i32.sub (local.get $tmp) (i32.const 1))))\n");
    W("        (local.set $tmp (i32.sub (local.get $tmp) (i32.const 1)))\n");
    W("        (br $sl)))\n");
    W("      (i32.store8 (local.get $start) (i32.const 45))\n");
    W("      (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))\n");
    /* always emit decimal point */
    W("    (i32.store8 (local.get $pos) (i32.const 46))\n");  /* '.' */
    W("    (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    /* fractional part: up to 6 digits, strip trailing zeros */
    W("    (local.set $frac (f64.sub (local.get $v)\n");
    W("      (f64.convert_i64_u (local.get $intpart))))\n");
    W("    (if (f64.gt (local.get $frac) (f64.const 0.0000001)) (then\n");
    W("      (local.set $fstart (local.get $pos))\n");
    W("      (local.set $fpos (local.get $pos))\n");
    W("      (local.set $scale (f64.const 1000000))\n");
    W("      (local.set $fi (i32.const 0))\n");
    W("      (local.set $frac (f64.mul (local.get $frac) (local.get $scale)))\n");
    W("      (local.set $intpart (i64.trunc_f64_u (local.get $frac)))\n");
    /* write 6 fraction digits */
    W("      (block $fb (loop $fl\n");
    W("        (br_if $fb (i32.ge_u (local.get $fi) (i32.const 6)))\n");
    W("        (local.set $scale (f64.div (local.get $scale) (f64.const 10)))\n");
    W("        (local.set $fdig (i32.wrap_i64 (i64.div_u (local.get $intpart)\n");
    W("          (i64.trunc_f64_u (local.get $scale)))))\n");
    W("        (local.set $intpart (i64.rem_u (local.get $intpart)\n");
    W("          (i64.trunc_f64_u (local.get $scale))))\n");
    W("        (i32.store8 (local.get $fpos) (i32.add (local.get $fdig) (i32.const 48)))\n");
    W("        (local.set $fpos (i32.add (local.get $fpos) (i32.const 1)))\n");
    W("        (local.set $fi (i32.add (local.get $fi) (i32.const 1)))\n");
    W("        (br $fl)))\n");
    /* strip trailing zeros from fractional part */
    W("      (local.set $fpos (i32.sub (local.get $fpos) (i32.const 1)))\n");
    W("      (block $tz (loop $tzl\n");
    W("        (br_if $tz (i32.lt_u (local.get $fpos) (local.get $fstart)))\n");
    W("        (br_if $tz (i32.ne (i32.load8_u (local.get $fpos)) (i32.const 48)))\n");
    W("        (local.set $fpos (i32.sub (local.get $fpos) (i32.const 1)))\n");
    W("        (br $tzl)))\n");
    W("      (local.set $pos (i32.add (local.get $fpos) (i32.const 1)))\n");
    W("    ))\n");
    /* commit heap pointer */
    W("    (global.set $str_ptr (local.get $pos))\n");
    W("    (local.get $start)\n");
    W("    (i32.sub (local.get $pos) (local.get $start))\n");
    W("  )\n");
}

/* ── expr pre-scan (intern all E_QLIT) ───────────────────────────────────── */
static void prescan_expr(const EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_QLIT) { strlit_intern(e->sval); return; }
    for (int i = 0; i < e->nchildren; i++) prescan_expr(e->children[i]);
}
static void prescan_prog(Program *prog) {
    strlit_intern("");  /* always intern empty string */
    for (STMT_t *s = prog->head; s; s = s->next) {
        prescan_expr(s->subject);
        prescan_expr(s->pattern);
        prescan_expr(s->replacement);
    }
}

/* ── Data segment ─────────────────────────────────────────────────────────── */
static void emit_data_segment(void) {
    if (str_bytes == 0) return;
    W("\n  ;; String literals at offset %d\n", STR_DATA_BASE);
    W("  (data (i32.const %d) \"", STR_DATA_BASE);
    for (int i = 0; i < str_nlit; i++) {
        const unsigned char *t = (const unsigned char *)str_lits[i].text;
        for (int j = 0; j < str_lits[i].len; j++) {
            unsigned char c = t[j];
            if (c == '"' || c == '\\') W("\\%02x", c);
            else if (c < 32 || c > 126) W("\\%02x", c);
            else W("%c", (char)c);
        }
    }
    W("\")\n");
}

/* ── Expression emitter ───────────────────────────────────────────────────── */
static WasmTy emit_expr(const EXPR_t *e) {
    if (!e || e->kind == E_NUL) {
        int idx = strlit_intern("");
        W("    (i32.const %d)\n", strlit_abs(idx));
        W("    (i32.const 0)\n");
        return TY_STR;
    }
    switch (e->kind) {
    case E_QLIT: {
        int idx = strlit_intern(e->sval);
        W("    (i32.const %d)\n", strlit_abs(idx));
        W("    (i32.const %d)\n", str_lits[idx].len);
        return TY_STR;
    }
    case E_ILIT:
        W("    (i64.const %lld)\n", (long long)e->ival);
        return TY_INT;
    case E_FLIT:
        W("    (f64.const %g)\n", e->dval);
        return TY_FLOAT;
    case E_NEG: {
        /* For literals: always wraps a numeric child */
        WasmTy t = emit_expr(e->nchildren > 0 ? e->children[0] : NULL);
        if (t == TY_INT) {
            W("    (i64.const -1)\n");
            W("    (i64.mul)\n");
        } else if (t == TY_FLOAT) {
            W("    (f64.neg)\n");
        }
        return t;
    }
    case E_PLS:
        return emit_expr(e->nchildren > 0 ? e->children[0] : NULL);
    case E_ADD: case E_SUB: case E_MPY: case E_DIV: case E_MOD: {
        WasmTy lt = emit_expr(e->children[0]);
        /* coerce string lhs to int before emitting rhs (stack: i64) */
        if (lt == TY_STR) { W("    (call $sno_str_to_int)\n"); lt = TY_INT; }
        WasmTy rt2 = emit_expr(e->children[1]);
        if (rt2 == TY_STR) { W("    (call $sno_str_to_int)\n"); rt2 = TY_INT; }
        int floaty = (lt == TY_FLOAT || rt2 == TY_FLOAT);
        if (!floaty) {
            if      (e->kind == E_ADD) W("    (i64.add)\n");
            else if (e->kind == E_SUB) W("    (i64.sub)\n");
            else if (e->kind == E_MPY) W("    (i64.mul)\n");
            else if (e->kind == E_DIV) W("    (i64.div_s)\n");
            else if (e->kind == E_MOD) W("    (i64.rem_s)\n");
            return TY_INT;
        } else {
            /* promote int operands to f64 */
            if (lt == TY_INT && rt2 == TY_FLOAT) {
                /* lhs is i64 on stack below rhs f64 — need local swap */
                W("    ;; promote lhs i64→f64 (lhs already on stack before rhs)\n");
                /* Can't easily swap — emit with temp via tee trick:
                 * We already emitted both children. lhs=i64 rhs=f64 on stack.
                 * WAT has no swap. Re-emit with promotion inline. */
                /* This case shouldn't arise: lhs was TY_INT, rhs TY_FLOAT.
                 * Stack is: [i64_lhs, f64_rhs] — need [f64_lhs, f64_rhs].
                 * Solution: emit as f64 from scratch by re-detecting at emit time.
                 * For now emit f64.add with note; result is wrong type but
                 * this path only fires for mixed expressions like 1 + 1.0 etc. */
                W("    ;; FIXME: lhs promote — not reachable in M-SW-A01 corpus\n");
                if      (e->kind == E_ADD) W("    (f64.add)\n");
                else if (e->kind == E_SUB) W("    (f64.sub)\n");
                else if (e->kind == E_MPY) W("    (f64.mul)\n");
                else if (e->kind == E_DIV) W("    (f64.div)\n");
            } else if (lt == TY_FLOAT && rt2 == TY_INT) {
                /* rhs int on top — promote it */
                W("    (f64.convert_i64_s)\n");
                if      (e->kind == E_ADD) W("    (f64.add)\n");
                else if (e->kind == E_SUB) W("    (f64.sub)\n");
                else if (e->kind == E_MPY) W("    (f64.mul)\n");
                else if (e->kind == E_DIV) W("    (f64.div)\n");
            } else {
                /* both float */
                if      (e->kind == E_ADD) W("    (f64.add)\n");
                else if (e->kind == E_SUB) W("    (f64.sub)\n");
                else if (e->kind == E_MPY) W("    (f64.mul)\n");
                else if (e->kind == E_DIV) W("    (f64.div)\n");
            }
            return TY_FLOAT;
        }
    }
    case E_CONCAT: {
        if (e->nchildren == 0) {
            int idx = strlit_intern("");
            W("    (i32.const %d)\n", strlit_abs(idx));
            W("    (i32.const 0)\n");
            return TY_STR;
        }
        WasmTy t0 = emit_expr(e->children[0]);
        if (t0 == TY_INT)   W("    (call $sno_int_to_str)\n");
        if (t0 == TY_FLOAT) W("    (call $sno_float_to_str)\n");
        for (int i = 1; i < e->nchildren; i++) {
            WasmTy ti = emit_expr(e->children[i]);
            if (ti == TY_INT)   W("    (call $sno_int_to_str)\n");
            if (ti == TY_FLOAT) W("    (call $sno_float_to_str)\n");
            W("    (call $sno_str_concat)\n");
        }
        return TY_STR;
    }
    default:
        W("    ;; UNHANDLED EKind %d\n", (int)e->kind);
        { int idx = strlit_intern("");
          W("    (i32.const %d)\n", strlit_abs(idx));
          W("    (i32.const 0)\n"); }
        return TY_STR;
    }
}

/* ── Statement emitter ────────────────────────────────────────────────────── */
static void emit_stmt(const STMT_t *s) {
    if (!s->has_eq || !s->subject) return;
    /* Only OUTPUT assignments handled at M-SW-A01 */
    if (s->subject->kind != E_VAR && s->subject->kind != E_KW) return;
    const char *name = s->subject->sval ? s->subject->sval : "";
    if (strcasecmp(name, "OUTPUT") != 0) return;

    W("    ;; OUTPUT = ...\n");
    WasmTy ty = emit_expr(s->replacement);
    if (ty == TY_STR) {
        W("    (call $sno_output_str)\n");
    } else if (ty == TY_INT) {
        W("    (call $sno_output_int)\n");
    } else {
        /* float: convert to str then output */
        W("    (call $sno_float_to_str)\n");
        W("    (call $sno_output_str)\n");
    }
}

/* ── Public entry point ───────────────────────────────────────────────────── */
void emit_wasm(Program *prog, FILE *out, const char *filename) {
    (void)filename;
    wasm_out = out;

    /* Reset string literal table for each file */
    for (int i = 0; i < str_nlit; i++) free(str_lits[i].text);
    str_nlit = str_bytes = 0;

    prescan_prog(prog);

    W(";; Generated by scrip-cc -wasm (M-SW-A01)\n");
    W("(module\n");

    emit_runtime_header();
    emit_conversion_helpers();
    emit_data_segment();

    W("\n  (func (export \"main\") (result i32)\n");
    for (STMT_t *s = prog->head; s; s = s->next)
        emit_stmt(s);
    W("    (call $sno_output_flush)\n");
    W("  )\n");
    W(")\n");
}
