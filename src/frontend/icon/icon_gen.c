/*============================================================================================================================
 * icon_gen.c — Icon Value-Generator Byrd Box Implementations (GOAL-ICN-BROKER, GOAL-UNIFIED-BROKER U-7/U-10)
 *
 * U-7:  icn_gen_t → bb_node_t; icn_box_fn → bb_box_fn throughout.
 * U-10: icn_broker removed — all call sites use bb_broker(..., BB_PUMP, ...) directly.
 *
 * Architecture mirrors SNOBOL4's exec_stmt Phase 3 broker loop (stmt_exec.c):
 *   Phase 3 (SNOBOL4):  root.fn(ζ,α) → body → root.fn(ζ,β) → … → ω
 *   Icon generators:    gen.fn(ζ,α)   → body → gen.fn(ζ,β)  → … → ω  (bb_broker BB_PUMP)
 *
 * Value type: DESCR_t (not spec_t).  Failure sentinel: FAILDESCR / IS_FAIL_fn().
 *============================================================================================================================*/

#include "icon_gen.h"
#include "../../ir/ir.h"            /* EXPR_t, EKind, E_TO, E_TO_BY, E_ITERATE, E_SUSPEND, E_FNC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

/*============================================================================================================================
 * B-3: icn_bb_to — E_TO Byrd box  (i to j)
 *
 * State: lo, hi, cur.
 *   α: cur = lo; if cur > hi → ω; else return integer cur (γ).
 *   β: cur++; if cur > hi → ω; else return integer cur (γ).
 *============================================================================================================================*/

DESCR_t icn_bb_to(void *zeta, int entry) {
    icn_to_state_t *z = (icn_to_state_t *)zeta;
    if (entry == α) z->cur = z->lo;
    else            z->cur++;
    if (z->cur > z->hi) return FAILDESCR;
    return (DESCR_t){ .v = DT_I, .i = z->cur };
}

/*============================================================================================================================
 * icn_bb_to_nested — (lo_gen) to (hi_gen) cross-product Byrd box
 *
 * JCON irgen.icn ir_a_To nested case: when lo or hi is itself a generator,
 * pre-collect all values from each, then iterate outer lo × hi pairs,
 * yielding each inner lo_val..hi_val range in sequence.
 *
 * State pre-populated by icn_eval_gen before returning this box.
 * α: li=0, hi2=0, cur=lo_vals[0]; step through inner range.
 * β: cur++; if cur > hi_vals[hi2]: hi2++; if hi2 >= nhi: li++, hi2=0; reset cur.
 * ω: li >= nlo.
 *============================================================================================================================*/

DESCR_t icn_bb_to_nested(void *zeta, int entry) {
    icn_to_nested_state_t *z = (icn_to_nested_state_t *)zeta;
    if (z->nlo == 0 || z->nhi == 0) return FAILDESCR;
    if (entry == α) { z->li = 0; z->hi2 = 0; z->cur = z->lo_vals[0]; }
    else            { z->cur++; }
    /* Advance outer indices when inner range is exhausted */
    for (;;) {
        if (z->li >= z->nlo) return FAILDESCR;
        long hi_bound = z->hi_vals[z->hi2];
        if (z->cur <= hi_bound) return (DESCR_t){ .v = DT_I, .i = z->cur };
        /* exhausted this (li, hi2) pair — advance hi2, then li */
        z->hi2++;
        if (z->hi2 >= z->nhi) { z->li++; z->hi2 = 0; }
        if (z->li >= z->nlo) return FAILDESCR;
        z->cur = z->lo_vals[z->li];
    }
}

/*============================================================================================================================
 * B-4: icn_bb_to_by — E_TO_BY Byrd box  (i to j by k)
 *
 * State: lo, hi, step, cur.
 *   α: cur = lo.
 *   β: cur += step.
 *   if step > 0: cur > hi → ω.   if step < 0: cur < hi → ω.
 *============================================================================================================================*/

DESCR_t icn_bb_to_by(void *zeta, int entry) {
    icn_to_by_state_t *z = (icn_to_by_state_t *)zeta;
    if (entry == α) z->cur = z->lo;
    else            z->cur += z->step;
    long step = z->step ? z->step : 1;
    if (step > 0 && z->cur > z->hi) return FAILDESCR;
    if (step < 0 && z->cur < z->hi) return FAILDESCR;
    return (DESCR_t){ .v = DT_I, .i = z->cur };
}

/* icn_bb_to_by_real — E_TO_BY with real (float) step/bounds */
DESCR_t icn_bb_to_by_real(void *zeta, int entry) {
    icn_to_by_real_state_t *z = (icn_to_by_real_state_t *)zeta;
    if (entry == α) z->cur = z->lo;
    else            z->cur += z->step;
    double step = z->step != 0.0 ? z->step : 1.0;
    if (step > 0.0 && z->cur > z->hi + 1e-10) return FAILDESCR;
    if (step < 0.0 && z->cur < z->hi - 1e-10) return FAILDESCR;
    return (DESCR_t){ .v = DT_R, .r = z->cur };
}

/*============================================================================================================================
 * B-5: icn_bb_iterate — E_ITERATE Byrd box  (!str, Icon char iteration)
 *
 * State: str, len, pos.
 *   α: pos = 0.  β: pos++.  ω: pos >= len.  γ: single-char string at pos.
 *============================================================================================================================*/

DESCR_t icn_bb_iterate(void *zeta, int entry) {
    icn_iterate_state_t *z = (icn_iterate_state_t *)zeta;
    if (entry == α) z->pos = 0;
    else            z->pos++;
    if (z->pos >= z->len) return FAILDESCR;
    z->ch[0] = z->str[z->pos];
    z->ch[1] = '\0';
    return (DESCR_t){ .v = DT_S, .slen = 1, .s = z->ch };
}

/*============================================================================================================================
 * B-5b: icn_bb_tbl_iterate — E_ITERATE Byrd box for DT_T tables  (!T yields values)
 *
 * State: tbl, bucket (0..TABLE_BUCKETS-1), entry (current TBPAIR_t*).
 *   α: bucket=0, entry=tbl->buckets[0].
 *   β: advance to next entry (or next non-empty bucket).
 *   ω: all buckets exhausted.
 *   γ: return entry->val.
 *============================================================================================================================*/

DESCR_t icn_bb_tbl_iterate(void *zeta, int entry) {
    icn_tbl_iterate_state_t *z = (icn_tbl_iterate_state_t *)zeta;
    if (!z->tbl) return FAILDESCR;
    if (entry == α) { z->bucket = 0; z->entry = z->tbl->buckets[0]; }
    else if (z->entry) { z->entry = z->entry->next; }
    /* advance past empty buckets */
    while (!z->entry && z->bucket < TABLE_BUCKETS - 1) {
        z->bucket++;
        z->entry = z->tbl->buckets[z->bucket];
    }
    if (!z->entry) return FAILDESCR;
    return z->entry->val;
}

/*============================================================================================================================
 * IC-5: icn_bb_list_iterate — E_ITERATE Byrd box for DT_DATA icnlist  (!L yields elements)
 *   α: reset pos=0, return elems[0].
 *   β: advance pos, return elems[pos].
 *   ω: pos >= n.
 *============================================================================================================================*/
DESCR_t icn_bb_list_iterate(void *zeta, int entry) {
    icn_list_iterate_state_t *z = (icn_list_iterate_state_t *)zeta;
    if (!z->elems || z->n <= 0) return FAILDESCR;
    if (entry == α) z->pos = 0;
    else            z->pos++;
    if (z->pos >= z->n) return FAILDESCR;
    return z->elems[z->pos];
}



/*============================================================================================================================
 * B-6: icn_bb_suspend — E_SUSPEND Byrd box (coroutine wrapper)
 *
 * Wraps existing ucontext coroutine machinery from scrip.c.
 * The zeta pointer carries an opaque Icn_coro_entry* already set up by the caller.
 * α: start (fresh call) — swapcontext into coroutine.
 * β: resume — swapcontext into coroutine again.
 * ω: coroutine set exhausted=1.
 *
 * This box does NOT own the coroutine — icn_eval_gen (B-8) wires it up.
 * The zeta is cast to icn_suspend_state_t which the broker caller populates.
 *============================================================================================================================*/

DESCR_t icn_bb_suspend(void *zeta, int entry) {
    icn_suspend_state_t *z = (icn_suspend_state_t *)zeta;
    /* exhausted+yielded_returned: truly done, return ω */
    if (z->exhausted && z->yielded_returned) return FAILDESCR;
    if (entry == α && !z->started) {
        /* First α: set up and enter the coroutine */
        z->started = 1;
        getcontext(&z->gen_ctx);
        z->gen_ctx.uc_stack.ss_sp   = z->stack;
        z->gen_ctx.uc_stack.ss_size = 256 * 1024;
        z->gen_ctx.uc_link          = NULL;
        /* RK-21: if using gather trampoline, pass ss via the static staging pointer
         * (makecontext cannot pass pointer args portably on x86-64). */
        if (z->trampoline == icn_gather_trampoline)
            icn_gather_trampoline_ss = z;
        makecontext(&z->gen_ctx, z->trampoline, 0);
        swapcontext(&z->caller_ctx, &z->gen_ctx);
    } else if (!z->exhausted) {
        /* β or α-after-started: resume only if not exhausted */
        swapcontext(&z->caller_ctx, &z->gen_ctx);
    } else {
        /* exhausted but not yet returned — fall through to return yielded */
    }
    if (z->exhausted) {
        if (IS_FAIL_fn(z->yielded)) return FAILDESCR;   /* proc failed — ω immediately */
        z->yielded_returned = 1;
        return z->yielded;
    }
    return z->yielded;
}

/*============================================================================================================================
 * B-7: icn_bb_find — find() generator Byrd box
 *
 * State: needle, haystack, pos (byte offset into haystack, 0-based).
 *   α: pos = 0, find first match.
 *   β: advance past last match, find next.
 *   returns 1-based position of match, or ω.
 *============================================================================================================================*/

DESCR_t icn_bb_find(void *zeta, int entry) {
    icn_find_state_t *z = (icn_find_state_t *)zeta;
    if (entry == α) z->next = z->hay;
    const char *hit = strstr(z->next, z->needle);
    if (!hit) return FAILDESCR;
    long pos1 = (long)(hit - z->hay) + 1;   /* 1-based */
    z->next = hit + (z->nlen > 0 ? z->nlen : 1);
    return (DESCR_t){ .v = DT_I, .i = pos1 };
}

/*============================================================================================================================
 * icn_bb_binop_gen — IC-2a: generative binary operator Byrd box
 *
 * Protocol (JCON irgen.icn §4.3, funcs-set):
 *   α: pump left α → get left_val; pump right α → get right_val; apply op.
 *   β (arithmetic):  resume right β; if right ω → resume left β, reset right α.
 *   β (relational):  resume right β; if right ω → resume left β, reset right α.
 *   On relational comparison failure: retry right β (goal-directed).
 *
 * left/right are bb_node_t generators (may be oneshot wrappers for scalar operands).
 *============================================================================================================================*/

static DESCR_t icn_binop_apply(IcnBinopKind op, DESCR_t lv, DESCR_t rv, int *rel_fail) {
    *rel_fail = 0;
    if (IS_FAIL_fn(lv) || IS_FAIL_fn(rv)) return FAILDESCR;
    long li = IS_INT_fn(lv) ? lv.i : (long)lv.r;
    long ri = IS_INT_fn(rv) ? rv.i : (long)rv.r;
    switch (op) {
        case ICN_BINOP_ADD:    return INTVAL(li + ri);
        case ICN_BINOP_SUB:    return INTVAL(li - ri);
        case ICN_BINOP_MUL:    return INTVAL(li * ri);
        case ICN_BINOP_DIV:    return ri ? INTVAL(li / ri) : FAILDESCR;
        case ICN_BINOP_MOD:    return ri ? INTVAL(li % ri) : FAILDESCR;
        case ICN_BINOP_LT:     *rel_fail = !(li <  ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_LE:     *rel_fail = !(li <= ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_GT:     *rel_fail = !(li >  ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_GE:     *rel_fail = !(li >= ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_EQ:     *rel_fail = !(li == ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_NE:     *rel_fail = !(li != ri); return *rel_fail ? FAILDESCR : INTVAL(ri);
        case ICN_BINOP_CONCAT: {
            /* String concatenation — use static buffer (adequate for test suite) */
            const char *ls = lv.s ? lv.s : "", *rs = rv.s ? rv.s : "";
            size_t ll = lv.slen > 0 ? (size_t)lv.slen : strlen(ls);
            size_t rl = rv.slen > 0 ? (size_t)rv.slen : strlen(rs);
            char *buf = malloc(ll + rl + 1);
            memcpy(buf, ls, ll); memcpy(buf + ll, rs, rl); buf[ll + rl] = '\0';
            return (DESCR_t){ .v = DT_S, .slen = (int)(ll + rl), .s = buf };
        }
        default: return FAILDESCR;
    }
}

DESCR_t icn_bb_binop_gen(void *zeta, int entry) {
    icn_binop_gen_state_t *z = (icn_binop_gen_state_t *)zeta;

    if (entry == α) {
        /* Fresh: pump left α, then right α */
        z->left_val  = z->left.fn(z->left.ζ, α);
        if (IS_FAIL_fn(z->left_val)) return FAILDESCR;
        z->right_val = z->right.fn(z->right.ζ, α);
        if (IS_FAIL_fn(z->right_val)) return FAILDESCR;
        z->phase = 2;
    } else {
        /* β: try to advance right first */
        for (;;) {
            DESCR_t rv = z->right.fn(z->right.ζ, β);
            if (!IS_FAIL_fn(rv)) { z->right_val = rv; break; }
            /* right exhausted — advance left, reset right */
            DESCR_t lv = z->left.fn(z->left.ζ, β);
            if (IS_FAIL_fn(lv)) return FAILDESCR;   /* both exhausted */
            z->left_val  = lv;
            z->right_val = z->right.fn(z->right.ζ, α);
            if (!IS_FAIL_fn(z->right_val)) break;
            /* right empty on reset — try next left */
        }
    }

    /* Apply op; for relational failure, retry right (goal-directed) */
    for (;;) {
        int rel_fail = 0;
        DESCR_t result = icn_binop_apply(z->op, z->left_val, z->right_val, &rel_fail);
        if (!IS_FAIL_fn(result)) return result;
        if (!rel_fail) return FAILDESCR;   /* arithmetic error (div by zero etc.) */
        /* Relational failure: retry right β (JCON §4.3 goal-directed) */
        DESCR_t rv = z->right.fn(z->right.ζ, β);
        if (!IS_FAIL_fn(rv)) { z->right_val = rv; continue; }
        /* right exhausted — advance left, reset right */
        DESCR_t lv = z->left.fn(z->left.ζ, β);
        if (IS_FAIL_fn(lv)) return FAILDESCR;
        z->left_val  = lv;
        z->right_val = z->right.fn(z->right.ζ, α);
        if (IS_FAIL_fn(z->right_val)) return FAILDESCR;
    }
}

/*============================================================================================================================
 * icn_bb_alternate — IC-2a: E_ALTERNATE Byrd box
 *
 * JCON irgen.icn ir_a_Alt (binary case):
 *   α: pump gen[0] α; if γ → return. If ω → switch which=1, pump gen[1] α.
 *   β: pump current gen β; if ω and which==0 → switch to gen[1] α.
 *============================================================================================================================*/

DESCR_t icn_bb_alternate(void *zeta, int entry) {
    icn_alternate_state_t *z = (icn_alternate_state_t *)zeta;
    if (entry == α) {
        z->which = 0;
        DESCR_t v = z->gen[0].fn(z->gen[0].ζ, α);
        if (!IS_FAIL_fn(v)) return v;
        z->which = 1;
        return z->gen[1].fn(z->gen[1].ζ, α);
    }
    /* β */
    DESCR_t v = z->gen[z->which].fn(z->gen[z->which].ζ, β);
    if (!IS_FAIL_fn(v)) return v;
    if (z->which == 0) {
        z->which = 1;
        return z->gen[1].fn(z->gen[1].ζ, α);
    }
    return FAILDESCR;
}

/* icn_eval_gen — implemented in scrip.c where interp_eval and proc tables are visible. */

/*============================================================================================================================
 * Unit tests: B-2 constant box, B-3 icn_bb_to, B-4 icn_bb_to_by, B-5 icn_bb_iterate, B-7 icn_bb_find
 *============================================================================================================================*/
#ifdef ICON_GEN_UNIT_TEST

typedef struct { DESCR_t value; int fired; } const_box_state_t;

static DESCR_t const_box_fn(void *zeta, int entry) {
    const_box_state_t *z = (const_box_state_t *)zeta;
    if (entry == α && !z->fired) { z->fired = 1; return z->value; }
    return FAILDESCR;
}

typedef struct { long *vals; int n; int cap; } collector_t;
static void collect_int(DESCR_t val, void *arg) {
    collector_t *c = (collector_t *)arg;
    if (c->n < c->cap) c->vals[c->n++] = val.i;
}

typedef struct { char *buf; int idx; int cap; } str_collector_t;
static void collect_str(DESCR_t val, void *arg) {
    str_collector_t *c = (str_collector_t *)arg;
    if (c->idx < c->cap && val.s) c->buf[c->idx++] = val.s[0];
}

static int test_fail = 0;
#define ASSERT(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); test_fail=1; } } while(0)

int main(void) {
    /* B-2: constant box → 1 tick, value=42 */
    {
        const_box_state_t *s = calloc(1, sizeof(*s));
        s->value = (DESCR_t){ .v = DT_I, .i = 42 };
        bb_node_t gen = { const_box_fn, s, 0 };
        long vals[8]; collector_t c = { vals, 0, 8 };
        int ticks = bb_broker(gen, BB_PUMP, collect_int, &c);
        ASSERT(ticks == 1, "B-2: ticks==1");
        ASSERT(c.n == 1 && vals[0] == 42, "B-2: value==42");
        free(s);
    }

    /* B-3: icn_bb_to (1 to 5) → 1,2,3,4,5 */
    {
        icn_to_state_t *s = calloc(1, sizeof(*s));
        s->lo = 1; s->hi = 5;
        bb_node_t gen = { icn_bb_to, s, 0 };
        long vals[8]; collector_t c = { vals, 0, 8 };
        int ticks = bb_broker(gen, BB_PUMP, collect_int, &c);
        ASSERT(ticks == 5, "B-3: ticks==5");
        ASSERT(c.n==5 && vals[0]==1 && vals[4]==5, "B-3: values 1..5");
        free(s);
    }

    /* B-4: icn_bb_to_by (1 to 10 by 2) → 1,3,5,7,9 */
    {
        icn_to_by_state_t *s = calloc(1, sizeof(*s));
        s->lo = 1; s->hi = 10; s->step = 2;
        bb_node_t gen = { icn_bb_to_by, s, 0 };
        long vals[8]; collector_t c = { vals, 0, 8 };
        int ticks = bb_broker(gen, BB_PUMP, collect_int, &c);
        ASSERT(ticks == 5, "B-4: ticks==5");
        ASSERT(vals[0]==1 && vals[1]==3 && vals[4]==9, "B-4: values 1,3,5,7,9");
        free(s);
    }

    /* B-5: icn_bb_iterate !("abc") → 'a','b','c' */
    {
        icn_iterate_state_t *s = calloc(1, sizeof(*s));
        s->str = "abc"; s->len = 3;
        bb_node_t gen = { icn_bb_iterate, s, 0 };
        char got[4] = {0};
        str_collector_t sc = { got, 0, 3 };
        bb_broker(gen, BB_PUMP, collect_str, &sc);
        ASSERT(strcmp(got, "abc") == 0, "B-5: iterate abc");
        free(s);
    }

    /* B-7: icn_bb_find find("is","this is it") → 3,6 */
    {
        icn_find_state_t *s = calloc(1, sizeof(*s));
        s->needle = "is"; s->hay = "this is it"; s->nlen = 2; s->next = s->hay;
        bb_node_t gen = { icn_bb_find, s, 0 };
        long vals[8]; collector_t c = { vals, 0, 8 };
        int ticks = bb_broker(gen, BB_PUMP, collect_int, &c);
        ASSERT(ticks == 2, "B-7: find ticks==2");
        ASSERT(vals[0]==3 && vals[1]==6, "B-7: find positions 3,6");
        free(s);
    }

    if (!test_fail) printf("PASS: all box gates\n");
    return test_fail;
}

#endif /* ICON_GEN_UNIT_TEST */
