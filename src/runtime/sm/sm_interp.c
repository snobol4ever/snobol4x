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

/* ── Stack helpers ──────────────────────────────────────────────────── */

void sm_state_init(SM_State *st)
{
    memset(st, 0, sizeof *st);
    st->sp      = 0;
    st->last_ok = 1;
    st->pc      = 0;
}

void sm_push(SM_State *st, DESCR_t d)
{
    if (st->sp >= SM_STACK_MAX) {
        fprintf(stderr, "sm_interp: stack overflow\n");
        abort();
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
        case SM_EXP: return REALVAL(pow((double)l.i, (double)r.i));
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
            break;

        case SM_PUSH_VAR: {
            const char *name = ins->a[0].s;
            DESCR_t val = NV_GET_fn(name);
            sm_push(st, val);
            break;
        }

        case SM_STORE_VAR: {
            const char *name = ins->a[0].s;
            DESCR_t val = sm_pop(st);
            NV_SET_fn(name, val);
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

        /* ── Pattern and statement ops (stubs — wired in U4) ───────── */

        case SM_PAT_LIT:
        case SM_PAT_ANY:
        case SM_PAT_NOTANY:
        case SM_PAT_SPAN:
        case SM_PAT_BREAK:
        case SM_PAT_LEN:
        case SM_PAT_POS:
        case SM_PAT_RPOS:
        case SM_PAT_TAB:
        case SM_PAT_RTAB:
        case SM_PAT_ARB:
        case SM_PAT_REM:
        case SM_PAT_BAL:
        case SM_PAT_FENCE:
        case SM_PAT_ABORT:
        case SM_PAT_FAIL:
        case SM_PAT_SUCCEED:
        case SM_PAT_ALT:
        case SM_PAT_CAT:
        case SM_PAT_DEREF:
        case SM_PAT_CAPTURE:
        case SM_EXEC_STMT:
            fprintf(stderr, "sm_interp: %s not yet wired (M-SCRIP-U4)\n",
                    sm_opcode_name(ins->op));
            return -1;

        /* ── Functions (stubs — wired in U3) ───────────────────────── */

        case SM_CALL: {
            /* stub: call via INVOKE_fn */
            const char *name  = ins->a[0].s;
            int         nargs = (int)ins->a[1].i;
            DESCR_t args[32];
            /* args on stack: top = last arg */
            for (int k = nargs - 1; k >= 0; k--)
                args[k] = sm_pop(st);
            DESCR_t result = INVOKE_fn(name, args, nargs);
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
