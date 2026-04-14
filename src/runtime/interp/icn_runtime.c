/*
 * icn_runtime.c — Icon interpreter runtime
 *
 * FI-4: extracted from src/driver/scrip.c.
 * IcnFrame stack, icn_gen_*, icn_scan_*, icn_global_*, icn_proc_table,
 * icn_call_proc, icn_drive, icn_eval_gen, icn_oneshot_box, icn_scope_*.
 *
 * interp_eval() stays in scrip.c — referenced via extern.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6 (FI-4, 2026-04-14)
 */
#include "icn_runtime.h"
#include "../../ir/ir.h"
#include "../../frontend/snobol4/scrip_cc.h"
#include "../../runtime/x86/bb_broker.h"
#include "../../frontend/icon/icon_gen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>

/* interp_eval lives in scrip.c */
extern DESCR_t interp_eval(EXPR_t *e);

/* NV_SET_fn lives in snobol4.c — needed by RK-16 loop-var binding */
extern DESCR_t NV_SET_fn(const char *name, DESCR_t val);

/* ── Icon unified interpreter state ────────────────────────────────────────
 * Icon procedures use slot-indexed locals (e->ival on E_VAR nodes).
 * When interp_eval is running inside an Icon procedure call, icn_env points
 * to the current frame's slot array. E_VAR case checks icn_env first.
 * ICN_CUR.env_n is the slot count. Both are NULL/0 when in SNOBOL4 context.
 *
 * Icon procedure table: built from Program* at execute_program time.
 * Each entry maps procname → the E_FNC node (from STMT_t subject).
 * ────────────────────────────────────────────────────────────────────────── */
IcnProcEntry icn_proc_table[ICN_PROC_MAX];
int          icn_proc_count = 0;
int          g_lang         = 0;     /* 0=SNOBOL4 1=Icon */
EXPR_t      *g_icn_root     = NULL;  /* current Icon drive root */

/* OE-1: IcnFrame — per-call context for Icon procedure invocations.
 * Replaces the flat globals icn_env/ICN_CUR.env_n/ICN_CUR.returning/ICN_CUR.return_val/
 * icn_gen_stack/icn_gen_depth/ICN_CUR.loop_break with a pushed/popped frame stack.
 * ICN_CUR refers to the active frame (frame_depth must be >0 in Icon context). */
IcnFrame icn_frame_stack[ICN_FRAME_MAX];
int      icn_frame_depth = 0;

/* icn_drive_fnc suspend-value passthrough: while running the every-body,
 * set icn_drive_node = the E_FNC being driven and icn_drive_val = suspended value.
 * interp_eval(E_FNC) returns icn_drive_val directly when e == icn_drive_node. */
EXPR_t  *icn_drive_node = NULL;
DESCR_t  icn_drive_val;

/* Convenience helpers that mirror the old flat-global helpers */
void icn_gen_push(EXPR_t *n, long v, const char *sv) {
    IcnFrame *f = &ICN_CUR;
    if (f->gen_depth < ICN_GEN_MAX) { f->gen[f->gen_depth].node=n; f->gen[f->gen_depth].cur=v; f->gen[f->gen_depth].sval=sv; f->gen_depth++; }
}
void icn_gen_pop(void) { if (ICN_CUR.gen_depth > 0) ICN_CUR.gen_depth--; }
int  icn_gen_lookup(EXPR_t *n, long *out) {
    IcnFrame *f = &ICN_CUR;
    for (int i=f->gen_depth-1;i>=0;i--) if(f->gen[i].node==n){*out=f->gen[i].cur;return 1;} return 0;
}
int  icn_gen_lookup_sv(EXPR_t *n, long *out, const char **sv) {
    IcnFrame *f = &ICN_CUR;
    for (int i=f->gen_depth-1;i>=0;i--) if(f->gen[i].node==n){*out=f->gen[i].cur;*sv=f->gen[i].sval;return 1;} return 0;
}
int  icn_gen_active(EXPR_t *n) {
    IcnFrame *f = &ICN_CUR;
    for (int i=0;i<f->gen_depth;i++) if(f->gen[i].node==n) return 1; return 0;
}

/* Icon scan state globals (not per-frame: scan nesting is within one call) */
const char *icn_scan_subj  = NULL;
int         icn_scan_pos   = 0;
IcnScanEntry icn_scan_stack[ICN_SCAN_STACK_MAX];
int         icn_scan_depth = 0;

/* Active coroutine suspend state — set by trampoline before calling icn_call_proc,
 * so icn_call_proc can swapcontext back on E_SUSPEND. NULL when not in a coroutine. */
icn_suspend_state_t *icn_active_ss = NULL;

/* U-23: Icon global variable names -- bridge to SNO NV store.
 * Names declared `global X` in an Icon block are stored here.
 * icn_scope_patch skips slot assignment for these; E_VAR read/write
 * calls NV_GET_fn / NV_SET_fn instead of icn_env[slot]. */
const char *icn_global_names[ICN_GLOBAL_MAX];
int         icn_global_count = 0;
int icn_is_global(const char *name) {
    for (int i = 0; i < icn_global_count; i++)
        if (icn_global_names[i] && strcmp(icn_global_names[i], name) == 0) return 1;
    return 0;
}
void icn_global_register(const char *name) {
    if (!name || icn_is_global(name) || icn_global_count >= ICN_GLOBAL_MAX) return;
    icn_global_names[icn_global_count++] = name;
}

int icn_drive(EXPR_t *e) {
    if (!e) return 0;
    if (icn_gen_active(e)) return 0;
    EXPR_t *root = ICN_CUR.body_root;
    if (e->kind == E_TO && e->nchildren >= 2) {
        /* For scalar children: evaluate directly.
         * For generator children (e.g. (1 to 2) to (2 to 3)): drive each child
         * as a generator, iterating the cross-product of (lo_seq × hi_seq),
         * and for each (lo,hi) pair produce the inner lo..hi sequence. */
        EXPR_t *lo_expr = e->children[0];
        EXPR_t *hi_expr = e->children[1];
        int is_lo_gen = (lo_expr->kind == E_TO || lo_expr->kind == E_TO_BY || lo_expr->kind == E_ALTERNATE);
        int is_hi_gen = (hi_expr->kind == E_TO || hi_expr->kind == E_TO_BY || hi_expr->kind == E_ALTERNATE);
        int ticks = 0;

        if (!is_lo_gen && !is_hi_gen) {
            /* Fast path: both scalars */
            DESCR_t lo_d = interp_eval(lo_expr);
            DESCR_t hi_d = interp_eval(hi_expr);
            if (IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)) return 0;
            long lo=lo_d.i, hi=hi_d.i;
            for (long i=lo; i<=hi && !ICN_CUR.returning; i++) {
                icn_gen_push(e, i, NULL);
                int inner = icn_drive(root);
                if (!inner) interp_eval(ICN_CUR.body_root);
                icn_gen_pop(); ticks++;
                if (ICN_CUR.returning) break;
            }
        } else {
            /* General path: drive lo_expr as generator (or scalar once) */
            /* Collect lo values */
            long lo_vals[256]; int nlo = 0;
            if (!is_lo_gen) {
                DESCR_t d = interp_eval(lo_expr);
                if (!IS_FAIL_fn(d)) lo_vals[nlo++] = d.i;
            } else {
                /* Drive lo_expr collecting all values */
                EXPR_t *saved_root = ICN_CUR.body_root;
                /* Use icn_gen_push/pop trick: temporarily drive lo_expr inline */
                /* Simple approach: evaluate lo as E_TO sequence manually */
                if (lo_expr->kind == E_TO && lo_expr->nchildren >= 2) {
                    DESCR_t a = interp_eval(lo_expr->children[0]);
                    DESCR_t b = interp_eval(lo_expr->children[1]);
                    if (!IS_FAIL_fn(a) && !IS_FAIL_fn(b))
                        for (long v = a.i; v <= b.i && nlo < 256; v++) lo_vals[nlo++] = v;
                }
                ICN_CUR.body_root = saved_root;
            }
            /* Collect hi values */
            long hi_vals[256]; int nhi = 0;
            if (!is_hi_gen) {
                DESCR_t d = interp_eval(hi_expr);
                if (!IS_FAIL_fn(d)) hi_vals[nhi++] = d.i;
            } else {
                if (hi_expr->kind == E_TO && hi_expr->nchildren >= 2) {
                    DESCR_t a = interp_eval(hi_expr->children[0]);
                    DESCR_t b = interp_eval(hi_expr->children[1]);
                    if (!IS_FAIL_fn(a) && !IS_FAIL_fn(b))
                        for (long v = a.i; v <= b.i && nhi < 256; v++) hi_vals[nhi++] = v;
                }
            }
            /* Cross-product: for each lo, for each hi, iterate lo..hi */
            for (int li = 0; li < nlo && !ICN_CUR.returning; li++) {
                for (int hi2 = 0; hi2 < nhi && !ICN_CUR.returning; hi2++) {
                    long lo = lo_vals[li], hi = hi_vals[hi2];
                    for (long i = lo; i <= hi && !ICN_CUR.returning; i++) {
                        icn_gen_push(e, i, NULL);
                        int inner = icn_drive(root);
                        if (!inner) interp_eval(ICN_CUR.body_root);
                        icn_gen_pop(); ticks++;
                        if (ICN_CUR.returning) break;
                    }
                }
            }
        }
        return ticks;
    }
    if (e->kind == E_TO_BY && e->nchildren >= 3) {
        DESCR_t lo_d=interp_eval(e->children[0]);
        DESCR_t hi_d=interp_eval(e->children[1]);
        DESCR_t st_d=interp_eval(e->children[2]);
        if(IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)||IS_FAIL_fn(st_d)) return 0;
        long lo=lo_d.i,hi=hi_d.i,st=st_d.i?st_d.i:1; int ticks=0;
        if(st>0){for(long i=lo;i<=hi&&!ICN_CUR.returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root);if(!inner)interp_eval(ICN_CUR.body_root);icn_gen_pop();ticks++;if(ICN_CUR.returning)break;}}
        else    {for(long i=lo;i>=hi&&!ICN_CUR.returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root);if(!inner)interp_eval(ICN_CUR.body_root);icn_gen_pop();ticks++;if(ICN_CUR.returning)break;}}
        return ticks;
    }
    /* S-6 / RK-16: E_ITERATE — iterate string chars OR Raku @array elements.
     * If the string contains \x01 (SOH) it is a Raku array: split on SOH and
     * bind each element to the loop variable named in e->sval (if any).
     * Otherwise fall through to character-by-character Icon iteration. */
    if (e->kind == E_ITERATE && e->nchildren >= 1) {
        DESCR_t sv_d = interp_eval(e->children[0]);
        if (IS_FAIL_fn(sv_d) || !IS_STR_fn(sv_d)) return 0;
        const char *str = sv_d.s ? sv_d.s : "";
        const char *loopvar = e->sval;   /* loop variable name, or NULL */

        /* Raku array iteration: use when loopvar is set (for @arr -> $x)
         * OR when string contains \x01 (multi-element array). */
        if (loopvar || strchr(str, '\x01')) {
            /* Split on \x01 and iterate elements */
            char *copy = GC_malloc(strlen(str) + 1);
            strcpy(copy, str);
            int ticks = 0;
            char *p = copy;
            while (!ICN_CUR.returning) {
                char *sep = strchr(p, '\x01');
                if (sep) *sep = '\0';
                /* bind loop variable to the slot in the current frame */
                if (loopvar && *loopvar) {
                    DESCR_t elem = STRVAL(p);
                    /* coerce to int if purely numeric */
                    char *end;
                    long iv = strtol(p, &end, 10);
                    if (end != p && *end == '\0') elem = INTVAL(iv);
                    /* try slot first, fall back to NV */
                    int slot = icn_scope_get(&ICN_CUR.sc, loopvar);
                    if (slot >= 0 && slot < ICN_CUR.env_n)
                        ICN_CUR.env[slot] = elem;
                    else
                        NV_SET_fn(loopvar, elem);
                }
                icn_gen_push(e, ticks, p);
                int inner = icn_drive(root);
                if (!inner) interp_eval(ICN_CUR.body_root);
                icn_gen_pop(); ticks++;
                if (!sep || ICN_CUR.returning) break;
                p = sep + 1;
            }
            return ticks;
        }

        /* Icon-style character iteration */
        long len = (long)strlen(str); int ticks = 0;
        for (long i = 0; i < len && !ICN_CUR.returning; i++) {
            icn_gen_push(e, i, str);
            int inner = icn_drive(root);
            if (!inner) interp_eval(ICN_CUR.body_root);
            icn_gen_pop(); ticks++;
            if (ICN_CUR.returning) break;
        }
        return ticks;
    }
    /* S-7: find(pat,str) as generator — successive 1-based positions. */
    if (e->kind == E_FNC && e->nchildren>=3
        && e->children[0] && e->children[0]->sval
        && strcmp(e->children[0]->sval,"find")==0) {
        DESCR_t s1 = interp_eval(e->children[1]);
        DESCR_t s2 = interp_eval(e->children[2]);
        if (IS_FAIL_fn(s1)||IS_FAIL_fn(s2)) return 0;
        const char *needle = VARVAL_fn(s1), *hay = VARVAL_fn(s2);
        if (!needle||!hay) return 0;
        int nlen=(int)strlen(needle), ticks=0;
        const char *p = hay;
        while (!ICN_CUR.returning) {
            char *hit = strstr(p, needle);
            if (!hit) break;
            long pos1 = (long)(hit - hay) + 1;
            icn_gen_push(e, pos1, NULL);
            int inner = icn_drive(root);
            if (!inner) interp_eval(ICN_CUR.body_root);
            icn_gen_pop(); ticks++;
            if (ICN_CUR.returning) break;
            p = hit + (nlen > 0 ? nlen : 1);
        }
        return ticks;
    }
    /* ── E_FNC user proc — suspend-aware coroutine driver ────────────────── */
    if (e->kind == E_FNC) { int t = icn_drive_fnc(e); if (t > 0) return t; }
    for(int i=0;i<e->nchildren;i++){int t=icn_drive(e->children[i]);if(t>0)return t;}
    return 0;
}


/* icn_scope_add/patch: mirror of scope_add/scope_patch in icon_interp.c.
 * Assigns slot indices to E_VAR nodes by name, in-place on the AST. */

int icn_scope_add(IcnScope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    if (sc->n >= ICN_SLOT_MAX) return -1;
    int slot = sc->n;
    sc->e[sc->n].name=name; sc->e[sc->n].slot=slot; sc->n++;
    return slot;
}
int icn_scope_get(IcnScope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    return -1;
}
void icn_scope_patch(IcnScope *sc, EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_GLOBAL) {
        for (int i=0;i<e->nchildren;i++)
            if(e->children[i]&&e->children[i]->sval) icn_scope_add(sc, e->children[i]->sval);
        return;
    }
    if (e->kind == E_VAR && e->sval) {
        /* U-23: globals bridge to SNO NV store — skip slot, preserve sval, set ival=-1 */
        if (icn_is_global(e->sval)) { e->ival = -1; }
        else { int s = icn_scope_add(sc, e->sval); if (s >= 0) e->ival = s; }
    }
    for (int i=0;i<e->nchildren;i++) icn_scope_patch(sc, e->children[i]);
}

/* icn_call_proc: call an Icon procedure node (E_FNC with body children).
 * Mirrors icn_call() in icon_interp.c exactly, but uses DESCR_t and icn_env. */
DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs) {
    int nparams = (int)proc->ival;
    int body_start = 1 + nparams;
    int nbody = proc->nchildren - body_start;

    /* Build name→slot scope: params first, then locals from E_GLOBAL decls */
    IcnScope sc; sc.n = 0;
    for (int i = 0; i < nparams && i < ICN_SLOT_MAX; i++) {
        EXPR_t *pn = proc->children[1+i];
        if (pn && pn->sval) icn_scope_add(&sc, pn->sval);
    }
    for (int i = 0; i < nbody; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (st && st->kind == E_GLOBAL)
            for (int j = 0; j < st->nchildren; j++)
                if (st->children[j] && st->children[j]->sval)
                    icn_scope_add(&sc, st->children[j]->sval);
    }
    /* Patch E_VAR.ival with slot indices throughout body.
     * scope_patch also adds any undeclared vars it encounters to sc,
     * so sc.n after patching is the true slot count. */
    for (int i = 0; i < nbody; i++)
        icn_scope_patch(&sc, proc->children[body_start+i]);

    /* nslots = total slots assigned (params + locals + any undeclared vars) */
    int nslots = sc.n > 0 ? sc.n : (nparams > 0 ? nparams : ICN_SLOT_MAX);
    if (nslots > ICN_SLOT_MAX) nslots = ICN_SLOT_MAX;

    /* Push a fresh IcnFrame for this call */
    if (icn_frame_depth >= ICN_FRAME_MAX) return FAILDESCR;
    IcnFrame *f = &icn_frame_stack[icn_frame_depth++];
    memset(f, 0, sizeof *f);
    f->env_n = nslots;
    f->sc    = sc;   /* IM-10: save name→slot map so monitor can name locals */
    for (int i = 0; i < nparams && i < nargs && i < ICN_SLOT_MAX; i++)
        f->env[i] = args[i];

    /* Execute body statements */
    DESCR_t result = NULVCL;
    for (int i = 0; i < nbody && !ICN_CUR.returning; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (!st || st->kind == E_GLOBAL) continue;
        ICN_CUR.body_root = st;   /* OE-2: drive root for this statement */
        result = interp_eval(st);
        /* Suspend: yield value to coroutine caller, then resume here on β */
        while (ICN_CUR.suspending && icn_active_ss) {
            icn_suspend_state_t *ss = icn_active_ss;
            ss->yielded       = ICN_CUR.suspend_val;
            ICN_CUR.suspending = 0;
            swapcontext(&ss->gen_ctx, &ss->caller_ctx);
            /* Resumed by β — re-evaluate the same statement (for loops) or advance */
            if (st->kind == E_WHILE || st->kind == E_REPEAT || st->kind == E_UNTIL) {
                result = interp_eval(st);   /* re-enter the loop */
            }
            /* If still suspending after re-entry, the while loop above catches it */
        }
    }
    /* Icon semantics: explicit return → return the value; fall off end → fail. */
    if (ICN_CUR.returning) result = ICN_CUR.return_val;
    else result = FAILDESCR;

    /* Pop frame — restores caller's ICN_CUR automatically */
    icn_frame_depth--;
    return result;
}

/*============================================================================================================================
 * icn_eval_gen — U-17 (B-8): walk Icon IR node, return a drivable bb_node_t.
 *
 * Dispatch:
 *   E_TO        → icn_bb_to      (icn_to_state_t:    lo/hi/cur)
 *   E_TO_BY     → icn_bb_to_by   (icn_to_by_state_t: lo/hi/step/cur)
 *   E_ITERATE   → icn_bb_iterate  (icn_iterate_state_t: str/len/pos)
 *   E_FNC (user proc) → icn_bb_suspend (coroutine wrapping icn_call_proc)
 *   fallback    → one-shot box returning interp_eval(e)
 *
 * Visible here: interp_eval, icn_call_proc, icn_proc_table, icn_proc_count.
 *============================================================================================================================*/

/* One-shot fallback box state — holds a pre-evaluated DESCR_t, fires γ once then ω. */
typedef struct { DESCR_t val; int fired; } icn_oneshot_state_t;
static DESCR_t icn_oneshot_box(void *zeta, int entry) {
    icn_oneshot_state_t *z = (icn_oneshot_state_t *)zeta;
    if (entry == α && !z->fired && !IS_FAIL_fn(z->val)) { z->fired = 1; return z->val; }
    return FAILDESCR;
}


/*----------------------------------------------------------------------------------------------------------------------------
 * icn_bb_fnc_gen — composite box: pump arg-generator, call builtin with substituted arg each tick.
 *
 * Evaluates all non-generative args eagerly at setup. On each tick:
 *   1. Pump arg_box to get current gen value v.
 *   2. Substitute v into args[gen_idx].
 *   3. Call interp_eval_fnc_with_args(call, args, nargs) — re-invokes the builtin
 *      with pre-resolved args, skipping re-evaluation of all children.
 *
 * This mirrors how x64/JVM emitters propagate generator context into function args:
 * the generator value is on the stack; the builtin call pops it as its argument.
 *--------------------------------------------------------------------------------------------------------------------------*/
#define ICN_FNC_GEN_ARGS 8
typedef struct {
    bb_node_t   arg_box;
    EXPR_t     *call;       /* the E_FNC node */
    int         gen_idx;    /* which arg (0-based) is the generator */
    int         nargs;
    DESCR_t     args[ICN_FNC_GEN_ARGS];  /* pre-evaluated; args[gen_idx] filled each tick */
} icn_fnc_gen_state_t;

/* Forward declaration — defined in interp.c */
extern DESCR_t icn_call_builtin(EXPR_t *call, DESCR_t *args, int nargs);

static DESCR_t icn_bb_fnc_gen(void *zeta, int entry) {
    icn_fnc_gen_state_t *z = (icn_fnc_gen_state_t *)zeta;
    DESCR_t v = z->arg_box.fn(z->arg_box.ζ, entry);
    if (IS_FAIL_fn(v)) return FAILDESCR;
    z->args[z->gen_idx] = v;
    return icn_call_builtin(z->call, z->args, z->nargs);
}
/* Coroutine trampoline for E_FNC user-proc wrapper.
 * icn_bb_suspend calls this via makecontext; it reads from icn_coro_stage. */
typedef struct {
    icn_suspend_state_t *ss;
    EXPR_t              *proc;
    DESCR_t             *args;
    int                  nargs;
} Icn_coro_stage_t;
static Icn_coro_stage_t icn_coro_stage;   /* staging area — set before makecontext */

static void icn_proc_trampoline(void) {
    Icn_coro_stage_t st = icn_coro_stage;        /* copy before first yield */
    icn_active_ss = st.ss;                        /* expose to icn_call_proc */
    DESCR_t result = icn_call_proc(st.proc, st.args, st.nargs);
    icn_active_ss = NULL;
    /* proc finished — store final value if not fail, mark exhausted, yield back */
    st.ss->yielded   = IS_FAIL_fn(result) ? FAILDESCR : result;
    st.ss->exhausted = 1;
    swapcontext(&st.ss->gen_ctx, &st.ss->caller_ctx);
}

bb_node_t icn_eval_gen(EXPR_t *e) {
    if (!e) {
        icn_oneshot_state_t *z = calloc(1, sizeof(*z));
        z->val = FAILDESCR; z->fired = 1;   /* immediately ω */
        return (bb_node_t){ icn_oneshot_box, z, 0 };
    }

    /* ── E_TO: (lo to hi) ────────────────────────────────────────────────── */
    if (e->kind == E_TO && e->nchildren >= 2) {
        DESCR_t lo_d = interp_eval(e->children[0]);
        DESCR_t hi_d = interp_eval(e->children[1]);
        icn_to_state_t *z = calloc(1, sizeof(*z));
        z->lo = IS_FAIL_fn(lo_d) ? 0 : lo_d.i;
        z->hi = IS_FAIL_fn(hi_d) ? 0 : hi_d.i;
        return (bb_node_t){ icn_bb_to, z, 0 };
    }

    /* ── E_TO_BY: (lo to hi by step) ─────────────────────────────────────── */
    if (e->kind == E_TO_BY && e->nchildren >= 3) {
        DESCR_t lo_d   = interp_eval(e->children[0]);
        DESCR_t hi_d   = interp_eval(e->children[1]);
        DESCR_t step_d = interp_eval(e->children[2]);
        icn_to_by_state_t *z = calloc(1, sizeof(*z));
        z->lo   = IS_FAIL_fn(lo_d)   ? 0 : lo_d.i;
        z->hi   = IS_FAIL_fn(hi_d)   ? 0 : hi_d.i;
        z->step = IS_FAIL_fn(step_d) ? 1 : step_d.i;
        return (bb_node_t){ icn_bb_to_by, z, 0 };
    }

    /* ── E_ITERATE: (!str) ───────────────────────────────────────────────── */
    if (e->kind == E_ITERATE && e->nchildren >= 1) {
        DESCR_t sv = interp_eval(e->children[0]);
        icn_iterate_state_t *z = calloc(1, sizeof(*z));
        if (!IS_FAIL_fn(sv) && sv.s) {
            z->str = sv.s;
            z->len = sv.slen > 0 ? sv.slen : (long)strlen(sv.s);
        }
        return (bb_node_t){ icn_bb_iterate, z, 0 };
    }

    /* ── E_ALTERNATE: (left | right) ────────────────────────────────────── */
    if (e->kind == E_ALTERNATE && e->nchildren >= 2) {
        icn_alternate_state_t *z = calloc(1, sizeof(*z));
        z->gen[0] = icn_eval_gen(e->children[0]);
        z->gen[1] = icn_eval_gen(e->children[1]);
        z->which  = 0;
        return (bb_node_t){ icn_bb_alternate, z, 0 };
    }

    /* ── Arithmetic / relational binop with generative operand(s) ─────────
     * Detects when either child is a generator kind.  Non-generator children
     * are wrapped as oneshot boxes by the recursive icn_eval_gen call.      */
    {
        static const struct { EKind ek; IcnBinopKind bk; int is_rel; } binop_map[] = {
            { E_ADD, ICN_BINOP_ADD, 0 }, { E_SUB, ICN_BINOP_SUB, 0 },
            { E_MUL, ICN_BINOP_MUL, 0 }, { E_DIV, ICN_BINOP_DIV, 0 },
            { E_MOD, ICN_BINOP_MOD, 0 },
            { E_LT,  ICN_BINOP_LT,  1 }, { E_LE,  ICN_BINOP_LE,  1 },
            { E_GT,  ICN_BINOP_GT,  1 }, { E_GE,  ICN_BINOP_GE,  1 },
            { E_EQ,  ICN_BINOP_EQ,  1 }, { E_NE,  ICN_BINOP_NE,  1 },
        };
        for (int mi = 0; mi < (int)(sizeof binop_map/sizeof binop_map[0]); mi++) {
            if (e->kind != binop_map[mi].ek) continue;
            if (e->nchildren < 2) break;
            EXPR_t *lc = e->children[0], *rc = e->children[1];
            /* Only use binop_gen box when at least one child is a generator */
            static const EKind gen_kinds[] = {
                E_TO, E_TO_BY, E_ITERATE, E_ALTERNATE, E_FNC, E_SUSPEND
            };
            int l_gen = 0, r_gen = 0;
            for (int g = 0; g < 6; g++) {
                if (lc && lc->kind == gen_kinds[g]) l_gen = 1;
                if (rc && rc->kind == gen_kinds[g]) r_gen = 1;
            }
            if (!l_gen && !r_gen) break;   /* scalar — let interp_eval handle it */
            icn_binop_gen_state_t *z = calloc(1, sizeof(*z));
            z->left     = icn_eval_gen(lc);
            z->right    = icn_eval_gen(rc);
            z->op       = binop_map[mi].bk;
            z->is_relop = binop_map[mi].is_rel;
            return (bb_node_t){ icn_bb_binop_gen, z, 0 };
        }
    }

    /* ── E_FNC user proc — coroutine wrapper ─────────────────────────────── */
    if (e->kind == E_FNC && e->nchildren >= 1 && e->children[0] && e->children[0]->sval) {
        const char *fn = e->children[0]->sval;
        int nargs = e->nchildren - 1;
        for (int i = 0; i < icn_proc_count; i++) {
            if (strcmp(icn_proc_table[i].name, fn) != 0) continue;
            /* Build args array */
            DESCR_t *args = nargs > 0 ? calloc(nargs, sizeof(DESCR_t)) : NULL;
            for (int j = 0; j < nargs; j++)
                args[j] = interp_eval(e->children[1+j]);
            /* Allocate suspend state + stack */
            icn_suspend_state_t *ss = calloc(1, sizeof(*ss));
            ss->stack       = malloc(256 * 1024);
            ss->trampoline  = icn_proc_trampoline;
            ss->trampoline_arg = NULL;   /* unused — trampoline reads icn_coro_stage */
            /* Stage the call parameters before makecontext */
            icn_coro_stage.ss    = ss;
            icn_coro_stage.proc  = icn_proc_table[i].proc;
            icn_coro_stage.args  = args;
            icn_coro_stage.nargs = nargs;
            return (bb_node_t){ icn_bb_suspend, ss, 0 };
        }
        /* ── Builtin E_FNC with generative arg — icn_bb_fnc_gen ─────────── */
        /* Find first argument that is itself a generator expression.
         * Pre-evaluate all non-generative args; the gen arg is filled each tick. */
        for (int j = 0; j < nargs && j < ICN_FNC_GEN_ARGS; j++) {
            EXPR_t *arg = e->children[1+j];
            if (!arg) continue;
            EKind k = arg->kind;
            if (k == E_TO || k == E_TO_BY || k == E_ITERATE || k == E_ALTERNATE || k == E_FNC) {
                icn_fnc_gen_state_t *fg = calloc(1, sizeof(*fg));
                fg->arg_box = icn_eval_gen(arg);
                fg->call    = e;
                fg->gen_idx = j;
                fg->nargs   = nargs;
                /* Pre-evaluate all other args */
                for (int k2 = 0; k2 < nargs && k2 < ICN_FNC_GEN_ARGS; k2++) {
                    if (k2 == j) continue;
                    fg->args[k2] = interp_eval(e->children[1+k2]);
                }
                return (bb_node_t){ icn_bb_fnc_gen, fg, 0 };
            }
        }
    }

    /* ── IC-2b: E_LIMIT  (gen \ N) ──────────────────────────────────────── */
    if (e->kind == E_LIMIT && e->nchildren >= 2) {
        icn_limit_state_t *z = calloc(1, sizeof(*z));
        z->gen = icn_eval_gen(e->children[0]);
        DESCR_t nd = interp_eval(e->children[1]);
        z->max = IS_INT_fn(nd) ? nd.i : 0;
        return (bb_node_t){ icn_bb_limit, z, 0 };
    }

    /* ── IC-2b: E_EVERY  (every gen [do body]) ──────────────────────────── */
    if (e->kind == E_EVERY && e->nchildren >= 1) {
        icn_every_state_t *z = calloc(1, sizeof(*z));
        z->gen  = icn_eval_gen(e->children[0]);
        z->body = (e->nchildren >= 2) ? e->children[1] : NULL;
        return (bb_node_t){ icn_bb_every, z, 0 };
    }

    /* ── IC-2b: E_BANG_BINARY  (E1 ! E2) ────────────────────────────────── */
    if (e->kind == E_BANG_BINARY && e->nchildren >= 2) {
        icn_bang_binary_state_t *z = calloc(1, sizeof(*z));
        z->proc_expr = e->children[0];
        z->arg_box   = icn_eval_gen(e->children[1]);
        return (bb_node_t){ icn_bb_bang_binary, z, 0 };
    }

    /* ── IC-2b: E_SEQ_EXPR  ((E1; E2; …; En)) ───────────────────────────── */
    if (e->kind == E_SEQ_EXPR && e->nchildren >= 1) {
        icn_seq_state_t *z = calloc(1, sizeof(*z));
        z->children = e->children;
        z->n        = e->nchildren;
        return (bb_node_t){ icn_bb_seq_expr, z, 0 };
    }

    /* ── Fallback: one-shot box wrapping interp_eval ─────────────────────── */
    icn_oneshot_state_t *z = calloc(1, sizeof(*z));
    z->val = interp_eval(e);
    return (bb_node_t){ icn_oneshot_box, z, 0 };
}


/* icn_drive_fnc: suspend-aware driver for user procedures called as generators.
 * Called by icn_drive when e->kind == E_FNC and a matching proc exists.
 * Runs the proc body in-frame, pausing at each E_SUSPEND, running the
 * every-body (body_root of the *caller* frame), then the do-clause, then
 * continuing from the same statement (so while-loops around suspend iterate). */
int icn_drive_fnc(EXPR_t *e) {
    if (!e || e->kind != E_FNC || e->nchildren < 1 || !e->children[0]) return 0;
    const char *fn = e->children[0]->sval;
    if (!fn) return 0;
    int pi;
    for (pi = 0; pi < icn_proc_count; pi++)
        if (strcmp(icn_proc_table[pi].name, fn) == 0) break;
    if (pi >= icn_proc_count) return 0;

    EXPR_t *proc   = icn_proc_table[pi].proc;
    int nparams    = (int)proc->ival;
    int body_start = 1 + nparams;
    int nbody      = proc->nchildren - body_start;

    /* Build scope */
    IcnScope sc; sc.n = 0;
    for (int i = 0; i < nparams && i < ICN_SLOT_MAX; i++) {
        EXPR_t *pn = proc->children[1+i];
        if (pn && pn->sval) icn_scope_add(&sc, pn->sval);
    }
    for (int i = 0; i < nbody; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (st && st->kind == E_GLOBAL)
            for (int j = 0; j < st->nchildren; j++)
                if (st->children[j] && st->children[j]->sval)
                    icn_scope_add(&sc, st->children[j]->sval);
    }
    for (int i = 0; i < nbody; i++)
        icn_scope_patch(&sc, proc->children[body_start+i]);
    int nslots = sc.n > 0 ? sc.n : (nparams > 0 ? nparams : 1);
    if (nslots > ICN_SLOT_MAX) nslots = ICN_SLOT_MAX;

    /* Capture every-body from caller frame BEFORE pushing callee frame */
    EXPR_t *every_body = (icn_frame_depth >= 1)
                         ? icn_frame_stack[icn_frame_depth-1].body_root : NULL;

    /* Push frame */
    if (icn_frame_depth >= ICN_FRAME_MAX) return 0;
    IcnFrame *f = &icn_frame_stack[icn_frame_depth++];
    memset(f, 0, sizeof *f);
    f->env_n = nslots;
    f->sc    = sc;
    int nargs = e->nchildren - 1;
    for (int i = 0; i < nparams && i < nargs && i < ICN_SLOT_MAX; i++)
        f->env[i] = interp_eval(e->children[1+i]);

    /* Suspend-aware body loop */
    int ticks = 0;
    int stmt  = 0;
    while (stmt < nbody && !f->returning && !f->loop_break) {
        EXPR_t *st = proc->children[body_start + stmt];
        if (!st || st->kind == E_GLOBAL) { stmt++; continue; }
        f->body_root  = st;
        f->suspending = 0;
        interp_eval(st);
        if (f->suspending) {
            DESCR_t sv       = f->suspend_val;
            EXPR_t *doclause = f->suspend_do;
            f->suspending    = 0;
            /* Run every-body with suspended value visible via ICN_CUR being
             * the proc frame — write() etc. will call interp_eval on their
             * argument, which is the result of the generator call.  We need
             * the every-body to see sv as the result of the E_FNC expression.
             * Accomplish this by temporarily storing sv in a gen slot keyed
             * on e, so E_EVERY's interp_eval(gen) path retrieves it. */
            /* Set drive passthrough so interp_eval(E_FNC e) returns sv directly
             * instead of re-calling the procedure. */
            icn_drive_node = e;
            icn_drive_val  = sv;
            if (every_body) {
                /* Execute every-body in caller frame: step back so ICN_CUR
                 * is the caller (who owns the every/write expression), not
                 * the generator proc frame. */
                icn_frame_depth--;
                interp_eval(every_body);
                icn_frame_depth++;
                /* Refresh f in case frame array was touched */
                f = &icn_frame_stack[icn_frame_depth - 1];
            }
            icn_drive_node = NULL;
            if (doclause) interp_eval(doclause);
            ticks++;
            /* If the stmt that suspended was a loop (E_WHILE/E_REPEAT/E_UNTIL),
             * re-enter it so it can re-check its condition next tick.
             * For bare E_SUSPEND (or any other stmt), advance past it — it fired once. */
            if (st->kind != E_WHILE && st->kind != E_REPEAT && st->kind != E_UNTIL)
                stmt++;
        } else {
            stmt++;
        }
        /* Refresh pointer in case frame was reallocated (it isn't, but be safe) */
        f = &icn_frame_stack[icn_frame_depth - 1];
        if (f->returning || f->loop_break) break;
    }

    icn_frame_depth--;
    return ticks;
}

/*============================================================================================================================
 * IC-2b: Four missing GDE ops as BB boxes.
 * Live here (not icon_gen.c) because they need interp_eval / icn_scan_*.
 * E_SCAN is intentionally absent: it is the same IR node as SNOBOL4 matching,
 * already handled correctly by the oneshot fallback in icn_eval_gen.
 *============================================================================================================================*/

/*----------------------------------------------------------------------------------------------------------------------------
 * icn_bb_limit — E_LIMIT  (gen \ N)
 * α: pump inner gen α; count=1; return value if count<=max.
 * β: if count>=max → ω; pump inner gen β; if ω → ω; count++; return value.
 *--------------------------------------------------------------------------------------------------------------------------*/
DESCR_t icn_bb_limit(void *zeta, int entry) {
    icn_limit_state_t *z = (icn_limit_state_t *)zeta;
    if (z->max <= 0) return FAILDESCR;
    DESCR_t v;
    if (entry == α) {
        z->count = 0;
        v = z->gen.fn(z->gen.ζ, α);
    } else {
        if (z->count >= z->max) return FAILDESCR;
        v = z->gen.fn(z->gen.ζ, β);
    }
    if (IS_FAIL_fn(v)) return FAILDESCR;
    z->count++;
    if (z->count > z->max) return FAILDESCR;
    return v;
}

/*----------------------------------------------------------------------------------------------------------------------------
 * icn_bb_every — E_EVERY  (every gen [do body])
 * α: pump gen α → if γ eval body → return gen value.
 * β: pump gen β → if ω → ω → if γ eval body → return gen value.
 * body may be NULL (bare "every gen" for side effects).
 *--------------------------------------------------------------------------------------------------------------------------*/
DESCR_t icn_bb_every(void *zeta, int entry) {
    icn_every_state_t *z = (icn_every_state_t *)zeta;
    DESCR_t v = (entry == α)
        ? z->gen.fn(z->gen.ζ, α)
        : z->gen.fn(z->gen.ζ, β);
    if (IS_FAIL_fn(v)) return FAILDESCR;
    if (z->body) interp_eval(z->body);
    return v;
}

/*----------------------------------------------------------------------------------------------------------------------------
 * icn_bb_bang_binary — E_BANG_BINARY  (E1 ! E2)
 * Call procedure/expression E1 with each successive value from E2 generator.
 * α: pump E2 α → call E1(arg). β: pump E2 β → call E1(arg).
 * If E1 fails on an arg, skip to next (goal-directed).
 *--------------------------------------------------------------------------------------------------------------------------*/
DESCR_t icn_bb_bang_binary(void *zeta, int entry) {
    icn_bang_binary_state_t *z = (icn_bang_binary_state_t *)zeta;
    int is_first = (entry == α);
    for (;;) {
        DESCR_t arg = z->arg_box.fn(z->arg_box.ζ, is_first ? α : β);
        is_first = 0;
        if (IS_FAIL_fn(arg)) return FAILDESCR;
        z->cur_arg = arg;
        if (!z->proc_expr) return FAILDESCR;
        /* Inject arg as result of first argument child via drive passthrough */
        if (z->proc_expr->nchildren >= 2 && z->proc_expr->children[1]) {
            icn_drive_node = z->proc_expr->children[1];
            icn_drive_val  = arg;
        }
        DESCR_t result = interp_eval(z->proc_expr);
        icn_drive_node = NULL;
        if (!IS_FAIL_fn(result)) return result;
        /* E1 failed — try next E2 value */
    }
}

/*----------------------------------------------------------------------------------------------------------------------------
 * icn_bb_seq_expr — E_SEQ_EXPR  ((E1; E2; …; En))
 * α: eval E1..E(n-1) for side effects; build last_box from En; pump last_box α.
 * β: pump last_box β.
 *--------------------------------------------------------------------------------------------------------------------------*/
DESCR_t icn_bb_seq_expr(void *zeta, int entry) {
    icn_seq_state_t *z = (icn_seq_state_t *)zeta;
    if (entry == α) {
        for (int i = 0; i < z->n - 1; i++)
            if (z->children[i]) interp_eval(z->children[i]);
        if (z->n <= 0 || !z->children[z->n - 1]) return FAILDESCR;
        z->last_box = icn_eval_gen(z->children[z->n - 1]);
        z->started  = 1;
        return z->last_box.fn(z->last_box.ζ, α);
    }
    if (!z->started) return FAILDESCR;
    return z->last_box.fn(z->last_box.ζ, β);
}
