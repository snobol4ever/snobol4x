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
 *       Parse src as SNOBOL4 statements via snoc_parse() (fmemopen),
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
 *   code:      fmemopen → snoc_parse → Program* stored as DT_C
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

/* ── frontend (parse_expr_from_str, snoc_parse) ──────────────────────── */
#include "../../frontend/snobol4/lex.h"
#include "../../frontend/snobol4/scrip_cc.h"

/* parse_expr_from_str declared in parse.c, exposed via scrip_cc.h context */
extern EXPR_t   *parse_expr_from_str(const char *src);
extern Program  *snoc_parse(FILE *f, const char *filename);

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

static DESCR_t eval_node(EXPR_t *e)
{
    if (!e) return NULVCL;

    switch (e->kind) {

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
    case E_PAT_SEQ: {
        if (e->nchildren == 0) return NULVCL;
        DESCR_t acc = eval_node(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t next = eval_node(e->children[i]);
            if (IS_FAIL_fn(next)) return FAILDESCR;
            acc = CONCAT_fn(acc, next);
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

    /* ── unhandled / pattern nodes — fall through as NULVCL ─────────── */
    default:
        /* Pattern-context nodes (E_PAT_ALT, E_CAPT_COND_ASGN, etc.) arrive here
         * when EVAL is given a pattern expression string.  The old
         * _ev_expr hand-roller in snobol4_pattern.c handles those.
         * In value context, treat unknown nodes as null. */
        return NULVCL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * eval_expr — public entry point
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t eval_expr(const char *src)
{
    if (!src || !*src) return NULVCL;

    EXPR_t *tree = parse_expr_from_str(src);
    if (!tree) return FAILDESCR;

    return eval_node(tree);
}

/* ══════════════════════════════════════════════════════════════════════════
 * code — parse statement block, return DT_C DESCR_t
 * ══════════════════════════════════════════════════════════════════════════ */

DESCR_t code(const char *src)
{
    if (!src || !*src) return FAILDESCR;

    /* Wrap the source string as a FILE* via fmemopen.
     * fmemopen requires a non-const buffer; we duplicate first. */
    size_t len = strlen(src);
    char  *buf = malloc(len + 2);
    if (!buf) return FAILDESCR;
    memcpy(buf, src, len);
    buf[len]   = '\n';   /* ensure trailing newline for parser */
    buf[len+1] = '\0';

    FILE *f = fmemopen(buf, len + 1, "r");
    if (!f) { free(buf); return FAILDESCR; }

    Program *prog = snoc_parse(f, "<eval>");
    fclose(f);
    free(buf);

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
