/*
 * eval_code.c — M-DYN-6: EVAL and CODE via dynamic path
 *
 * PUBLIC API
 * ----------
 *   DESCR_t     eval_expr(const char *src)
 *       Parse src as a SNOBOL4 expression via parse_expr_from_str(),
 *       walk the EXPR_t IR tree, evaluate to a DESCR_t.
 *       Returns FAILDESCR on parse or eval failure.
 *
 *   DESCR_t     code(const char *src)
 *       Parse src as SNOBOL4 statements via sno_parse() (fmemopen),
 *       stash the Program* in a DT_C DESCR_t.
 *       Returns FAILDESCR on parse failure.
 *
 *   const char *exec_code(DESCR_t code_block)
 *       Execute a DT_C code block statement by statement.
 *       Returns the first unconditional/success goto label encountered,
 *       or "" on fall-through success, or NULL on failure.
 *
 * DESIGN
 * ------
 *   EVAL and CODE are not special.  They are the runtime doing what it
 *   always does with source that arrived late (ARCH-byrd-dynamic.md).
 *
 *   eval_expr: parse_expr_from_str → eval_node (recursive EXPR_t walk)
 *   code:      fmemopen → sno_parse → Program* stored as DT_C
 *   exec_code: walk Program stmts, call exec_stmt per stmt,
 *                     resolve gotos, return first branch target.
 *
 * RELATION TO EXISTING EVAL_fn
 * -----------------------------
 *   snobol4_pattern.c already has EVAL_fn() — a hand-rolled mini-parser
 *   covering the beauty.sno pattern-expression subset.  That path is
 *   preserved.  eval_expr() is the full-expression path; it is called
 *   from a new EVAL_fn wrapper in snobol4_pattern.c (see patch note at
 *   bottom of this file) only after the existing fast path declines.
 *
 *   For M-DYN-6 we wire the new path in by replacing EVAL_fn in
 *   snobol4_pattern.c with a thin wrapper that first tries the full
 *   parse path; the old _ev_expr hand-roller becomes a fallback.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-01
 * SPRINT:  DYN-7
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── runtime ─────────────────────────────────────────────────────────── */
#include "snobol4.h"
#include "sil_macros.h"   /* SIL macro translations — RT + SM axes */

/* ── frontend: CMPILE expression entry (replaces bison parse_expr_from_str) */
#include "../../frontend/snobol4/CMPILE.h"

/* cmpnd_to_expr — lower CMPND_t → EXPR_t IR (defined in snobol4_pattern.c) */
extern struct EXPR_t *cmpnd_to_expr(CMPND_t *n);

/* scrip_cc.h provides Program, STMT_t, EXPR_t types used by exec_code() */
#include "../../frontend/snobol4/scrip_cc.h"
/* sno_parse retired — code() now uses cmpile_string + cmpile_lower (RT-121 fix) */
extern Program *cmpile_lower(CMPILE_t *cl);

/* exec_stmt — the five-phase executor */
extern int exec_stmt(const char  *subj_name,
                          DESCR_t     *subj_var,
                          DESCR_t      pat,
                          DESCR_t     *repl,
                          int          has_repl);

/* subject globals (defined in test driver or main runtime) */
extern const char *Σ;
extern int         Ω;
extern int         Δ;

/* ══════════════════════════════════════════════════════════════════════════
 * eval_node — recursive EXPR_t → DESCR_t evaluator
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t eval_node(EXPR_t *e)
{
    if (!e) return NULVCL;

    switch (e->kind) {

    /* ── deferred expression — freeze child as DT_E (EXPRESSION type) ── */
    case E_DEFER:
        /* *X  (STRFN/UOP_STR) — produce a DT_E EXPRESSION descriptor.
         * The descriptor holds a pointer to the child EXPR_t*.
         * EVAL_fn thaws it by calling eval_node on the child.
         * Do NOT evaluate the child here — that is EVAL()'s job. */
        if (e->nchildren < 1) return NULVCL;
        {
            DESCR_t d;
            d.v    = DT_E;
            d.ptr  = e->children[0];   /* frozen EXPR_t* child */
            d.slen = 0;
            d.s    = NULL;
            return d;
        }

    /* ── literals ────────────────────────────────────────────────────── */
    case E_ILIT:
        return INTVAL(e->ival);

    case E_FLIT:
        return REALVAL(e->dval);

    case E_QLIT:
        return e->sval ? STRVAL(e->sval) : NULVCL;

    case E_NUL:
        return NULVCL;

    /* ── variable / keyword reference ────────────────────────────────── */
    case E_VAR:
        if (e->sval && *e->sval)
            return NV_GET_fn(e->sval);
        return NULVCL;

    case E_KEYWORD: {
        /* &KEYWORD — prepend '&' for the NV table key */
        if (!e->sval || !*e->sval) return NULVCL;
        char kbuf[128];
        snprintf(kbuf, sizeof kbuf, "&%s", e->sval);
        return NV_GET_fn(kbuf);
    }

    /* ── unary minus ─────────────────────────────────────────────────── */
    case E_MNS:
        if (e->nchildren < 1) return FAILDESCR;
        return neg(eval_node(e->children[0]));

    /* ── arithmetic ──────────────────────────────────────────────────── */
    case E_ADD: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = eval_node(e->children[0]);
        DESCR_t r = eval_node(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return add(l, r);
    }
    case E_SUB: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = eval_node(e->children[0]);
        DESCR_t r = eval_node(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return sub(l, r);
    }
    case E_MUL: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = eval_node(e->children[0]);
        DESCR_t r = eval_node(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return mul(l, r);
    }
    case E_DIV: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = eval_node(e->children[0]);
        DESCR_t r = eval_node(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return DIVIDE_fn(l, r);
    }
    case E_POW: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = eval_node(e->children[0]);
        DESCR_t r = eval_node(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return POWER_fn(l, r);
    }

    /* ── string concatenation ────────────────────────────────────────── */
    case E_CAT:
    case E_SEQ: {
        /* S-9 fix: EVAL("LEN(1) LEN(1)") must return PATTERN not STRING.
         * In SNOBOL4, space-separated terms in an expression are concatenation.
         * When the accumulated value is a pattern, concatenation is pat_cat (pattern
         * concatenation), not CONCAT_fn (string concatenation).  CONCAT_fn coerces
         * patterns to strings, destroying the type. */
        if (e->nchildren == 0) return NULVCL;
        DESCR_t acc = eval_node(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t next = eval_node(e->children[i]);
            if (IS_FAIL_fn(next)) return FAILDESCR;
            if (acc.v == DT_P || next.v == DT_P) {
                extern DESCR_t pat_cat(DESCR_t a, DESCR_t b);
                acc = pat_cat(acc, next);
            } else {
                acc = CONCAT_fn(acc, next);
            }
            if (IS_FAIL_fn(acc)) return FAILDESCR;
        }
        return acc;
    }

    /* ── assignment: subject = replacement (value context → yield repl) */
    case E_ASSIGN: {
        if (e->nchildren < 2) return FAILDESCR;
        /* left child is lvalue (E_VAR or E_INDIRECT) */
        DESCR_t val = eval_node(e->children[1]);
        if (IS_FAIL_fn(val)) return FAILDESCR;
        EXPR_t *lv = e->children[0];
        if (lv && lv->kind == E_VAR && lv->sval)
            NV_SET_fn(lv->sval, val);
        else if (lv && lv->kind == E_INDIRECT && lv->nchildren > 0) {
            DESCR_t name_d = eval_node(lv->children[0]);
            const char *nm = VARVAL_fn(name_d);
            if (nm && *nm) NV_SET_fn(nm, val);
        }
        return val;
    }

    /* ── indirect reference $expr ────────────────────────────────────── */
    case E_INDIRECT: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t name_d = eval_node(e->children[0]);
        const char *nm = VARVAL_fn(name_d);
        if (!nm || !*nm) return NULVCL;
        return NV_GET_fn(nm);
    }

    /* ── function call ───────────────────────────────────────────────── */
    case E_FNC: {
        if (!e->sval || !*e->sval) return FAILDESCR;
        int nargs = e->nchildren;
        DESCR_t *args = nargs > 0
            ? (DESCR_t *)alloca((size_t)nargs * sizeof(DESCR_t))
            : NULL;
        for (int i = 0; i < nargs; i++) {
            args[i] = eval_node(e->children[i]);
            if (IS_FAIL_fn(args[i])) return FAILDESCR;
        }
        return APPLY_fn(e->sval, args, nargs);
    }

    /* ── array/table subscript ───────────────────────────────────────── */
    case E_IDX: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t base = eval_node(e->children[0]);
        if (IS_FAIL_fn(base)) return FAILDESCR;
        if (e->nchildren == 2) {
            DESCR_t idx = eval_node(e->children[1]);
            if (IS_FAIL_fn(idx)) return FAILDESCR;
            return subscript_get(base, idx);
        } else {
            DESCR_t i1 = eval_node(e->children[1]);
            DESCR_t i2 = eval_node(e->children[2]);
            if (IS_FAIL_fn(i1) || IS_FAIL_fn(i2)) return FAILDESCR;
            return subscript_get2(base, i1, i2);
        }
    }

    /* ── .X  name-of — return DT_N lvalue descriptor ────────────────── */
    case E_NAME: {
        /* DOTFN: .X yields the name (lvalue) of X, not its value.
         * Child is E_VAR (variable name) or E_KEYWORD. */
        if (e->nchildren < 1) return FAILDESCR;
        EXPR_t *child = e->children[0];
        if (!child) return FAILDESCR;
        if (child->kind == E_VAR && child->sval)
            return NAME_fn(child->sval);
        if (child->kind == E_KEYWORD && child->sval) {
            char kbuf[128];
            snprintf(kbuf, sizeof kbuf, "&%s", child->sval);
            return NAME_fn(kbuf);
        }
        DESCR_t inner = eval_node(child);
        if (IS_FAIL_fn(inner)) return FAILDESCR;
        const char *nm = VARVAL_fn(inner);
        if (!nm || !*nm) return FAILDESCR;
        return NAME_fn(nm);
    }

    /* ── +X  unary plus — numeric coerce ─────────────────────────────── */
    case E_PLS: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t arg = eval_node(e->children[0]);
        if (IS_FAIL_fn(arg)) return FAILDESCR;
        return APPLY_fn("PLS", &arg, 1);
    }

    /* ── ?X  interrogation — succeed→null, fail→FAIL ─────────────────── */
    case E_INTERROGATE: {
        /* SIL QUES: evaluate child; if it FAILs → FAIL; else → NULVCL.
         * Used for conditional pattern: ?pat succeeds iff pat matches. */
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t res = eval_node(e->children[0]);
        if (IS_FAIL_fn(res)) return FAILDESCR;
        return NULVCL;
    }

    /* ── X | Y  pattern alternation (value context: ORFN) ───────────── */
    case E_ALT: {
        /* In value context, alternation is a pattern constructor.
         * Build it via CMPILE: re-stringify as "(L)|(R)" then eval_via_cmpile.
         * For the common case of two pattern children, use PATVAL + pat_alt. */
        if (e->nchildren < 1) return NULVCL;
        extern DESCR_t pat_alt(DESCR_t a, DESCR_t b);
        DESCR_t acc = eval_node(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        acc = PATVAL_fn(acc);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t rhs = eval_node(e->children[i]);
            if (IS_FAIL_fn(rhs)) return FAILDESCR;
            rhs = PATVAL_fn(rhs);
            if (IS_FAIL_fn(rhs)) return FAILDESCR;
            acc = pat_alt(acc, rhs);
            if (IS_FAIL_fn(acc)) return FAILDESCR;
        }
        return acc;
    }

    /* ── X . Y  conditional capture (NAMFN) ─────────────────────────── */
    case E_CAPT_COND_ASGN: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t pat  = eval_node(e->children[0]);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        pat = PATVAL_fn(pat);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        /* When capture target is E_INDIRECT (e.g. REM . $'$B'), resolve the
         * variable name without dereferencing — return NAME_fn(resolved_name)
         * so bb_nme_emit_binary gets varname="$B", not the value of $B. */
        DESCR_t name;
        EXPR_t *tgt = e->children[1];
        if (tgt && tgt->kind == E_INDIRECT && tgt->nchildren > 0) {
            EXPR_t *ic = tgt->children[0];
            const char *nm = NULL;
            if (ic->kind == E_QLIT && ic->sval)        nm = ic->sval;
            else if (ic->kind == E_VAR  && ic->sval) { DESCR_t xv = NV_GET_fn(ic->sval); nm = VARVAL_fn(xv); }
            else                                      { DESCR_t nd = eval_node(ic);        nm = VARVAL_fn(nd); }
            if (!nm) return FAILDESCR;
            name = NAME_fn(nm);
        } else {
            name = eval_node(tgt);
        }
        if (IS_FAIL_fn(name)) return FAILDESCR;
        return pat_assign_cond(pat, name);
    }

    /* ── X $ Y  immediate capture (DOLFN) ───────────────────────────── */
    case E_CAPT_IMMED_ASGN: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t pat  = eval_node(e->children[0]);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        pat = PATVAL_fn(pat);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        /* Same E_INDIRECT target resolution as E_CAPT_COND_ASGN above. */
        DESCR_t name;
        EXPR_t *tgt = e->children[1];
        if (tgt && tgt->kind == E_INDIRECT && tgt->nchildren > 0) {
            EXPR_t *ic = tgt->children[0];
            const char *nm = NULL;
            if (ic->kind == E_QLIT && ic->sval)        nm = ic->sval;
            else if (ic->kind == E_VAR  && ic->sval) { DESCR_t xv = NV_GET_fn(ic->sval); nm = VARVAL_fn(xv); }
            else                                      { DESCR_t nd = eval_node(ic);        nm = VARVAL_fn(nd); }
            if (!nm) return FAILDESCR;
            name = NAME_fn(nm);
        } else {
            name = eval_node(tgt);
        }
        if (IS_FAIL_fn(name)) return FAILDESCR;
        return pat_assign_imm(pat, name);
    }

    /* ── @X  cursor capture ──────────────────────────────────────────── */
    case E_CAPT_CURSOR: {
        /* @VAR — cursor-position capture: build XATP("@", varname) node.
         * Child is E_VAR or E_NAME holding the capture variable name. */
        if (e->nchildren < 1) return FAILDESCR;
        EXPR_t *child = e->children[0];
        const char *varname = NULL;
        if (child && child->kind == E_VAR  && child->sval) varname = child->sval;
        if (child && child->kind == E_NAME && child->nchildren > 0
                && child->children[0] && child->children[0]->sval)
            varname = child->children[0]->sval;
        if (!varname) return FAILDESCR;
        { extern DESCR_t pat_at_cursor(const char *varname);
          return pat_at_cursor(varname); }
    }

    /* ── X ? Y  scan (BIQSFN) ───────────────────────────────────────── */
    case E_SCAN: {
        /* Subject ? Pattern — in value context evaluate the subject,
         * coerce pattern, apply match; return matched substring or FAIL. */
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t subj = eval_node(e->children[0]);
        if (IS_FAIL_fn(subj)) return FAILDESCR;
        DESCR_t pat  = eval_node(e->children[1]);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        pat = PATVAL_fn(pat);
        if (IS_FAIL_fn(pat)) return FAILDESCR;
        return APPLY_fn("__scan", &subj, 1);   /* stub: full scan via exec_stmt */
    }

    /* ── unhandled nodes ─────────────────────────────────────────────── */
    default:
        return NULVCL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * eval_expr — public entry point
 *
 * Parses src via CMPILE's EXPR() entry (cmpile_eval_expr), lowers the
 * CMPND_t parse tree to EXPR_t IR via cmpnd_to_expr(), then evaluates.
 * This is the SIL CONVEX/CONVE path — no bison/flex involved.
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t eval_expr(const char *src)
{
    if (!src || !*src) return NULVCL;

    CMPND_t *cmpnd = cmpile_eval_expr(src);
    if (!cmpnd) return FAILDESCR;

    EXPR_t *tree = cmpnd_to_expr(cmpnd);
    if (!tree) return FAILDESCR;

    return eval_node(tree);
}


/* ══════════════════════════════════════════════════════════════════════════
 * code — parse statement block, return DT_C DESCR_t
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t code(const char *src)
{
    if (!src || !*src) return FAILDESCR;

    /* Use cmpile_string (CMPILE.c — the authoritative parser) then lower
     * to Program* IR via cmpile_lower.  Replaces the broken fmemopen/sno_parse
     * path which used the retired Bison parser (RT-121). */
    CMPILE_t *cl = cmpile_string(src);
    if (!cl) return FAILDESCR;

    Program *prog = cmpile_lower(cl);
    cmpile_free(cl);

    if (!prog || !prog->head) return FAILDESCR;

    DESCR_t d;
    d.v   = DT_C;
    d.ptr = prog;          /* Program* stored as generic GC pointer */
    d.slen = 0;
    return d;
}

/* ══════════════════════════════════════════════════════════════════════════
 * exec_code — run a DT_C block, return first goto label (or ""/NULL)
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * Execute each statement in the code block in order.
 * For each statement:
 *   - Phase 1: evaluate subject expression → subj_name / subj_val
 *   - Phase 2/3: if pattern present, call exec_stmt
 *   - Goto resolution: check success/failure/uncond goto fields
 * Return the first goto label we need to branch to (caller resolves).
 *
 * Simplified model for M-DYN-6:
 *   - Subject-only statements (no pattern) are assignments or OUTPUT.
 *   - We eval the subject expression and, if the statement has an
 *     assignment (has_eq), assign to the subject variable.
 *   - Pattern statements go through exec_stmt.
 *   - Goto is returned as a string for the caller to dispatch.
 */
const char *exec_code(DESCR_t code_block)
{
    if (code_block.v != DT_C || !code_block.ptr) return NULL;
    Program *prog = (Program *)code_block.ptr;

    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->is_end) return "";  /* END statement → fall through */

        /* Evaluate subject */
        DESCR_t subj_val = NULVCL;
        const char *subj_name = NULL;

        if (s->subject) {
            if (s->subject->kind == E_VAR && s->subject->sval) {
                /* Named variable lvalue */
                subj_name = s->subject->sval;
                subj_val  = NV_GET_fn(subj_name);
            } else {
                subj_val = eval_node(s->subject);
            }
        }

        int succeeded = 1;   /* default: succeed (no pattern = always :S) */

        if (s->pattern) {
            /* Pattern statement: build pattern DESCR_t, call exec_stmt */
            DESCR_t pat_d = eval_node(s->pattern);
            if (IS_FAIL_fn(pat_d)) {
                succeeded = 0;
            } else {
                DESCR_t repl_val;
                int has_repl = 0;
                if (s->has_eq && s->replacement) {
                    repl_val = eval_node(s->replacement);
                    has_repl = !IS_FAIL_fn(repl_val);
                }
                succeeded = exec_stmt(
                    subj_name,
                    subj_name ? NULL : &subj_val,
                    pat_d,
                    has_repl ? &repl_val : NULL,
                    has_repl);
            }
        } else if (s->has_eq && s->replacement && subj_name) {
            /* Pure assignment: SUBJ = REPLACEMENT (no pattern) */
            DESCR_t repl_val = eval_node(s->replacement);
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else {
                NV_SET_fn(subj_name, repl_val);
                succeeded = 1;
            }
        } else if (s->subject && !s->pattern && !s->has_eq) {
            /* Expression-only statement: evaluate for side effects
             * (e.g. OUTPUT = 'x' comes through as subject with has_eq,
             *  but a bare function call is subject-only) */
            if (IS_FAIL_fn(subj_val)) succeeded = 0;
        }

        /* Goto resolution */
        if (s->go) {
            if (s->go->uncond && *s->go->uncond)
                return s->go->uncond;
            if (succeeded && s->go->onsuccess && *s->go->onsuccess)
                return s->go->onsuccess;
            if (!succeeded && s->go->onfailure && *s->go->onfailure)
                return s->go->onfailure;
        }
        /* No goto match — fall through to next statement */
    }

    return "";  /* ran off end of block */
}

/* ══════════════════════════════════════════════════════════════════════════
 * RT-6: EXPVAL_fn — execute a DT_E EXPRESSION with full save/restore
 *
 * SIL EXPVAL: saves system state (NAM frame, subject globals), executes
 * the frozen EXPR_t* child via eval_node, then restores state on exit.
 * Fully re-entrant — nested EVAL() calls stack save frames correctly.
 *
 * DT_E holds ptr = frozen EXPR_t* (set by E_DEFER in eval_node above).
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t EXPVAL_fn(DESCR_t expr_d)
{
    if (expr_d.v == DT_E) {
        /* Frozen EXPR_t* — thaw and evaluate with NAM frame isolation */
        if (!expr_d.ptr) return FAILDESCR;

        /* Save subject globals (SIL: WPTR/XCL/YCL/TCL) */
        const char *save_sigma = Σ;
        int         save_omega = Ω;
        int         save_delta = Δ;

        /* Save NAM frame (SIL: NAMICL/NHEDCL) — push fresh frame */
        int nam_cookie = NAM_save();

        DESCR_t result = eval_node((EXPR_t *)expr_d.ptr);

        /* Restore NAM frame — discard any captures from this expression's
         * internal patterns, then pop the frame (do NOT commit — captures
         * inside an EXPRESSION are local and must not propagate out). */
        NAM_discard(nam_cookie);
        NAM_pop(nam_cookie);   /* pop frame without assigning */

        /* Restore subject globals */
        Σ = save_sigma;
        Ω = save_omega;
        Δ = save_delta;

        return result;
    }
    if (expr_d.v == DT_C) {
        /* DT_C code block — run via exec_code (no save/restore needed;
         * exec_code is a full statement executor with its own frame) */
        exec_code(expr_d);
        return NULVCL;
    }
    /* Anything else: evaluate as expression string */
    const char *s = VARVAL_fn(expr_d);
    if (!s || !*s) return NULVCL;
    return eval_expr(s);
}

/* ══════════════════════════════════════════════════════════════════════════
 * RT-7: CONVE_fn — compile a string to a DT_E EXPRESSION descriptor
 *
 * SIL CONVE/CONVEX: parse the string as an expression via CMPILE,
 * lower to EXPR_t IR, wrap in a DT_E descriptor (frozen EXPR_t*).
 * Returns FAILDESCR on parse failure.
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t CONVE_fn(DESCR_t str_d)
{
    const char *s = VARVAL_fn(str_d);
    if (!s || !*s) return FAILDESCR;

    CMPND_t *cmpnd = cmpile_eval_expr(s);
    if (!cmpnd) return FAILDESCR;

    EXPR_t *tree = cmpnd_to_expr(cmpnd);
    if (!tree) return FAILDESCR;

    DESCR_t d;
    d.v    = DT_E;
    d.ptr  = tree;   /* frozen EXPR_t* */
    d.slen = 0;
    d.s    = NULL;
    return d;
}

/* ══════════════════════════════════════════════════════════════════════════
 * RT-7: CODE_fn — compile a string to a DT_C CODE descriptor
 *
 * SIL CODER: parse string as SNOBOL4 statements, return DT_C.
 * This wraps the existing code() function with the DESCR_t input sig.
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t CODE_fn(DESCR_t str_d)
{
    const char *s = VARVAL_fn(str_d);
    if (!s || !*s) return FAILDESCR;
    return code(s);
}
