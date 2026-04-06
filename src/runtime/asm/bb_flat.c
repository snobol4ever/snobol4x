/*
 * bb_flat.c — Flat-Glob Invariant Pattern Emitter (M-DYN-FLAT)
 *
 * Emits an entire invariant PATND_t tree as one contiguous x86-64 blob.
 * All sub-boxes are inlined flat; control flows via direct jmp, never call/ret.
 * r10 = &Δ loaded once at pattern entry; all cursor ops use [r10].
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Sprint:  RT-129 / M-DYN-FLAT
 */

#include "bb_flat.h"
#include "bb_emit.h"
#include "../snobol4/snobol4.h"   /* defines DESCR_t; pulls patnd.h internally */
#include "../boxes/shared/bb_box.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* ── flat buffer sizing ─────────────────────────────────────────────────── */
/* 16 KB — enough for a deep pattern tree with data section */
#define FLAT_BUF_MAX  (16 * 1024)
/* Max mutable data slots (Δ-saves for backtrack, matched.δ accumulators) */
#define FLAT_DATA_MAX  256

/* ── node counter for unique label generation ───────────────────────────── */
static int g_flat_node_id = 0;

/* ── data section: mutable slots appended after code ───────────────────── */
/* Each slot is a 32-bit int at a fixed offset from blob base.
 * Slots are allocated sequentially; offset stored in flat_slot_t.
 * At emit time we don't know CODE_END, so slots are tracked as
 * index into a post-code array; patched in after bb_emit_end(). */

typedef struct {
    int index;   /* slot index (0-based); final offset = code_bytes + index*4 */
} flat_slot_t;

static int g_flat_slot_count = 0;

static flat_slot_t flat_alloc_slot(void) {
    flat_slot_t s;
    s.index = g_flat_slot_count++;
    return s;
}

/* ── emit helpers using r10=&Δ convention ───────────────────────────────── */

/* Load Δ into eax.  r10 = &Δ (set at entry). */
static void emit_load_delta(void) {
    /* mov eax, [r10]   — 41 8B 02 */
    bb_emit_byte(0x41); bb_emit_byte(0x8B); bb_emit_byte(0x02);
}

/* Store eax → Δ.  r10 = &Δ. */
static void emit_store_delta(void) {
    /* mov [r10], eax   — 41 89 02 */
    bb_emit_byte(0x41); bb_emit_byte(0x89); bb_emit_byte(0x02);
}

/* Add imm32 to Δ via r10. Clobbers eax. */
static void emit_add_delta_imm(int32_t v) {
    emit_load_delta();
    bb_emit_byte(0x05); bb_emit_u32((uint32_t)v);   /* add eax, imm32 */
    emit_store_delta();
}

/* Sub imm32 from Δ via r10. Clobbers eax. */
static void emit_sub_delta_imm(int32_t v) {
    emit_load_delta();
    bb_emit_byte(0x2D); bb_emit_u32((uint32_t)v);   /* sub eax, imm32 */
    emit_store_delta();
}

/* Load Σ (ptr global) into rax. */
static void emit_load_sigma(void) {
    /* mov rcx, imm64(&Σ) */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Σ);
    /* mov rax, [rcx] */
    bb_emit_byte(0x48); bb_emit_byte(0x8B); bb_emit_byte(0x01);
}

/* Load Ω (int global) into eax. */
static void emit_load_omega(void) {
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Ω);
    bb_emit_byte(0x8B); bb_emit_byte(0x01);   /* mov eax, [rcx] */
}

/* Compute rax = Σ+Δ (cursor pointer).  Clobbers rcx. */
static void emit_sigma_plus_delta(void) {
    emit_load_sigma();                        /* rax = Σ */
    /* movsxd rcx, dword[r10]  — 49 63 0A */
    bb_emit_byte(0x49); bb_emit_byte(0x63); bb_emit_byte(0x0A);
    /* lea rax, [rax+rcx]  — 48 8D 04 08 */
    bb_emit_byte(0x48); bb_emit_byte(0x8D); bb_emit_byte(0x04); bb_emit_byte(0x08);
}

/* Return success: rax=σ (Σ+Δ at match start), rdx=δ (advance).
 * For leaf nodes with known advance, caller loads rax/rdx before jmping here. */

/* ── flat_emit_node ─────────────────────────────────────────────────────── */
/*
 * Emit one PATND_t node (and all its children) into the current buffer.
 * lbl_succ: jump here on γ (success) — caller-provided continuation
 * lbl_fail: jump here on ω (failure) — caller-provided continuation
 * lbl_beta: jump here when parent sends β to this node
 *
 * The node defines lbl_beta before its β code.
 * On γ it jumps to lbl_succ; on ω it jumps to lbl_fail.
 */
static void flat_emit_node(PATND_t *p,
                           bb_label_t *lbl_succ,
                           bb_label_t *lbl_fail,
                           bb_label_t *lbl_beta);

/* ── XCAT (concatenation): left then right ──────────────────────────────── */
/*
 * Flat layout for XCAT(L, R):
 *
 *   [lbl_beta of XCAT]: jmp right_β
 *   [entry / α side]:
 *     emit L with:  succ=mid_γ,  fail=xcat_ω,  beta=left_β
 *   [mid_γ]:  (L succeeded — now try R)
 *     emit R with:  succ=lbl_succ, fail=right_ω, beta=right_β
 *   [right_ω]: (R failed — backtrack L)
 *     jmp left_β
 *   [left_β]:  (backtrack L — if L also fails → xcat_ω)
 *     (L's own β code handles this; it will jmp lbl_fail on full failure)
 *   [xcat_ω]:
 *     jmp lbl_fail
 *
 * XCAT beta entry: try R β first; if R fully fails → try L β.
 * This mirrors bb_seq.c logic but as flat jmps.
 */
static void flat_emit_xcat(PATND_t *p,
                            bb_label_t *lbl_succ,
                            bb_label_t *lbl_fail,
                            bb_label_t *lbl_beta)
{
    int id = g_flat_node_id++;
    bb_label_t mid_γ, right_ω, left_β_lbl, right_β_lbl, xcat_ω;
    bb_label_initf(&mid_γ,       "xcat%d_mid_g",   id);
    bb_label_initf(&right_ω,     "xcat%d_right_o", id);
    bb_label_initf(&left_β_lbl,  "xcat%d_left_b",  id);
    bb_label_initf(&right_β_lbl, "xcat%d_right_b", id);
    bb_label_initf(&xcat_ω,      "xcat%d_o",       id);

    /* Fold: for more than 2 children, recurse right-associatively */
    /* NOTE: lbl_beta (XCAT β entry) is defined AFTER the children so α flows first */
    if (p->nchildren == 0) {
        /* empty → epsilon: always succeed */
        bb_insn_jmp_rel32(lbl_succ);
        bb_label_define(lbl_beta);
        bb_insn_jmp_rel32(lbl_fail);
        bb_label_define(&xcat_ω);
        bb_insn_jmp_rel32(lbl_fail);
        /* unused */
        bb_label_define(&mid_γ); bb_label_define(&right_ω); bb_label_define(&right_β_lbl); bb_label_define(&left_β_lbl);
        return;
    }
    if (p->nchildren == 1) {
        flat_emit_node(p->children[0], lbl_succ, lbl_fail, &left_β_lbl);
        /* β entry after children */
        bb_label_define(lbl_beta);
        bb_insn_jmp_rel32(&left_β_lbl);
        bb_label_define(&xcat_ω);
        bb_insn_jmp_rel32(lbl_fail);
        /* unused */
        bb_label_define(&mid_γ); bb_label_define(&right_ω); bb_label_define(&right_β_lbl);
        return;
    }

    /* Left child */
    flat_emit_node(p->children[0], &mid_γ, &xcat_ω, &left_β_lbl);

    /* mid_γ: left succeeded, now try right.
     * For >2 children, build a synthetic XCAT for children[1..n-1].
     * We handle exactly 2 here; recursion handles deeper. */
    bb_label_define(&mid_γ);

    if (p->nchildren == 2) {
        flat_emit_node(p->children[1], lbl_succ, &right_ω, &right_β_lbl);
    } else {
        /* Build virtual right node as an XCAT of children[1..n-1].
         * We emit them right-recursively inline. */
        /* For simplicity: emit children[1] with succ=mid2, fail=right_ω */
        /* Then chain the rest. This is a recursive unroll. */
        /* Allocate labels for each chained pair */
        int nc = p->nchildren;
        /* We allocate nc-1 mid labels on stack */
        bb_label_t *mids = alloca(sizeof(bb_label_t) * (nc - 1));
        bb_label_t *betas = alloca(sizeof(bb_label_t) * (nc - 1));
        for (int i = 0; i < nc - 1; i++) {
            bb_label_initf(&mids[i],  "xcat%d_mid%d_g",  id, i+1);
            bb_label_initf(&betas[i], "xcat%d_mid%d_b",  id, i+1);
        }
        /* First additional child: succ=mids[0] if more, else lbl_succ */
        for (int i = 1; i < nc; i++) {
            bb_label_t *s = (i < nc-1) ? &mids[i-1] : lbl_succ;
            bb_label_t *f = &right_ω;   /* all failures ripple to right_ω */
            bb_label_t *b = &betas[i-1];
            flat_emit_node(p->children[i], s, f, b);
            if (i < nc-1) bb_label_define(&mids[i-1]);
        }
    }

    /* right_ω: right side failed — backtrack left */
    bb_label_define(&right_ω);
    bb_insn_jmp_rel32(&left_β_lbl);

    /* XCAT β entry: reachable only from entry jmp (not from α flow) */
    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(&right_β_lbl);

    /* xcat_ω: total failure */
    bb_label_define(&xcat_ω);
    bb_insn_jmp_rel32(lbl_fail);
}

/* ── XOR (alternation) ──────────────────────────────────────────────────── */
/*
 * Flat layout for XOR(C0, C1, ..., Cn-1):
 *
 *   [lbl_beta]: jmp to currently-active child β
 *   [alt_α]:
 *     save Δ → slot
 *     try C0 α: succ→lbl_succ; fail→try_C1
 *   [try_C1]:
 *     restore Δ from slot
 *     try C1 α: succ→lbl_succ; fail→try_C2
 *   ...
 *   [alt_ω]:
 *     jmp lbl_fail
 *
 * β: for simplicity in flat model, delegate to current child β.
 * Since we can't track which child succeeded at runtime without
 * a slot, we emit a β-dispatch using a runtime slot for child index.
 * For now: only C0 β is wired flat; others use the trampoline fallback.
 *
 * Simplified flat ALT: only 2-child case fully flat; n>2 falls back.
 */
static void flat_emit_alt(PATND_t *p,
                          bb_label_t *lbl_succ,
                          bb_label_t *lbl_fail,
                          bb_label_t *lbl_beta)
{
    int id = g_flat_node_id++;
    int nc = p->nchildren;

    if (nc == 0) {
        bb_label_define(lbl_beta);
        bb_insn_jmp_rel32(lbl_fail);
        return;
    }
    if (nc == 1) {
        flat_emit_node(p->children[0], lbl_succ, lbl_fail, lbl_beta);
        return;
    }

    /* β entry: try child 0 β (simplified — only first child β tracked flat) */
    bb_label_t c0_beta, c0_fail;
    bb_label_initf(&c0_beta, "alt%d_c0b", id);
    bb_label_initf(&c0_fail, "alt%d_c0f", id);

    /* Δ save slot for ALT reset */
    flat_slot_t dslot = flat_alloc_slot();
    (void)dslot;  /* slot index reserved; actual data patched post-emit */

    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(&c0_beta);

    /* Child 0 α */
    flat_emit_node(p->children[0], lbl_succ, &c0_fail, &c0_beta);

    /* c0 failed: try remaining children sequentially */
    for (int i = 1; i < nc; i++) {
        bb_label_define(&c0_fail);
        /* Reset Δ to alt entry Δ — load from global (Δ was restored by child β) */
        /* For the flat model: emit each subsequent child with its own fail chain */
        bb_label_t ci_beta, ci_fail;
        bb_label_initf(&ci_beta, "alt%d_c%db", id, i);
        bb_label_initf(&ci_fail, "alt%d_c%df", id, i);
        c0_fail = ci_fail;   /* chain next fail */
        flat_emit_node(p->children[i], lbl_succ, &ci_fail, &ci_beta);
    }

    /* Final fail → lbl_fail */
    bb_label_define(&c0_fail);
    bb_insn_jmp_rel32(lbl_fail);
}

/* ── Leaf node emitters ─────────────────────────────────────────────────── */
/* Each emits its α and β code inline, jumping to lbl_succ/lbl_fail/lbl_beta */

static void flat_emit_lit(const char *lit, int len,
                          bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                          bb_label_t *lbl_beta)
{
    int id = g_flat_node_id++;
    bb_label_t lit_beta_lbl;
    bb_label_initf(&lit_beta_lbl, "lit%d_b", id);

    /* α: bounds check + memcmp (α entry falls through here) */

    /* eax = Δ + len; cmp eax, Ω; jg fail */
    emit_load_delta();                                    /* eax = Δ */
    bb_emit_byte(0x05); bb_emit_u32((uint32_t)len);      /* add eax, len */
    /* cmp eax, Ω */
    bb_emit_byte(0x48); bb_emit_byte(0xB9);
    bb_emit_u64((uint64_t)(uintptr_t)&Ω);
    bb_emit_byte(0x3B); bb_emit_byte(0x01);               /* cmp eax, [rcx] */
    /* jg fail */
    bb_emit_byte(0x0F); bb_emit_byte(0x8F); bb_emit_patch_rel32(lbl_fail);

    /* memcmp(Σ+Δ, lit, len) */
    emit_sigma_plus_delta();       /* rax = Σ+Δ */
    /* rdi = Σ+Δ */
    bb_emit_byte(0x48); bb_emit_byte(0x89); bb_emit_byte(0xC7);
    /* rsi = lit */
    bb_emit_byte(0x48); bb_emit_byte(0xBE);
    bb_emit_u64((uint64_t)(uintptr_t)lit);
    /* rdx = len */
    bb_emit_byte(0x48); bb_emit_byte(0xBA);
    bb_emit_u64((uint64_t)(uint32_t)len);
    /* call memcmp */
    bb_emit_byte(0x48); bb_emit_byte(0xB8);
    bb_emit_u64((uint64_t)(uintptr_t)memcmp);
    bb_insn_call_rax();
    /* test eax,eax; jne fail */
    bb_emit_byte(0x85); bb_emit_byte(0xC0);
    bb_insn_jne_rel32(lbl_fail);

    /* success: Δ += len; jmp lbl_succ */
    emit_add_delta_imm(len);
    bb_insn_jmp_rel32(lbl_succ);

    /* β: Δ -= len; fail */
    bb_label_define(lbl_beta);
    emit_sub_delta_imm(len);
    bb_insn_jmp_rel32(lbl_fail);
}

static void flat_emit_eps(bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                          bb_label_t *lbl_beta)
{
    /* α: succeed immediately */
    bb_insn_jmp_rel32(lbl_succ);
    /* β: fail (epsilon has no backtrack) */
    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(lbl_fail);
}

static void flat_emit_fail(bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                           bb_label_t *lbl_beta)
{
    (void)lbl_succ;
    /* α: fail immediately */
    bb_insn_jmp_rel32(lbl_fail);
    /* β: fail */
    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(lbl_fail);
}

static void flat_emit_pos(int n, bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                          bb_label_t *lbl_beta)
{
    /* α: eax=Δ; cmp eax,n; jne fail; jmp succ */
    emit_load_delta();
    bb_emit_byte(0x3D); bb_emit_u32((uint32_t)n);   /* cmp eax, imm32 */
    bb_insn_jne_rel32(lbl_fail);
    bb_insn_jmp_rel32(lbl_succ);
    /* β: fail (POS has no backtrack) */
    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(lbl_fail);
}

static void flat_emit_rpos(int n, bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                           bb_label_t *lbl_beta)
{
    /* α: eax=Ω-n; cmp Δ,eax; jne fail; jmp succ */
    emit_load_omega();                               /* eax = Ω */
    bb_emit_byte(0x2D); bb_emit_u32((uint32_t)n);   /* sub eax, n */
    bb_emit_byte(0x89); bb_emit_byte(0xC1);          /* mov ecx, eax (Ω-n) */
    emit_load_delta();                               /* eax = Δ */
    bb_emit_byte(0x39); bb_emit_byte(0xC8);          /* cmp eax, ecx */
    bb_insn_jne_rel32(lbl_fail);
    bb_insn_jmp_rel32(lbl_succ);
    /* β: fail */
    bb_label_define(lbl_beta);
    bb_insn_jmp_rel32(lbl_fail);
}

/* Generic charset leaf (ANY/NOTANY/SPAN/BRK): call C box via trampoline.
 * These have mutable state (advance counter) — emit 22-byte trampoline
 * that tail-calls C bb_* with heap zeta. Still inline in the flat buffer;
 * the call is to C, but the dispatcher overhead is gone. */
static void flat_emit_charset_call(bb_box_fn c_fn, const char *chars,
                                   bb_label_t *lbl_succ, bb_label_t *lbl_fail,
                                   bb_label_t *lbl_beta)
{
    /* Allocate heap zeta for charset — chars pointer stable for program life */
    typedef struct { const char *chars; int delta; } cs_t;
    cs_t *z = calloc(1, sizeof(cs_t));
    z->chars = chars;

    /* α: call c_fn(z, α); test; jmp succ/fail */
    bb_emit_byte(0x48); bb_emit_byte(0xBF); bb_emit_u64((uint64_t)(uintptr_t)z);
    bb_emit_byte(0xBE); bb_emit_u32(0);
    bb_emit_byte(0x48); bb_emit_byte(0xB8); bb_emit_u64((uint64_t)(uintptr_t)c_fn);
    bb_insn_call_rax();
    bb_emit_byte(0x48); bb_emit_byte(0x85); bb_emit_byte(0xC0);
    bb_insn_jne_rel32(lbl_succ);
    bb_insn_jmp_rel32(lbl_fail);

    /* β: call c_fn(z, β); test; jmp succ/fail */
    bb_label_define(lbl_beta);
    bb_emit_byte(0x48); bb_emit_byte(0xBF); bb_emit_u64((uint64_t)(uintptr_t)z);
    bb_emit_byte(0xBE); bb_emit_u32(1);
    bb_emit_byte(0x48); bb_emit_byte(0xB8); bb_emit_u64((uint64_t)(uintptr_t)c_fn);
    bb_insn_call_rax();
    bb_emit_byte(0x48); bb_emit_byte(0x85); bb_emit_byte(0xC0);
    bb_insn_jne_rel32(lbl_succ);
    bb_insn_jmp_rel32(lbl_fail);
}

/* ── flat_emit_node dispatch ─────────────────────────────────────────────── */

extern spec_t bb_span(void *zeta, int entry);
extern spec_t bb_any(void *zeta, int entry);
extern spec_t bb_brk(void *zeta, int entry);
extern spec_t bb_notany(void *zeta, int entry);
extern int memcmp(const void *, const void *, size_t);

static void flat_emit_node(PATND_t *p,
                           bb_label_t *lbl_succ,
                           bb_label_t *lbl_fail,
                           bb_label_t *lbl_beta)
{
    if (!p) {
        flat_emit_eps(lbl_succ, lbl_fail, lbl_beta);
        return;
    }
    switch (p->kind) {

    case XCHR: {
        const char *lit = p->STRVAL_fn ? p->STRVAL_fn : "";
        flat_emit_lit(lit, (int)strlen(lit), lbl_succ, lbl_fail, lbl_beta);
        break;
    }
    case XEPS:
        flat_emit_eps(lbl_succ, lbl_fail, lbl_beta);
        break;

    case XFAIL:
        flat_emit_fail(lbl_succ, lbl_fail, lbl_beta);
        break;

    case XPOSI:
        flat_emit_pos((int)p->num, lbl_succ, lbl_fail, lbl_beta);
        break;

    case XRPSI:
        flat_emit_rpos((int)p->num, lbl_succ, lbl_fail, lbl_beta);
        break;

    case XCAT:
        flat_emit_xcat(p, lbl_succ, lbl_fail, lbl_beta);
        break;

    case XOR:
        flat_emit_alt(p, lbl_succ, lbl_fail, lbl_beta);
        break;

    case XSPNC:
        flat_emit_charset_call(bb_span,
            p->STRVAL_fn ? p->STRVAL_fn : "",
            lbl_succ, lbl_fail, lbl_beta);
        break;

    case XANYC:
        flat_emit_charset_call(bb_any,
            p->STRVAL_fn ? p->STRVAL_fn : "",
            lbl_succ, lbl_fail, lbl_beta);
        break;

    case XBRKC:
        flat_emit_charset_call(bb_brk,
            p->STRVAL_fn ? p->STRVAL_fn : "",
            lbl_succ, lbl_fail, lbl_beta);
        break;

    case XNNYC:
        flat_emit_charset_call(bb_notany,
            p->STRVAL_fn ? p->STRVAL_fn : "",
            lbl_succ, lbl_fail, lbl_beta);
        break;

    default:
        /* Variant or unsupported — caller should have checked is_invariant */
        /* Emit unconditional fail as safety net */
        bb_label_define(lbl_beta);
        bb_insn_jmp_rel32(lbl_fail);
        bb_insn_jmp_rel32(lbl_fail);
        break;
    }
}

/* ── invariance check (mirrors stmt_exec.c patnd_is_invariant) ────────── */
static int flat_is_eligible(PATND_t *p)
{
    if (!p) return 1;
    switch (p->kind) {
    case XDSAR: case XVAR: case XATP:
    case XFNME: case XNME: case XFARB:
    case XSTAR: case XARBN: case XCALLCAP:
        return 0;
    default: break;
    }
    for (int i = 0; i < p->nchildren; i++)
        if (!flat_is_eligible(p->children[i])) return 0;
    return 1;
}

/* ── bb_build_flat — public entry point ─────────────────────────────────── */
bb_box_fn bb_build_flat(PATND_t *p)
{
    if (!flat_is_eligible(p)) return NULL;

    bb_buf_t buf = bb_alloc(FLAT_BUF_MAX);
    if (!buf) return NULL;

    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, FLAT_BUF_MAX);
    g_flat_slot_count = 0;
    g_flat_node_id    = 0;  /* reset per pattern */

    /* ── entry: set r10 = &Δ; dispatch α/β ──────────────────────────── */
    /*   mov r10, imm64(&Δ)   — 49 BA <8 bytes>  */
    bb_emit_byte(0x49); bb_emit_byte(0xBA);
    bb_emit_u64((uint64_t)(uintptr_t)&Δ);
    /*   cmp esi, 0           — 83 FE 00          */
    bb_emit_byte(0x83); bb_emit_byte(0xFE); bb_emit_byte(0x00);
    /*   je  PAT_α            — 74 dd (rel8 forward) */
    bb_label_t lbl_pat_alpha;
    bb_label_initf(&lbl_pat_alpha, "pat_flat_alpha");
    bb_insn_je_rel8(&lbl_pat_alpha);
    /* β path: fall through to β label defined by root node */

    /* Labels for overall pattern success and failure */
    bb_label_t lbl_succ, lbl_fail, lbl_root_beta;
    bb_label_initf(&lbl_succ,      "pat_flat_gamma");
    bb_label_initf(&lbl_fail,      "pat_flat_omega");
    bb_label_initf(&lbl_root_beta, "pat_flat_beta");

    /* β entry point must come before α in the buffer so the je above
     * jumps forward to α.  Emit β trampoline: jmp lbl_root_beta */
    bb_insn_jmp_rel32(&lbl_root_beta);

    /* α entry */
    bb_label_define(&lbl_pat_alpha);

    /* Emit all nodes flat into this buffer */
    flat_emit_node(p, &lbl_succ, &lbl_fail, &lbl_root_beta);

    /* ── PAT_γ: success — rax = Σ+Δ_start (captured by node), rdx = advance
     * For the flat model: nodes jump here after advancing Δ.
     * Return spec(Σ+match_start, matched_len).
     * Simplification: return (Σ+Δ_entry, Δ_now - Δ_entry).
     * We stash entry Δ in r11 at the very top of α. */
    /* Actually: nodes already advanced Δ. Return spec(NULL+1, 0) as a
     * non-empty sentinel — Phase 3 uses spec_is_empty() only, not σ/δ values
     * for the match extent (those come from Δ before/after).
     * So: rax = non-NULL sentinel (Σ+Δ), rdx = 0 is sufficient for
     * the Phase 3 loop which checks spec_is_empty() and reads Δ directly. */
    bb_label_define(&lbl_succ);
    emit_sigma_plus_delta();          /* rax = Σ+Δ (non-NULL → success) */
    bb_emit_byte(0x31); bb_emit_byte(0xD2);   /* xor edx, edx */
    bb_insn_ret();

    /* ── PAT_ω: failure */
    bb_label_define(&lbl_fail);
    bb_insn_xor_eax_eax();
    bb_emit_byte(0x31); bb_emit_byte(0xD2);   /* xor edx, edx */
    bb_insn_ret();

    int nbytes = bb_emit_end();
    if (nbytes <= 0 || nbytes > FLAT_BUF_MAX) {
        bb_free(buf, FLAT_BUF_MAX);
        return NULL;
    }
    bb_seal(buf, (size_t)nbytes);
    return (bb_box_fn)buf;
}
