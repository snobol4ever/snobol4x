/*
 * sm_interp.c — SM_Program C interpreter dispatch loop (M-SCRIP-U2)
 *
 * Executes SM_Program instructions one-by-one via a switch dispatch.
 * This is Mode I (--interp) and the correctness reference for Mode G.
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-06
 */

#include "sm_interp.h"
#include "sm_prog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Pattern runtime (M-SCRIP-U4) ──────────────────────────────────────── */
#include "snobol4.h"   /* DESCR_t, PATND_t, DT_* */
#include "sil_macros.h" /* IS_NAMEPTR, NAME_DEREF_PTR, IS_NAMEVAL, etc. */

/* EXPR_t / EKind for SM_PAT_CAPTURE_FN synthetic E_FNC node */
#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"

/* Pattern constructors from snobol4_pattern.c */
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
extern DESCR_t pat_ref(const char *name);         /* deferred *var ref */
extern DESCR_t pat_assign_imm(DESCR_t child, DESCR_t var);
extern DESCR_t pat_assign_cond(DESCR_t child, DESCR_t var);
extern DESCR_t pat_at_cursor(const char *varname);  

/* exec_stmt from stmt_exec.c */
extern int exec_stmt(const char *subj_name, DESCR_t *subj_var,
                     DESCR_t pat, DESCR_t *repl, int has_repl);

/* VARVAL_fn / NV_GET_fn from snobol4.c */
extern char    *VARVAL_fn(DESCR_t d);
extern DESCR_t  NV_GET_fn(const char *name);

/* OE-10: Icon/Prolog BB opcode support */
#include "bb_broker.h"
#include <setjmp.h>
extern bb_node_t icn_eval_gen(EXPR_t *e);   /* scrip.c — builds a drivable bb_node_t */

/* IM-4: SM step-limit for in-process sync monitor */
int      g_sm_step_limit = 0;
int      g_sm_steps_done = 0;
jmp_buf  g_sm_step_jmp;

/* OE-10: body_fn for BB_PUMP — print each generated Icon value to stdout */
static void pump_print(DESCR_t val, void *arg) {
    (void)arg;
    char *s = VARVAL_fn(val);
    if (s) printf("%s\n", s);
}
#define SM_PAT_STACK_INIT 16
static DESCR_t *g_pat_stack = NULL;
static int      g_pat_sp    = 0;
static int      g_pat_cap   = 0;

static void pat_push(DESCR_t d)
{
    if (g_pat_sp >= g_pat_cap) {
        g_pat_cap = g_pat_cap ? g_pat_cap * 2 : SM_PAT_STACK_INIT;
        g_pat_stack = realloc(g_pat_stack, g_pat_cap * sizeof(DESCR_t));
        if (!g_pat_stack) { fprintf(stderr, "sm_interp: pat-stack OOM\n"); abort(); }
    }
    g_pat_stack[g_pat_sp++] = d;
}

static DESCR_t pat_pop(void)
{
    if (g_pat_sp <= 0) {
        fprintf(stderr, "sm_interp: pat-stack underflow\n"); abort();
    }
    return g_pat_stack[--g_pat_sp];
}

/* ── Stack helpers ──────────────────────────────────────────────────── */

#define SM_STACK_INIT 16
void sm_state_init(SM_State *st)
{
    memset(st, 0, sizeof *st);
    st->stack     = malloc(SM_STACK_INIT * sizeof(DESCR_t));
    st->stack_cap = SM_STACK_INIT;
    st->sp        = 0;
    st->last_ok   = 1;
    st->pc        = 0;
}

void sm_state_free(SM_State *st)
{
    free(st->stack);
    st->stack     = NULL;
    st->stack_cap = 0;
    st->sp        = 0;
}

void sm_push(SM_State *st, DESCR_t d)
{
    if (st->sp >= st->stack_cap) {
        st->stack_cap *= 2;
        st->stack = realloc(st->stack, st->stack_cap * sizeof(DESCR_t));
        if (!st->stack) { fprintf(stderr, "sm_interp: out of memory\n"); abort(); }
    }
    st->stack[st->sp++] = d;
}

DESCR_t sm_pop(SM_State *st)
{
    if (st->sp <= 0) {
        fprintf(stderr, "sm_interp: stack underflow\n");
        abort();
    }
    return st->stack[--st->sp];
}

DESCR_t sm_peek(SM_State *st)
{
    if (st->sp <= 0) {
        fprintf(stderr, "sm_interp: peek on empty stack\n");
        abort();
    }
    return st->stack[st->sp - 1];
}

/* ── Arithmetic helpers ─────────────────────────────────────────────── */

static DESCR_t sm_arith(DESCR_t l, DESCR_t r, sm_opcode_t op)
{
    /* Prefer integer if both are integer */
    if (l.v == DT_I && r.v == DT_I) {
        switch (op) {
        case SM_ADD: return INTVAL(l.i + r.i);
        case SM_SUB: return INTVAL(l.i - r.i);
        case SM_MUL: return INTVAL(l.i * r.i);
        case SM_DIV:
            if (r.i == 0) { fprintf(stderr, "Error 2: division by zero\n"); return FAILDESCR; }
            return INTVAL(l.i / r.i);
        case SM_EXP: {
            /* integer ** non-negative integer → integer if result fits */
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
    double ld = (l.v == DT_I) ? (double)l.i : l.r;
    double rd = (r.v == DT_I) ? (double)r.i : r.r;
    switch (op) {
    case SM_ADD: return REALVAL(ld + rd);
    case SM_SUB: return REALVAL(ld - rd);
    case SM_MUL: return REALVAL(ld * rd);
    case SM_DIV:
        if (rd == 0.0) { fprintf(stderr, "Error 2: division by zero\n"); return FAILDESCR; }
        return REALVAL(ld / rd);
    case SM_EXP: return REALVAL(pow(ld, rd));
    default: break;
    }
    return FAILDESCR;
}

/* ── Main dispatch loop ─────────────────────────────────────────────── */

int sm_interp_run(SM_Program *prog, SM_State *st)
{
    while (st->pc < prog->count) {
        SM_Instr *ins = &prog->instrs[st->pc];
        st->pc++;

        switch (ins->op) {

        /* ── Control ───────────────────────────────────────────────── */

        case SM_LABEL:
            /* no-op at runtime — label is a marker for the builder */
            break;

        case SM_HALT:
            return 0;

        case SM_STNO: {
            extern void comm_stno(int n);
            static int g_sm_stno = 0;
            comm_stno(++g_sm_stno);
            st->sp = 0;   /* reset value stack at each statement boundary */
            /* IM-4: step-limit */
            if (g_sm_step_limit > 0 && g_sm_steps_done++ >= g_sm_step_limit)
                longjmp(g_sm_step_jmp, 1);
            break;
        }

        case SM_JUMP:
            st->pc = (int)ins->a[0].i;
            break;

        case SM_JUMP_S:
            if (st->last_ok) st->pc = (int)ins->a[0].i;
            break;

        case SM_JUMP_F:
            if (!st->last_ok) st->pc = (int)ins->a[0].i;
            break;

        /* ── Values ────────────────────────────────────────────────── */

        case SM_PUSH_LIT_S: {
            const char *s = ins->a[0].s ? ins->a[0].s : "";
            int64_t     n = ins->a[1].i;  /* explicit byte length; 0 = use strlen */
            DESCR_t d;
            d.v    = DT_S;
            d.s    = (char *)s;
            d.slen = (n > 0) ? (uint32_t)n : 0;
            sm_push(st, d);
            break;
        }

        case SM_PUSH_LIT_I:
            sm_push(st, INTVAL(ins->a[0].i));
            break;

        case SM_PUSH_LIT_F:
            sm_push(st, REALVAL(ins->a[0].f));
            break;

        case SM_PUSH_NULL:
            sm_push(st, NULVCL);
            st->last_ok = 1;    /* null is a valid (non-fail) result */
            break;

        case SM_PUSH_VAR: {
            const char *name = ins->a[0].s;
            DESCR_t val = NV_GET_fn(name);
            sm_push(st, val);
            break;
        }

        case SM_PUSH_EXPR: {
            /* Push a frozen DT_E expression descriptor (for *expr / EVAL()) */
            DESCR_t d;
            d.v    = DT_E;
            d.slen = 0;
            d.ptr  = ins->a[0].ptr;   /* ptr and s share a union — set ptr last */
            sm_push(st, d);
            st->last_ok = 1;
            break;
        }

        case SM_STORE_VAR: {
            const char *name = ins->a[0].s;
            DESCR_t val = sm_pop(st);
            /* SNOBOL4 semantics: if RHS evaluated to FAIL, the statement fails
             * and no assignment occurs. Without this guard, OUTPUT = DIFFER(X,Y)
             * would call output_val(FAILDESCR) and print a spurious blank line. */
            if (val.v == DT_FAIL) {
                st->last_ok = 0;
                break;
            }
            /* RT-5: NV_SET_fn returns the assigned value — push it so that
             * embedded assignment X = (A = B) leaves B's value on stack for X. */
            DESCR_t stored = NV_SET_fn(name, val);
            sm_push(st, stored);
            /* leave last_ok unchanged — assignment always succeeds */
            break;
        }

        case SM_POP:
            sm_pop(st);
            break;

        /* ── Arithmetic / String ───────────────────────────────────── */

        case SM_ADD:
        case SM_SUB:
        case SM_MUL:
        case SM_DIV:
        case SM_EXP: {
            DESCR_t r = sm_pop(st);
            DESCR_t l = sm_pop(st);
            /* coerce strings to numeric if needed */
            if (l.v == DT_S) l = INTVAL(to_int(l));
            if (r.v == DT_S) r = INTVAL(to_int(r));
            DESCR_t result = sm_arith(l, r, ins->op);
            sm_push(st, result);
            st->last_ok = (result.v != DT_FAIL);
            break;
        }

        case SM_NEG: {
            DESCR_t v = sm_pop(st);
            if (v.v == DT_I) sm_push(st, INTVAL(-v.i));
            else              sm_push(st, REALVAL(-to_real(v)));
            break;
        }

        case SM_CONCAT: {
            DESCR_t r = sm_pop(st);
            DESCR_t l = sm_pop(st);
            DESCR_t result = CONCAT_fn(l, r);
            sm_push(st, result);
            st->last_ok = (result.v != DT_FAIL);
            break;
        }

        case SM_COERCE_NUM: {
            /* unary +: coerce string to int (or real if not integer) */
            DESCR_t v = sm_pop(st);
            if (v.v == DT_S) {
                int64_t iv = to_int(v);
                if (iv != 0 || (v.s && v.s[0] == '0')) { sm_push(st, INTVAL(iv)); }
                else { double rv = to_real(v); sm_push(st, REALVAL(rv)); }
            } else { sm_push(st, v); }
            st->last_ok = 1;
            break;
        }

        /* ── Pattern construction ops (M-SCRIP-U4) ─────────────────── */

        case SM_PAT_LIT: {
            /* a[0].s = literal string */
            pat_push(pat_lit(ins->a[0].s ? ins->a[0].s : ""));
            break;
        }
        case SM_PAT_ANY: {
            /* arg on value stack: charset string */
            DESCR_t arg = sm_pop(st);
            const char *cs = VARVAL_fn(arg);
            pat_push(pat_any_cs(cs ? cs : ""));
            break;
        }
        case SM_PAT_NOTANY: {
            DESCR_t arg = sm_pop(st);
            const char *cs = VARVAL_fn(arg);
            pat_push(pat_notany(cs ? cs : ""));
            break;
        }
        case SM_PAT_SPAN: {
            DESCR_t arg = sm_pop(st);
            const char *cs = VARVAL_fn(arg);
            pat_push(pat_span(cs ? cs : ""));
            break;
        }
        case SM_PAT_BREAK: {
            DESCR_t arg = sm_pop(st);
            const char *cs = VARVAL_fn(arg);
            pat_push(pat_break_(cs ? cs : ""));
            break;
        }
        case SM_PAT_LEN: {
            DESCR_t arg = sm_pop(st);
            int64_t n = (arg.v == DT_I) ? arg.i : 0;
            pat_push(pat_len(n));
            break;
        }
        case SM_PAT_POS: {
            DESCR_t arg = sm_pop(st);
            int64_t n = (arg.v == DT_I) ? arg.i : 0;
            pat_push(pat_pos(n));
            break;
        }
        case SM_PAT_RPOS: {
            DESCR_t arg = sm_pop(st);
            int64_t n = (arg.v == DT_I) ? arg.i : 0;
            pat_push(pat_rpos(n));
            break;
        }
        case SM_PAT_TAB: {
            DESCR_t arg = sm_pop(st);
            int64_t n = (arg.v == DT_I) ? arg.i : 0;
            pat_push(pat_tab(n));
            break;
        }
        case SM_PAT_RTAB: {
            DESCR_t arg = sm_pop(st);
            int64_t n = (arg.v == DT_I) ? arg.i : 0;
            pat_push(pat_rtab(n));
            break;
        }
        case SM_PAT_ARB:     pat_push(pat_arb());     break;
        case SM_PAT_REM:     pat_push(pat_rem());     break;
        case SM_PAT_FAIL:    pat_push(pat_fail());    break;
        case SM_PAT_SUCCEED: pat_push(pat_succeed()); break;
        case SM_PAT_EPS:     pat_push(pat_epsilon()); break;
        case SM_PAT_FENCE:   pat_push(pat_fence());   break;
        case SM_PAT_ABORT:   pat_push(pat_abort());   break;
        case SM_PAT_BAL:     pat_push(pat_bal());     break;

        case SM_PAT_CAT: {
            /* pop right then left (left was pushed first) */
            DESCR_t right = pat_pop();
            DESCR_t left  = pat_pop();
            pat_push(pat_cat(left, right));
            break;
        }
        case SM_PAT_ALT: {
            DESCR_t right = pat_pop();
            DESCR_t left  = pat_pop();
            pat_push(pat_alt(left, right));
            break;
        }
        case SM_PAT_BOXVAL:
            /* pop pat-stack top, push as DT_P onto value-stack */
            sm_push(st, pat_pop());
            break;
        case SM_PAT_DEREF: {
            DESCR_t v = sm_pop(st);
            if (v.v == DT_P) {
                pat_push(v);                        /* already a pattern */
            } else if (v.v == DT_S && v.s) {
                pat_push(pat_lit(v.s));             /* string → literal */
            } else {
                /* variable name or other — deferred ref */
                const char *name = VARVAL_fn(v);
                pat_push(pat_ref(name ? name : ""));
            }
            break;
        }
        case SM_PAT_CAPTURE: {
            /* a[0].s = variable name; a[1].i = 0=cond 1=imm 2=cursor
             * pat_assign_cond/imm expects a NAME descriptor (DT_N). */
            DESCR_t child = pat_pop();
            const char *vname = ins->a[0].s ? ins->a[0].s : "";
            DESCR_t var = NAME_fn(vname);
            int kind = (int)ins->a[1].i;
            if (kind == 1)
                pat_push(pat_assign_imm(child, var));
            else if (kind == 2)
                pat_push(pat_cat(child, pat_at_cursor(vname)));  /* cursor: seq child then @var */
            else
                pat_push(pat_assign_cond(child, var));
            break;
        }

        case SM_PAT_CAPTURE_FN: {
            /* . *func() or $ *func() — a[0].s = function name, a[1].i = 0(cond)/1(imm).
             * Use pat_assign_callcap → XCALLCAP node, handled by bb_build/bb_callcap
             * in the byrd-box path.  bb_callcap calls g_user_call_hook(fname, args, 0)
             * at match time (deferred for '.', immediate for '$'), which fires the
             * SNOBOL4 function as a side effect.
             * The old DT_E/pat_assign_cond approach only worked via the snobol4_pattern.c
             * materialise() path, which --sm-run does not use. */
            DESCR_t child = pat_pop();
            const char *fname = ins->a[0].s ? ins->a[0].s : "";
            pat_push(pat_assign_callcap(child, fname, NULL, 0));
            break;
        }

        case SM_EXEC_STMT: {
            /*
             * Stack at entry (top-of-stack = last pushed):
             *   [subj_descr] [pat_on_pat_stack] [repl_or_zero]
             *   a[0].s  = subject variable name (or NULL)
             *   a[1].i  = has_repl flag
             *
             * The subject was pushed onto the value stack by sm_lower.
             * The pattern was built on g_pat_stack by SM_PAT_* ops.
             * The replacement (or INTVAL(0)) is on top of the value stack.
             */
            int has_repl = (int)ins->a[1].i;
            DESCR_t repl   = sm_pop(st);    /* replacement or INTVAL(0) */
            DESCR_t subj_d = sm_pop(st);    /* subject descriptor */
            DESCR_t pat_d  = (g_pat_sp > 0) ? pat_pop() : pat_epsilon();

            const char *sname = ins->a[0].s;   /* subject var name for write-back */

            int ok = exec_stmt(sname, &subj_d, pat_d,
                               has_repl ? &repl : NULL, has_repl);
            st->last_ok = ok;
            g_pat_sp = 0;   /* reset pat-stack after each statement */
            break;
        }

        /* ── OE-10/11: Byrd box broker opcodes — Icon/Prolog SM-run support ── */
        case SM_BB_PUMP: {
            /* Pop DT_E descriptor whose .ptr is the EXPR_t* of the Icon statement subject.
             * Build a drivable bb_node_t via icn_eval_gen, pump all values via bb_broker. */
            DESCR_t expr_d = sm_pop(st);
            EXPR_t *expr   = (EXPR_t *)expr_d.ptr;
            if (!expr) { st->last_ok = 0; break; }
            bb_node_t node = icn_eval_gen(expr);
            int ticks = bb_broker(node, BB_PUMP, pump_print, NULL);
            st->last_ok = (ticks > 0);
            break;
        }

        case SM_BB_ONCE: {
            /* Pop DT_E descriptor whose .ptr is the EXPR_t* of the Prolog statement subject.
             * Build a bb_node_t via icn_eval_gen (shared builder handles E_CHOICE/E_CLAUSE),
             * drive once via bb_broker(BB_ONCE). */
            DESCR_t expr_d = sm_pop(st);
            EXPR_t *expr   = (EXPR_t *)expr_d.ptr;
            if (!expr) { st->last_ok = 0; break; }
            bb_node_t node = icn_eval_gen(expr);
            int ticks = bb_broker(node, BB_ONCE, NULL, NULL);
            st->last_ok = (ticks > 0);
            break;
        }

        /* ── Functions (stubs — wired in U3) ───────────────────────── */

        case SM_CALL: {
            const char *name  = ins->a[0].s;
            int         nargs = (int)ins->a[1].i;

            /* Special pseudo-calls handled inline */
            if (name && strcmp(name, "INDIR_GET") == 0) {
                /* $expr: pop descriptor from value stack, look up variable, push its value.
                 * Three cases:
                 *   DT_S "bal"  → $'bal' : look up variable named by string
                 *   DT_N NAMEVAL("bal") → $.bal path: name-of-bal; $ deref = value of bal
                 *   DT_N NAMEPTR(p)     → $ on interior ptr: deref pointer directly
                 */
                DESCR_t name_d = sm_pop(st);
                DESCR_t val;
                if (IS_NAMEPTR(name_d)) {
                    val = NAME_DEREF_PTR(name_d);   /* interior ptr → value directly */
                } else if (IS_NAMEVAL(name_d)) {
                    val = NV_GET_fn(name_d.s);       /* name string → value of that var */
                } else {
                    const char *vname = VARVAL_fn(name_d);
                    val = (vname && *vname) ? NV_GET_fn(vname) : NULVCL;
                }
                sm_push(st, val);
                st->last_ok = 1;
                break;
            }
            if (name && strcmp(name, "NAME_PUSH") == 0) {
                /* .X: pop name string, push DT_N NAMEVAL descriptor.
                 * Use NAMEVAL (slen=0, name in .s) so VARVAL_fn returns the
                 * name string correctly. NAMEPTR (interior ptr) would cause
                 * VARVAL_fn to do NV_name_from_ptr which fails for names not
                 * yet in the NV table (e.g. OPSYN(.facto,'fact') before facto
                 * is ever read/written). */
                DESCR_t name_d = sm_pop(st);
                const char *vname = VARVAL_fn(name_d);
                sm_push(st, NAMEVAL(GC_strdup(vname ? vname : "")));
                st->last_ok = 1;
                break;
            }
            if (name && strcmp(name, "ASGN_INDIR") == 0) {
                DESCR_t name_d = sm_pop(st);
                DESCR_t val    = sm_pop(st);
                int ok = 0;
                if (IS_NAMEPTR(name_d)) {
                    /* $(.var) — write through name pointer directly */
                    *(DESCR_t*)name_d.ptr = val; ok = 1;
                } else if (IS_NAMEVAL(name_d)) {
                    NV_SET_fn(name_d.s, val); ok = 1;
                } else {
                    const char *vname = VARVAL_fn(name_d);
                    if (vname && *vname) { NV_SET_fn(vname, val); ok = 1; }
                    /* else: empty/null name — fail the statement */
                }
                sm_push(st, val);
                st->last_ok = ok;
                break;
            }
            if (name && strcmp(name, "NRETURN_ASGN") == 0) {
                /* NRETURN lvalue assignment: fname() = rhs
                 * a[0].i = nargs (1), a[1].s = function name
                 * Stack: [rhs]  (1 item, already popped via nargs loop below)
                 * Call zero-param user fn; if it returns DT_N write through name,
                 * else try fname_SET(rhs, result) field-mutator convention. */
                const char *fname = ins->a[1].s;
                DESCR_t rhs = sm_pop(st);
                DESCR_t fres = INVOKE_fn(fname, NULL, 0);
                int ok = 0;
                if (IS_NAMEPTR(fres)) { NAME_DEREF_PTR(fres) = rhs; ok = 1; }
                else if (IS_NAMEVAL(fres)) { NV_SET_fn(fres.s, rhs); ok = 1; }
                else {
                    /* Field mutator fallback: fname_SET(rhs, obj) */
                    char setname[256];
                    snprintf(setname, sizeof(setname), "%s_SET", fname ? fname : "");
                    DESCR_t sargs[2] = { rhs, fres };
                    DESCR_t sr = INVOKE_fn(setname, sargs, 2);
                    ok = (sr.v != DT_FAIL);
                }
                sm_push(st, rhs);
                st->last_ok = ok;
                break;
            }
            if (name && strcmp(name, "IDX") == 0) {
                /* subscript read: stack top=last_idx ... base; nargs=nchildren */
                if (nargs == 2) {
                    DESCR_t idx  = sm_pop(st);
                    DESCR_t base = sm_pop(st);
                    DESCR_t r = subscript_get(base, idx);
                    sm_push(st, r);
                    st->last_ok = (r.v != DT_FAIL);
                } else if (nargs == 3) {
                    DESCR_t j    = sm_pop(st); DESCR_t i = sm_pop(st);
                    DESCR_t base = sm_pop(st);
                    DESCR_t r = subscript_get2(base, i, j);
                    sm_push(st, r);
                    st->last_ok = (r.v != DT_FAIL);
                } else { sm_push(st, FAILDESCR); st->last_ok = 0; }
                break;
            }
            if (name && strcmp(name, "IDX_SET") == 0) {
                /* sm_lower emits: rhs, then base, then i [, j]
                 * Stack top-to-bottom at IDX_SET: i [j], base, rhs
                 * Pop: indices first (top), then base, then rhs (bottom). */
                if (nargs == 3) {        /* 1D: children=2, nc+1=3 */
                    DESCR_t i    = sm_pop(st);
                    DESCR_t base = sm_pop(st);
                    DESCR_t val  = sm_pop(st);
                    st->last_ok = subscript_set(base, i, val);
                    sm_push(st, val);
                } else if (nargs == 4) { /* 2D: children=3, nc+1=4 */
                    DESCR_t j    = sm_pop(st); DESCR_t i = sm_pop(st);
                    DESCR_t base = sm_pop(st);
                    DESCR_t val  = sm_pop(st);
                    st->last_ok = subscript_set2(base, i, j, val);
                    sm_push(st, val);
                } else { st->last_ok = 0; }
                break;
            }

            DESCR_t args[32];
            for (int k = nargs - 1; k >= 0; k--)
                args[k] = sm_pop(st);
            DESCR_t result = INVOKE_fn(name, args, nargs);
            /* NRETURN: user fn returned DT_N — dereference like tree-walk E_FNC */
            if (IS_NAMEPTR(result))      result = NAME_DEREF_PTR(result);
            else if (IS_NAMEVAL(result)) result = NV_GET_fn(result.s);
            sm_push(st, result);
            st->last_ok = (result.v != DT_FAIL);
            break;
        }

        case SM_RETURN:
        case SM_FRETURN:
            /* stub: return from top-level program = halt */
            return 0;

        case SM_DEFINE:
            /* stub: function definition handled by preprocessor */
            break;

        case SM_INCR: {
            DESCR_t v = sm_pop(st);
            sm_push(st, INTVAL(v.i + ins->a[0].i));
            break;
        }

        case SM_DECR: {
            DESCR_t v = sm_pop(st);
            sm_push(st, INTVAL(v.i - ins->a[0].i));
            break;
        }

        default:
            fprintf(stderr, "sm_interp: unhandled opcode %d (%s) at pc=%d\n",
                    (int)ins->op, sm_opcode_name(ins->op), st->pc - 1);
            return -1;
        }
    }
    return 0;  /* fell off end = implicit HALT */
}

/* IM-4: sm_interp_run_steps — run at most n statements then return */
int sm_interp_run_steps(SM_Program *prog, SM_State *st, int n) {
    g_sm_step_limit = n;
    g_sm_steps_done = 0;
    int rc = 0;
    if (setjmp(g_sm_step_jmp) == 0)
        rc = sm_interp_run(prog, st);
    g_sm_step_limit = 0;
    g_sm_steps_done = 0;
    return rc;
}
