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

/* forward declarations */
extern spec_t bb_seq(void *zeta, int entry);
extern spec_t bb_tab(void *zeta, int entry);
extern spec_t bb_rtab(void *zeta, int entry);

/* M-DYN-B7: capture box — bb_capture is static in stmt_exec.c;
 * exposed via bb_capture_exported() thin wrapper + bb_capture_new() ctor. */
extern spec_t bb_capture_exported(void *zeta, int entry);

/* Mirror of capture_t from stmt_exec.c — must stay in sync. */
typedef struct {
    bb_box_fn    fn;
    void        *state;
    const char  *varname;
    void        *var_ptr;
    int          immediate;
    spec_t       pending;
    int          has_pending;
    int          registered;
} capture_t_bin;
extern capture_t_bin *bb_capture_new(bb_box_fn child_fn, void *child_state,
                                     const char *varname, void *var_ptr, int immediate);

static bb_box_fn bb_build_binary_node(PATND_t *p);

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

/* ── M-DYN-B3: bb_pos_emit_binary ──────────────────────────────────────── */
/*
 * Emits POS(n) as x86-64 binary.  n is baked as imm32.
 * Mirrors bb_pos.s exactly:
 *   POS_α: eax=Δ; cmp eax,n; jne → ω; rax=Σ+Δ; rdx=0; → γ
 *   POS_β: → ω
 *   POS_γ: ret
 *   POS_ω: xor eax,eax; xor edx,edx; ret
 * No ζ needed — n baked into imm32.
 */
bb_box_fn bb_pos_emit_binary(int n)
{
#define POS_BUF_SIZE 256
    bb_buf_t buf = bb_alloc(POS_BUF_SIZE);
    if (!buf) return NULL;

    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, POS_BUF_SIZE);

    bb_label_t lbl_α, lbl_β, lbl_γ, lbl_ω;
    bb_label_init(&lbl_α, "POS_α");
    bb_label_init(&lbl_β, "POS_β");
    bb_label_init(&lbl_γ, "POS_γ");
    bb_label_init(&lbl_ω, "POS_ω");

    /* prologue: push rbx; cmp esi,0; je POS_α; jmp POS_β */
    bb_emit_byte(0x53);                         /* push rbx */
    bb_insn_cmp_esi_imm8(0);
    bb_insn_je_rel8(&lbl_α);
    bb_insn_jmp_rel32(&lbl_β);

    /* POS_α: eax = Δ; cmp eax, n; jne POS_ω */
    bb_label_define(&lbl_α);
    emit_load_int_global(&Δ);               /* eax = Δ */
    bb_emit_byte(0x3D); bb_emit_u32((uint32_t)n);  /* cmp eax, imm32(n) */
    bb_insn_jne_rel32(&lbl_ω);

    /* rax = Σ+Δ; rdx = 0 */
    emit_load_ptr_global(&Σ);               /* rax = Σ */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);  /* mov rcx, imm64(&Δ) */
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x09); /* movsxd rcx,[rcx] */
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x04); bb_emit_byte(0x08); /* lea rax,[rax+rcx] */
    bb_emit_byte(0x31); bb_emit_byte(0xD2); /* xor edx, edx */
    bb_insn_jmp_rel32(&lbl_γ);

    /* POS_β: → ω */
    bb_label_define(&lbl_β);
    bb_insn_jmp_rel32(&lbl_ω);

    /* POS_γ: pop rbx; ret */
    bb_label_define(&lbl_γ);
    bb_emit_byte(0x5B);
    bb_insn_ret();

    /* POS_ω: xor eax,eax; xor edx,edx; pop rbx; ret */
    bb_label_define(&lbl_ω);
    bb_insn_xor_eax_eax();
    bb_emit_byte(0x31); bb_emit_byte(0xD2);
    bb_emit_byte(0x5B);
    bb_insn_ret();

    int nbytes = bb_emit_end();
    if (nbytes <= 0 || nbytes > POS_BUF_SIZE) { bb_free(buf, POS_BUF_SIZE); return NULL; }
    bb_seal(buf, (size_t)nbytes);
    return (bb_box_fn)buf;
#undef POS_BUF_SIZE
}

/* ── M-DYN-B3: bb_rpos_emit_binary ─────────────────────────────────────── */
/*
 * Emits RPOS(n) as x86-64 binary.  n baked as imm32.
 * Mirrors bb_rpos.s:
 *   RPOS_α: eax=Ω-n; cmp Δ,eax; jne → ω; rax=Σ+Δ; rdx=0; → γ
 *   RPOS_β: → ω
 */
bb_box_fn bb_rpos_emit_binary(int n)
{
#define RPOS_BUF_SIZE 256
    bb_buf_t buf = bb_alloc(RPOS_BUF_SIZE);
    if (!buf) return NULL;

    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, RPOS_BUF_SIZE);

    bb_label_t lbl_α, lbl_β, lbl_γ, lbl_ω;
    bb_label_init(&lbl_α, "RPOS_α");
    bb_label_init(&lbl_β, "RPOS_β");
    bb_label_init(&lbl_γ, "RPOS_γ");
    bb_label_init(&lbl_ω, "RPOS_ω");

    bb_emit_byte(0x53);
    bb_insn_cmp_esi_imm8(0);
    bb_insn_je_rel8(&lbl_α);
    bb_insn_jmp_rel32(&lbl_β);

    /* RPOS_α: eax = Ω; eax -= n; cmp Δ, eax; jne → ω */
    bb_label_define(&lbl_α);
    emit_load_int_global(&Ω);               /* eax = Ω */
    bb_emit_byte(0x2D); bb_emit_u32((uint32_t)n);  /* sub eax, imm32(n)  → eax = Ω-n */
    /* cmp [&Δ], eax  — load Δ into ecx, compare */
    bb_emit_byte(0x89); bb_emit_byte(0xC1);  /* mov ecx, eax  (save Ω-n) */
    emit_load_int_global(&Δ);               /* eax = Δ */
    bb_emit_byte(0x39); bb_emit_byte(0xC8);  /* cmp eax, ecx  (Δ == Ω-n?) */
    bb_insn_jne_rel32(&lbl_ω);

    /* rax = Σ+Δ; rdx = 0 */
    emit_load_ptr_global(&Σ);
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);
    bb_emit_byte(0x48); bb_emit_byte(0x63); bb_emit_byte(0x09);
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x04); bb_emit_byte(0x08);
    bb_emit_byte(0x31); bb_emit_byte(0xD2);
    bb_insn_jmp_rel32(&lbl_γ);

    bb_label_define(&lbl_β);
    bb_insn_jmp_rel32(&lbl_ω);

    bb_label_define(&lbl_γ);
    bb_emit_byte(0x5B);
    bb_insn_ret();

    bb_label_define(&lbl_ω);
    bb_insn_xor_eax_eax();
    bb_emit_byte(0x31); bb_emit_byte(0xD2);
    bb_emit_byte(0x5B);
    bb_insn_ret();

    int nbytes = bb_emit_end();
    if (nbytes <= 0 || nbytes > RPOS_BUF_SIZE) { bb_free(buf, RPOS_BUF_SIZE); return NULL; }
    bb_seal(buf, (size_t)nbytes);
    return (bb_box_fn)buf;
#undef RPOS_BUF_SIZE
}

/* ── seq_t / bchild_t — mirror of stmt_exec.c definitions ─────────────── */
/* These must match the layouts used by bb_seq.s and bb_seq.c exactly.
 * Kept local to bb_build_bin.c to avoid cross-file dependency. */
typedef struct { bb_box_fn fn; void *state; } bin_bchild_t;
typedef struct {
    bin_bchild_t left;       /* @0: fn@0, state@8  */
    bin_bchild_t right;      /* @16: fn@16, state@24 */
    spec_t       matched;    /* @32: σ@32, δ@40    */
} bin_seq_t;

/* Forward declaration for mutual recursion in XCAT */
extern spec_t bb_seq(void *zeta, int entry);
extern spec_t bb_len(void *zeta, int entry);

/*
 * bb_nme_emit_binary(PATND_t *p) — M-DYN-B7
 * bb_fnme_emit_binary(PATND_t *p) — M-DYN-B7
 *
 * XNME (pat . var) and XFNME (pat $ var) wrap a child pattern in a
 * capture_t. The child is built recursively; if it can't go binary the
 * whole node falls back.
 *
 * Strategy: same trampoline as TAB/LEN —
 *   alloc heap capture_t_bin via bb_capture_new(),
 *   emit 22-byte trampoline: mov rdi,imm64(z); mov rax,imm64(bb_capture_exported); jmp rax
 */
static bb_box_fn bb_nme_emit_binary(PATND_t *p)
{
    /* Recursively build child */
    bb_box_fn child_fn = (p->nchildren > 0)
                         ? bb_build_binary_node(p->children[0])
                         : bb_build_binary_node(NULL);  /* eps */
    if (!child_fn) return NULL;

    const char *varname = (p->var.v == DT_S && p->var.s) ? p->var.s : NULL;
    void       *var_ptr = (p->var.v == DT_N && p->var.ptr)
                          ? (void *)p->var.ptr : NULL;

    capture_t_bin *z = bb_capture_new(child_fn, NULL, varname, var_ptr, 0 /*immediate=0*/);
    if (!z) return NULL;

#define NME_TRAM_SIZE 32
    bb_buf_t tbuf = bb_alloc(NME_TRAM_SIZE);
    if (!tbuf) { free(z); return NULL; }
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(tbuf, NME_TRAM_SIZE);

    /* mov rdi, imm64(z) */
    bb_emit_byte(0x48); bb_emit_byte(0xBF);
    bb_emit_u64((uint64_t)(uintptr_t)z);
    /* mov rax, imm64(bb_capture_exported) */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)bb_capture_exported);
    /* jmp rax */
    bb_emit_byte(0xFF); bb_emit_byte(0xE0);

    int nb = bb_emit_end();
    if (nb <= 0 || nb > NME_TRAM_SIZE) { bb_free(tbuf, NME_TRAM_SIZE); free(z); return NULL; }
    bb_seal(tbuf, (size_t)nb);
    return (bb_box_fn)tbuf;
#undef NME_TRAM_SIZE
}

static bb_box_fn bb_fnme_emit_binary(PATND_t *p)
{
    bb_box_fn child_fn = (p->nchildren > 0)
                         ? bb_build_binary_node(p->children[0])
                         : bb_build_binary_node(NULL);
    if (!child_fn) return NULL;

    const char *varname = (p->var.v == DT_S && p->var.s) ? p->var.s : NULL;
    void       *var_ptr = (p->var.v == DT_N && p->var.ptr)
                          ? (void *)p->var.ptr : NULL;

    capture_t_bin *z = bb_capture_new(child_fn, NULL, varname, var_ptr, 1 /*immediate=1*/);
    if (!z) return NULL;

#define FNME_TRAM_SIZE 32
    bb_buf_t tbuf = bb_alloc(FNME_TRAM_SIZE);
    if (!tbuf) { free(z); return NULL; }
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(tbuf, FNME_TRAM_SIZE);

    bb_emit_byte(0x48); bb_emit_byte(0xBF);
    bb_emit_u64((uint64_t)(uintptr_t)z);
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)bb_capture_exported);
    bb_emit_byte(0xFF); bb_emit_byte(0xE0);

    int nb = bb_emit_end();
    if (nb <= 0 || nb > FNME_TRAM_SIZE) { bb_free(tbuf, FNME_TRAM_SIZE); free(z); return NULL; }
    bb_seal(tbuf, (size_t)nb);
    return (bb_box_fn)tbuf;
#undef FNME_TRAM_SIZE
}

/*
 * bb_len_emit_binary(int n) — M-DYN-B5
 *
 * LEN(n) needs a runtime-mutable `bspan` field (UTF-8 byte span of last
 * match, written in α, read back in β for backtrack). Trampoline strategy
 * identical to TAB/RTAB (M-DYN-B4):
 *   calloc a heap len_t, set ->n = n (->bspan = 0 from calloc).
 *   Emit 22-byte trampoline:
 *     mov rdi, imm64(z)       ; bake zeta ptr
 *     mov rax, imm64(bb_len)  ; bake fn ptr
 *     jmp rax                 ; tail call — esi/entry unchanged
 * bb_len C logic handles all UTF-8 accounting at match time via the heap ζ.
 */
static bb_box_fn bb_len_emit_binary(int n)
{
#define LEN_TRAM_SIZE 32
    len_t *z = calloc(1, sizeof(len_t));
    if (!z) return NULL;
    z->n = n;

    bb_buf_t tbuf = bb_alloc(LEN_TRAM_SIZE);
    if (!tbuf) { free(z); return NULL; }
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(tbuf, LEN_TRAM_SIZE);

    /* mov rdi, imm64(z) */
    bb_emit_byte(0x48); bb_emit_byte(0xBF);
    bb_emit_u64((uint64_t)(uintptr_t)z);
    /* mov rax, imm64(bb_len) */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)bb_len);
    /* jmp rax */
    bb_emit_byte(0xFF); bb_emit_byte(0xE0);

    int nb = bb_emit_end();
    if (nb <= 0 || nb > LEN_TRAM_SIZE) { bb_free(tbuf, LEN_TRAM_SIZE); free(z); return NULL; }
    bb_seal(tbuf, (size_t)nb);
    return (bb_box_fn)tbuf;
#undef LEN_TRAM_SIZE
}

/*
 * bb_tab_emit_binary(int n) — M-DYN-B4
 *
 * TAB(n) needs a runtime-mutable `advance` field written into ζ during α,
 * then read back during β. We use the same trampoline strategy as XCAT:
 *   1. calloc a heap tab_t, set ->n = n (->advance starts 0 from calloc).
 *   2. Emit a 22-byte trampoline into bb_pool:
 *        mov rdi, imm64(z)       ; bake ζ ptr
 *        mov rax, imm64(bb_tab)  ; bake fn ptr
 *        jmp rax                 ; tail call — esi/entry unchanged
 * The trampoline is a self-contained bb_box_fn; caller's ζ is ignored.
 * The heap tab_t persists for the lifetime of the pattern.
 */
static bb_box_fn bb_tab_emit_binary(int n)
{
#define TAB_TRAM_SIZE 32
    tab_t *z = calloc(1, sizeof(tab_t));
    if (!z) return NULL;
    z->n = n;

    bb_buf_t tbuf = bb_alloc(TAB_TRAM_SIZE);
    if (!tbuf) { free(z); return NULL; }
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(tbuf, TAB_TRAM_SIZE);

    /* mov rdi, imm64(z) */
    bb_emit_byte(0x48); bb_emit_byte(0xBF);
    bb_emit_u64((uint64_t)(uintptr_t)z);
    /* mov rax, imm64(bb_tab) */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)bb_tab);
    /* jmp rax */
    bb_emit_byte(0xFF); bb_emit_byte(0xE0);

    int nb = bb_emit_end();
    if (nb <= 0 || nb > TAB_TRAM_SIZE) { bb_free(tbuf, TAB_TRAM_SIZE); free(z); return NULL; }
    bb_seal(tbuf, (size_t)nb);
    return (bb_box_fn)tbuf;
#undef TAB_TRAM_SIZE
}

/*
 * bb_rtab_emit_binary(int n) — M-DYN-B4
 * Same trampoline strategy as bb_tab_emit_binary, delegating to bb_rtab.
 */
static bb_box_fn bb_rtab_emit_binary(int n)
{
#define RTAB_TRAM_SIZE 32
    rtab_t *z = calloc(1, sizeof(rtab_t));
    if (!z) return NULL;
    z->n = n;

    bb_buf_t tbuf = bb_alloc(RTAB_TRAM_SIZE);
    if (!tbuf) { free(z); return NULL; }
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(tbuf, RTAB_TRAM_SIZE);

    /* mov rdi, imm64(z) */
    bb_emit_byte(0x48); bb_emit_byte(0xBF);
    bb_emit_u64((uint64_t)(uintptr_t)z);
    /* mov rax, imm64(bb_rtab) */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)bb_rtab);
    /* jmp rax */
    bb_emit_byte(0xFF); bb_emit_byte(0xE0);

    int nb = bb_emit_end();
    if (nb <= 0 || nb > RTAB_TRAM_SIZE) { bb_free(tbuf, RTAB_TRAM_SIZE); free(z); return NULL; }
    bb_seal(tbuf, (size_t)nb);
    return (bb_box_fn)tbuf;
#undef RTAB_TRAM_SIZE
}

static bb_box_fn bb_build_binary_node(PATND_t *p)
{
    if (!p) {
        /* null → epsilon */
        return bb_eps_emit_binary();
    }
    switch (p->kind) {

    /* ── M-DYN-B1: literal string ─────────────────────────────────── */
    case XCHR: {
        const char *lit = p->STRVAL_fn ? p->STRVAL_fn : "";
        return bb_lit_emit_binary(lit, (int)strlen(lit));
    }

    /* ── M-DYN-B2: epsilon ─────────────────────────────────────────── */
    case XEPS:
        return bb_eps_emit_binary();

    /* ── M-DYN-B3: POS(n) ─────────────────────────────────────────── */
    case XPOSI:
        return bb_pos_emit_binary((int)p->num);

    /* ── M-DYN-B3: RPOS(n) ────────────────────────────────────────── */
    case XRPSI:
        return bb_rpos_emit_binary((int)p->num);

    /* ── M-DYN-B4: TAB(n) — trampoline to heap tab_t + bb_tab ────── */
    case XTB:
        return bb_tab_emit_binary((int)p->num);

    /* ── M-DYN-B4: RTAB(n) — trampoline to heap rtab_t + bb_rtab ── */
    case XRTB:
        return bb_rtab_emit_binary((int)p->num);

    /* ── M-DYN-B5: LEN(n) — trampoline to heap len_t + bb_len ─────── */
    case XLNTH:
        return bb_len_emit_binary((int)p->num);

    /* ── M-DYN-B7: XNME — pat . var  conditional capture ─────────── */
    case XNME:
        return bb_nme_emit_binary(p);

    /* ── M-DYN-B7: XFNME — pat $ var  immediate capture ──────────── */
    case XFNME:
        return bb_fnme_emit_binary(p);

    /* ── M-DYN-B3: XCAT — recursive hybrid seq ──────────────────────
     * Build left and right children as binary; wire into heap seq_t;
     * emit a tiny trampoline that bakes (bb_seq, ζ) as imm64 constants
     * and tail-calls bb_seq(ζ, entry).  The trampoline is a self-contained
     * bb_box_fn: caller passes any ζ (ignored), trampoline uses baked ζ.
     *
     * Trampoline layout (~22 bytes):
     *   mov  rdi, imm64(seq_zeta)  ; baked ζ ptr
     *   mov  rax, imm64(bb_seq)    ; baked fn ptr
     *   jmp  rax                   ; tail call — esi/entry unchanged
     *
     * If any child returns NULL → fall back to C path for whole XCAT.
     * ──────────────────────────────────────────────────────────────── */
    case XCAT: {
        if (p->nchildren == 0)
            return bb_eps_emit_binary();
        if (p->nchildren == 1)
            return bb_build_binary_node(p->children[0]);

        /* Fold right: seq(children[0], seq(children[1], ...)) */
        bb_box_fn  right_fn    = bb_build_binary_node(p->children[p->nchildren - 1]);
        if (!right_fn) return NULL;
        void      *right_state = NULL;   /* binary leaves carry no ζ */

        for (int i = p->nchildren - 2; i >= 0; i--) {
            bb_box_fn left_fn = bb_build_binary_node(p->children[i]);
            if (!left_fn) return NULL;

            bin_seq_t *seq_zeta = calloc(1, sizeof(bin_seq_t));
            if (!seq_zeta) return NULL;
            seq_zeta->left.fn     = left_fn;
            seq_zeta->left.state  = NULL;         /* binary leaf — no ζ */
            seq_zeta->right.fn    = right_fn;
            seq_zeta->right.state = right_state;

            /* Emit trampoline: ignores caller's ζ; calls bb_seq(seq_zeta, entry) */
#define TRAM_BUF_SIZE 64
            bb_buf_t tbuf = bb_alloc(TRAM_BUF_SIZE);
            if (!tbuf) { free(seq_zeta); return NULL; }
            bb_emit_mode = EMIT_BINARY;
            bb_emit_begin(tbuf, TRAM_BUF_SIZE);

            /* mov rdi, imm64(seq_zeta) */
            bb_emit_byte(0x48); bb_emit_byte(0xBF);
            bb_emit_u64((uint64_t)(uintptr_t)seq_zeta);
            /* mov rax, imm64(bb_seq) */
            bb_emit_byte(0x48); bb_emit_byte(0xB8);
            bb_emit_u64((uint64_t)(uintptr_t)bb_seq);
            /* jmp rax  (tail call — esi/entry unchanged) */
            bb_emit_byte(0xFF); bb_emit_byte(0xE0);

            int tnbytes = bb_emit_end();
            if (tnbytes <= 0 || tnbytes > TRAM_BUF_SIZE) {
                bb_free(tbuf, TRAM_BUF_SIZE);
                free(seq_zeta);
                return NULL;
            }
            bb_seal(tbuf, (size_t)tnbytes);
#undef TRAM_BUF_SIZE

            right_fn    = (bb_box_fn)tbuf;  /* trampoline is the new right node */
            right_state = NULL;
        }
        /* right_fn is now a self-contained trampoline for the whole tree */
        return right_fn;
    }

    default:
        /* Not yet implemented in binary path — signal fallback to C bb_build */
        if (getenv("SNO_BIN_MISS_LOG"))
            fprintf(stderr, "BIN_MISS: kind=%d\n", (int)p->kind);
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
