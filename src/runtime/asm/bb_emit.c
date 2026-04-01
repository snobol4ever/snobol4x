/*
 * bb_emit.c — Dual-Mode x86-64 Emitter (M-DYN-1)
 *
 * See bb_emit.h for design notes.
 */

#include "bb_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/* ── global state ───────────────────────────────────────────────────────── */

bb_emit_mode_t  bb_emit_mode = EMIT_TEXT;
FILE           *bb_emit_out  = NULL;   /* set by caller; defaults to stdout */

bb_buf_t        bb_emit_buf  = NULL;
int             bb_emit_pos  = 0;
int             bb_emit_size = 0;

bb_patch_t      bb_patch_list[BB_PATCH_MAX];
int             bb_patch_count = 0;

/* ── label ──────────────────────────────────────────────────────────────── */

void bb_label_init(bb_label_t *lbl, const char *name)
{
    strncpy(lbl->name, name, BB_LABEL_NAME_MAX - 1);
    lbl->name[BB_LABEL_NAME_MAX - 1] = '\0';
    lbl->offset = BB_LABEL_UNRESOLVED;
}

void bb_label_initf(bb_label_t *lbl, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lbl->name, BB_LABEL_NAME_MAX, fmt, ap);
    va_end(ap);
    lbl->offset = BB_LABEL_UNRESOLVED;
}

void bb_label_define(bb_label_t *lbl)
{
    if (bb_emit_mode == EMIT_TEXT) {
        FILE *f = bb_emit_out ? bb_emit_out : stdout;
        fprintf(f, "%s:\n", lbl->name);
        return;
    }

    /* Binary mode: record offset, patch all pending forward refs */
    lbl->offset = bb_emit_pos;

    for (int i = 0; i < bb_patch_count; i++) {
        bb_patch_t *p = &bb_patch_list[i];
        if (p->label != lbl) continue;

        int target = lbl->offset;

        if (p->kind == PATCH_REL8) {
            /* rel8: displacement = target - (site + 1) */
            int disp = target - (p->site + 1);
            if (disp < -128 || disp > 127) {
                fprintf(stderr,
                        "bb_label_define: rel8 overflow for '%s': disp=%d\n",
                        lbl->name, disp);
                abort();
            }
            bb_emit_buf[p->site] = (uint8_t)(int8_t)disp;
        } else {
            /* rel32: displacement = target - (site + 4) */
            int disp = target - (p->site + 4);
            uint32_t u;
            memcpy(&u, &disp, 4);
            bb_emit_buf[p->site + 0] = (uint8_t)(u      );
            bb_emit_buf[p->site + 1] = (uint8_t)(u >>  8);
            bb_emit_buf[p->site + 2] = (uint8_t)(u >> 16);
            bb_emit_buf[p->site + 3] = (uint8_t)(u >> 24);
        }

        /* Remove from patch list (swap with last) */
        bb_patch_list[i] = bb_patch_list[--bb_patch_count];
        i--;   /* re-check this slot */
    }
}

/* ── session ────────────────────────────────────────────────────────────── */

void bb_emit_begin(bb_buf_t buf, int size)
{
    bb_emit_buf   = buf;
    bb_emit_pos   = 0;
    bb_emit_size  = size;
    bb_patch_count = 0;
}

int bb_emit_end(void)
{
    if (bb_patch_count > 0) {
        fprintf(stderr, "bb_emit_end: %d unresolved forward reference(s):\n",
                bb_patch_count);
        for (int i = 0; i < bb_patch_count; i++)
            fprintf(stderr, "  site=%d label='%s'\n",
                    bb_patch_list[i].site,
                    bb_patch_list[i].label->name);
        abort();
    }
    return bb_emit_pos;
}

/* ── patch helpers ──────────────────────────────────────────────────────── */

void bb_emit_patch_rel8(bb_label_t *lbl)
{
    if (bb_label_defined(lbl)) {
        /* Already resolved — emit directly */
        int disp = lbl->offset - (bb_emit_pos + 1);
        if (disp < -128 || disp > 127) {
            fprintf(stderr,
                    "bb_emit_patch_rel8: rel8 overflow for '%s': disp=%d\n",
                    lbl->name, disp);
            abort();
        }
        bb_emit_byte((uint8_t)(int8_t)disp);
        return;
    }
    /* Unresolved: record patch site, emit placeholder */
    if (bb_patch_count >= BB_PATCH_MAX) {
        fprintf(stderr, "bb_emit_patch_rel8: patch list full\n");
        abort();
    }
    bb_patch_list[bb_patch_count].site  = bb_emit_pos;
    bb_patch_list[bb_patch_count].label = lbl;
    bb_patch_list[bb_patch_count].kind  = PATCH_REL8;
    bb_patch_count++;
    bb_emit_byte(0x00);   /* placeholder */
}

void bb_emit_patch_rel32(bb_label_t *lbl)
{
    if (bb_label_defined(lbl)) {
        int disp = lbl->offset - (bb_emit_pos + 4);
        bb_emit_i32(disp);
        return;
    }
    if (bb_patch_count >= BB_PATCH_MAX) {
        fprintf(stderr, "bb_emit_patch_rel32: patch list full\n");
        abort();
    }
    bb_patch_list[bb_patch_count].site  = bb_emit_pos;
    bb_patch_list[bb_patch_count].label = lbl;
    bb_patch_list[bb_patch_count].kind  = PATCH_REL32;
    bb_patch_count++;
    bb_emit_u32(0x00000000);   /* placeholder */
}

/* ── byte primitives ────────────────────────────────────────────────────── */

void bb_emit_byte(uint8_t b)
{
    if (bb_emit_pos >= bb_emit_size) {
        fprintf(stderr, "bb_emit_byte: buffer overflow at pos=%d size=%d\n",
                bb_emit_pos, bb_emit_size);
        abort();
    }
    bb_emit_buf[bb_emit_pos++] = b;
}

void bb_emit_u16(uint16_t v)
{
    bb_emit_byte((uint8_t)(v     ));
    bb_emit_byte((uint8_t)(v >> 8));
}

void bb_emit_u32(uint32_t v)
{
    bb_emit_byte((uint8_t)(v      ));
    bb_emit_byte((uint8_t)(v >>  8));
    bb_emit_byte((uint8_t)(v >> 16));
    bb_emit_byte((uint8_t)(v >> 24));
}

void bb_emit_u64(uint64_t v)
{
    bb_emit_u32((uint32_t)(v      ));
    bb_emit_u32((uint32_t)(v >> 32));
}

void bb_emit_i8(int8_t v)   { bb_emit_byte((uint8_t)v); }
void bb_emit_i32(int32_t v) { uint32_t u; memcpy(&u, &v, 4); bb_emit_u32(u); }

/* ── text mode helpers ───────────────────────────────────────────────────── */

void bb_text(const char *fmt, ...)
{
    if (bb_emit_mode != EMIT_TEXT) return;
    FILE *f = bb_emit_out ? bb_emit_out : stdout;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
}

void bb_text_label(bb_label_t *lbl)
{
    if (bb_emit_mode == EMIT_TEXT) {
        FILE *f = bb_emit_out ? bb_emit_out : stdout;
        fprintf(f, "%s:\n", lbl->name);
    } else {
        bb_label_define(lbl);
    }
}

void bb_text_comment(const char *fmt, ...)
{
    if (bb_emit_mode != EMIT_TEXT) return;
    FILE *f = bb_emit_out ? bb_emit_out : stdout;
    fprintf(f, "; ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
}

/* ── x86-64 instruction helpers ─────────────────────────────────────────── */

void bb_insn_mov_eax_imm32(uint32_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    mov     eax, %u\n", imm);
        return;
    }
    bb_emit_byte(0xB8);
    bb_emit_u32(imm);
}

void bb_insn_mov_rax_imm64(uint64_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    mov     rax, 0x%llx\n", (unsigned long long)imm);
        return;
    }
    /* REX.W + B8 /r  — mov rax, imm64 */
    bb_emit_byte(0x48);
    bb_emit_byte(0xB8);
    bb_emit_u64(imm);
}

void bb_insn_ret(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    ret\n"); return; }
    bb_emit_byte(0xC3);
}

void bb_insn_nop(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    nop\n"); return; }
    bb_emit_byte(0x90);
}

void bb_insn_call_rax(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    call    rax\n"); return; }
    bb_emit_byte(0xFF); bb_emit_byte(0xD0);
}

void bb_insn_jmp_rel8(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jmp     %s\n", target->name); return;
    }
    bb_emit_byte(0xEB);
    bb_emit_patch_rel8(target);
}

void bb_insn_jmp_rel32(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jmp     %s\n", target->name); return;
    }
    bb_emit_byte(0xE9);
    bb_emit_patch_rel32(target);
}

void bb_insn_jl_rel8(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jl      %s\n", target->name); return;
    }
    bb_emit_byte(0x7C);
    bb_emit_patch_rel8(target);
}

void bb_insn_jge_rel8(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jge     %s\n", target->name); return;
    }
    bb_emit_byte(0x7D);
    bb_emit_patch_rel8(target);
}

void bb_insn_je_rel8(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    je      %s\n", target->name); return;
    }
    bb_emit_byte(0x74);
    bb_emit_patch_rel8(target);
}

void bb_insn_jne_rel8(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jne     %s\n", target->name); return;
    }
    bb_emit_byte(0x75);
    bb_emit_patch_rel8(target);
}

void bb_insn_jne_rel32(bb_label_t *target)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    jne     %s\n", target->name); return;
    }
    bb_emit_byte(0x0F); bb_emit_byte(0x85);
    bb_emit_patch_rel32(target);
}

void bb_insn_cmp_esi_imm8(uint8_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    cmp     esi, %u\n", (unsigned)imm); return;
    }
    bb_emit_byte(0x83); bb_emit_byte(0xFE); bb_emit_byte(imm);
}

void bb_insn_cmp_esi_imm32(uint32_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    cmp     esi, %u\n", imm); return;
    }
    bb_emit_byte(0x81); bb_emit_byte(0xFE); bb_emit_u32(imm);
}

void bb_insn_movzx_eax_rdi_off8(uint8_t off)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    movzx   eax, byte [rdi + %u]\n", (unsigned)off); return;
    }
    /* 0F B6 47 imm8 */
    bb_emit_byte(0x0F); bb_emit_byte(0xB6);
    bb_emit_byte(0x47); bb_emit_byte(off);
}

void bb_insn_cmp_al_imm8(uint8_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    cmp     al, %u\n", (unsigned)imm); return;
    }
    bb_emit_byte(0x3C); bb_emit_byte(imm);
}

void bb_insn_xor_eax_eax(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    xor     eax, eax\n"); return; }
    bb_emit_byte(0x31); bb_emit_byte(0xC0);
}

void bb_insn_push_rbp(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    push    rbp\n"); return; }
    bb_emit_byte(0x55);
}

void bb_insn_pop_rbp(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    pop     rbp\n"); return; }
    bb_emit_byte(0x5D);
}

void bb_insn_mov_rbp_rsp(void)
{
    if (bb_emit_mode == EMIT_TEXT) { bb_text("    mov     rbp, rsp\n"); return; }
    /* REX.W + 89 /r  — mov rbp, rsp  (89 E5) */
    bb_emit_byte(0x48); bb_emit_byte(0x89); bb_emit_byte(0xE5);
}

void bb_insn_sub_rsp_imm8(uint8_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    sub     rsp, %u\n", (unsigned)imm); return;
    }
    bb_emit_byte(0x48); bb_emit_byte(0x83); bb_emit_byte(0xEC);
    bb_emit_byte(imm);
}

void bb_insn_add_rsp_imm8(uint8_t imm)
{
    if (bb_emit_mode == EMIT_TEXT) {
        bb_text("    add     rsp, %u\n", (unsigned)imm); return;
    }
    bb_emit_byte(0x48); bb_emit_byte(0x83); bb_emit_byte(0xC4);
    bb_emit_byte(imm);
}
