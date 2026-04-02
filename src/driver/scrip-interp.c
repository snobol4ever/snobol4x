/*
 * scrip-interp.c — SNOBOL4 tree-walk interpreter (M-INTERP-A01)
 *
 * Reuses the existing frontend (lex + parse → Program* IR) and the
 * dynamic runtime (stmt_exec_dyn, eval_code.c) to execute SNOBOL4
 * programs without emitting any assembly or bytecode.
 *
 * Usage:
 *   scrip-interp <file.sno>
 *
 * Exit: 0 on normal END, 1 on error.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-02
 * SPRINT:  DYN-24  M-INTERP-A01
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc.h>

/* ── frontend ─────────────────────────────────────────────────────────── */
#include "../frontend/snobol4/scrip_cc.h"
extern Program *snoc_parse(FILE *f, const char *filename);

/* ── runtime ──────────────────────────────────────────────────────────── */
#include "../runtime/snobol4/snobol4.h"
#include "../runtime/snobol4/runtime_shim.h"

/* ── stmt_init (from snobol4_stmt_rt.c) ──────────────────────────────── */
extern void stmt_init(void);

/* ── eval_code.c ─────────────────────────────────────────────────────── */
extern DESCR_t      eval_expr_dyn(const char *src);
extern const char  *execute_code_dyn(DESCR_t code_block);

/* ── stmt_exec_dyn (from stmt_exec.c) ────────────────────────────────── */
extern int stmt_exec_dyn(const char *subj_name,
                          DESCR_t    *subj_var,
                          DESCR_t     pat,
                          DESCR_t    *repl,
                          int         has_repl);

/* subject globals owned by stmt_exec.c — extern here */
extern const char *Σ;
extern int         Ω;
extern int         Δ;

/* ══════════════════════════════════════════════════════════════════════════
 * label_table — map SNOBOL4 source labels → STMT_t*
 * ══════════════════════════════════════════════════════════════════════════ */

#define LABEL_MAX 4096

typedef struct {
    const char *name;
    STMT_t     *stmt;
} LabelEntry;

static LabelEntry label_table[LABEL_MAX];
static int        label_count = 0;

static void label_table_build(Program *prog)
{
    label_count = 0;
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label && *s->label && label_count < LABEL_MAX) {
            label_table[label_count].name = s->label;
            label_table[label_count].stmt = s;
            label_count++;
        }
    }
}

static STMT_t *label_lookup(const char *name)
{
    if (!name || !*name) return NULL;
    for (int i = 0; i < label_count; i++)
        if (strcasecmp(label_table[i].name, name) == 0)
            return label_table[i].stmt;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * eval_node_interp — thin wrapper; reuses eval_node from eval_code.c
 * via eval_expr_dyn (we call it by re-parsing for non-trivial exprs,
 * but for the common case we use NV_GET_fn / NV_SET_fn directly).
 *
 * For the interpreter we need direct EXPR_t evaluation, not string
 * re-parse.  We replicate the minimal logic needed here rather than
 * exposing eval_node (which is static in eval_code.c).
 * ══════════════════════════════════════════════════════════════════════════ */

/* Forward declaration */
static DESCR_t interp_eval(EXPR_t *e);

static DESCR_t interp_eval(EXPR_t *e)
{
    if (!e) return NULVCL;

    switch (e->kind) {
    case E_ILIT:   return INTVAL(e->ival);
    case E_FLIT:   return REALVAL(e->dval);
    case E_QLIT:   return e->sval ? STRVAL(e->sval) : NULVCL;
    case E_NUL:    return NULVCL;

    case E_VAR:
        if (e->sval && *e->sval) return NV_GET_fn(e->sval);
        return NULVCL;

    case E_KW: {
        if (!e->sval || !*e->sval) return NULVCL;
        /* Keywords stored without & prefix in NV store */
        return NV_GET_fn(e->sval);
    }

    case E_NEG:
        if (e->nchildren < 1) return FAILDESCR;
        return neg(interp_eval(e->children[0]));

    case E_ADD: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return add(l, r);
    }
    case E_SUB: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return sub(l, r);
    }
    case E_MPY: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return mul(l, r);
    }
    case E_DIV: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return DIVIDE_fn(l, r);
    }
    case E_POW: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return POWER_fn(l, r);
    }

    case E_CONCAT:
    case E_SEQ: {
        if (e->nchildren == 0) return NULVCL;
        DESCR_t acc = interp_eval(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t nxt = interp_eval(e->children[i]);
            if (IS_FAIL_fn(nxt)) return FAILDESCR;
            acc = CONCAT_fn(acc, nxt);
            if (IS_FAIL_fn(acc)) return FAILDESCR;
        }
        return acc;
    }

    case E_ASSIGN: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t val = interp_eval(e->children[1]);
        if (IS_FAIL_fn(val)) return FAILDESCR;
        EXPR_t *lv = e->children[0];
        if (lv && lv->kind == E_VAR && lv->sval)
            NV_SET_fn(lv->sval, val);
        else if (lv && lv->kind == E_INDR && lv->nchildren > 0) {
            DESCR_t nd = interp_eval(lv->children[0]);
            const char *nm = VARVAL_fn(nd);
            if (nm && *nm) NV_SET_fn(nm, val);
        }
        return val;
    }

    case E_INDR: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t nd = interp_eval(e->children[0]);
        const char *nm = VARVAL_fn(nd);
        if (!nm || !*nm) return NULVCL;
        return NV_GET_fn(nm);
    }

    case E_FNC: {
        if (!e->sval || !*e->sval) return FAILDESCR;
        int nargs = e->nchildren;
        DESCR_t *args = nargs > 0
            ? (DESCR_t *)alloca((size_t)nargs * sizeof(DESCR_t))
            : NULL;
        for (int i = 0; i < nargs; i++) {
            args[i] = interp_eval(e->children[i]);
            if (IS_FAIL_fn(args[i])) return FAILDESCR;
        }
        return APPLY_fn(e->sval, args, nargs);
    }

    case E_IDX: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t base = interp_eval(e->children[0]);
        if (IS_FAIL_fn(base)) return FAILDESCR;
        if (e->nchildren == 2) {
            DESCR_t idx = interp_eval(e->children[1]);
            if (IS_FAIL_fn(idx)) return FAILDESCR;
            return subscript_get(base, idx);
        }
        DESCR_t i1 = interp_eval(e->children[1]);
        DESCR_t i2 = interp_eval(e->children[2]);
        if (IS_FAIL_fn(i1) || IS_FAIL_fn(i2)) return FAILDESCR;
        return subscript_get2(base, i1, i2);
    }

    default:
        return NULVCL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * execute_program — full-program executor with label-table goto resolution
 * ══════════════════════════════════════════════════════════════════════════ */

static void execute_program(Program *prog)
{
    label_table_build(prog);

    STMT_t *s = prog->head;
    int step_limit = 10000000;   /* guard against infinite loops in smoke tests */

    while (s && step_limit-- > 0) {
        if (s->is_end) break;

        /* Skip Prolog/Icon nodes that sneak in via shared parser */
        if (s->subject && (s->subject->kind == E_CHOICE ||
                           s->subject->kind == E_UNIFY  ||
                           s->subject->kind == E_CLAUSE)) {
            s = s->next; continue;
        }

        /* ── evaluate subject ──────────────────────────────────────── */
        DESCR_t     subj_val  = NULVCL;
        const char *subj_name = NULL;

        if (s->subject) {
            if (s->subject->kind == E_VAR && s->subject->sval) {
                subj_name = s->subject->sval;
                subj_val  = NV_GET_fn(subj_name);
            } else {
                subj_val = interp_eval(s->subject);
            }
        }

        int succeeded = 1;

        /* ── pattern match ─────────────────────────────────────────── */
        if (s->pattern) {
            /* Build pattern descriptor via interp_eval then stmt_exec_dyn */
            DESCR_t pat_d = interp_eval(s->pattern);
            if (IS_FAIL_fn(pat_d)) {
                succeeded = 0;
            } else {
                DESCR_t  repl_val;
                int      has_repl = 0;
                if (s->has_eq && s->replacement) {
                    repl_val = interp_eval(s->replacement);
                    has_repl = !IS_FAIL_fn(repl_val);
                }
                Σ = subj_name ? subj_name : "";
                succeeded = stmt_exec_dyn(
                    subj_name,
                    subj_name ? NULL : &subj_val,
                    pat_d,
                    has_repl ? &repl_val : NULL,
                    has_repl);
            }

        /* ── pure assignment (direct or null) ─────────────────────── */
        } else if (s->has_eq && subj_name) {
            /* X = expr  OR  X =  (null assign, no replacement node) */
            DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else {
                NV_SET_fn(subj_name, repl_val);
                succeeded = 1;
            }

        /* ── indirect assignment: $expr = rhs ─────────────────────── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_INDR) {
            DESCR_t name_d = interp_eval(s->subject->nchildren > 0
                                         ? s->subject->children[0] : NULL);
            const char *nm = VARVAL_fn(name_d);
            if (!nm || !*nm) {
                succeeded = 0;
            } else {
                DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
                if (IS_FAIL_fn(repl_val)) {
                    succeeded = 0;
                } else {
                    NV_SET_fn(nm, repl_val);
                    succeeded = 1;
                }
            }

        /* ── expression-only (side effects, e.g. bare function call) ─ */
        } else if (s->subject && !s->pattern && !s->has_eq) {
            if (IS_FAIL_fn(subj_val)) succeeded = 0;
        }

        /* ── goto resolution ───────────────────────────────────────── */
        const char *target = NULL;
        if (s->go) {
            if (s->go->uncond && *s->go->uncond)
                target = s->go->uncond;
            else if (succeeded && s->go->onsuccess && *s->go->onsuccess)
                target = s->go->onsuccess;
            else if (!succeeded && s->go->onfailure && *s->go->onfailure)
                target = s->go->onfailure;
        }

        if (target) {
            /* Check for END pseudo-label */
            if (strcasecmp(target, "END") == 0) break;
            STMT_t *dest = label_lookup(target);
            if (dest) { s = dest; continue; }
            /* Unknown label — treat as program end */
            break;
        }

        s = s->next;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: scrip-interp <file.sno>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "scrip-interp: cannot open '%s'\n", argv[1]);
        return 1;
    }

    Program *prog = snoc_parse(f, argv[1]);
    fclose(f);

    if (!prog || !prog->head) {
        fprintf(stderr, "scrip-interp: parse failed for '%s'\n", argv[1]);
        return 1;
    }

    stmt_init();
    execute_program(prog);
    return 0;
}
