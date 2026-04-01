/*
 * bb_emit.h — Dual-Mode x86-64 Emitter (M-DYN-1)
 *
 * Every NASM macro in snobol4_asm.mac has a corresponding C function
 * mac_*() in bb_macros.c.  Those functions call the primitives here.
 *
 * MODE SWITCH
 * -----------
 * bb_emit_mode controls all output:
 *
 *   EMIT_TEXT   — write NASM assembly text to bb_emit_out (FILE*).
 *                 Labels are symbolic strings.  Output is a .s file
 *                 fed to NASM → ELF → linker.  This is the current
 *                 proven static path.
 *
 *   EMIT_BINARY — write raw x86-64 bytes into the current bb_pool
 *                 buffer via bb_emit_buf / bb_emit_pos.
 *                 Labels are byte offsets.  Forward refs are tracked
 *                 in a patch list and resolved when labels are defined.
 *
 * LABEL SYSTEM
 * ------------
 * bb_label_t carries both a symbolic name (text mode) and a buffer
 * offset (binary mode).  Labels start unresolved (offset == -1).
 * bb_label_define() resolves a label at the current emit position and
 * patches all pending forward references to it.
 *
 * PATCH LIST
 * ----------
 * When binary mode emits a jump to an unresolved label, it records a
 * (patch_site, label_id) entry.  bb_label_define() walks the list and
 * fills in the correct rel8 or rel32 displacement.
 *
 * USAGE SEQUENCE (binary mode)
 * ----------------------------
 *   bb_emit_begin(buf, size);        // attach emitter to pool buffer
 *   mac_LIT_α(...);                  // emit bytes + record patches
 *   mac_LIT_β(...);
 *   bb_emit_end();                   // resolve all patches, returns bytes written
 *   bb_seal(buf, bytes_written);     // mprotect RW→RX
 *   box_fn fn = (box_fn)buf;
 *   fn(subject, len);
 */

#ifndef BB_EMIT_H
#define BB_EMIT_H

#include "bb_pool.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* ── mode ───────────────────────────────────────────────────────────────── */

typedef enum {
    EMIT_TEXT   = 0,
    EMIT_BINARY = 1
} bb_emit_mode_t;

extern bb_emit_mode_t bb_emit_mode;
extern FILE          *bb_emit_out;   /* text mode: output FILE* (default stdout) */

/* ── label ──────────────────────────────────────────────────────────────── */

#define BB_LABEL_NAME_MAX  80
#define BB_LABEL_UNRESOLVED (-1)

typedef struct {
    char name[BB_LABEL_NAME_MAX];   /* symbolic name — text mode + diagnostics */
    int  offset;                    /* byte offset in current buffer; -1=unresolved */
} bb_label_t;

/* Initialise a label with a symbolic name, unresolved offset */
void bb_label_init(bb_label_t *lbl, const char *name);

/* Initialise a label with printf-style name formatting */
void bb_label_initf(bb_label_t *lbl, const char *fmt, ...);

/* Define label at the current emit position (binary: sets offset + patches).
 * In text mode: emits "name:" on its own line. */
void bb_label_define(bb_label_t *lbl);

/* True if label has been defined (offset != BB_LABEL_UNRESOLVED) */
#define bb_label_defined(lbl)  ((lbl)->offset != BB_LABEL_UNRESOLVED)

/* ── binary mode state ──────────────────────────────────────────────────── */

extern bb_buf_t  bb_emit_buf;   /* current pool buffer */
extern int       bb_emit_pos;   /* current write position (bytes written so far) */
extern int       bb_emit_size;  /* total buffer size */

/* Attach emitter to a freshly-allocated pool buffer */
void bb_emit_begin(bb_buf_t buf, int size);

/* Finalise: resolve any remaining patches, return bytes written.
 * Aborts if any labels are still unresolved. */
int  bb_emit_end(void);

/* ── patch list ─────────────────────────────────────────────────────────── */

/* Maximum simultaneous forward references (generous: deep ARBNO patterns) */
#define BB_PATCH_MAX  512

typedef enum {
    PATCH_REL8,    /* 1-byte signed displacement, relative to patch_site+1 */
    PATCH_REL32    /* 4-byte signed displacement, relative to patch_site+4 */
} bb_patch_kind_t;

typedef struct {
    int              site;    /* offset of the displacement field in the buffer */
    bb_label_t      *label;   /* label whose offset we need */
    bb_patch_kind_t  kind;
} bb_patch_t;

extern bb_patch_t bb_patch_list[BB_PATCH_MAX];
extern int        bb_patch_count;

/* Record a forward reference at the current position.
 * Emits a 0-placeholder of the appropriate width. */
void bb_emit_patch_rel8 (bb_label_t *lbl);
void bb_emit_patch_rel32(bb_label_t *lbl);

/* ── byte primitives ────────────────────────────────────────────────────── */

/* These are the only functions that write bytes.  All higher-level
 * helpers (instruction emitters, mac_* functions) call these. */

void bb_emit_byte(uint8_t b);
void bb_emit_u16 (uint16_t v);
void bb_emit_u32 (uint32_t v);
void bb_emit_u64 (uint64_t v);
void bb_emit_i8  (int8_t   v);
void bb_emit_i32 (int32_t  v);

/* ── x86-64 instruction helpers ─────────────────────────────────────────── */
/*
 * One function per instruction form used by the mac_* layer.
 * Names follow: bb_insn_MNEMONIC_OPERAND_FORM
 *
 * Registers are implicit (SysV AMD64 calling convention throughout):
 *   subject ptr  = rdi
 *   subject len  = esi / rsi
 *   cursor       = a .bss slot or stack slot addressed by label
 *   scratch      = rax, rcx, rdx
 *
 * In text mode these emit the NASM instruction string.
 * In binary mode these emit raw bytes.
 */

/* mov eax, imm32 */
void bb_insn_mov_eax_imm32(uint32_t imm);

/* mov rax, imm64  (for absolute C shim addresses) */
void bb_insn_mov_rax_imm64(uint64_t imm);

/* ret */
void bb_insn_ret(void);

/* nop */
void bb_insn_nop(void);

/* call rax */
void bb_insn_call_rax(void);

/* jmp rel8  — short unconditional jump, forward ref supported */
void bb_insn_jmp_rel8(bb_label_t *target);

/* jmp rel32 — near unconditional jump, forward ref supported */
void bb_insn_jmp_rel32(bb_label_t *target);

/* jl  rel8  — jump if less (SF≠OF), forward ref */
void bb_insn_jl_rel8 (bb_label_t *target);

/* jge rel8  — jump if greater-or-equal, forward ref */
void bb_insn_jge_rel8(bb_label_t *target);

/* je  rel8  — jump if equal (ZF=1), forward ref */
void bb_insn_je_rel8 (bb_label_t *target);

/* jne rel8  — jump if not equal, forward ref */
void bb_insn_jne_rel8(bb_label_t *target);

/* jne rel32 — jump if not equal, near, forward ref */
void bb_insn_jne_rel32(bb_label_t *target);

/* cmp esi, imm8  — compare subject length against literal */
void bb_insn_cmp_esi_imm8(uint8_t imm);

/* cmp esi, imm32 */
void bb_insn_cmp_esi_imm32(uint32_t imm);

/* movzx eax, byte [rdi + imm8]  — load subject byte at offset */
void bb_insn_movzx_eax_rdi_off8(uint8_t off);

/* cmp al, imm8 */
void bb_insn_cmp_al_imm8(uint8_t imm);

/* xor eax, eax  — zero eax */
void bb_insn_xor_eax_eax(void);

/* push rbp / pop rbp / mov rbp,rsp */
void bb_insn_push_rbp(void);
void bb_insn_pop_rbp(void);
void bb_insn_mov_rbp_rsp(void);

/* sub rsp, imm8 / add rsp, imm8 */
void bb_insn_sub_rsp_imm8(uint8_t imm);
void bb_insn_add_rsp_imm8(uint8_t imm);

/* ── text mode helpers ───────────────────────────────────────────────────── */

/* Emit a raw text line (text mode only — no-op in binary mode) */
void bb_text(const char *fmt, ...);

/* Emit a label definition line "name:\n" (text) or define offset (binary) */
void bb_text_label(bb_label_t *lbl);

/* Emit a comment line (text mode only) */
void bb_text_comment(const char *fmt, ...);

#endif /* BB_EMIT_H */
