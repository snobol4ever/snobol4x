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
#include "bb_broker.h"   /* SN-9b: SM_BB_PUMP / SM_BB_ONCE handlers */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <setjmp.h>
#include <gc/gc.h>

/* ── JIT execution state (globals shared with handler functions) ──────── */

static SM_Program *g_jit_prog   = NULL;
static SM_State   *g_jit_state  = NULL;
static int         g_jit_halted = 0;

/* IM-5: JIT step-limit for in-process sync monitor */
int      g_jit_step_limit = 0;   /* 0 = unlimited; N = stop after N stmts */
int      g_jit_steps_done = 0;
jmp_buf  g_jit_step_jmp;

/* Pat-stack (mirrors sm_interp.c's private g_pat_stack) */
#define JIT_PAT_STACK_INIT 16
static DESCR_t *g_jit_pat_stack = NULL;
static int      g_jit_pat_sp    = 0;
static int      g_jit_pat_cap   = 0;

static void jit_pat_push(DESCR_t d)
{
    if (g_jit_pat_sp >= g_jit_pat_cap) {
        g_jit_pat_cap = g_jit_pat_cap ? g_jit_pat_cap * 2 : JIT_PAT_STACK_INIT;
        g_jit_pat_stack = realloc(g_jit_pat_stack, g_jit_pat_cap * sizeof(DESCR_t));
        if (!g_jit_pat_stack) { fprintf(stderr, "sm_codegen: pat-stack OOM\n"); abort(); }
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
extern DESCR_t  sc_dat_field_call(const char *name, DESCR_t *args, int nargs);
extern DESCR_t  NAME_fn(const char *name);
extern int      exec_stmt(const char *subj_name, DESCR_t *subj_var,
                          DESCR_t pat, DESCR_t *repl, int has_repl);
extern void     comm_stno(int n);

/* subscript helpers */
extern DESCR_t subscript_get(DESCR_t base, DESCR_t idx);
extern DESCR_t subscript_get2(DESCR_t base, DESCR_t i, DESCR_t j);
extern int     subscript_set(DESCR_t base, DESCR_t idx, DESCR_t val);    /* 1=ok, 0=fail */
extern int     subscript_set2(DESCR_t base, DESCR_t i, DESCR_t j, DESCR_t val); /* 1=ok, 0=fail */

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
static void h_stno(void) {
    comm_stno(++g_sm_stno_jit);
    /* IM-5: step-limit — longjmp out when limit reached */
    if (g_jit_step_limit > 0 && g_jit_steps_done++ >= g_jit_step_limit)
        longjmp(g_jit_step_jmp, 1);
}

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
static void h_push_null(void)  { PUSH(NULVCL); STATE->last_ok = 1; }

static void h_push_var(void)
{
    /* SN-9c-c: SN-6 parity with sm_interp.c:261-268.  Keyword reads (e.g.
     * INPUT at EOF) return FAILDESCR; last_ok must reflect that so the
     * statement's :F branch fires.  Without this, a prior iteration's
     * last_ok=1 from a successful match bleeds across the loop-back goto
     * and makes `LINE = INPUT :F(END)` never fire at EOF — the JIT-only
     * failure mode of word1.sno / wordcount.sno. */
    DESCR_t val = NV_GET_fn(CUR_INS->a[0].s);
    PUSH(val);
    STATE->last_ok = (val.v != DT_FAIL);
}

/* SN-9a: frozen DT_E descriptor for *expr / EVAL().  Mirrors sm_interp.c
 * SM_PUSH_EXPR handler.  Note the union-aliasing hazard — d.s and d.ptr
 * share a union, so .ptr must be written last after .slen = 0 (same
 * ordering rule as the four constructor sites fixed at SN-6b). */
static void h_push_expr(void)
{
    DESCR_t d;
    d.v    = DT_E;
    d.slen = 0;
    d.ptr  = CUR_INS->a[0].ptr;
    PUSH(d);
    STATE->last_ok = 1;
}

/* SN-9b: Byrd-box broker opcodes — Icon (SM_BB_PUMP) / Prolog (SM_BB_ONCE).
 * Direct ports of sm_interp.c:612-635.  Polyglot programs emit these for
 * LANG_ICN and LANG_PL statements; codegen previously left both as
 * h_unimpl, silently producing last_ok=0 on every Icon/Prolog statement.
 *
 * icn_eval_gen and pump_print live in scrip.c / sm_interp.c — declared
 * extern here so the handler bodies are identical to the sm_interp cases. */
extern bb_node_t icn_eval_gen(EXPR_t *e);
static void jit_pump_print(DESCR_t val, void *arg)
{
    (void)arg;
    char *s = VARVAL_fn(val);
    if (s) printf("%s\n", s);
}

static void h_bb_pump(void)
{
    DESCR_t expr_d = POP();
    EXPR_t *expr   = (EXPR_t *)expr_d.ptr;
    if (!expr) { STATE->last_ok = 0; return; }
    bb_node_t node = icn_eval_gen(expr);
    int ticks = bb_broker(node, BB_PUMP, jit_pump_print, NULL);
    STATE->last_ok = (ticks > 0);
}

static void h_bb_once(void)
{
    DESCR_t expr_d = POP();
    EXPR_t *expr   = (EXPR_t *)expr_d.ptr;
    if (!expr) { STATE->last_ok = 0; return; }
    bb_node_t node = icn_eval_gen(expr);
    int ticks = bb_broker(node, BB_ONCE, NULL, NULL);
    STATE->last_ok = (ticks > 0);
}

static void h_store_var(void)
{
    DESCR_t val = POP();
    if (val.v == DT_FAIL) { STATE->last_ok = 0; return; }
    DESCR_t stored = NV_SET_fn(CUR_INS->a[0].s, val);
    PUSH(stored);   /* match sm_interp: SM_STORE_VAR pushes result for chained assignment */
    /* SN-9c-c: SN-6 parity with sm_interp.c:296-301.  Successful assignment
     * sets last_ok=1 so a prior failure (e.g. pattern-match FAIL in the
     * previous iteration) does not bleed into this statement's :F branch
     * across a loop-back goto.  Root cause of JIT-only word1/wordcount
     * premature termination. */
    STATE->last_ok = 1;
}

static void h_pop(void) { POP(); }

static void h_arith(void)
{
    DESCR_t r = POP(), l = POP();
    /* SN-9c-c-bis: full parity with sm_interp.c:321-331.  Three pieces JIT
     * was missing: FAIL propagation (e.g. CHARS + SIZE(INPUT) at EOF) and
     * DT_SNUL→INTVAL(0) coercion (unset variables read as null string).
     * Without the SNUL coercion, `N + 1` with unset N left l.v=DT_SNUL,
     * jit_arith fell through to the REALVAL branch, and the result
     * propagated as DT_R — formatter then emitted `2.` instead of `2`. */
    if (l.v == DT_FAIL || r.v == DT_FAIL) {
        PUSH(FAILDESCR);
        STATE->last_ok = 0;
        return;
    }
    if (l.v == DT_S) l = INTVAL(to_int_jit(l));
    if (r.v == DT_S) r = INTVAL(to_int_jit(r));
    if (l.v == DT_SNUL) l = INTVAL(0);
    if (r.v == DT_SNUL) r = INTVAL(0);
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
static void h_pat_arbno(void)   { DESCR_t _inner = jit_pat_pop(); jit_pat_push(pat_arbno(_inner)); }
static void h_pat_rem(void)     { jit_pat_push(pat_rem()); }
static void h_pat_fail(void)    { jit_pat_push(pat_fail()); }
static void h_pat_succeed(void) { jit_pat_push(pat_succeed()); }
static void h_pat_eps(void)     { jit_pat_push(pat_epsilon()); }
static void h_pat_fence(void)   { jit_pat_push(pat_fence()); }
static void h_pat_fence1(void)  { DESCR_t _ch = jit_pat_pop(); jit_pat_push(pat_fence_p(_ch)); }
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

static void h_pat_refname(void)
{
    /* SN-6: *var in pattern context — build XDSAR from the NAME,
     * never fetching variable's current value at build time.
     * Mirrors sm_interp.c case SM_PAT_REFNAME. */
    const char *name = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    jit_pat_push(pat_ref(name));
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
    /* . *func() or $ *func() — a[0].s = function name.
     * a[2].s (TL-2): optional '\t'-separated arg *names* for flush-time
     * resolution — set when every arg of *func() is a plain E_VAR.
     * Use pat_assign_callcap → XCALLCAP, lowered to bb_cap with NM_CALL
     * NameKind_t (SN-21d).  The old DT_E/pat_assign_cond approach only
     * worked via materialise() which is not used in the byrd-box
     * (--sm-run / --jit-emit) path. */
    DESCR_t child  = jit_pat_pop();
    const char *fname = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    const char *namelist = CUR_INS->a[2].s;
    if (namelist && namelist[0]) {
        int nnames = 1;
        for (const char *q = namelist; *q; q++) if (*q == '\t') nnames++;
        char **names = (char **)GC_MALLOC((size_t)nnames * sizeof(char *));
        int ni = 0;
        const char *start = namelist;
        for (const char *q = namelist; ; q++) {
            if (*q == '\t' || *q == '\0') {
                size_t len = (size_t)(q - start);
                char *nm = (char *)GC_MALLOC(len + 1);
                memcpy(nm, start, len);
                nm[len] = '\0';
                names[ni++] = nm;
                if (*q == '\0') break;
                start = q + 1;
            }
        }
        int is_imm = (int)CUR_INS->a[1].i;  /* SN-26c-parseerr-f */
        jit_pat_push(is_imm
            ? pat_assign_callcap_named_imm(child, fname, NULL, 0, names, nnames)
            : pat_assign_callcap_named(child, fname, NULL, 0, names, nnames));
    } else {
        int is_imm = (int)CUR_INS->a[1].i;  /* SN-26c-parseerr-f */
        jit_pat_push(is_imm
            ? pat_assign_callcap_named_imm(child, fname, NULL, 0, NULL, 0)
            : pat_assign_callcap(child, fname, NULL, 0));
    }
}

static void h_pat_capture_fn_args(void)
{
    /* SN-8a: . *func(args) / $ *func(args) — args-on-stack form.
     * a[0].s = fname, a[1].i = kind, a[2].i = nargs.  Args were pushed in
     * order 0..nargs-1 onto the value stack; pop into positions nargs-1..0
     * to reconstruct original order.  Then pop child pattern and build
     * pat_assign_callcap(child, fname, values, nargs). */
    int nargs = (int)CUR_INS->a[2].i;
    DESCR_t *argv = nargs > 0
        ? (DESCR_t *)GC_MALLOC((size_t)nargs * sizeof(DESCR_t))
        : NULL;
    for (int i = nargs - 1; i >= 0; i--) argv[i] = POP();
    DESCR_t child = jit_pat_pop();
    const char *fname = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    int is_imm = (int)CUR_INS->a[1].i;  /* SN-26c-parseerr-f: 0=cond(.) 1=imm($) */
    jit_pat_push(is_imm
        ? pat_assign_callcap_named_imm(child, fname, argv, nargs, NULL, 0)
        : pat_assign_callcap(child, fname, argv, nargs));
}

static void h_pat_usercall(void)
{
    /* SN-17a: bare *func() in pattern context.
     * a[0].s = function name; a[2].s = '\t'-separated arg names (or NULL).
     * No child pattern is popped — bare *fn() wraps nothing.
     * Build XATP deferred-usercall node so the match engine invokes func()
     * per position; func's FAIL propagates as pattern FAIL (landing in SN-17d). */
    const char *fname = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    jit_pat_push(pat_user_call(fname, NULL, 0));
}

static void h_pat_usercall_args(void)
{
    /* SN-8a: bare *func(args) in pattern context — args-on-stack form.
     * a[0].s = fname, a[1].i = nargs.  Pop nargs values (last-pushed = last
     * arg), build XATP deferred-usercall with the evaluated args.
     * No child pattern is popped — bare *fn() wraps nothing. */
    int nargs = (int)CUR_INS->a[1].i;
    DESCR_t *argv = nargs > 0
        ? (DESCR_t *)GC_MALLOC((size_t)nargs * sizeof(DESCR_t))
        : NULL;
    for (int i = nargs - 1; i >= 0; i--) argv[i] = POP();
    const char *fname = CUR_INS->a[0].s ? CUR_INS->a[0].s : "";
    jit_pat_push(pat_user_call(fname, argv, nargs));
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
        /* $expr: pop descriptor, look up variable, push its value.  Must fold
         * the name to SNOBOL4 canonical case (SN-19) or $'bal' won't find the
         * variable the parser stored as BAL.  Parity with sm_interp.c:644. */
        DESCR_t name_d = POP(), val;
        if (IS_NAMEPTR(name_d)) {
            val = NAME_DEREF_PTR(name_d);
        } else if (IS_NAMEVAL(name_d)) {
            char *fn = GC_strdup(name_d.s); sno_fold_name(fn);  /* SN-19 */
            val = NV_GET_fn(fn);
        } else {
            const char *vn0 = VARVAL_fn(name_d);
            char *vn = (vn0 && *vn0) ? GC_strdup(vn0) : NULL;
            if (vn) sno_fold_name(vn);                          /* SN-19 */
            val = (vn && *vn) ? NV_GET_fn(vn) : NULVCL;
        }
        PUSH(val); STATE->last_ok = 1; return;
    }
    if (name && strcmp(name, "NAME_PUSH") == 0) {
        /* .X: pop name string, push DT_N NAMEVAL descriptor with folded name
         * so downstream lookups hit the same NV key the parser produced. */
        DESCR_t nd = POP();
        const char *vn0 = VARVAL_fn(nd);
        char *vn = GC_strdup(vn0 ? vn0 : "");
        sno_fold_name(vn);                                      /* SN-19 */
        PUSH(NAMEVAL(vn)); STATE->last_ok = 1; return;
    }
    if (name && strcmp(name, "ASGN_INDIR") == 0) {
        /* $name = val: same folding rule — must hit the same NV key. */
        DESCR_t nd = POP(), val = POP();
        int ok = 0;
        if (IS_NAMEPTR(nd)) {
            *(DESCR_t*)nd.ptr = val; ok = 1;
        } else if (IS_NAMEVAL(nd)) {
            char *fn = GC_strdup(nd.s); sno_fold_name(fn);      /* SN-19 */
            NV_SET_fn(fn, val); ok = 1;
        } else {
            const char *vn0 = VARVAL_fn(nd);
            char *vn = (vn0 && *vn0) ? GC_strdup(vn0) : NULL;
            if (vn) sno_fold_name(vn);                          /* SN-19 */
            if (vn && *vn) { NV_SET_fn(vn, val); ok = 1; }
        }
        PUSH(val); STATE->last_ok = ok; return;
    }
    if (name && strcmp(name, "NRETURN_ASGN") == 0) {
        /* NRETURN lvalue assignment: fname() = rhs
         * Encoding: a[0].s = "NRETURN_ASGN", a[1].s = function name (the
         * sm_lower pass overwrites a[1].s after sm_emit_si set a[1].i=1).
         * Stack: [rhs].  Call zero-param user fn; if it returns DT_N write
         * through the name, else try fname_SET(rhs, result) field mutator.
         * Parity with sm_interp.c:704. */
        const char *fname = CUR_INS->a[1].s;
        DESCR_t rhs = POP();
        DESCR_t fres = INVOKE_fn(fname, NULL, 0);
        int ok = 0;
        if (IS_NAMEPTR(fres)) { NAME_DEREF_PTR(fres) = rhs; ok = 1; }
        else if (IS_NAMEVAL(fres)) {
            char *fn = GC_strdup(fres.s); sno_fold_name(fn);    /* SN-19 */
            NV_SET_fn(fn, rhs); ok = 1;
        }
        else {
            /* Field mutator fallback: fname_SET(rhs, obj) */
            char setname[256];
            snprintf(setname, sizeof(setname), "%s_SET", fname ? fname : "");
            DESCR_t sargs[2] = { rhs, fres };
            DESCR_t sr = INVOKE_fn(setname, sargs, 2);
            ok = (sr.v != DT_FAIL);
        }
        PUSH(rhs); STATE->last_ok = ok; return;
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
        } else {
            /* N-dim (nargs >= 4): sm_lower pushed base first, then indices.
             * Stack top→bot: idx[n-1]...idx[0], base. Pop n items. */
            int n = nargs;
            DESCR_t raw[32];
            for (int k = 0; k < n; k++) raw[k] = POP();
            /* raw[0]=last_idx, raw[n-2]=first_idx, raw[n-1]=base */
            DESCR_t base = raw[n-1];
            DESCR_t fargs[32]; fargs[0] = base;
            for (int k = 0; k < n-1; k++) fargs[k+1] = raw[n-2-k];
            DESCR_t r = INVOKE_fn("ITEM", fargs, n);
            PUSH(r); STATE->last_ok = (r.v != DT_FAIL);
        }
        return;
    }
    if (name && strcmp(name, "IDX_SET") == 0) {
        if (nargs == 3) {
            DESCR_t i = POP(), base = POP(), val = POP();
            STATE->last_ok = subscript_set(base, i, val); PUSH(val);
        } else if (nargs == 4) {
            DESCR_t j = POP(), i = POP(), base = POP(), val = POP();
            STATE->last_ok = subscript_set2(base, i, j, val); PUSH(val);
        } else {
            /* N-dim (nargs >= 5): sm_lower pushed rhs, base, then indices.
             * Stack top→bot: idx[n-1]...idx[0], base, rhs(val). ndim=nargs-2. */
            int ndim = nargs - 2;
            DESCR_t idx[32];
            for (int k = ndim - 1; k >= 0; k--) idx[k] = POP();
            DESCR_t base = POP(), val = POP();
            DESCR_t fargs[32]; fargs[0] = val; fargs[1] = base;
            for (int k = 0; k < ndim; k++) fargs[k+2] = idx[k];
            DESCR_t r = INVOKE_fn("ITEM_SET", fargs, ndim + 2);
            STATE->last_ok = (r.v != DT_FAIL); PUSH(val);
        }
        return;
    }

    DESCR_t args[32];
    if (nargs > 32) nargs = 32;
    for (int k = nargs - 1; k >= 0; k--) args[k] = POP();
    /* SN-9c-d: SN-6 parity with sm_interp.c:799-810.  SNOBOL4 semantics — if
     * any argument is FAIL, the call fails without invoking the function.
     * This is what allows CHARS + SIZE(INPUT) :F(DONE) to branch when INPUT
     * hits EOF: INPUT returns FAILDESCR → SIZE receives it → SIZE would
     * swallow it and return 0, but we catch the FAIL here before the call.
     * Without this, the loop-exit :F branch never fires and fileinfo.sno
     * hangs (accumulator stays at same value forever). */
    for (int k = 0; k < nargs; k++) {
        if (args[k].v == DT_FAIL) {
            PUSH(FAILDESCR);
            STATE->last_ok = 0;
            return;
        }
    }
    /* DATA field accessor/mutator/constructor: give DATA dispatch priority over
     * same-named builtins (e.g. field 'real' vs REAL() builtin). */
    DESCR_t result = FAILDESCR;
    int _data_first = (nargs >= 1 && args[0].v == DT_DATA);
    int _data_set   = (nargs >= 2 && args[1].v == DT_DATA && name &&
                       strlen(name) > 4 &&
                       strcasecmp(name + strlen(name) - 4, "_SET") == 0);
    if (_data_first || _data_set)
        result = sc_dat_field_call(name, args, nargs);
    if (result.v == DT_FAIL || (!_data_first && !_data_set))
        result = INVOKE_fn(name, args, nargs);
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
    g_handlers[SM_PUSH_EXPR]  = h_push_expr;
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
    g_handlers[SM_PAT_ARBNO]   = h_pat_arbno;
    g_handlers[SM_PAT_REM]     = h_pat_rem;
    g_handlers[SM_PAT_BAL]     = h_pat_bal;
    g_handlers[SM_PAT_FENCE]   = h_pat_fence;
    g_handlers[SM_PAT_FENCE1]  = h_pat_fence1;
    g_handlers[SM_PAT_ABORT]   = h_pat_abort;
    g_handlers[SM_PAT_FAIL]    = h_pat_fail;
    g_handlers[SM_PAT_SUCCEED] = h_pat_succeed;
    g_handlers[SM_PAT_EPS]     = h_pat_eps;
    g_handlers[SM_PAT_ALT]     = h_pat_alt;
    g_handlers[SM_PAT_CAT]     = h_pat_cat;
    g_handlers[SM_PAT_DEREF]   = h_pat_deref;
    g_handlers[SM_PAT_REFNAME] = h_pat_refname;
    g_handlers[SM_PAT_CAPTURE]    = h_pat_capture;
    g_handlers[SM_PAT_CAPTURE_FN] = h_pat_capture_fn;
    g_handlers[SM_PAT_CAPTURE_FN_ARGS] = h_pat_capture_fn_args;
    g_handlers[SM_PAT_USERCALL]   = h_pat_usercall;
    g_handlers[SM_PAT_USERCALL_ARGS] = h_pat_usercall_args;
    g_handlers[SM_PAT_BOXVAL]  = h_pat_boxval;

    g_handlers[SM_EXEC_STMT]   = h_exec_stmt;
    g_handlers[SM_CALL]        = h_call;
    g_handlers[SM_RETURN]      = h_return;
    g_handlers[SM_FRETURN]     = h_freturn;
    g_handlers[SM_NRETURN]     = h_nreturn;
    g_handlers[SM_DEFINE]      = h_define;
    g_handlers[SM_INCR]        = h_incr;
    g_handlers[SM_DECR]        = h_decr;

    /* SN-9b: BB broker — Icon (PUMP) and Prolog (ONCE) generator dispatch. */
    g_handlers[SM_BB_PUMP]     = h_bb_pump;
    g_handlers[SM_BB_ONCE]     = h_bb_once;
    /* Opcodes still stubbed as h_unimpl — by design, not by omission:
     *   SM_ACOMP, SM_LCOMP  — emitted by sm_lower for E_EQ/E_NE/E_LT/etc.
     *     (SNOBOL4 numeric/string comparison EKinds) but NEVER actually
     *     generated by the SNOBOL4 frontend.  The Icon frontend generates
     *     these EKinds, but Icon statements bypass sm_lower and go through
     *     icn_runtime.c directly.  sm_interp has no handler either;
     *     adding codegen handlers without sm_interp handlers would be
     *     asymmetric.  Document as dead when the emit paths confirm dead.
     *   SM_JUMP_INDIR     — computed gotos `:($expr)`.  sm_lower emits
     *     this from E_COMPUTED_GOTO, but the SNOBOL4 parser currently
     *     treats computed gotos as undefined labels (Error 24) in all
     *     three modes.  Not a JIT-specific gap; cross-mode issue tracked
     *     outside SN-9.
     *   SM_TRIM, SM_SPCINT, SM_SPREAL, SM_SELBRA, SM_STATE_PUSH,
     *   SM_STATE_POP, SM_RCOMP — never emitted by current sm_lower.
     */
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

/* IM-5: sm_jit_run_steps — run at most n statements then return.
 * Sets up g_jit_step_jmp so the step-limit longjmp lands here safely. */
int sm_jit_run_steps(SM_Program *prog, SM_State *st, int n) {
    g_jit_step_limit = n;
    g_jit_steps_done = 0;
    g_sm_stno_jit    = 0;
    int rc = 0;
    if (setjmp(g_jit_step_jmp) == 0)
        rc = sm_jit_run(prog, st);
    g_jit_step_limit = 0;
    g_jit_steps_done = 0;
    return rc;
}
