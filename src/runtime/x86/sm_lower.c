/*
 * sm_lower.c — IR → SM_Program compiler pass (M-SCRIP-U3)
 *
 * Walks a Program* (linked list of STMT_t, each holding EXPR_t trees)
 * and emits a flat SM_Program instruction sequence.
 *
 * SNOBOL4 statement model:
 *   label:  subject  pattern = replacement  :(goto)
 *
 * SM lowering strategy per statement:
 *   1. Emit SM_LABEL for stmt->label (if present) → recorded in label_table
 *   2. Eval subject   → value on stack
 *   3. If pattern:    → emit SM_PAT_* tree; emit SM_EXEC_STMT
 *      Else if replacement only: emit subject eval + SM_STORE_VAR
 *   4. Gotos: SM_JUMP_S / SM_JUMP_F / SM_JUMP (patched after all stmts)
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * Date: 2026-04-06
 */

#include "sm_lower.h"
#include "sm_prog.h"

#include "../../frontend/snobol4/scrip_cc.h"
#include "../../ir/ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <gc/gc.h>
#include "snobol4.h"   /* FNCEX_fn, FUNC_NPARAMS_fn */

/* ── Label resolution table ─────────────────────────────────────────────── */

#define MAX_LABELS 4096

typedef struct {
    char *name;         /* SNOBOL4 label string (interned) */
    int   instr_idx;    /* SM_Program instruction index of the SM_LABEL instr */
} LabelEntry;

typedef struct {
    /* Forward-reference patches: a goto whose target isn't defined yet */
    int   jump_instr_idx;   /* index of the SM_JUMP / SM_JUMP_S / SM_JUMP_F */
    char *target_name;      /* label name to resolve */
} PatchEntry;

typedef struct {
    LabelEntry labels[MAX_LABELS];
    int        nlabels;

    PatchEntry patches[MAX_LABELS * 2];
    int        npatches;
} LabelTable;

static void lt_init(LabelTable *lt)
{
    memset(lt, 0, sizeof *lt);
}

/* Record a defined label → its SM_LABEL instruction index */
static void lt_define(LabelTable *lt, const char *name, int instr_idx)
{
    assert(lt->nlabels < MAX_LABELS);
    lt->labels[lt->nlabels].name      = strdup(name);
    lt->labels[lt->nlabels].instr_idx = instr_idx;
    lt->nlabels++;
}

/* Find a label by name; returns instr_idx or -1 */
static int lt_find(const LabelTable *lt, const char *name)
{
    for (int i = 0; i < lt->nlabels; i++)
        if (strcasecmp(lt->labels[i].name, name) == 0)
            return lt->labels[i].instr_idx;
    return -1;
}

/* Record a forward-reference patch */
static void lt_patch_later(LabelTable *lt, int jump_instr_idx, const char *name)
{
    assert(lt->npatches < MAX_LABELS * 2);
    lt->patches[lt->npatches].jump_instr_idx = jump_instr_idx;
    lt->patches[lt->npatches].target_name    = strdup(name);
    lt->npatches++;
}

/* Resolve all forward patches; returns 0 on success, -1 on unresolved ref */
static int lt_resolve(LabelTable *lt, SM_Program *p)
{
    int ok = 0;
    for (int i = 0; i < lt->npatches; i++) {
        const char *name = lt->patches[i].target_name;
        int target = lt_find(lt, name);
        if (target < 0) {
            fprintf(stderr, "sm_lower: unresolved label '%s'\n", name);
            ok = -1;
            continue;
        }
        sm_patch_jump(p, lt->patches[i].jump_instr_idx, target);
    }
    return ok;
}

static void lt_free(LabelTable *lt)
{
    for (int i = 0; i < lt->nlabels; i++)  free(lt->labels[i].name);
    for (int i = 0; i < lt->npatches; i++) free(lt->patches[i].target_name);
}

/* ── Emit a goto target (possibly forward ref) ──────────────────────────── */

/*
 * Emit a SM_JUMP / SM_JUMP_S / SM_JUMP_F for a named SNOBOL4 goto target.
 * If the target is already defined, patch immediately.
 * Otherwise register a forward patch.
 * Special target "RETURN" → SM_RETURN; "FRETURN" → SM_FRETURN.
 * Returns the index of the emitted jump instruction.
 */
static int emit_goto(SM_Program *p, LabelTable *lt,
                     sm_opcode_t op, const char *target)
{
    if (!target) return -1;

    /* Special names (case-insensitive per SNOBOL4 spec) */
    if (strcasecmp(target, "RETURN") == 0) {
        return sm_emit(p, SM_RETURN);
    }
    if (strcasecmp(target, "FRETURN") == 0) {
        return sm_emit(p, SM_FRETURN);
    }
    if (strcasecmp(target, "NRETURN") == 0) {
        /* NRETURN: return DT_N from fn return var; ret_kind=2 in call_user_function */
        return sm_emit(p, SM_NRETURN);
    }

    /* Emit the jump with a placeholder target (0) */
    int idx = sm_emit_i(p, op, 0);

    int resolved = lt_find(lt, target);
    if (resolved >= 0) {
        sm_patch_jump(p, idx, resolved);
    } else {
        lt_patch_later(lt, idx, target);
    }
    return idx;
}

/* ── Expression lowering ────────────────────────────────────────────────── */

/* Forward declaration */
static void lower_expr(SM_Program *p, LabelTable *lt, const EXPR_t *e);

static void lower_pat_expr(SM_Program *p, LabelTable *lt, const EXPR_t *e)
{
    if (!e) return;

    switch (e->kind) {

    /* Literals in pattern context → SM_PAT_LIT */
    case E_QLIT:
        sm_emit_s(p, SM_PAT_LIT, e->sval ? e->sval : "");
        return;

    /* Variable → dereference, then pattern */
    case E_VAR:
        sm_emit_s(p, SM_PUSH_VAR, e->sval);
        sm_emit(p, SM_PAT_DEREF);
        return;

    /* Primitives */
    case E_ARB:      sm_emit(p, SM_PAT_ARB);     return;
    case E_REM:      sm_emit(p, SM_PAT_REM);      return;
    case E_FAIL:     sm_emit(p, SM_PAT_FAIL);     return;
    case E_SUCCEED:  sm_emit(p, SM_PAT_SUCCEED);  return;
    case E_FENCE:    sm_emit(p, SM_PAT_FENCE);    return;
    case E_ABORT:    sm_emit(p, SM_PAT_ABORT);    return;
    case E_BAL:      sm_emit(p, SM_PAT_BAL);      return;

    /* Parameterised primitives — child[0] is the argument expr */
    case E_ANY:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_ANY);
        return;
    case E_NOTANY:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_NOTANY);
        return;
    case E_SPAN:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_SPAN);
        return;
    case E_BREAK:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_BREAK);
        return;
    case E_BREAKX:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_BREAK);   /* BREAKX → BREAK with backtrack semantics in bb */
        return;
    case E_LEN:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_LEN);
        return;
    case E_POS:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_POS);
        return;
    case E_RPOS:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_RPOS);
        return;
    case E_TAB:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_TAB);
        return;
    case E_RTAB:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_RTAB);
        return;
    case E_ARBNO:
        lower_pat_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_ARB);     /* ARBNO lowered as ARB for now; bb handles looping */
        return;

    /* Concatenation (sequence in pattern) → left then right, then SM_PAT_CAT */
    case E_SEQ:
    case E_CAT:
        for (int i = 0; i < e->nchildren; i++)
            lower_pat_expr(p, lt, e->children[i]);
        /* n-ary: emit n-1 SM_PAT_CAT */
        for (int i = 1; i < e->nchildren; i++)
            sm_emit(p, SM_PAT_CAT);
        return;

    /* Alternation */
    case E_ALT:
        for (int i = 0; i < e->nchildren; i++)
            lower_pat_expr(p, lt, e->children[i]);
        for (int i = 1; i < e->nchildren; i++)
            sm_emit(p, SM_PAT_ALT);
        return;

    /* Captures */
    case E_CAPT_COND_ASGN:
        /* child[0] = sub-pattern, child[1] = variable; a[1].i=0 → cond (.V) */
        lower_pat_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        if (e->nchildren > 1 && e->children[1]) {
            EXPR_t *var_expr = e->children[1];
            /* Detect . *func() — E_DEFER(E_FNC) — emit SM_PAT_CAPTURE_FN */
            if (var_expr->kind == E_DEFER
                    && var_expr->nchildren > 0
                    && var_expr->children[0]
                    && var_expr->children[0]->kind == E_FNC
                    && var_expr->children[0]->sval) {
                int idx = sm_emit_s(p, SM_PAT_CAPTURE_FN,
                                    var_expr->children[0]->sval);
                p->instrs[idx].a[1].i = 0;  /* conditional */
            } else {
                int idx = sm_emit_s(p, SM_PAT_CAPTURE, var_expr->sval);
                p->instrs[idx].a[1].i = 0;  /* conditional */
            }
        }
        return;
    case E_CAPT_IMMED_ASGN:
        /* a[1].i=1 → immediate ($V) */
        lower_pat_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        if (e->nchildren > 1 && e->children[1]) {
            EXPR_t *var_expr = e->children[1];
            /* Detect $ *func() — E_DEFER(E_FNC) — emit SM_PAT_CAPTURE_FN */
            if (var_expr->kind == E_DEFER
                    && var_expr->nchildren > 0
                    && var_expr->children[0]
                    && var_expr->children[0]->kind == E_FNC
                    && var_expr->children[0]->sval) {
                int idx = sm_emit_s(p, SM_PAT_CAPTURE_FN,
                                    var_expr->children[0]->sval);
                p->instrs[idx].a[1].i = 1;  /* immediate */
            } else {
                int idx = sm_emit_s(p, SM_PAT_CAPTURE, var_expr->sval);
                p->instrs[idx].a[1].i = 1;  /* immediate */
            }
        }
        return;
    case E_CAPT_CURSOR:
        /* Two forms from the parser:
         *   unary @var  → nchildren=1, children[0] = var-name node (ATFN)
         *   binary X@V  → nchildren=2, children[0] = sub-pat, children[1] = var-name
         * For unary @var there is no sub-pattern — emit epsilon implicitly
         * (pat_pop in SM_PAT_CAPTURE will get pat_epsilon via pat_cat). */
        if (e->nchildren == 1) {
            /* unary @var: no sub-pattern child — child[0] IS the variable name */
            const char *vname = (e->children[0] && e->children[0]->sval)
                                 ? e->children[0]->sval : "";
            sm_emit(p, SM_PAT_EPS);          /* push epsilon as sub-pattern */
            int idx = sm_emit_s(p, SM_PAT_CAPTURE, vname);
            p->instrs[idx].a[1].i = 2;      /* cursor */
        } else {
            lower_pat_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
            if (e->nchildren > 1 && e->children[1]) {
                int idx = sm_emit_s(p, SM_PAT_CAPTURE, e->children[1]->sval);
                p->instrs[idx].a[1].i = 2;  /* cursor (@V) */
            }
        }
        return;

    /* Deferred pattern reference: *VAR */
    case E_DEFER:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_PAT_DEREF);
        return;

    /* Function call in pattern context → eval as value, then deref as pat */
    case E_FNC:
        lower_expr(p, lt, e);
        sm_emit(p, SM_PAT_DEREF);
        return;

    default:
        /* Value expression used as pattern — eval and deref */
        lower_expr(p, lt, e);
        sm_emit(p, SM_PAT_DEREF);
        return;
    }
}

static void lower_expr(SM_Program *p, LabelTable *lt, const EXPR_t *e)
{
    if (!e) {
        sm_emit(p, SM_PUSH_NULL);
        return;
    }

    switch (e->kind) {

    /* ── Literals ── */
    case E_QLIT:
        sm_emit_s(p, SM_PUSH_LIT_S, e->sval ? e->sval : "");
        return;
    case E_ILIT:
        sm_emit_i(p, SM_PUSH_LIT_I, (int64_t)e->ival);
        return;
    case E_FLIT:
        sm_emit_f(p, SM_PUSH_LIT_F, e->dval);
        return;
    case E_NUL:
        sm_emit(p, SM_PUSH_NULL);
        return;

    /* ── References ── */
    case E_VAR:
        sm_emit_s(p, SM_PUSH_VAR, e->sval ? e->sval : "");
        return;
    case E_KEYWORD:
        sm_emit_s(p, SM_PUSH_VAR, e->sval ? e->sval : "");
        return;
    case E_INDIRECT:
        /* $expr — eval name-string, look up variable → push value on value stack */
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit_si(p, SM_CALL, "INDIR_GET", 1);
        return;
    case E_DEFER:
        /* *expr in value context — freeze as DT_E for EVAL() to thaw.
         * SM_PUSH_EXPR bakes the EXPR_t* pointer into the instruction. */
        sm_emit_ptr(p, SM_PUSH_EXPR, (void *)(e->nchildren > 0 ? e->children[0] : NULL));
        return;

    /* ── Arithmetic ── */
    case E_ADD:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_ADD);
        return;
    case E_SUB:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_SUB);
        return;
    case E_MUL:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_MUL);
        return;
    case E_DIV:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_DIV);
        return;
    case E_POW:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_EXP);
        return;
    case E_MOD:
        /* SNOBOL4 has no modulo; map to SM_CALL for now */
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit_si(p, SM_CALL, "REMDR", 2);
        return;
    case E_MNS:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_NEG);
        return;
    case E_PLS:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit(p, SM_COERCE_NUM);   /* unary +: string→int or real */
        return;

    /* ── String concatenation ── */
    case E_CAT:
    case E_SEQ:
        for (int i = 0; i < e->nchildren; i++)
            lower_expr(p, lt, e->children[i]);
        for (int i = 1; i < e->nchildren; i++)
            sm_emit(p, SM_CONCAT);
        return;

    /* ── Assignment ── */
    case E_ASSIGN:
        /* child[1] = rhs value; child[0] = lhs variable */
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        if (e->nchildren > 0 && e->children[0]) {
            const EXPR_t *lhs = e->children[0];
            if (lhs->kind == E_VAR || lhs->kind == E_KEYWORD)
                sm_emit_s(p, SM_STORE_VAR, lhs->sval ? lhs->sval : "");
            else if (lhs->kind == E_FNC && lhs->sval) {
                /* Field mutator: fname(obj) = val  →  push obj, SM_CALL fname_SET 2
                 * Stack on entry to setter: [val, obj] (val pushed first above) */
                lower_expr(p, lt, lhs->nchildren > 0 ? lhs->children[0] : NULL);
                char setname[256];
                snprintf(setname, sizeof(setname), "%s_SET", lhs->sval);
                sm_emit_si(p, SM_CALL, setname, 2);
            } else {
                /* Computed lhs — push lhs expr, then generic store */
                lower_expr(p, lt, lhs);
                sm_emit_si(p, SM_CALL, "ASGN", 2);
            }
        }
        return;

    /* ── Function / builtin call ── */
    case E_FNC: {
        int nargs = e->nchildren;
        for (int i = 0; i < nargs; i++)
            lower_expr(p, lt, e->children[i]);
        sm_emit_si(p, SM_CALL, e->sval ? e->sval : "", (int64_t)nargs);
        return;
    }

    /* ── Array / table subscript ── */
    case E_IDX:
        for (int i = 0; i < e->nchildren; i++)
            lower_expr(p, lt, e->children[i]);
        sm_emit_si(p, SM_CALL, "IDX", (int64_t)e->nchildren);
        return;

    /* ── Relational comparisons (numeric) → SM_ACOMP or SM_CALL ── */
    case E_EQ:
    case E_NE:
    case E_LT:
    case E_LE:
    case E_GT:
    case E_GE: {
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_ACOMP);   /* leaves -1/0/1 on stack; stmt goto uses it */
        return;
    }

    /* ── Relational comparisons (string) → SM_LCOMP ── */
    case E_LLT:
    case E_LLE:
    case E_LGT:
    case E_LGE:
    case E_LEQ:
    case E_LNE:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit(p, SM_LCOMP);
        return;

    /* ── Interrogation ?X → succeed if X succeeds ── */
    case E_INTERROGATE:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        /* result already on stack; success/failure propagates */
        return;

    /* ── Name reference .X — push DT_N name descriptor onto value stack ── */
    case E_NAME: {
        /* Push the variable name as a string, then NAME_PUSH converts to DT_N */
        const char *vname = (e->nchildren > 0 && e->children[0] && e->children[0]->sval)
                            ? e->children[0]->sval : "";
        sm_emit_s(p, SM_PUSH_LIT_S, vname);
        sm_emit_si(p, SM_CALL, "NAME_PUSH", 1);
        return;
    }

    /* ── Scan E ? E ── */
    case E_SCAN:
        lower_pat_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        sm_emit_i(p, SM_PUSH_LIT_I, 0);   /* no replacement */
        sm_emit(p, SM_EXEC_STMT);
        return;

    /* ── OPSYN operator & / @ / | — dispatch via APPLY_fn(sval, args, n) ── */
    case E_OPSYN: {
        /* sval is either:
         *   mangled: "BIATFN(@)", "ORFN(|)", "BIAMFN(&)" — op char between '(' and ')'
         *   bare:    "BARFN", "AROWFN" — unary ops from uop_names[] table
         * Extract the bare operator char so APPLY_fn finds the opsyn alias. */
        const char *raw = e->sval ? e->sval : "&";
        const char *op = raw;
        static char op_buf[4];
        const char *lp = strchr(raw, '(');
        if (lp && lp[1] && lp[2] == ')') {
            /* mangled form: extract char between parens */
            op_buf[0] = lp[1]; op_buf[1] = '\0';
            op = op_buf;
        } else if (strcmp(raw, "BARFN")  == 0) { op = "|"; }
        else if (strcmp(raw, "AROWFN") == 0) { op = "^"; }
        for (int i = 0; i < e->nchildren; i++)
            lower_expr(p, lt, e->children[i]);
        sm_emit_si(p, SM_CALL, op, (int64_t)e->nchildren);
        return;
    }

    /* ── Swap :=: ── */
    case E_SWAP:
        lower_expr(p, lt, e->nchildren > 0 ? e->children[0] : NULL);
        lower_expr(p, lt, e->nchildren > 1 ? e->children[1] : NULL);
        sm_emit_si(p, SM_CALL, "SWAP", 2);
        return;

    /* ── Pattern primitives used as values (not already handled above) ── */
    case E_ALT:
    case E_ARB:  case E_REM:  case E_FAIL: case E_SUCCEED:
    case E_FENCE: case E_ABORT: case E_BAL:
    case E_ANY:  case E_NOTANY: case E_SPAN: case E_BREAK: case E_BREAKX:
    case E_LEN:  case E_POS:  case E_RPOS: case E_TAB: case E_RTAB:
    case E_ARBNO:
    case E_CAPT_COND_ASGN: case E_CAPT_IMMED_ASGN: case E_CAPT_CURSOR:
        lower_pat_expr(p, lt, e);
        sm_emit(p, SM_PAT_BOXVAL);  /* bridge: pop pat-stack → push DT_P on value-stack */
        return;

    default:
        fprintf(stderr, "sm_lower: unhandled expr kind %d\n", (int)e->kind);
        sm_emit(p, SM_PUSH_NULL);
        return;
    }
}

/* ── Statement lowering ─────────────────────────────────────────────────── */

static void lower_stmt(SM_Program *p, LabelTable *lt, const STMT_t *s)
{
    /* 0. Statement counter tick — increments &STCOUNT / &STNO */
    sm_emit(p, SM_STNO);

    /* 1. Define label if present */
    if (s->label && s->label[0]) {
        int lbl_idx = sm_label(p);
        lt_define(lt, s->label, lbl_idx);
    }

    /* END statement → SM_HALT */
    if (s->is_end) {
        sm_emit(p, SM_HALT);
        return;
    }

    /* OE-9: language-aware dispatch — ICN and PL stmts use BB opcodes.
     * LANG_SNO (0) falls through to the existing SNOBOL4 lowering path. */
    if (s->lang == LANG_ICN) {
        /* Icon statement: lower subject expression, emit SM_BB_PUMP.
         * bb_broker(BB_PUMP) drives the generator to exhaustion. */
        if (s->subject)
            lower_expr(p, lt, s->subject);
        else
            sm_emit(p, SM_PUSH_NULL);
        sm_emit(p, SM_BB_PUMP);
        goto emit_gotos;
    }
    if (s->lang == LANG_PL) {
        /* Prolog statement: lower subject (E_CHOICE/E_CLAUSE tree),
         * emit SM_BB_ONCE. bb_broker(BB_ONCE) finds one solution. */
        if (s->subject)
            lower_expr(p, lt, s->subject);
        else
            sm_emit(p, SM_PUSH_NULL);
        sm_emit(p, SM_BB_ONCE);
        goto emit_gotos;
    }

    /*
     * 2. Pattern match statement:
     *      subject  pattern  [= replacement]  :(goto)
     *
     * SM layout (Option A — pattern tree emitted FIRST):
     *   lower pattern tree → SM_PAT_* sequence
     *     (parameterised ops like SM_PAT_LEN pop their args from value stack;
     *      those args are pushed by lower_pat_expr via lower_expr internally,
     *      so they are consumed before subject is on the stack)
     *   eval subject → value stack
     *   eval replacement → value stack  (or INTVAL(0) if no replacement)
     *   SM_EXEC_STMT  (value stack top: repl; below: subj; pat-stack: built pat)
     *
     * This avoids interleaving parameterised-pattern value-stack args
     * with the subject descriptor.
     */
    if (s->pattern) {
        lower_pat_expr(p, lt, s->pattern);

        if (s->subject)
            lower_expr(p, lt, s->subject);
        else
            sm_emit(p, SM_PUSH_NULL);

        if (s->has_eq && s->replacement)
            lower_expr(p, lt, s->replacement);
        else if (s->has_eq)
            sm_emit_si(p, SM_PUSH_LIT_S, "", 0);  /* X pat = with no RHS → empty string replacement */
        else
            sm_emit_i(p, SM_PUSH_LIT_I, 0);       /* no = at all → no replacement */

        sm_emit(p, SM_EXEC_STMT);
        /* operand a[0].s = subject variable name for write-back (NULL if not a simple var) */
        {
            const char *sname = NULL;
            if (s->subject && (s->subject->kind == E_VAR || s->subject->kind == E_KEYWORD))
                sname = s->subject->sval;
            p->instrs[p->count - 1].a[0].s = sname;
            p->instrs[p->count - 1].a[1].i = s->has_eq;
        }
        goto emit_gotos;
    }

    /*
     * 3. Pure assignment / expression statement:
     *      label:  expr = value   :(goto)
     *      or just:  expr         :(goto)
     */
    if (s->subject) {
        if (s->has_eq) {
            /* Assignment: rhs is replacement (or null if omitted) */
            if (s->replacement)
                lower_expr(p, lt, s->replacement);
            else
                sm_emit(p, SM_PUSH_NULL);   /* X =   → assign null */
            /* lhs */
            if (s->subject->kind == E_VAR || s->subject->kind == E_KEYWORD) {
                sm_emit_s(p, SM_STORE_VAR, s->subject->sval ? s->subject->sval : "");
            } else if (s->subject->kind == E_INDIRECT) {
                lower_expr(p, lt, s->subject->nchildren > 0 ? s->subject->children[0] : NULL);
                sm_emit_si(p, SM_CALL, "ASGN_INDIR", 2);
            } else if (s->subject->kind == E_IDX) {
                /* a<i> = rhs  or  a<i,j> = rhs — stack: rhs already pushed above.
                 * Push base, then indices; sm_interp IDX_SET pops all and calls subscript_set. */
                int nc = s->subject->nchildren;  /* child[0]=base, child[1]=i, [2]=j */
                for (int ci = 0; ci < nc; ci++) lower_expr(p, lt, s->subject->children[ci]);
                sm_emit_si(p, SM_CALL, "IDX_SET", (int64_t)(nc + 1)); /* +1 for rhs */
            } else if (s->subject->kind == E_FNC && s->subject->sval) {
                /* NRETURN lvalue or field mutator: fname(...) = rhs
                 * If fname is a zero-param user function (NRETURN path), emit
                 * NRETURN_ASGN pseudo-call: stack = [rhs], fname in a[0].s.
                 * Otherwise field mutator: push obj, call fname_SET(rhs, obj). */
                if (s->subject->nchildren == 0) {
                    /* Zero-arg call on LHS: NRETURN path (forward-declared fns allowed).
                     * NRETURN_ASGN calls fn at runtime; if it returns DT_N writes through
                     * the name, else falls back to fname_SET field-mutator convention. */
                    sm_emit_si(p, SM_CALL, "NRETURN_ASGN", 1);
                    p->instrs[p->count - 1].a[1].s = GC_strdup(s->subject->sval);
                } else {
                    /* Multi-arg LHS: field mutator fname(obj,...) = rhs */
                    lower_expr(p, lt, s->subject->nchildren > 0 ? s->subject->children[0] : NULL);
                    char _setname[256];
                    snprintf(_setname, sizeof(_setname), "%s_SET", s->subject->sval);
                    sm_emit_si(p, SM_CALL, _setname, 2);
                }
            } else {
                lower_expr(p, lt, s->subject);
                sm_emit_si(p, SM_CALL, "ASGN", 2);
            }
        } else {
            lower_expr(p, lt, s->subject);
            sm_emit(p, SM_POP);  /* expression statement, result unused */
        }
    }

emit_gotos: {
    const SnoGoto *g = s->go;
    if (!g) return;

    /*
     * SNOBOL4 goto:  :(L)      unconditional
     *                :S(L)     on success
     *                :F(L)     on failure
     *                :S(A)F(B) both
     */
    if (g->uncond && g->uncond[0]) {
        emit_goto(p, lt, SM_JUMP, g->uncond);
        return;
    }

    /* Computed gotos → SM_JUMP_INDIR (not yet supported; fall back) */
    if (g->computed_uncond_expr) {
        sm_emit_s(p, SM_PUSH_LIT_S, "(computed-goto)");
        sm_emit(p, SM_JUMP_INDIR);
        return;
    }

    if (g->onsuccess && g->onsuccess[0])
        emit_goto(p, lt, SM_JUMP_S, g->onsuccess);
    if (g->onfailure && g->onfailure[0])
        emit_goto(p, lt, SM_JUMP_F, g->onfailure);
    }
}

/* ── Public entry point ─────────────────────────────────────────────────── */

SM_Program *sm_lower(const Program *prog)
{
    if (!prog) return NULL;

    SM_Program *p  = sm_prog_new();
    LabelTable  lt;
    lt_init(&lt);

    /* First pass: lower all statements */
    for (const STMT_t *s = prog->head; s; s = s->next)
        lower_stmt(p, &lt, s);

    /* Implicit HALT at end if not already there */
    if (p->count == 0 || p->instrs[p->count - 1].op != SM_HALT)
        sm_emit(p, SM_HALT);

    /* Second pass: resolve all forward label references */
    if (lt_resolve(&lt, p) < 0) {
        fprintf(stderr, "sm_lower: label resolution failed\n");
        /* Return program anyway; unresolved jumps go to 0 (safe) */
    }

    lt_free(&lt);
    return p;
}
