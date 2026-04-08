/*
 * sm_codegen.c — SM_Program → x86-64 in-memory code (M-JIT-RUN)
 *
 * Architecture: threaded-call JIT (first pass)
 * ─────────────────────────────────────────────
 * SEG_DISPATCH holds one C-ABI handler stub per SM opcode.
 * SEG_CODE is a sequence of CALL rel32 instructions, one per SM instruction,
 * followed by operand data read by the handler via a global pointer.
 *
 * Execution model:
 *   - g_jit_prog   points to the SM_Program (read by handlers for operands)
 *   - g_jit_pc     is the logical SM program counter
 *   - g_jit_state  is a shared SM_State (stack, last_ok)
 *   - Each handler increments g_jit_pc and performs the same logic as
 *     the corresponding sm_interp case.
 *   - SEG_CODE entry is called as:  typedef void (*jit_fn_t)(void);
 *
 * This produces real x86-64 machine code in a sealed RX slab; the handlers
 * themselves are existing C functions — no hand-rolled x86 per opcode yet.
 * That native-emission layer (M-JITEM-X64) comes later.
 *
 * Calling convention for dispatch stubs:
 *   Each stub is a small x86-64 function:
 *     push rbp; mov rbp,rsp
 *     mov rdi, <handler_fn_ptr>   (imm64 via mov rdi,imm64 + call *rdi)
 *     pop rbp; ret
 *   The real work is in the C handler functions below.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-07 (M-JIT-RUN)
 */

#include "sm_codegen.h"
#include "sm_image.h"
#include "sm_prog.h"
#include "sm_interp.h"   /* SM_State, sm_push, sm_pop, sm_state_init */
#include "snobol4.h"
#include "sil_macros.h"
#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"  /* EXPR_t, E_FNC for SM_PAT_CAPTURE_FN */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ── JIT execution state (globals shared with handler functions) ──────── */

static SM_Program *g_jit_prog   = NULL;
static SM_State   *g_jit_state  = NULL;
static int         g_jit_halted = 0;

/* Pat-stack (mirrors sm_interp.c's private g_pat_stack) */
#define JIT_PAT_STACK_MAX 128
static DESCR_t g_jit_pat_stack[JIT_PAT_STACK_MAX];
static int     g_jit_pat_sp = 0;

static void jit_pat_push(DESCR_t d)
{
    if (g_jit_pat_sp >= JIT_PAT_STACK_MAX) {
        fprintf(stderr, "sm_codegen: pat-stack overflow\n"); abort();
    }
    g_jit_pat_stack[g_jit_pat_sp++] = d;
}

static DESCR_t jit_pat_pop(void)
{
    if (g_jit_pat_sp <= 0) {
        fprintf(stderr, "sm_codegen: pat-stack underflow\n"); abort();
    }
    return g_jit_pat_stack[--g_jit_pat_sp];
}

/* ── Externs from snobol4 runtime ────────────────────────────────────── */

extern DESCR_t  NV_GET_fn(const char *name);
extern DESCR_t  NV_SET_fn(const char *name, DESCR_t val);  /* RT-5 */
extern char    *VARVAL_fn(DESCR_t d);
extern DESCR_t  CONCAT_fn(DESCR_t l, DESCR_t r);
extern DESCR_t  INVOKE_fn(const char *name, DESCR_t *args, int nargs);
extern DESCR_t  NAME_fn(const char *name);
extern int      exec_stmt(const char *subj_name, DESCR_t *subj_var,
                          DESCR_t pat, DESCR_t *repl, int has_repl);
extern void     comm_stno(int n);

/* subscript helpers */
extern DESCR_t subscript_get(DESCR_t base, DESCR_t idx);
extern DESCR_t subscript_get2(DESCR_t base, DESCR_t i, DESCR_t j);
extern void    subscript_set(DESCR_t base, DESCR_t idx, DESCR_t val);
extern void    subscript_set2(DESCR_t base, DESCR_t i, DESCR_t j, DESCR_t val);

/* pattern constructors */
extern DESCR_t pat_lit(const char *s);
extern DESCR_t pat_span(const char *chars);
extern DESCR_t pat_break_(const char *chars);
extern DESCR_t pat_breakx(const char *chars);
extern DESCR_t pat_any_cs(const char *chars);
extern DESCR_t pat_notany(const char *chars);
extern DESCR_t pat_len(int64_t n);
extern DESCR_t pat_pos(int64_t n);
extern DESCR_t pat_rpos(int64_t n);
extern DESCR_t pat_tab(int64_t n);
extern DESCR_t pat_rtab(int64_t n);
extern DESCR_t pat_arb(void);
extern DESCR_t pat_arbno(DESCR_t inner);
extern DESCR_t pat_rem(void);
extern DESCR_t pat_fence(void);
extern DESCR_t pat_fail(void);
extern DESCR_t pat_abort(void);
extern DESCR_t pat_succeed(void);
extern DESCR_t pat_bal(void);
extern DESCR_t pat_epsilon(void);
extern DESCR_t pat_cat(DESCR_t left, DESCR_t right);
extern DESCR_t pat_alt(DESCR_t left, DESCR_t right);
extern DESCR_t pat_ref(const char *name);
extern DESCR_t pat_assign_imm(DESCR_t child, DESCR_t var);
extern DESCR_t pat_assign_cond(DESCR_t child, DESCR_t var);
extern DESCR_t pat_at_cursor(const char *varname);

/* ── Arithmetic helper (mirrors sm_interp.c's sm_arith) ──────────────── */

static int64_t to_int_jit(DESCR_t d)
{
    if (d.v == DT_I) return d.i;
    if (d.v == DT_R) return (int64_t)d.r;
    if (d.v == DT_S && d.s) return (int64_t)strtoll(d.s, NULL, 10);
    return 0;
}

static double to_real_jit(DESCR_t d)
{
    if (d.v == DT_R) return d.r;
    if (d.v == DT_I) return (double)d.i;
    if (d.v == DT_S && d.s) return strtod(d.s, NULL);
    return 0.0;
}

static DESCR_t jit_arith(DESCR_t l, DESCR_t r, sm_opcode_t op)
{
    if (l.v == DT_I && r.v == DT_I) {
        switch (op) {
        case SM_ADD: return INTVAL(l.i + r.i);
        case SM_SUB: return INTVAL(l.i - r.i);
        case SM_MUL: return INTVAL(l.i * r.i);
        case SM_DIV: return (r.i == 0) ? FAILDESCR : INTVAL(l.i / r.i);
        case SM_EXP: {
            /* integer ** non-negative integer → integer (mirrors sm_interp.c) */
            if (r.i >= 0) {
                int64_t base = l.i, exp = r.i, res = 1;
                while (exp-- > 0) res *= base;
                return INTVAL(res);
            }
            return REALVAL(pow((double)l.i, (double)r.i));
        }
        default: break;
        }
    }
    double ld = to_real_jit(l), rd = to_real_jit(r);
    switch (op) {
    case SM_ADD: return REALVAL(ld + rd);
    case SM_SUB: return REALVAL(ld - rd);
    case SM_MUL: return REALVAL(ld * rd);
    case SM_DIV: return (rd == 0.0) ? FAILDESCR : REALVAL(ld / rd);
    case SM_EXP: return REALVAL(pow(ld, rd));
    default: return FAILDESCR;
    }
}

/* ── Per-opcode handler functions ────────────────────────────────────── */
/*
 * Each handler is called with no arguments.
 * It reads g_jit_prog->instrs[g_jit_state->pc - 1] for its operands
 * (pc was already incremented by the dispatch loop before calling).
 * Jumps: set g_jit_state->pc directly.
 */

#define CUR_INS  (&g_jit_prog->instrs[g_jit_state->pc - 1])
#define STATE    (g_jit_state)
#define PUSH(d)  sm_push(STATE, (d))
#define POP()    sm_pop(STATE)

static void h_label(void)    { /* no-op */ }
static void h_halt(void)     { g_jit_halted = 1; }
static void h_define(void)   { /* handled by prescan */ }
static void h_return(void)   { g_jit_halted = 1; }
static void h_freturn(void)  { g_jit_halted = 1; }
static void h_nreturn(void)  { g_jit_halted = 1; }

static int g_sm_stno_jit = 0;
static void h_stno(void)     { comm_stno(++g_sm_stno_jit); }

static void h_jump(void)   { STATE->pc = (int)CUR_INS->a[0].i; }
static void h_jump_s(void) { if ( STATE->last_ok) STATE->pc = (int)CUR_INS->a[0].i; }
static void h_jump_f(void) { if (!STATE->last_ok) STATE->pc = (int)CUR_INS->a[0].i; }

static void h_push_lit_s(void)
{
    const char *s = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    int64_t     n = CUR_INS->a[1].i;
    DESCR_t d; d.v = DT_S; d.s = (char *)s; d.slen = (n > 0) ? (uint32_t)n : 0;
    PUSH(d);
}
static void h_push_lit_i(void) { PUSH(INTVAL(CUR_INS->a[0].i)); }
static void h_push_lit_f(void) { PUSH(REALVAL(CUR_INS->a[0].f)); }
static void h_push_null(void)  { PUSH(NULVCL); }

static void h_push_var(void)
{
    PUSH(NV_GET_fn(CUR_INS->a[0].s));
}

static void h_store_var(void)
{
    DESCR_t val = POP();
    NV_SET_fn(CUR_INS->a[0].s, val);
}

static void h_pop(void) { POP(); }

static void h_arith(void)
{
    DESCR_t r = POP(), l = POP();
    if (l.v == DT_S) l = INTVAL(to_int_jit(l));
    if (r.v == DT_S) r = INTVAL(to_int_jit(r));
    DESCR_t result = jit_arith(l, r, CUR_INS->op);
    PUSH(result);
    STATE->last_ok = (result.v != DT_FAIL);
}

static void h_neg(void)
{
    DESCR_t v = POP();
    if (v.v == DT_I) PUSH(INTVAL(-v.i));
    else              PUSH(REALVAL(-to_real_jit(v)));
}

static void h_concat(void)
{
    DESCR_t r = POP(), l = POP();
    DESCR_t result = CONCAT_fn(l, r);
    PUSH(result);
    STATE->last_ok = (result.v != DT_FAIL);
}

static void h_coerce_num(void)
{
    DESCR_t v = POP();
    if (v.v == DT_S) {
        int64_t iv = to_int_jit(v);
        if (iv != 0 || (v.s && v.s[0] == '0')) PUSH(INTVAL(iv));
        else PUSH(REALVAL(to_real_jit(v)));
    } else { PUSH(v); }
    STATE->last_ok = 1;
}

static void h_pat_lit(void)
{
    jit_pat_push(pat_lit(CUR_INS->a[0].s ? CUR_INS->a[0].s : ""));
}
static void h_pat_any(void)
{
    DESCR_t arg = POP(); const char *cs = VARVAL_fn(arg);
    jit_pat_push(pat_any_cs(cs ? cs : ""));
}
static void h_pat_notany(void)
{
    DESCR_t arg = POP(); const char *cs = VARVAL_fn(arg);
    jit_pat_push(pat_notany(cs ? cs : ""));
}
static void h_pat_span(void)
{
    DESCR_t arg = POP(); const char *cs = VARVAL_fn(arg);
    jit_pat_push(pat_span(cs ? cs : ""));
}
static void h_pat_break(void)
{
    DESCR_t arg = POP(); const char *cs = VARVAL_fn(arg);
    jit_pat_push(pat_break_(cs ? cs : ""));
}
static void h_pat_len(void)
{
    DESCR_t arg = POP();
    jit_pat_push(pat_len(arg.v == DT_I ? arg.i : 0));
}
static void h_pat_pos(void)
{
    DESCR_t arg = POP();
    jit_pat_push(pat_pos(arg.v == DT_I ? arg.i : 0));
}
static void h_pat_rpos(void)
{
    DESCR_t arg = POP();
    jit_pat_push(pat_rpos(arg.v == DT_I ? arg.i : 0));
}
static void h_pat_tab(void)
{
    DESCR_t arg = POP();
    jit_pat_push(pat_tab(arg.v == DT_I ? arg.i : 0));
}
static void h_pat_rtab(void)
{
    DESCR_t arg = POP();
    jit_pat_push(pat_rtab(arg.v == DT_I ? arg.i : 0));
}
static void h_pat_arb(void)     { jit_pat_push(pat_arb()); }
static void h_pat_rem(void)     { jit_pat_push(pat_rem()); }
static void h_pat_fail(void)    { jit_pat_push(pat_fail()); }
static void h_pat_succeed(void) { jit_pat_push(pat_succeed()); }
static void h_pat_eps(void)     { jit_pat_push(pat_epsilon()); }
static void h_pat_fence(void)   { jit_pat_push(pat_fence()); }
static void h_pat_abort(void)   { jit_pat_push(pat_abort()); }
static void h_pat_bal(void)     { jit_pat_push(pat_bal()); }

static void h_pat_cat(void)
{
    DESCR_t right = jit_pat_pop(), left = jit_pat_pop();
    jit_pat_push(pat_cat(left, right));
}
static void h_pat_alt(void)
{
    DESCR_t right = jit_pat_pop(), left = jit_pat_pop();
    jit_pat_push(pat_alt(left, right));
}
static void h_pat_boxval(void) { PUSH(jit_pat_pop()); }

static void h_pat_deref(void)
{
    DESCR_t v = POP();
    if (v.v == DT_P) {
        jit_pat_push(v);
    } else if (v.v == DT_S && v.s) {
        jit_pat_push(pat_lit(v.s));
    } else {
        const char *name = VARVAL_fn(v);
        jit_pat_push(pat_ref(name ? name : ""));
    }
}

static void h_pat_capture(void)
{
    DESCR_t child  = jit_pat_pop();
    const char *vn = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    DESCR_t var    = NAME_fn(vn);
    int kind       = (int)CUR_INS->a[1].i;
    if (kind == 1)
        jit_pat_push(pat_assign_imm(child, var));
    else if (kind == 2)
        jit_pat_push(pat_cat(child, pat_at_cursor(vn)));
    else
        jit_pat_push(pat_assign_cond(child, var));
}

static void h_pat_capture_fn(void)
{
    /* . *func() — a[0].s = function name.
     * Pass DT_E var with synthetic E_FNC so XNME materialise fires call_fn. */
    DESCR_t child  = jit_pat_pop();
    const char *fname = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    EXPR_t *efnc = GC_malloc(sizeof(EXPR_t));
    memset(efnc, 0, sizeof(EXPR_t));
    efnc->kind = E_FNC;
    efnc->sval = GC_strdup(fname);
    DESCR_t var;
    var.v   = DT_E;
    var.ptr = efnc;
    var.slen = 0;
    var.s   = NULL;
    int kind = (int)CUR_INS->a[1].i;
    if (kind == 1)
        jit_pat_push(pat_assign_imm(child, var));
    else
        jit_pat_push(pat_assign_cond(child, var));
}

static void h_exec_stmt(void)
{
    int has_repl   = (int)CUR_INS->a[1].i;
    DESCR_t repl   = POP();
    DESCR_t subj_d = POP();
    DESCR_t pat_d  = (g_jit_pat_sp > 0) ? jit_pat_pop() : pat_epsilon();
    const char *sn = CUR_INS->a[0].s;
    int ok = exec_stmt(sn, &subj_d, pat_d, has_repl ? &repl : NULL, has_repl);
    STATE->last_ok = ok;
    g_jit_pat_sp   = 0;
}

static void h_call(void)
{
    const char *name  = CUR_INS->a[0].s;
    int         nargs = (int)CUR_INS->a[1].i;

    if (name && strcmp(name, "INDIR_GET") == 0) {
        DESCR_t name_d = POP(), val;
        if (IS_NAMEPTR(name_d))       val = NAME_DEREF_PTR(name_d);
        else if (IS_NAMEVAL(name_d))  val = NV_GET_fn(name_d.s);
        else { const char *vn = VARVAL_fn(name_d); val = (vn && *vn) ? NV_GET_fn(vn) : NULVCL; }
        PUSH(val); STATE->last_ok = 1; return;
    }
    if (name && strcmp(name, "NAME_PUSH") == 0) {
        DESCR_t nd = POP(); const char *vn = VARVAL_fn(nd);
        PUSH(NAMEVAL(GC_strdup(vn ? vn : ""))); STATE->last_ok = 1; return;
    }
    if (name && strcmp(name, "ASGN_INDIR") == 0) {
        DESCR_t nd  = POP(), val = POP();
        const char *vn = VARVAL_fn(nd);
        if (vn && *vn) NV_SET_fn(vn, val);
        PUSH(val); STATE->last_ok = 1; return;
    }
    if (name && strcmp(name, "IDX") == 0) {
        if (nargs == 2) {
            DESCR_t idx = POP(), base = POP();
            DESCR_t r = subscript_get(base, idx);
            PUSH(r); STATE->last_ok = (r.v != DT_FAIL);
        } else if (nargs == 3) {
            DESCR_t j = POP(), i = POP(), base = POP();
            DESCR_t r = subscript_get2(base, i, j);
            PUSH(r); STATE->last_ok = (r.v != DT_FAIL);
        } else { PUSH(FAILDESCR); STATE->last_ok = 0; }
        return;
    }
    if (name && strcmp(name, "IDX_SET") == 0) {
        if (nargs == 3) {
            DESCR_t i = POP(), base = POP(), val = POP();
            subscript_set(base, i, val); PUSH(val); STATE->last_ok = 1;
        } else if (nargs == 4) {
            DESCR_t j = POP(), i = POP(), base = POP(), val = POP();
            subscript_set2(base, i, j, val); PUSH(val); STATE->last_ok = 1;
        } else { STATE->last_ok = 0; }
        return;
    }

    DESCR_t args[32];
    if (nargs > 32) nargs = 32;
    for (int k = nargs - 1; k >= 0; k--) args[k] = POP();
    DESCR_t result = INVOKE_fn(name, args, nargs);
    if (IS_NAMEPTR(result))      result = NAME_DEREF_PTR(result);
    else if (IS_NAMEVAL(result)) result = NV_GET_fn(result.s);
    PUSH(result);
    STATE->last_ok = (result.v != DT_FAIL);
}

static void h_incr(void) { DESCR_t v = POP(); PUSH(INTVAL(v.i + CUR_INS->a[0].i)); }
static void h_decr(void) { DESCR_t v = POP(); PUSH(INTVAL(v.i - CUR_INS->a[0].i)); }

/* Unimplemented stubs — emit warning, set last_ok=0 */
static void h_unimpl(void)
{
    fprintf(stderr, "sm_codegen: unimplemented opcode %d (%s) at sm-pc=%d\n",
            (int)CUR_INS->op, sm_opcode_name(CUR_INS->op), STATE->pc - 1);
    STATE->last_ok = 0;
}

/* ── Handler dispatch table ──────────────────────────────────────────── */

typedef void (*handler_fn_t)(void);

static handler_fn_t g_handlers[SM_OPCODE_COUNT];

static void init_handler_table(void)
{
    for (int i = 0; i < SM_OPCODE_COUNT; i++) g_handlers[i] = h_unimpl;

    g_handlers[SM_LABEL]      = h_label;
    g_handlers[SM_JUMP]       = h_jump;
    g_handlers[SM_JUMP_S]     = h_jump_s;
    g_handlers[SM_JUMP_F]     = h_jump_f;
    g_handlers[SM_HALT]       = h_halt;
    g_handlers[SM_STNO]       = h_stno;

    g_handlers[SM_PUSH_LIT_S] = h_push_lit_s;
    g_handlers[SM_PUSH_LIT_I] = h_push_lit_i;
    g_handlers[SM_PUSH_LIT_F] = h_push_lit_f;
    g_handlers[SM_PUSH_NULL]  = h_push_null;
    g_handlers[SM_PUSH_VAR]   = h_push_var;
    g_handlers[SM_STORE_VAR]  = h_store_var;
    g_handlers[SM_POP]        = h_pop;

    g_handlers[SM_ADD]        = h_arith;
    g_handlers[SM_SUB]        = h_arith;
    g_handlers[SM_MUL]        = h_arith;
    g_handlers[SM_DIV]        = h_arith;
    g_handlers[SM_EXP]        = h_arith;
    g_handlers[SM_CONCAT]     = h_concat;
    g_handlers[SM_COERCE_NUM] = h_coerce_num;
    g_handlers[SM_NEG]        = h_neg;

    g_handlers[SM_PAT_LIT]     = h_pat_lit;
    g_handlers[SM_PAT_ANY]     = h_pat_any;
    g_handlers[SM_PAT_NOTANY]  = h_pat_notany;
    g_handlers[SM_PAT_SPAN]    = h_pat_span;
    g_handlers[SM_PAT_BREAK]   = h_pat_break;
    g_handlers[SM_PAT_LEN]     = h_pat_len;
    g_handlers[SM_PAT_POS]     = h_pat_pos;
    g_handlers[SM_PAT_RPOS]    = h_pat_rpos;
    g_handlers[SM_PAT_TAB]     = h_pat_tab;
    g_handlers[SM_PAT_RTAB]    = h_pat_rtab;
    g_handlers[SM_PAT_ARB]     = h_pat_arb;
    g_handlers[SM_PAT_REM]     = h_pat_rem;
    g_handlers[SM_PAT_BAL]     = h_pat_bal;
    g_handlers[SM_PAT_FENCE]   = h_pat_fence;
    g_handlers[SM_PAT_ABORT]   = h_pat_abort;
    g_handlers[SM_PAT_FAIL]    = h_pat_fail;
    g_handlers[SM_PAT_SUCCEED] = h_pat_succeed;
    g_handlers[SM_PAT_EPS]     = h_pat_eps;
    g_handlers[SM_PAT_ALT]     = h_pat_alt;
    g_handlers[SM_PAT_CAT]     = h_pat_cat;
    g_handlers[SM_PAT_DEREF]   = h_pat_deref;
    g_handlers[SM_PAT_CAPTURE]    = h_pat_capture;
    g_handlers[SM_PAT_CAPTURE_FN] = h_pat_capture_fn;
    g_handlers[SM_PAT_BOXVAL]  = h_pat_boxval;

    g_handlers[SM_EXEC_STMT]   = h_exec_stmt;
    g_handlers[SM_CALL]        = h_call;
    g_handlers[SM_RETURN]      = h_return;
    g_handlers[SM_FRETURN]     = h_freturn;
    g_handlers[SM_NRETURN]     = h_nreturn;
    g_handlers[SM_DEFINE]      = h_define;
    g_handlers[SM_INCR]        = h_incr;
    g_handlers[SM_DECR]        = h_decr;
    /* SM_LCOMP, SM_RCOMP, SM_TRIM, SM_ACOMP, SM_SPCINT, SM_SPREAL,
     * SM_JUMP_INDIR, SM_SELBRA, SM_STATE_PUSH, SM_STATE_POP:
     * left as h_unimpl stubs for now — not emitted by sm_lower for
     * the PASS=178 corpus. */
}

/* ── x86-64 dispatch stub emitter ────────────────────────────────────── */
/*
 * Emit into SEG_DISPATCH a 12-byte tail-call stub for handler fn:
 *
 *   mov  rax, <imm64>      ; handler address (10 bytes)
 *   jmp  rax               ; tail-call, no frame (2 bytes)
 *
 * No stack frame: the stub is called via CALL from sm_jit_run's dispatch
 * loop, which already has a 16-byte-aligned stack at that point.  Adding
 * push/sub here broke alignment for any handler that itself issues a CALL
 * (e.g. NV_SET_fn → printf).  Tail-call avoids the problem entirely.
 */
static uint8_t *emit_dispatch_stub(handler_fn_t fn)
{
    uint8_t *entry = scrip_segs[SEG_DISPATCH].top;

    /* mov rax, imm64 */
    seg_byte(SEG_DISPATCH, 0x48); seg_byte(SEG_DISPATCH, 0xb8);
    seg_u64(SEG_DISPATCH, (uint64_t)(uintptr_t)fn);
    /* jmp rax */
    seg_byte(SEG_DISPATCH, 0xff); seg_byte(SEG_DISPATCH, 0xe0);

    return entry;
}

/* ── Main codegen entry point ─────────────────────────────────────────── */

/*
 * sm_codegen — compile SM_Program into SEG_DISPATCH + SEG_CODE.
 *
 * SEG_DISPATCH: one stub per unique opcode (lazy — only opcodes that appear).
 * SEG_CODE: for each instruction, emit:
 *     ; increment g_jit_state->pc  (done in the main loop, not in code)
 *   We use a pure-C dispatch loop that calls into SEG_CODE stubs.
 *   SEG_CODE layout: array of 8-byte absolute pointers to dispatch stubs,
 *   one per instruction. The runner walks this array and indirect-calls each.
 *
 * Returns 0 on success, -1 on error.
 */
int sm_codegen(SM_Program *prog)
{
    init_handler_table();

    /* Build one dispatch stub per opcode (cache by opcode index) */
    uint8_t *opcode_stub[SM_OPCODE_COUNT];
    memset(opcode_stub, 0, sizeof(opcode_stub));

    for (int i = 0; i < prog->count; i++) {
        sm_opcode_t op = prog->instrs[i].op;
        if (!opcode_stub[op]) {
            opcode_stub[op] = emit_dispatch_stub(g_handlers[op]);
        }
    }

    /* SEG_CODE: array of (uint8_t *) stub pointers, one per instruction.
     * The runner at execution time does:
     *   for pc = 0..count: g_jit_state->pc = pc+1; call opcode_stub[instrs[pc].op]
     * We store the stub pointer directly so the runner can indirect-call it
     * without re-indexing through opcode_stub. */
    size_t ptr_array_bytes = (size_t)prog->count * sizeof(uint8_t *);
    uint8_t **code_ptrs = (uint8_t **)seg_alloc(SEG_CODE, ptr_array_bytes);
    if (!code_ptrs) {
        fprintf(stderr, "sm_codegen: SEG_CODE allocation failed\n");
        return -1;
    }
    for (int i = 0; i < prog->count; i++)
        code_ptrs[i] = opcode_stub[prog->instrs[i].op];

    seg_seal(SEG_DISPATCH);
    seg_seal(SEG_CODE);
    return 0;
}

/* ── JIT execution runner ─────────────────────────────────────────────── */

/*
 * sm_jit_run — execute a codegen'd SM_Program.
 *
 * Requires sm_codegen() to have been called first on the same prog.
 * Uses the pointer array at SEG_CODE base.
 * Mirrors sm_interp_run's error-recovery contract.
 */
int sm_jit_run(SM_Program *prog, SM_State *st)
{
    g_jit_prog   = prog;
    g_jit_state  = st;
    g_jit_halted = 0;
    g_jit_pat_sp = 0;

    uint8_t **code_ptrs = (uint8_t **)scrip_segs[SEG_CODE].base;

    while (st->pc < prog->count && !g_jit_halted) {
        st->pc++;  /* advance before handler reads CUR_INS (mirrors interp) */
        typedef void (*stub_fn_t)(void);
        stub_fn_t stub = (stub_fn_t)code_ptrs[st->pc - 1];
        stub();
    }
    return g_jit_halted ? 0 : 0;  /* 0 = normal exit */
}

/* sm_jit_run_plain — debug: pure C dispatch, no SEG_CODE, proves handler correctness */
int sm_jit_run_plain(SM_Program *prog, SM_State *st)
{
    init_handler_table();
    g_jit_prog   = prog;
    g_jit_state  = st;
    g_jit_halted = 0;
    g_jit_pat_sp = 0;
    while (st->pc < prog->count && !g_jit_halted) {
        st->pc++;
        g_handlers[prog->instrs[st->pc - 1].op]();
    }
    return 0;
}
