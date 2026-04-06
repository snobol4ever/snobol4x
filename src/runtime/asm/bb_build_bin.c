/*
 * bb_build_bin.c — Binary x86-64 Byrd Box Emitter (M-DYN-B1)
 *
 * Emits Byrd boxes as raw x86-64 bytes into bb_pool pages.
 * Each function mirrors the corresponding .s file byte-for-byte.
 *
 * Globals Σ/Δ/Ω are accessed via absolute imm64 addresses baked into
 * the emitted code — the pool slab can sit anywhere in the address space.
 * memcmp is called the same way: mov rax, imm64(&memcmp) / call rax.
 *
 * lit/len are baked as imm64/imm32 constants (no zeta pointer needed at
 * runtime — the box is closed over its arguments at emit time).
 *
 * Usage:
 *   bb_box_fn fn = bb_lit_emit_binary("hello", 5);
 *   if (fn) { root.fn = fn; root.ζ = NULL; }
 *
 * ABI: spec_t fn(void *zeta_ignored, int entry)
 *   rdi = zeta (ignored — lit/len baked in)
 *   esi = entry (0=α, 1=β)
 *   returns: rax=σ (ptr), rdx=δ (len)  — success
 *            rax=0, rdx=0              — failure (ω)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Sprint:  RT-116 / M-DYN-B1
 */

#include "bb_pool.h"
#include "bb_emit.h"
#include "../boxes/shared/bb_box.h"   /* spec_t, bb_box_fn, Σ/Δ/Ω externs */

/* M-DYN-B2: PATND_t needs DESCR_t first — pull in snobol4.h which
 * provides both.  bb_build_bin.c is compiled as part of the full runtime
 * (not standalone), so snobol4.h is available via the -I flags in the
 * Makefile.  We include it here only for the type definitions; no GC. */
#include "../snobol4/snobol4.h"
/* patnd.h is already included transitively by snobol4.h — don't include again */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── helpers ────────────────────────────────────────────────────────────── */

/* emit:  mov rax, imm64(&global) / mov eax, [rax]
 * Loads a 32-bit global (int) addressed by its absolute pointer.
 * Result in eax (zero-extended into rax). */
static void emit_load_int_global(const int *addr)
{
    /* mov rax, imm64 */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)addr);
    /* mov eax, [rax] */
    bb_emit_byte(0x8B); bb_emit_byte(0x00);
}

/* emit:  mov rax, imm64(&global_ptr) / mov rax, [rax]
 * Loads a 64-bit pointer global (const char *).
 * Result in rax. */
static void emit_load_ptr_global(const char **addr)
{
    /* mov rax, imm64 */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)addr);
    /* mov rax, [rax]  — 48 8B 00 */
    bb_emit_byte(0x48); bb_emit_byte(0x8B); bb_emit_byte(0x00);
}

/* emit:  mov rax, imm64(&global) / add eax, imm32(val) / mov [rax], eax
 * Adds val to a 32-bit global. Clobbers rax, ecx. */
static void emit_add_int_global(const int *addr, int32_t val)
{
    /* mov rcx, imm64(&global) */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)addr);
    /* mov eax, [rcx] */
    bb_emit_byte(0x8B); bb_emit_byte(0x01);
    /* add eax, imm32 */
    bb_emit_byte(0x05); bb_emit_u32((uint32_t)val);
    /* mov [rcx], eax */
    bb_emit_byte(0x89); bb_emit_byte(0x01);
}

/* emit:  mov rax, imm64(&global) / sub eax, imm32(val) / mov [rax], eax */
static void emit_sub_int_global(const int *addr, int32_t val)
{
    /* mov rcx, imm64(&global) */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)addr);
    /* mov eax, [rcx] */
    bb_emit_byte(0x8B); bb_emit_byte(0x01);
    /* sub eax, imm32 */
    bb_emit_byte(0x2D); bb_emit_u32((uint32_t)val);
    /* mov [rcx], eax */
    bb_emit_byte(0x89); bb_emit_byte(0x01);
}

/* ── bb_lit_emit_binary ─────────────────────────────────────────────────── */
/*
 * Emits the LIT box as x86-64 binary, mirroring bb_lit.s exactly.
 *
 * bb_lit.s structure (annotated):
 *
 *   bb_lit(rdi=ζ, esi=entry):
 *     push rbx; push r12             ; callee-saves
 *     sub  rsp, 16                   ; stack slot for spec_t (σ@0, δ@8)
 *     cmp  esi, 0
 *     je   LIT_α
 *     jmp  LIT_β
 *
 *   LIT_α:
 *     Δ + len > Ω  →  LIT_ω          ; bounds check
 *     memcmp(Σ+Δ, lit, len) ≠ 0 → LIT_ω
 *     [rsp+0] = Σ+Δ                  ; σ
 *     [rsp+8] = len                  ; δ
 *     Δ += len
 *     jmp LIT_γ
 *
 *   LIT_β:
 *     Δ -= len
 *     jmp LIT_ω
 *
 *   LIT_γ:
 *     rax = [rsp+0]; rdx = [rsp+8]  ; return spec(σ, δ)
 *     add rsp,16; pop r12; pop rbx; ret
 *
 *   LIT_ω:
 *     xor eax,eax; xor edx,edx      ; return spec_empty
 *     add rsp,16; pop r12; pop rbx; ret
 *
 * Binary adaptation:
 *   - ζ->lit / ζ->len replaced by baked imm64/imm32
 *   - Σ/Δ/Ω accessed via absolute imm64 pointer loads
 *   - memcmp called via mov rax,imm64(&memcmp) / call rax
 *   - push rbx/r12 replaced by push rbx/push r12 (same bytes, kept for ABI)
 *
 * Returns NULL on allocation failure.
 */

/* forward declaration of memcmp for address capture */
extern int memcmp(const void *, const void *, size_t);

/* ── forward declarations ───────────────────────────────────────────────── */
static bb_box_fn bb_build_binary_node(PATND_t *p);  /* M-DYN-B2 walk */

bb_box_fn bb_lit_emit_binary(const char *lit, int len)
{
#define BUF_SIZE 768

    bb_buf_t buf = bb_alloc(BUF_SIZE);
    if (!buf) return NULL;

    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, BUF_SIZE);

    /* ── labels ──────────────────────────────────────────────────────── */
    bb_label_t lbl_α, lbl_β, lbl_γ, lbl_ω;
    bb_label_init(&lbl_α, "LIT_α");
    bb_label_init(&lbl_β, "LIT_β");
    bb_label_init(&lbl_γ, "LIT_γ");
    bb_label_init(&lbl_ω, "LIT_ω");

    /* ── prologue ─────────────────────────────────────────────────────
     *   push rbx          53
     *   push r12          41 54
     *   sub  rsp, 16      48 83 EC 10   (stack slot for spec_t)
     *   cmp  esi, 0       83 FE 00
     *   je   LIT_α        74 xx
     *   jmp  LIT_β        EB xx
     * ─────────────────────────────────────────────────────────────── */
    bb_emit_byte(0x53);                         /* push rbx */
    bb_emit_byte(0x41); bb_emit_byte(0x54);     /* push r12 */
    bb_insn_sub_rsp_imm8(16);                   /* sub rsp, 16 */
    bb_insn_cmp_esi_imm8(0);                    /* cmp esi, 0 */
    bb_insn_je_rel8(&lbl_α);                    /* je LIT_α  (α is always nearby — backward) */
    bb_insn_jmp_rel32(&lbl_β);                  /* jmp LIT_β (β is far — use rel32) */

    /* ── LIT_α: bounds check + memcmp ────────────────────────────────
     *   eax = Δ
     *   eax += len
     *   cmp eax, Ω
     *   jg  LIT_ω
     *   call memcmp(Σ+Δ, lit, len)
     *   test eax, eax
     *   jne LIT_ω
     *   [rsp+0] = Σ+Δ  (rax = Σ, movsxd rcx,Δ, lea rax,[rax+rcx])
     *   [rsp+8] = len
     *   Δ += len
     *   jmp LIT_γ
     * ─────────────────────────────────────────────────────────────── */
    bb_label_define(&lbl_α);

    /* eax = Δ */
    emit_load_int_global(&Δ);           /* mov rax,&Δ; mov eax,[rax] */
    /* eax += len */
    bb_emit_byte(0x05); bb_emit_u32((uint32_t)len);  /* add eax, imm32(len) */
    /* cmp eax, Ω  →  mov rcx,&Ω; cmp eax,[rcx] */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Ω);  /* mov rcx, imm64(&Ω) */
    bb_emit_byte(0x3B); bb_emit_byte(0x01); /* cmp eax, [rcx] */
    /* jg LIT_ω  (rel32 — α body is ~160 bytes, beyond rel8 range) */
    bb_emit_byte(0x0F); bb_emit_byte(0x8F); bb_emit_patch_rel32(&lbl_ω);

    /* memcmp(Σ+Δ, lit, len):
     *   rdi = Σ+Δ
     *   rsi = lit (imm64)
     *   rdx = len (imm32)
     *   call [imm64(&memcmp)]
     */

    /* compute Σ+Δ into rdi */
    emit_load_ptr_global(&Σ);           /* rax = Σ */
    /* movsxd rcx, [&Δ] */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);   /* mov rcx, imm64(&Δ) */
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x09); /* movsxd rcx,[rcx] */
    /* lea rdi, [rax+rcx] */
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x3C); bb_emit_byte(0x08);

    /* rsi = lit (imm64) */
    bb_emit_byte(0x48); bb_emit_byte(0xBE);
    bb_emit_u64((uint64_t)(uintptr_t)lit);

    /* rdx = len (imm32, zero-extended) */
    bb_emit_byte(0x48); bb_emit_byte(0xBA);
    bb_emit_u64((uint64_t)(uint32_t)len);

    /* mov rax, imm64(&memcmp); call rax */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)memcmp);
    bb_insn_call_rax();

    /* test eax, eax */
    bb_emit_byte(0x85); bb_emit_byte(0xC0);
    /* jne LIT_ω */
    bb_insn_jne_rel32(&lbl_ω);      /* jne LIT_ω (rel32) */

    /* σ = Σ+Δ  →  store at [rsp+0] */
    emit_load_ptr_global(&Σ);           /* rax = Σ */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x09); /* movsxd rcx,[rcx] */
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x04); bb_emit_byte(0x08); /* lea rax,[rax+rcx] */
    /* mov [rsp+0], rax */
    bb_emit_byte(0x48); bb_emit_byte(0x89); bb_emit_byte(0x04); bb_emit_byte(0x24);

    /* δ = len  →  store at [rsp+8] */
    /* mov dword [rsp+8], imm32(len) */
    bb_emit_byte(0xC7); bb_emit_byte(0x44); bb_emit_byte(0x24); bb_emit_byte(0x08);
    bb_emit_u32((uint32_t)len);

    /* Δ += len */
    emit_add_int_global(&Δ, len);

    /* jmp LIT_γ (rel32) */
    bb_insn_jmp_rel32(&lbl_γ);

    /* ── LIT_β: Δ -= len; jmp LIT_ω ──────────────────────────────── */
    bb_label_define(&lbl_β);
    emit_sub_int_global(&Δ, len);
    /* jmp LIT_ω (rel32) */
    bb_insn_jmp_rel32(&lbl_ω);

    /* ── LIT_γ: return spec(σ, δ) ─────────────────────────────────── */
    bb_label_define(&lbl_γ);
    /* mov rax, [rsp+0]  — σ */
    bb_emit_byte(0x48); bb_emit_byte(0x8B); bb_emit_byte(0x04); bb_emit_byte(0x24);
    /* movsxd rdx, [rsp+8]  — δ */
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x54); bb_emit_byte(0x24);
    bb_emit_byte(0x08);
    /* epilogue */
    bb_insn_add_rsp_imm8(16);
    bb_emit_byte(0x41); bb_emit_byte(0x5C);  /* pop r12 */
    bb_emit_byte(0x5B);                       /* pop rbx */
    bb_insn_ret();

    /* ── LIT_ω: return spec_empty ─────────────────────────────────── */
    bb_label_define(&lbl_ω);
    bb_insn_xor_eax_eax();
    /* xor edx, edx */
    bb_emit_byte(0x31); bb_emit_byte(0xD2);
    /* epilogue */
    bb_insn_add_rsp_imm8(16);
    bb_emit_byte(0x41); bb_emit_byte(0x5C);  /* pop r12 */
    bb_emit_byte(0x5B);                       /* pop rbx */
    bb_insn_ret();

    /* ── seal ─────────────────────────────────────────────────────── */
    int nbytes = bb_emit_end();
    if (nbytes <= 0 || nbytes > BUF_SIZE) {
        bb_free(buf, BUF_SIZE);
        return NULL;
    }
    bb_seal(buf, (size_t)nbytes);
    return (bb_box_fn)buf;

#undef BUF_SIZE
}

/* ── bb_eps_emit_binary ─────────────────────────────────────────────────── */
/*
 * Emits the EPS box as x86-64 binary.  EPS has a `done` flag in its zeta,
 * but since in the binary path we bake all args at emit time and EPS needs
 * no per-instance state (binary path: stateless single-shot), we emit the
 * simplest correct version:
 *
 *   EPS α: always succeeds — return spec(Σ+Δ, 0)
 *   EPS β: always fails    — return spec_empty
 *
 * This is correct for the binary path because bb_build_binary is called
 * fresh per statement execution (no cross-statement state).  The done flag
 * in the C path guards against double-γ on backtrack, but our binary boxes
 * are rebuilt each time, so this is safe.
 *
 * ABI: spec_t fn(void *zeta_ignored, int entry)
 *   rdi = zeta (ignored)
 *   esi = entry (0=α → succeed, 1=β → fail)
 *
 * Layout:
 *   prologue: push rbx; push r12; sub rsp,16
 *   cmp esi,0; je EPS_α; jmp EPS_β
 *   EPS_α: rax = Σ+Δ; [rsp+0]=rax; [rsp+8]=0; jmp EPS_γ
 *   EPS_β: jmp EPS_ω
 *   EPS_γ: rax=[rsp+0]; rdx=[rsp+8]; epilogue; ret
 *   EPS_ω: xor eax,eax; xor edx,edx; epilogue; ret
 */
bb_box_fn bb_eps_emit_binary(void)
{
#define EPS_BUF_SIZE 256

    bb_buf_t buf = bb_alloc(EPS_BUF_SIZE);
    if (!buf) return NULL;

    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, EPS_BUF_SIZE);

    bb_label_t lbl_α, lbl_β, lbl_γ, lbl_ω;
    bb_label_init(&lbl_α, "EPS_α");
    bb_label_init(&lbl_β, "EPS_β");
    bb_label_init(&lbl_γ, "EPS_γ");
    bb_label_init(&lbl_ω, "EPS_ω");

    /* prologue */
    bb_emit_byte(0x53);                         /* push rbx */
    bb_emit_byte(0x41); bb_emit_byte(0x54);     /* push r12 */
    bb_insn_sub_rsp_imm8(16);                   /* sub rsp, 16 */
    bb_insn_cmp_esi_imm8(0);                    /* cmp esi, 0 */
    bb_insn_je_rel8(&lbl_α);                    /* je EPS_α */
    bb_insn_jmp_rel32(&lbl_β);                  /* jmp EPS_β */

    /* EPS_α: σ = Σ+Δ, δ = 0 */
    bb_label_define(&lbl_α);

    /* rax = Σ (load ptr global) */
    emit_load_ptr_global(&Σ);                   /* rax = Σ */
    /* movsxd rcx, [&Δ] */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);       /* mov rcx, imm64(&Δ) */
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x09); /* movsxd rcx,[rcx] */
    /* lea rax, [rax+rcx]  →  Σ+Δ */
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x04); bb_emit_byte(0x08);
    /* mov [rsp+0], rax  — σ */
    bb_emit_byte(0x48); bb_emit_byte(0x89); bb_emit_byte(0x04); bb_emit_byte(0x24);
    /* mov qword [rsp+8], 0  — δ = 0 */
    bb_emit_byte(0x48); bb_emit_byte(0xC7); bb_emit_byte(0x44); bb_emit_byte(0x24);
    bb_emit_byte(0x08); bb_emit_u32(0);
    /* jmp EPS_γ */
    bb_insn_jmp_rel32(&lbl_γ);

    /* EPS_β: → ω */
    bb_label_define(&lbl_β);
    bb_insn_jmp_rel32(&lbl_ω);

    /* EPS_γ: return spec(σ, δ) */
    bb_label_define(&lbl_γ);
    bb_emit_byte(0x48); bb_emit_byte(0x8B); bb_emit_byte(0x04); bb_emit_byte(0x24); /* mov rax,[rsp+0] */
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x54); bb_emit_byte(0x24);
    bb_emit_byte(0x08);                          /* movsxd rdx,[rsp+8] */
    bb_insn_add_rsp_imm8(16);
    bb_emit_byte(0x41); bb_emit_byte(0x5C);      /* pop r12 */
    bb_emit_byte(0x5B);                          /* pop rbx */
    bb_insn_ret();

    /* EPS_ω: return spec_empty */
    bb_label_define(&lbl_ω);
    bb_insn_xor_eax_eax();
    bb_emit_byte(0x31); bb_emit_byte(0xD2);      /* xor edx, edx */
    bb_insn_add_rsp_imm8(16);
    bb_emit_byte(0x41); bb_emit_byte(0x5C);      /* pop r12 */
    bb_emit_byte(0x5B);                          /* pop rbx */
    bb_insn_ret();

    int nbytes = bb_emit_end();
    if (nbytes <= 0 || nbytes > EPS_BUF_SIZE) {
        bb_free(buf, EPS_BUF_SIZE);
        return NULL;
    }
    bb_seal(buf, (size_t)nbytes);
    return (bb_box_fn)buf;

#undef EPS_BUF_SIZE
}

/* ── bb_build_binary — M-DYN-B2 ────────────────────────────────────────── */
/*
 * Walk a PATND_t tree and emit binary x86-64 boxes where possible.
 * Currently covers XCHR (literal) and XEPS — all other node types
 * return NULL (caller falls back to C bb_build path).
 *
 * Returns a callable bb_box_fn, or NULL if this node (or any child)
 * cannot be emitted as binary yet.  NULL → caller uses C path for whole tree.
 */
#include "../snobol4/patnd.h"

static bb_box_fn bb_build_binary_node(PATND_t *p)
{
    if (!p) {
        /* null → epsilon */
        return bb_eps_emit_binary();
    }
    switch (p->kind) {
    case XCHR: {
        const char *lit = p->STRVAL_fn ? p->STRVAL_fn : "";
        return bb_lit_emit_binary(lit, (int)strlen(lit));
    }
    case XEPS:
        return bb_eps_emit_binary();
    default:
        /* Not yet implemented in binary path — signal fallback to C bb_build */
        return NULL;
    }
}

/*
 * Public entry point: attempt full binary build of a PATND_t tree.
 * Returns NULL if any node in the tree requires the C path.
 * On NULL, caller should use the existing C bb_build() instead.
 */
bb_box_fn bb_build_binary(PATND_t *p)
{
    return bb_build_binary_node(p);
}
