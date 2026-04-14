/*
 * interp.c — IR tree-walk interpreter (SCRIP unified driver)
 *
 * Contains: label_table, call_stack, interp_eval(), interp_eval_pat(),
 *           interp_eval_ref(), execute_program(), and all static helpers.
 *
 * Extracted from scrip.c by GOAL-FULL-INTEGRATION FI-6.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * DATE:    2026-04-14
 * PURPOSE: Interpreter loop — separated to enable parallel frontend sessions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <gc.h>

/* ── frontend ─────────────────────────────────────────────────────────── */
#include "frontend/snobol4/scrip_cc.h"
extern Program *sno_parse(FILE *f, const char *filename);
#include "frontend/snocone/snocone_driver.h"
#include "frontend/snocone/snocone_cf.h"
#include "frontend/prolog/prolog_driver.h"
#include "frontend/prolog/term.h"
#include "frontend/prolog/prolog_runtime.h"
#include "frontend/prolog/prolog_atom.h"
#include "frontend/prolog/prolog_builtin.h"
#include "frontend/prolog/pl_broker.h"
#include "frontend/icon/icon_driver.h"
#include "frontend/raku/raku_driver.h"
#include "frontend/rebus/rebus_lower.h"
#include "frontend/icon/icon_gen.h"
#include "frontend/icon/icon_lex.h"

extern void ir_print_node   (const EXPR_t *e, FILE *f);
extern void ir_print_node_nl(const EXPR_t *e, FILE *f);

/* ── runtime ──────────────────────────────────────────────────────────── */
#include "runtime/x86/snobol4.h"
#include "runtime/x86/sil_macros.h"
#include "runtime/x86/snobol4_runtime_shim.h"
#include "runtime/x86/sm_lower.h"
#include "runtime/x86/sm_interp.h"
#include "runtime/x86/sm_prog.h"
#include "runtime/x86/bb_build.h"
#include "runtime/x86/sm_codegen.h"
#include "runtime/x86/sm_image.h"

extern DESCR_t pat_at_cursor(const char *varname);

#include "runtime/interp/icn_runtime.h"
#include "runtime/interp/pl_runtime.h"

extern DESCR_t      eval_expr(const char *src);
extern const char  *exec_code(DESCR_t code_block);
extern int exec_stmt(const char *subj_name,
                     DESCR_t    *subj_var,
                     DESCR_t     pat,
                     DESCR_t    *repl,
                     int         has_repl);
extern const char *Σ;
extern int         Ω;
extern int         Δ;

#include "interp.h"

static void stmt_init(void) {}

/* ── eval_code.c ─────────────────────────────────────────────────────── */
extern DESCR_t      eval_expr(const char *src);
extern const char  *exec_code(DESCR_t code_block);

/* ── exec_stmt (from stmt_exec.c) ────────────────────────────────── */
extern int exec_stmt(const char *subj_name,
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
int label_count = 0;

void label_table_build(Program *prog)
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

STMT_t *label_lookup(const char *name)
{
    if (!name || !*name) return NULL;
    for (int i = 0; i < label_count; i++)
        if (strcasecmp(label_table[i].name, name) == 0)
            return label_table[i].stmt;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * call_stack — RETURN/FRETURN longjmp infrastructure
 * ══════════════════════════════════════════════════════════════════════════ */

#define CALL_STACK_MAX 256

typedef struct {
    jmp_buf  ret_env;
    char     fname[128];    /* uppercase — also the return-value variable */
    char   **saved_names;
    DESCR_t *saved_vals;
    int      nsaved;
} CallFrame;

static CallFrame  call_stack[CALL_STACK_MAX];
static int        call_depth = 0;

/* The program being interpreted (set in main before execute_program) */
Program *g_prog = NULL;
int g_polyglot = 0; /* U-23: 1 when running a fenced polyglot .scrip file */
int g_opt_trace   = 0;  /* --trace:   print STMT N on each statement */
int g_opt_dump_bb = 0;  /* --dump-bb: print PATND tree before each match */

/* ── Extract DEFINE spec string from E_FNC("DEFINE",...) subject node ── */
static const char *define_spec_from_expr(EXPR_t *subj)
{
    if (!subj || subj->kind != E_FNC) return NULL;
    if (!subj->sval || strcasecmp(subj->sval, "DEFINE") != 0) return NULL;
    if (subj->nchildren < 1 || !subj->children[0]) return NULL;
    EXPR_t *arg = subj->children[0];
    if (arg->kind == E_QLIT) return arg->sval;
    if (arg->kind == E_CAT || arg->kind == E_SEQ) {
        static char flatbuf[1024];
        size_t pos = 0;
        flatbuf[0] = '\0';
        for (int i = 0; i < arg->nchildren && pos < sizeof(flatbuf)-1; i++) {
            EXPR_t *c = arg->children[i];
            if (c && c->kind == E_QLIT && c->sval) {
                size_t clen = strlen(c->sval);
                if (pos + clen >= sizeof(flatbuf)-1) break;
                memcpy(flatbuf + pos, c->sval, clen);
                pos += clen;
            }
        }
        flatbuf[pos] = '\0';
        return pos ? flatbuf : NULL;
    }
    return NULL;
}

/* ── Extract optional entry-label string from second arg of DEFINE ── */
static const char *define_entry_from_expr(EXPR_t *subj)
{
    if (!subj || subj->kind != E_FNC) return NULL;
    if (!subj->sval || strcasecmp(subj->sval, "DEFINE") != 0) return NULL;
    if (subj->nchildren < 2 || !subj->children[1]) return NULL;
    EXPR_t *arg2 = subj->children[1];
    /* .label_name → E_NAME(E_VAR sval="label_name") or E_CAPT_COND_ASGN */
    if (arg2->kind == E_NAME && arg2->nchildren == 1) {
        EXPR_t *inner = arg2->children[0];
        if (inner->kind == E_VAR && inner->sval) return inner->sval;
    }
    if (arg2->kind == E_CAPT_COND_ASGN && arg2->nchildren == 1) {
        EXPR_t *inner = arg2->children[0];
        if (inner->kind == E_VAR && inner->sval) return inner->sval;
    }
    if (arg2->kind == E_VAR && arg2->sval) return arg2->sval;
    if (arg2->kind == E_QLIT && arg2->sval) return arg2->sval;
    return NULL;
}

/* ── Pre-scan program and register all DEFINE'd functions ── */
void prescan_defines(Program *prog)
{
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        const char *spec = define_spec_from_expr(s->subject);
        if (spec && *spec) {
            const char *entry = define_entry_from_expr(s->subject);
            if (entry) DEFINE_fn_entry(spec, NULL, entry);
            else       DEFINE_fn(spec, NULL);
        }
    }
}


DESCR_t         interp_eval(EXPR_t *e);      /* forward */
DESCR_t  interp_eval_pat(EXPR_t *e);  /* forward — pattern context */
static DESCR_t *interp_eval_ref(EXPR_t *e);  /* forward — lvalue → DESCR_t* (SIL NAME ptr) */
static inline void set_and_trace(const char *name, DESCR_t val); /* forward */

/* NAME_DEREF: dereference a DT_N.
 * IS_NAMEPTR (slen=1) -> interior ptr, dereference directly.
 * IS_NAMEVAL (slen=0) -> name string, look up in NV store. */
static inline DESCR_t NAME_DEREF(DESCR_t d) {
    if (IS_NAME(d)) {
        if (IS_NAMEPTR(d)) return NAME_DEREF_PTR(d);
        if (IS_NAMEVAL(d)) return NV_GET_fn(d.s);
    }
    return d;
}
/* NAME_SET: write val through a DT_N lvalue.
 * IS_NAMEPTR -> interior ptr; IS_NAMEVAL -> NV store. */
static inline int NAME_SET(DESCR_t nd, DESCR_t val) {
    if (IS_NAME(nd)) {
        if (IS_NAMEPTR(nd)) { NAME_DEREF_PTR(nd) = val; return 1; }
        if (IS_NAMEVAL(nd)) { set_and_trace(nd.s, val); return 1; }
    }
    return 0;
}

/* T-0: set_and_trace — NV_SET_fn + VALUE trace hook for monitor.
 * Use at every plain-variable assignment site in the --ir-run path.
 * Keywords (&STLIMIT etc.) excluded: trace_is_active only fires on
 * names registered via TRACE(var,'VALUE'), never on &-keywords. */
static inline void set_and_trace(const char *name, DESCR_t val) {
    NV_SET_fn(name, val);
    if (name && name[0] != '&' && trace_is_active(name)) comm_var(name, val);
}

/* DYN-57: E_FNC names that always yield a pattern value.
 * Mirrors PAT_FNC_NAMES in SJ-17 (sno-interp.js ec6c0b3).
 * Scoped to _expr_is_pat only — do NOT intercept E_VAR (breaks 210_indirect_ref)
 * and do NOT touch S=PR split/has_eq guard (breaks 062_capture_replacement). */
static const char *PAT_FNC_NAMES[] = {
    "ANY","NOTANY","SPAN","BREAK","BREAKX","LEN","POS","RPOS","TAB","RTAB",
    "ARB","ARBNO","REM","FAIL","SUCCEED","FENCE","ABORT","BAL","CALL", NULL
};
static int _is_pat_fnc_name(const char *s) {
    if (!s) return 0;
    for (int i = 0; PAT_FNC_NAMES[i]; i++)
        if (strcasecmp(s, PAT_FNC_NAMES[i]) == 0) return 1;
    return 0;
}

/* DYN-54: returns 1 if expr tree contains any pattern-only node.
 * Mirrors is_pat() in snobol4.y but accessible at eval time. */
static int _expr_is_pat(EXPR_t *e) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ARB: case E_ARBNO: case E_CAPT_COND_ASGN:
        case E_CAPT_IMMED_ASGN: case E_CAPT_CURSOR: case E_DEFER:
            return 1;
        default: break;
    }
    /* DYN-57: E_FNC whose name is a pattern primitive (LEN, POS, TAB, ARB, etc.) */
    if (e->kind == E_FNC && _is_pat_fnc_name(e->sval)) return 1;
    /* DYN-58: E_VAR whose name is a zero-arg pattern primitive (ARB, REM, FAIL, etc.)
     * Only in _expr_is_pat — do NOT change the general E_VAR eval path (breaks 210). */
    if (e->kind == E_VAR && _is_pat_fnc_name(e->sval)) return 1;
    for (int i = 0; i < e->nchildren; i++)
        if (_expr_is_pat(e->children[i])) return 1;
    return 0;
}

/* BP-1: return interior ptr into DATA instance field, or NULL if not found */
static DESCR_t *data_field_ptr(const char *fname, DESCR_t inst) {
    if (inst.v < DT_DATA || !inst.u) return NULL;
    DATBLK_t *blk = inst.u->type;
    if (!blk) return NULL;
    for (int i = 0; i < blk->nfields; i++)
        if (blk->fields[i] && strcasecmp(blk->fields[i], fname) == 0)
            return &inst.u->fields[i];
    return NULL;
}

/* SC-1: forward declarations for DATA registry (defined near _builtin_DATA below) */
typedef struct { char name[64]; int nfields; char fields[64][64]; } ScDatType;
static ScDatType *sc_dat_find_type(const char *name);
static ScDatType *sc_dat_find_field(const char *name, int *fidx);
static DESCR_t    sc_dat_construct(ScDatType *t, DESCR_t *args, int nargs);
static DESCR_t    sc_dat_field_get(const char *fname, DESCR_t obj);
DESCR_t _builtin_print(DESCR_t *args, int nargs);

static DESCR_t call_user_function(const char *fname, DESCR_t *args, int nargs)
{
    if (call_depth >= CALL_STACK_MAX) return FAILDESCR;

    /* ── Gather param and local names via source-case accessors ── */
    int np = FUNC_NPARAMS_fn(fname);
    int nl = FUNC_NLOCALS_fn(fname);

    char *pnames[64]; if (np > 64) np = 64;
    char *lnames[64]; if (nl > 64) nl = 64;
    for (int i = 0; i < np; i++) {
        const char *p = FUNC_PARAM_fn(fname, i);
        pnames[i] = p ? GC_strdup(p) : GC_strdup("");
    }
    for (int i = 0; i < nl; i++) {
        const char *l = FUNC_LOCAL_fn(fname, i);
        lnames[i] = l ? GC_strdup(l) : GC_strdup("");
    }

    /* fname as uppercase (NV store is case-insensitive but uppercase is canonical) */
    char ufname[128];
    {
        size_t flen = strlen(fname);
        if (flen >= sizeof(ufname)) flen = sizeof(ufname)-1;
        for (size_t i = 0; i <= flen; i++)
            ufname[i] = (char)toupper((unsigned char)fname[i]);
    }

    /* ── Determine retname: the NV variable the body writes its return value into.
     * For a normal call: retname == fname (body writes "fact" = ...).
     * For an OPSYN alias: fname="facto" but entry_label="fact"; the body writes
     * "fact", so we must save/restore "fact" and read it back on RETURN.
     * We use FUNC_ENTRY_fn(fname) as retname whenever it differs from fname
     * (case-insensitively) — that's the canonical body name. ── */
    const char *entry_pre = FUNC_ENTRY_fn(fname);
    const char *retname = fname;
    /* For OPSYN aliases: fname="facto", entry_label="fact" — entry_label IS a registered
     * function whose body writes "fact=...".  Use entry_label as the return-value slot.
     * For alternate-entry: fname="fact2", entry_label="fact2_entry" — entry_label is just
     * a label, NOT a registered function; body still writes "fact2=...".  Use fname. */
    if (entry_pre && strcasecmp(entry_pre, fname) != 0 && FNCEX_fn(entry_pre))
        retname = entry_pre;

    /* ── Save current values of retname-var, params, locals ── */
    int nsaved = 1 + np + nl;
    char   **snames = GC_malloc((size_t)nsaved * sizeof(char *));
    DESCR_t *svals  = GC_malloc((size_t)nsaved * sizeof(DESCR_t));
    /* Save/clear the return-value slot using retname (may differ from fname for OPSYN).
     * NV store is case-sensitive: function body writes "fact" not "FACT". */
    snames[0] = GC_strdup(retname);
    svals[0]  = NV_GET_fn(retname);
    NV_SET_fn(retname, STRVAL(""));    /* BUG-QIZE: clear return slot to empty string,
                                        * not NULVCL, so DIFFER(retvar) fails on entry.
                                        * SPITBOL treats cleared retname as "" (zero-length
                                        * string); NULVCL has a type tag that makes DIFFER
                                        * succeed (non-null) → divergence on Qize body. */
    for (int i = 0; i < np; i++) {
        snames[1+i] = pnames[i];
        /* If this param name aliases retname (e.g. DEFINE('f(f)')), the NV cell
         * is shared.  Record the pre-call global (already in svals[0]), then
         * write the arg.  The body writes retname= to set return value — same
         * NV cell as the param, which is correct SIL behaviour. */
        if (strcasecmp(pnames[i], retname) == 0)
            svals[1+i] = svals[0];          /* dedup: same original global */
        else
            svals[1+i] = NV_GET_fn(pnames[i]);
        NV_SET_fn(pnames[i], (i < nargs) ? args[i] : NULVCL);
    }
    for (int i = 0; i < nl; i++) {
        snames[1+np+i] = lnames[i];
        svals[1+np+i]  = NV_GET_fn(lnames[i]);
        NV_SET_fn(lnames[i], NULVCL);
    }

    /* ── Push call frame ── */
    CallFrame *fr = &call_stack[call_depth++];
    kw_fnclevel = call_depth;  /* &FNCLEVEL tracks live nesting depth */
    strncpy(fr->fname, retname, sizeof(fr->fname)-1);
    fr->fname[sizeof(fr->fname)-1] = '\0';
    fr->saved_names = snames;
    fr->saved_vals  = svals;
    fr->nsaved      = nsaved;

    DESCR_t retval = NULVCL;

    comm_call(fname);   /* T-2: FUNCTION trace CALL event */

    int ret_kind = setjmp(fr->ret_env);
    if (ret_kind == 0) {
        /* ── Find body label: use entry_label (supports OPSYN aliases and
         * alternate entry points), then fall back to fname/ufname ── */
        const char *entry = FUNC_ENTRY_fn(fname);
        STMT_t *body = entry ? label_lookup(entry) : NULL;
        if (!body) body = label_lookup(fname);
        if (!body) body = label_lookup(ufname);

        /* SIL UNDF: no body label AND not a registered builtin → Error 5 (soft) */
        if (!body && !FNCEX_fn(fname) && !FNCEX_fn(ufname)) {
            sno_runtime_error(5, NULL);
            /* longjmp taken above; this line only reached if !g_sno_err_active */
            retval = FAILDESCR;
            goto fn_done;
        }

        if (body) {
            STMT_t *s = body;
            while (s) {
                if (s->is_end) break;
                if (s->subject && (s->subject->kind == E_CHOICE ||
                                   s->subject->kind == E_UNIFY  ||
                                   s->subject->kind == E_CLAUSE)) {
                    s = s->next; continue;
                }

                DESCR_t     subj_val  = NULVCL;
                const char *subj_name = NULL;
                if (s->subject) {
                    if (s->subject->kind == E_VAR && s->subject->sval) {
                        subj_name = s->subject->sval;
                        /* Only read value when needed for pattern match */
                        if (s->pattern)
                            subj_val = NV_GET_fn(subj_name);
                    } else if (s->subject->kind == E_INDIRECT && s->subject->nchildren > 0) {
                        /* $'$B' or $X as subject — resolve to variable name for write-back.
                         * child is E_QLIT "$B" (literal) or E_VAR X (runtime indirect). */
                        EXPR_t *ic = s->subject->children[0];
                        if (ic->kind == E_QLIT && ic->sval) {
                            subj_name = ic->sval;              /* $'name' — literal */
                        } else if (ic->kind == E_VAR && ic->sval) {
                            DESCR_t xv = NV_GET_fn(ic->sval); /* $X — indirect */
                            subj_name = VARVAL_fn(xv);
                        } else {
                            DESCR_t nd = interp_eval(ic);
                            subj_name = VARVAL_fn(nd);
                        }
                        if (subj_name && s->pattern) {
                            subj_val = NV_GET_fn(subj_name);
                        } else if (!subj_name)
                            subj_val = interp_eval(s->subject);
                    } else {
                        subj_val = interp_eval(s->subject);
                    }
                }

                int succeeded = 1;
                if (s->pattern) {
                    DESCR_t pat_d = interp_eval_pat(s->pattern);
                    if (IS_FAIL_fn(pat_d)) {
                        succeeded = 0;
                    } else {
                        DESCR_t repl_val; int has_repl = 0;
                        if (s->has_eq && s->replacement) {
                            repl_val = interp_eval(s->replacement);
                            has_repl = !IS_FAIL_fn(repl_val);
                        }
                        Σ = subj_name ? subj_name : "";
                        succeeded = exec_stmt(subj_name,
                            subj_name ? NULL : &subj_val,
                            pat_d, has_repl ? &repl_val : NULL, has_repl);
                    }
                } else if (s->has_eq && subj_name) {
                    /* Plain assignment: X = expr  — always value context.
                     * *expr produces DT_E EXPRESSION (RUNTIME-6), not pattern. */
                    DESCR_t repl_val = s->replacement
                        ? interp_eval(s->replacement)
                        : NULVCL;
                    /* BP-1: if the RHS was a NRETURN function call, interp_eval
                     * NAME_DEREFs the DT_N (value context). But we want to store the
                     * DT_N itself so the caller can later use $nm for indirect assign.
                     * kw_rtntype is set by call_user_function before it returns and
                     * is still valid here (no nested call between interp_eval return
                     * and this check). Re-fetch from the function's return variable. */
                    if (strcasecmp(kw_rtntype, "NRETURN") == 0
                            && s->replacement && s->replacement->kind == E_FNC
                            && s->replacement->sval) {
                        DESCR_t raw = NV_GET_fn(s->replacement->sval);
                        if (IS_NAME(raw)) repl_val = raw;
                    }
                    if (IS_FAIL_fn(repl_val)) succeeded = 0;
                    else {
                        /* NRETURN lvalue write-through: subj_name may be a zero-param
                         * user fn returning DT_N (name ref). Only check when not already
                         * inside a function body (call_depth==0) to avoid re-entrant
                         * assignment during body execution (e.g. "ref_a = .a" in body). */
                        if (call_depth == 0 && FNCEX_fn(subj_name)
                                && FUNC_NPARAMS_fn(subj_name) == 0) {
                            DESCR_t fres = call_user_function(subj_name, NULL, 0);
                            if (NAME_SET(fres, repl_val)) { succeeded = 1; }
                            else { set_and_trace(subj_name, repl_val); succeeded = 1; }
                        } else { set_and_trace(subj_name, repl_val); succeeded = 1; }
                    }
                } else if (s->has_eq && s->subject && s->subject->kind == E_KEYWORD && s->subject->sval) {
                    DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
                    if (IS_FAIL_fn(repl_val)) succeeded = 0;
                    else {
                        /* SIL ASGNIC: delegate to ASGNIC_fn (snobol4.c export).
                         * Coerces to INTEGER and writes keyword global.
                         * Falls back to NV_SET_fn for unrecognised names (safety). */
                        if (!ASGNIC_fn(s->subject->sval, repl_val))
                            NV_SET_fn(s->subject->sval, repl_val);
                        succeeded = 1;
                    }
                } else if (s->has_eq && s->subject && s->subject->kind == E_IDX &&
                           s->subject->nchildren >= 2) {
                    DESCR_t base = interp_eval(s->subject->children[0]);
                    DESCR_t idx  = interp_eval(s->subject->children[1]);
                    DESCR_t rv   = s->replacement ? interp_eval(s->replacement) : NULVCL;
                    if (IS_FAIL_fn(base)||IS_FAIL_fn(idx)||IS_FAIL_fn(rv)) succeeded = 0;
                    else {
                        if (s->subject->nchildren == 3) {
                            DESCR_t idx2 = interp_eval(s->subject->children[2]);
                            subscript_set2(base, idx, idx2, rv);
                        } else { subscript_set(base, idx, rv); }
                        succeeded = 1;
                    }
                } else if (s->has_eq && s->subject && s->subject->kind == E_FNC &&
                           s->subject->sval && s->subject->nchildren >= 1) {
                    /* ITEM(arr,i[,j]) = val  or  field(obj) = val at statement level */
                    DESCR_t rv = s->replacement ? interp_eval(s->replacement) : NULVCL;
                    if (!IS_FAIL_fn(rv)) {
                        if (strcasecmp(s->subject->sval, "ITEM") == 0 && s->subject->nchildren >= 2) {
                            DESCR_t base = interp_eval(s->subject->children[0]);
                            DESCR_t idx  = interp_eval(s->subject->children[1]);
                            if (!IS_FAIL_fn(base) && !IS_FAIL_fn(idx)) {
                                if (s->subject->nchildren >= 3) {
                                    DESCR_t idx2 = interp_eval(s->subject->children[2]);
                                    if (!IS_FAIL_fn(idx2)) subscript_set2(base, idx, idx2, rv);
                                } else {
                                    subscript_set(base, idx, rv);
                                }
                                succeeded = 1;
                            } else succeeded = 0;
                        } else {
                            /* DATA field setter: fname(obj) = val */
                            DESCR_t obj = interp_eval(s->subject->children[0]);
                            if (!IS_FAIL_fn(obj)) {
                                FIELD_SET_fn(obj, s->subject->sval, rv);
                                succeeded = 1;
                            } else succeeded = 0;
                        }
                    } else succeeded = 0;
                } else if (s->has_eq && s->subject && s->subject->kind == E_FNC &&
                           s->subject->sval && s->subject->nchildren == 0) {
                    /* NRETURN lvalue assign: ref_a() = val  (zero-arg fn call as lvalue)
                     * Call the function; if result is DT_N write through to named variable. */
                    DESCR_t fres = call_user_function(s->subject->sval, NULL, 0);
                    if (IS_NAME(fres)) {
                        DESCR_t rv = s->replacement ? interp_eval(s->replacement) : NULVCL;
                        if (IS_FAIL_fn(rv)) succeeded = 0;
                        else { succeeded = NAME_SET(fres, rv) ? 1 : 0; }
                    } else succeeded = 0;
                } else if (s->has_eq && s->subject && s->subject->kind == E_INDIRECT) {
                    EXPR_t *ichild = s->subject->nchildren > 0 ? s->subject->children[0] : NULL;
                    DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
                    if (IS_FAIL_fn(repl_val)) { succeeded = 0; }
                    else {
                        /* Evaluate the inner expr to get a NAME or string to indirect through */
                        DESCR_t ind_val = ichild ? interp_eval(ichild) : NULVCL;
                        /* If it's already a DT_N (e.g. $Push where Push = .stk[1]),
                         * write directly through the pointer — SIL ASGNVV semantics */
                        if (IS_NAMEPTR(ind_val)) {
                            *(DESCR_t*)ind_val.ptr = repl_val; succeeded = 1;
                        } else {
                            /* Otherwise treat as string variable name */
                            const char *nm = VARVAL_fn(ind_val);
                            if (!nm || !*nm) { succeeded = 0; }
                            else {
                                /* If the named variable itself holds a DT_N, write through */
                                DESCR_t named = NV_GET_fn(nm);
                                if (IS_NAMEPTR(named)) {
                                    NAME_DEREF_PTR(named) = repl_val; succeeded = 1;
                                } else {
                                    set_and_trace(nm, repl_val); succeeded = 1;
                                }
                            }
                        }
                    }
                } else if (s->subject && !s->pattern && !s->has_eq) {
                    if (IS_FAIL_fn(subj_val)) succeeded = 0;
                }

                const char *target = NULL;
                if (s->go) {
                    if (s->go->uncond && *s->go->uncond)
                        target = s->go->uncond;
                    else if (s->go->computed_uncond_expr) {
                        DESCR_t cv = interp_eval(s->go->computed_uncond_expr);
                        target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
                    } else if (succeeded && s->go->onsuccess && *s->go->onsuccess)
                        target = s->go->onsuccess;
                    else if (succeeded && s->go->computed_success_expr) {
                        DESCR_t cv = interp_eval(s->go->computed_success_expr);
                        target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
                    } else if (!succeeded && s->go->onfailure && *s->go->onfailure)
                        target = s->go->onfailure;
                    else if (!succeeded && s->go->computed_failure_expr) {
                        DESCR_t cv = interp_eval(s->go->computed_failure_expr);
                        target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
                    }
                }

                if (target) {
                        if (strcasecmp(target, "END") == 0) break;
                    if (strcasecmp(target, "RETURN") == 0) {
                        retval = NV_GET_fn(fr->fname);
                        strncpy(kw_rtntype, "RETURN",  sizeof(kw_rtntype)-1);
                        goto fn_done;
                    }
                    if (strcasecmp(target, "FRETURN") == 0) {
                        retval = FAILDESCR;
                        strncpy(kw_rtntype, "FRETURN", sizeof(kw_rtntype)-1);
                        goto fn_done;
                    }
                    if (strcasecmp(target, "NRETURN") == 0) {
                        /* NRETURN: return DT_N from fn return var as-is;
                         * caller (E_FNC) applies NAME_DEREF (slen discriminates
                         * NAMEPTR from NAMEVAL). */
                        retval = NV_GET_fn(fr->fname);
                        strncpy(kw_rtntype, "NRETURN", sizeof(kw_rtntype)-1);
                        goto fn_done;
                    }
                    STMT_t *dest = label_lookup(target);
                    if (dest) { s = dest; continue; }
                    break;
                }
                s = s->next;
            }
        }
        /* fell off body without RETURN — return function's name variable */
        retval = NV_GET_fn(fr->fname);
        strncpy(kw_rtntype, "RETURN",  sizeof(kw_rtntype)-1);
    } else if (ret_kind == 1) {
        retval = NV_GET_fn(fr->fname);
        strncpy(kw_rtntype, "RETURN",  sizeof(kw_rtntype)-1);
    } else {
        retval = FAILDESCR;
        strncpy(kw_rtntype, "FRETURN", sizeof(kw_rtntype)-1);
    }

fn_done:
    comm_return(fname, retval);  /* T-2: FUNCTION trace RETURN event */
    /* ── Restore saved variables and pop frame ── */
    for (int i = 0; i < nsaved; i++)
        NV_SET_fn(snames[i], svals[i]);
    call_depth--;
    kw_fnclevel = call_depth;
    return retval;
}

/* ══════════════════════════════════════════════════════════════════════════
 * eval_node_interp — thin wrapper; reuses eval_node from eval_code.c
 * via eval_expr (we call it by re-parsing for non-trivial exprs,
 * but for the common case we use NV_GET_fn / NV_SET_fn directly).
 *
 * For the interpreter we need direct EXPR_t evaluation, not string
 * re-parse.  We replicate the minimal logic needed here rather than
 * exposing eval_node (which is static in eval_code.c).
 * ══════════════════════════════════════════════════════════════════════════ */

/* Forward declaration (also declared above for call_user_function) */

DESCR_t interp_eval(EXPR_t *e)
{
    if (!e) return NULVCL;

    /* OE-5: Icon frame dispatch — E_VAR/E_ASSIGN/E_FNC differ between SNO and ICN.
     * All other EKinds fall through to the shared switch (already has Icon cases
     * from OE-3/OE-4). Guard: only active inside an Icon call frame. */
    if (icn_frame_depth > 0) {
        switch (e->kind) {
        case E_VAR: {
            if (e->sval && e->sval[0] == '&') {
                const char *kw = e->sval + 1;
                if (!strcmp(kw,"subject")) return icn_scan_subj ? STRVAL(icn_scan_subj) : NULVCL;
                if (!strcmp(kw,"pos"))     return INTVAL(icn_scan_pos);
                if (!strcmp(kw,"letters")) return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
                if (!strcmp(kw,"ucase"))   return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
                if (!strcmp(kw,"lcase"))   return STRVAL("abcdefghijklmnopqrstuvwxyz");
                if (!strcmp(kw,"digits"))  return STRVAL("0123456789");
                if (!strcmp(kw,"null"))    return NULVCL;
                if (!strcmp(kw,"fail"))    return FAILDESCR;
                return NULVCL;
            }
            int slot = (int)e->ival;
            if (slot >= 0 && slot < ICN_CUR.env_n) return ICN_CUR.env[slot];
            if (slot < 0 && e->sval && e->sval[0] != '&') return NV_GET_fn(e->sval);
            return NULVCL;
        }
        case E_ASSIGN: {
            if (e->nchildren < 2) return NULVCL;
            DESCR_t val = interp_eval(e->children[1]);
            if (IS_FAIL_fn(val)) return FAILDESCR;
            EXPR_t *lhs = e->children[0];
            if (lhs && lhs->kind == E_VAR) {
                int slot = (int)lhs->ival;
                if (slot >= 0 && slot < ICN_CUR.env_n) { ICN_CUR.env[slot] = val; return val; }
                if (slot < 0 && lhs->sval && lhs->sval[0] != '&') NV_SET_fn(lhs->sval, val);
            }
            return val;
        }
        case E_FNC: {
            /* Icon call nodes: sval=NULL, name in children[0]->sval */
            if (e->nchildren < 1) return NULVCL;
            const char *fn = e->children[0] ? e->children[0]->sval : NULL;
            if (!fn) return NULVCL;
            int nargs = e->nchildren - 1;
            if (!strcmp(fn,"write")) {
                if (nargs == 0) { printf("\n"); return NULVCL; }
                DESCR_t a = interp_eval(e->children[1]);
                if (IS_FAIL_fn(a)) return FAILDESCR;
                if (IS_INT_fn(a)) printf("%lld\n",(long long)a.i);
                else if (IS_REAL_fn(a)) printf("%g\n",a.r);
                else { const char *s=VARVAL_fn(a); printf("%s\n",s?s:""); }
                return a;
            }
            if (!strcmp(fn,"writes")) {
                if (nargs == 0) return NULVCL;
                DESCR_t a = interp_eval(e->children[1]);
                if (IS_INT_fn(a)) printf("%lld",(long long)a.i);
                else if (IS_REAL_fn(a)) printf("%g",a.r);
                else { const char *s=VARVAL_fn(a); printf("%s",s?s:""); }
                return a;
            }
            if (!strcmp(fn,"read"))  return NULVCL;
            if (!strcmp(fn,"stop"))  { exit(0); }
            if (!strcmp(fn,"any") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t cs=interp_eval(e->children[1]);
                const char *s=icn_scan_subj,*cv=VARVAL_fn(cs); int p=icn_scan_pos-1;
                if(!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
                icn_scan_pos++; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"many") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t cs=interp_eval(e->children[1]);
                const char *s=icn_scan_subj,*cv=VARVAL_fn(cs); int p=icn_scan_pos-1;
                if(!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
                while(p<(int)strlen(s)&&strchr(cv,s[p])) p++;
                icn_scan_pos=p+1; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"upto") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t cs=interp_eval(e->children[1]);
                const char *s=icn_scan_subj,*cv=VARVAL_fn(cs); if(!s||!cv) return FAILDESCR;
                int p=icn_scan_pos-1;
                while(p<(int)strlen(s)&&!strchr(cv,s[p])) p++;
                if(p>=(int)strlen(s)) return FAILDESCR; return INTVAL(p+1);
            }
            if (!strcmp(fn,"move") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t nv=interp_eval(e->children[1]); int newp=icn_scan_pos+(int)nv.i;
                if(!icn_scan_subj||newp<1||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
                int old=icn_scan_pos; icn_scan_pos=newp; size_t len=(size_t)nv.i;
                char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"tab") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t nv=interp_eval(e->children[1]); if(IS_FAIL_fn(nv)) return FAILDESCR;
                int newp=(int)nv.i;
                if(!icn_scan_subj||newp<icn_scan_pos||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
                int old=icn_scan_pos; icn_scan_pos=newp; size_t len=(size_t)(newp-old);
                char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"match") && nargs>=1 && icn_scan_pos>0) {
                DESCR_t sv=interp_eval(e->children[1]);
                const char *needle=VARVAL_fn(sv),*hay=icn_scan_subj?icn_scan_subj:"";
                if(!needle) return FAILDESCR;
                int p=icn_scan_pos-1,nl=(int)strlen(needle);
                if(strncmp(hay+p,needle,nl)!=0) return FAILDESCR;
                icn_scan_pos+=nl; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"find") && nargs>=2) {
                long pos1; if(icn_gen_lookup(e,&pos1)) return INTVAL(pos1);
                DESCR_t s1=interp_eval(e->children[1]),s2=interp_eval(e->children[2]);
                const char *needle=VARVAL_fn(s1),*hay=VARVAL_fn(s2);
                if(!needle||!hay) return FAILDESCR;
                char *p=strstr(hay,needle);
                return p?INTVAL((long long)(p-hay)+1):FAILDESCR;
            }
            for (int i=0; i<icn_proc_count; i++) {
                if (!strcmp(icn_proc_table[i].name,fn)) {
                    DESCR_t args[ICN_SLOT_MAX];
                    for (int j=0; j<nargs&&j<ICN_SLOT_MAX; j++)
                        args[j]=interp_eval(e->children[1+j]);
                    return icn_call_proc(icn_proc_table[i].proc,args,nargs);
                }
            }
            /* RK-14: array builtins — arrays stored as \x01-separated strings */
            if (!strcmp(fn,"push") && nargs == 2) {
                DESCR_t arr = interp_eval(e->children[1]);
                DESCR_t val = interp_eval(e->children[2]);
                char vbuf[64]; const char *vs;
                if (IS_INT_fn(val))       { snprintf(vbuf,sizeof vbuf,"%lld",(long long)val.i); vs=vbuf; }
                else if (IS_REAL_fn(val)) { snprintf(vbuf,sizeof vbuf,"%g",val.r); vs=vbuf; }
                else vs = (val.s && *val.s) ? val.s : "";
                const char *as = (arr.v==DT_S||arr.v==DT_SNUL) ? (arr.s?arr.s:"") : "";
                size_t al=strlen(as), vl=strlen(vs);
                char *buf;
                if (al == 0) { buf=GC_malloc(vl+1); memcpy(buf,vs,vl+1); }
                else { buf=GC_malloc(al+1+vl+1); memcpy(buf,as,al); buf[al]='\x01'; memcpy(buf+al+1,vs,vl+1); }
                if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                    e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                    ICN_CUR.env[e->children[1]->ival] = STRVAL(buf);
                return STRVAL(buf);
            }
            if (!strcmp(fn,"elems") && nargs == 1) {
                DESCR_t arr = interp_eval(e->children[1]);
                const char *as = (arr.v==DT_S||arr.v==DT_SNUL) ? (arr.s?arr.s:"") : "";
                if (!*as) return INTVAL(0);
                long cnt = 1;
                for (const char *p=as; *p; p++) if (*p=='\x01') cnt++;
                return INTVAL(cnt);
            }
            if (!strcmp(fn,"pop") && nargs == 1) {
                DESCR_t arr = interp_eval(e->children[1]);
                const char *as = (arr.v==DT_S||arr.v==DT_SNUL) ? (arr.s?arr.s:"") : "";
                if (!*as) return FAILDESCR;
                char *buf = GC_malloc(strlen(as)+1); strcpy(buf, as);
                char *last = strrchr(buf, '\x01');
                char *popped;
                if (last) { popped=GC_malloc(strlen(last+1)+1); strcpy(popped,last+1); *last='\0'; }
                else       { popped=GC_malloc(strlen(buf)+1);   strcpy(popped,buf);    buf[0]='\0'; }
                if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                    e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                    ICN_CUR.env[e->children[1]->ival] = STRVAL(buf);
                return STRVAL(popped);
            }
            if (!strcmp(fn,"arr_get") && nargs == 2) {
                DESCR_t arr = interp_eval(e->children[1]);
                DESCR_t idx = interp_eval(e->children[2]);
                const char *as = (arr.v==DT_S||arr.v==DT_SNUL) ? (arr.s?arr.s:"") : "";
                long i = IS_INT_fn(idx) ? idx.i : 0;
                long cur = 0; const char *seg = as;
                while (cur < i) {
                    const char *nx = strchr(seg, '\x01');
                    if (!nx) return FAILDESCR;
                    seg = nx+1; cur++;
                }
                const char *end = strchr(seg, '\x01');
                size_t len = end ? (size_t)(end-seg) : strlen(seg);
                char *out = GC_malloc(len+1); memcpy(out,seg,len); out[len]='\0';
                return STRVAL(out);
            }
            if (!strcmp(fn,"arr_set") && nargs == 3) {
                DESCR_t arr = interp_eval(e->children[1]);
                DESCR_t idx = interp_eval(e->children[2]);
                DESCR_t val = interp_eval(e->children[3]);
                const char *as = (arr.v==DT_S||arr.v==DT_SNUL) ? (arr.s?arr.s:"") : "";
                long target = IS_INT_fn(idx) ? idx.i : 0;
                char vbuf[64]; const char *vs;
                if (IS_INT_fn(val)) { snprintf(vbuf,sizeof vbuf,"%lld",(long long)val.i); vs=vbuf; }
                else vs = (val.s && *val.s) ? val.s : "";
                char *out = GC_malloc(strlen(as)+strlen(vs)+64);
                out[0]='\0'; long cur=0; const char *seg=as;
                while (*seg || cur <= target) {
                    const char *end2 = strchr(seg, '\x01');
                    size_t slen = end2 ? (size_t)(end2-seg) : strlen(seg);
                    if (out[0]) strcat(out,"\x01");
                    if (cur==target) strcat(out,vs);
                    else             strncat(out,seg,slen);
                    seg = end2 ? end2+1 : seg+slen;
                    cur++;
                    if (!end2 && cur > target) break;
                }
                if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                    e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                    ICN_CUR.env[e->children[1]->ival] = STRVAL(out);
                return STRVAL(out);
            }

            /* ── RK-15: Hash builtins ───────────────────────────────────────
             * Hashes stored as \x02-separated "key\x03value" pair strings.
             * hash_set(h,k,v): upsert; hash_get(h,k): lookup or NULVCL;
             * hash_exists(h,k): 1/0; hash_keys(h): \x01-sep key list;
             * hash_values(h): \x01-sep value list.                        */
#define HS '\x02'   /* pair separator */
#define HK '\x03'   /* key/value separator within a pair */
            if ((!strcmp(fn,"hash_set") && nargs == 3) ||
                (!strcmp(fn,"hash_get") && nargs == 2) ||
                (!strcmp(fn,"hash_exists") && nargs == 2) ||
                (!strcmp(fn,"hash_keys") && nargs == 1) ||
                (!strcmp(fn,"hash_values") && nargs == 1)) {
                DESCR_t hd = interp_eval(e->children[1]);
                const char *hs = (hd.v==DT_S||hd.v==DT_SNUL) ? (hd.s?hd.s:"") : "";
                if (!strcmp(fn,"hash_set")) {
                    DESCR_t kd = interp_eval(e->children[2]);
                    DESCR_t vd = interp_eval(e->children[3]);
                    char kb[64], vb[64];
                    const char *ks = IS_INT_fn(kd)  ? (snprintf(kb,sizeof kb,"%lld",(long long)kd.i),kb)
                                   : IS_REAL_fn(kd) ? (snprintf(kb,sizeof kb,"%g",kd.r),kb)
                                   : (kd.s&&*kd.s?kd.s:"");
                    const char *vs = IS_INT_fn(vd)  ? (snprintf(vb,sizeof vb,"%lld",(long long)vd.i),vb)
                                   : IS_REAL_fn(vd) ? (snprintf(vb,sizeof vb,"%g",vd.r),vb)
                                   : (vd.s&&*vd.s?vd.s:"");
                    size_t kl=strlen(ks);
                    char *out = GC_malloc(strlen(hs)+kl+strlen(vs)+4); out[0]='\0';
                    const char *p = hs;
                    while (*p) {
                        const char *sep = strchr(p, HK); const char *end = strchr(p, HS);
                        if (!sep) break;
                        size_t pkl=(size_t)(sep-p);
                        if (pkl!=kl || memcmp(p,ks,kl)!=0) {
                            if (out[0]) { size_t ol=strlen(out); out[ol]=HS; out[ol+1]='\0'; }
                            size_t plen=end?(size_t)(end-p):strlen(p); strncat(out,p,plen);
                        }
                        if (!end) break; p=end+1;
                    }
                    if (out[0]) { size_t ol=strlen(out); out[ol]=HS; out[ol+1]='\0'; }
                    strcat(out,ks); { size_t ol=strlen(out); out[ol]=HK; out[ol+1]='\0'; } strcat(out,vs);
                    if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                        e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                        ICN_CUR.env[e->children[1]->ival] = STRVAL(out);
                    return STRVAL(out);
                }
                if (!strcmp(fn,"hash_get") || !strcmp(fn,"hash_exists")) {
                    DESCR_t kd = interp_eval(e->children[2]);
                    char kb[64];
                    const char *ks = IS_INT_fn(kd)  ? (snprintf(kb,sizeof kb,"%lld",(long long)kd.i),kb)
                                   : IS_REAL_fn(kd) ? (snprintf(kb,sizeof kb,"%g",kd.r),kb)
                                   : (kd.s&&*kd.s?kd.s:"");
                    size_t kl=strlen(ks);
                    const char *p=hs;
                    while (*p) {
                        const char *sep=strchr(p,HK); const char *end=strchr(p,HS);
                        if (!sep) break;
                        size_t pkl=(size_t)(sep-p);
                        if (pkl==kl && memcmp(p,ks,kl)==0) {
                            if (!strcmp(fn,"hash_exists")) return INTVAL(1);
                            const char *vs=sep+1;
                            size_t vl=end?(size_t)(end-vs):strlen(vs);
                            char *out=GC_malloc(vl+1); memcpy(out,vs,vl); out[vl]='\0';
                            return STRVAL(out);
                        }
                        if (!end) break; p=end+1;
                    }
                    return !strcmp(fn,"hash_exists") ? INTVAL(0) : NULVCL;
                }
                if (!strcmp(fn,"hash_keys") || !strcmp(fn,"hash_values")) {
                    if (!*hs) { char *e2=GC_malloc(1); e2[0]='\0'; return STRVAL(e2); }
                    char *out=GC_malloc(strlen(hs)+2); out[0]='\0';
                    const char *p=hs;
                    while (*p) {
                        const char *sep=strchr(p,HK); const char *end=strchr(p,HS);
                        if (!sep) break;
                        if (out[0]) { size_t ol=strlen(out); out[ol]='\x01'; out[ol+1]='\0'; }
                        if (!strcmp(fn,"hash_keys")) {
                            strncat(out,p,(size_t)(sep-p));
                        } else {
                            const char *vs=sep+1;
                            size_t vl=end?(size_t)(end-vs):strlen(vs);
                            strncat(out,vs,vl);
                        }
                        if (!end) break; p=end+1;
                    }
                    return STRVAL(out);
                }
            }
#undef HS
#undef HK

            return NULVCL;
        }
        default: break;
        }
    }

    switch (e->kind) {
    case E_ILIT:   return INTVAL(e->ival);
    case E_FLIT:   return REALVAL(e->dval);
    case E_QLIT:   return e->sval ? STRVAL(e->sval) : NULVCL;
    case E_NUL:    return NULVCL;

    case E_VAR:
        if (e->sval && *e->sval) {
            DESCR_t _vr = NV_GET_fn(e->sval);
            if (!IS_NULL(_vr)) return _vr;
            /* Zero-arg builtin (ARB, REM, FAIL, SUCCEED, etc.) stored as
               function, not variable — only try if name is a registered fn.
               Guard prevents unset ordinary variables from spuriously calling
               APPLY_fn and triggering Error 5. */
            if (FNCEX_fn(e->sval)) {
                DESCR_t _fr = APPLY_fn(e->sval, NULL, 0);
                if (!IS_NULL(_fr)) return _fr;
            }
            return _vr; /* unset variable */
        }
        return NULVCL;

    case E_KEYWORD: {
        if (!e->sval || !*e->sval) return NULVCL;
        /* Keywords stored without & prefix in NV store */
        return NV_GET_fn(e->sval);
    }

    case E_INTERROGATE: {
        /* ?X — o$int: null string if X succeeds; fail if X fails */
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t v = interp_eval(e->children[0]);
        if (IS_FAIL_fn(v)) return FAILDESCR;
        return NULVCL;
    }

    case E_NAME: {
        /* .X — dot operator: delegate to NAME_fn (snobol4.c export).
         * NAME_fn returns NAMEVAL for keywords/IO vars (not addressable by ptr)
         * and NAMEPTR (interior ptr) for ordinary NV cells.
         * BP-1: .field(x) — E_FNC child with one arg — must return NAMEPTR into
         * the DATA struct field cell, not a name-table lookup. */
        if (e->nchildren < 1) return FAILDESCR;
        EXPR_t *child = e->children[0];
        if (child->kind == E_FNC && child->sval && child->nchildren == 1) {
            DESCR_t inst = interp_eval(child->children[0]);
            DESCR_t *cell = data_field_ptr(child->sval, inst);
            if (cell) return NAMEPTR(cell);
        }
        if ((child->kind == E_VAR || child->kind == E_KEYWORD)
                && child->sval)
            return NAME_fn(child->sval);
        DESCR_t *cell = interp_eval_ref(child);
        if (cell) return NAMEPTR(cell);
        return FAILDESCR;
    }

    case E_MNS:
        if (e->nchildren < 1) return FAILDESCR;
        return neg(interp_eval(e->children[0]));

    /* OE-5: E_RETURN for Icon/Raku return statements */
    case E_RETURN: {
        if (icn_frame_depth > 0) {
            ICN_CUR.return_val = (e->nchildren > 0)
                ? interp_eval(e->children[0]) : NULVCL;
            ICN_CUR.returning = 1;
            return ICN_CUR.return_val;
        }
        return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
    }

    case E_PLS: {
        /* Unary + coerces operand to numeric (int or real) */
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t v = interp_eval(e->children[0]);
        if (IS_FAIL_fn(v)) return FAILDESCR;
        if (IS_INT(v) || IS_REAL(v)) return v;
        /* String → try integer, then real */
        const char *s = VARVAL_fn(v);
        if (!s || !*s) return INTVAL(0);
        char *end = NULL;
        long long iv = strtoll(s, &end, 10);
        if (end && *end == '\0') return INTVAL(iv);
        double dv = strtod(s, &end);
        if (end && *end == '\0') return REALVAL(dv);
        return INTVAL(0);
    }

    case E_OPSYN: {
        /* OPSYN operator: sval holds the operator symbol ("@", "&").
         * Dispatch via APPLY_fn — OPSYN registration aliased the symbol
         * to the target function via register_fn_alias. */
        if (!e->sval) return FAILDESCR;
        if (e->nchildren == 2) {
            DESCR_t l = interp_eval(e->children[0]);
            DESCR_t r = interp_eval(e->children[1]);
            if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
            DESCR_t args[2] = { l, r };
            return APPLY_fn(e->sval, args, 2);
        } else if (e->nchildren == 1) {
            DESCR_t v = interp_eval(e->children[0]);
            if (IS_FAIL_fn(v)) return FAILDESCR;
            DESCR_t args[1] = { v };
            return APPLY_fn(e->sval, args, 1);
        }
        return FAILDESCR;
    }

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
    case E_MUL: {
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
    case E_MOD: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        long li = IS_INT_fn(l) ? l.i : (long)l.r;
        long ri = IS_INT_fn(r) ? r.i : (long)r.r;
        return ri ? INTVAL(li % ri) : FAILDESCR;
    }
    case E_POW: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = interp_eval(e->children[0]);
        DESCR_t r = interp_eval(e->children[1]);
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR;
        return POWER_fn(l, r);
    }

    case E_CAT:
    case E_SEQ: {
        if (e->nchildren == 0) return NULVCL;
        /* DYN-59: interp_eval is STRING context by default; pattern context
         * uses interp_eval_pat() which calls pat_cat unconditionally.
         * DYN-68: mixed-mode: if the accumulated value is DT_P (pattern),
         * switch to pat_cat so that pattern-building expressions like
         * "icase = icase (upr(c) | lwr(c))" work correctly in value context.
         * SNOBOL4 rule: concatenation of pattern with anything yields pattern.
         * RT-112: once we detect a pattern operand, re-evaluate ALL remaining
         * children via interp_eval_pat so *var/*func become XDSAR/XATP nodes
         * rather than frozen DT_E (which pat_cat cannot handle). */
        DESCR_t acc = interp_eval(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        int in_pat_mode = IS_PAT(acc);
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t nxt;
            if (in_pat_mode) {
                nxt = interp_eval_pat(e->children[i]);
            } else {
                nxt = interp_eval(e->children[i]);
            }
            if (IS_FAIL_fn(nxt)) return FAILDESCR;
            if (in_pat_mode || IS_PAT(nxt)) {
                if (!in_pat_mode) {
                    /* First pattern seen mid-concat: re-eval this child in pat ctx */
                    nxt = interp_eval_pat(e->children[i]);
                    in_pat_mode = 1;
                }
                acc = pat_cat(acc, nxt);
            } else {
                /* SPITBOL rule: if either operand is null string, return the
                 * other operand UNCHANGED (no type coercion). Spec §concat:
                 * "if either operand is the null string, the other operand is
                 *  returned unchanged. It is not coerced into the string type." */
                if (acc.v == DT_SNUL)
                    acc = nxt;
                else if (nxt.v == DT_SNUL)
                    { /* acc unchanged */ }
                else
                    acc = CONCAT_fn(acc, nxt);
            }
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
            NV_SET_fn(lv->sval, val);  /* inner expr assign: no trace (stmt-level already traced) */
        else if (lv && lv->kind == E_IDX && lv->nchildren >= 2) {
            /* arr<i> = val  or  arr<i,j> = val */
            DESCR_t base = interp_eval(lv->children[0]);
            if (!IS_FAIL_fn(base)) {
                DESCR_t idx = interp_eval(lv->children[1]);
                if (!IS_FAIL_fn(idx)) {
                    if (lv->nchildren >= 3) {
                        DESCR_t idx2 = interp_eval(lv->children[2]);
                        if (!IS_FAIL_fn(idx2))
                            subscript_set2(base, idx, idx2, val);
                    } else {
                        subscript_set(base, idx, val);
                    }
                }
            }
        }
        else if (lv && lv->kind == E_FNC && lv->sval && lv->nchildren >= 1) {
            if (strcasecmp(lv->sval, "ITEM") == 0 && lv->nchildren >= 2) {
                /* ITEM(arr, i [,j]) = val — programmatic subscript setter */
                DESCR_t base = interp_eval(lv->children[0]);
                if (!IS_FAIL_fn(base)) {
                    DESCR_t idx = interp_eval(lv->children[1]);
                    if (!IS_FAIL_fn(idx)) {
                        if (lv->nchildren >= 3) {
                            DESCR_t idx2 = interp_eval(lv->children[2]);
                            if (!IS_FAIL_fn(idx2))
                                subscript_set2(base, idx, idx2, val);
                        } else {
                            subscript_set(base, idx, val);
                        }
                    }
                }
            } else {
                /* DATA field setter: fname(obj) = val
                 * Evaluate the first argument; if it's a DT_DATA instance,
                 * dispatch through FIELD_SET_fn using the function name as field name. */
                DESCR_t obj = interp_eval(lv->children[0]);
                if (!IS_FAIL_fn(obj))
                    FIELD_SET_fn(obj, lv->sval, val);
            }
        }
        else if (lv && lv->kind == E_INDIRECT && lv->nchildren > 0) {
            EXPR_t *ichild = lv->children[0];
            const char *nm = NULL;
            if (ichild->kind == E_CAPT_COND_ASGN && ichild->nchildren == 1
                    && ichild->children[0]->kind == E_VAR && ichild->children[0]->sval)
                nm = ichild->children[0]->sval;
            else { DESCR_t nd = interp_eval(ichild); nm = VARVAL_fn(nd); }
            if (nm && *nm) NV_SET_fn(nm, val);  /* inner expr: no trace */
        }
        return val;
    }

    case E_INDIRECT: {
        if (e->nchildren < 1) return FAILDESCR;
        EXPR_t *child = e->children[0];
        /* $.var parses as E_INDIRECT(E_NAME(E_CAPT_COND_ASGN(E_VAR)))
         * $.var<idx> parses as E_INDIRECT(E_NAME(E_CAPT_COND_ASGN(E_VAR, idx)))
         * The E_NAME wrapper (from the dot-prefix parse) is unwrapped first.
         * Semantics: the identifier name is used literally (not its value).
         *   $.var      => NV_GET_fn("var")
         *   $.var<idx> => subscript( NV_GET_fn("var"), idx ) */
        /* $.var<idx> parses as E_INDIRECT(E_NAME(E_CAPT_COND_ASGN(E_VAR,idx)))
         * — the E_NAME wrapper is from the dot-prefix parse. Unwrap it.
         * $X (no dot) parses as E_INDIRECT(E_VAR("X")) — no E_NAME wrapper.
         * Track whether we unwrapped an E_NAME to distinguish:
         *   $.var = literal name lookup (return var's value directly)
         *   $X    = runtime indirect (evaluate X, use its value as lookup name) */
        int had_name_wrap = 0;
        if (child->kind == E_NAME && child->nchildren == 1) {
            child = child->children[0];
            had_name_wrap = 1;
        }

        /* E_VAR child: $.var (had_name_wrap=1) vs $X (had_name_wrap=0) */
        if (child->kind == E_VAR && child->sval) {
            if (had_name_wrap)
                return NV_GET_fn(child->sval);          /* $.var — literal name */
            /* $X — evaluate X's runtime value, use it as the variable name.
             * IS_NAMEPTR (slen=1, .ptr = live DESCR_t*) vs IS_NAMEVAL (slen=0, .s = name).
             * Do NOT use ptr!=NULL — for NAMEVAL .s and .ptr alias the same union. */
            DESCR_t _xv = NV_GET_fn(child->sval);
            if (IS_NAMEPTR(_xv)) return NAME_DEREF_PTR(_xv);
            if (IS_NAMEVAL(_xv)) return NV_GET_fn(_xv.s);
            const char *_xnm = VARVAL_fn(_xv);
            if (!_xnm || !*_xnm) return NULVCL;
            DESCR_t _xnamed = NV_GET_fn(_xnm);
            if (IS_NAMEPTR(_xnamed)) return NAME_DEREF_PTR(_xnamed);
            if (IS_NAMEVAL(_xnamed)) return NV_GET_fn(_xnamed.s);
            return _xnamed;
        }

        /* E_IDX after E_NAME unwrap: $.var<idx> subscript form
         * children[0]=E_VAR "name", children[1]=index expr */
        if (child->kind == E_IDX && child->nchildren >= 2
                && child->children[0]->kind == E_VAR && child->children[0]->sval) {
            const char *nm = child->children[0]->sval;
            DESCR_t base = NV_GET_fn(nm);
            if (IS_FAIL_fn(base)) return FAILDESCR;
            if (child->nchildren == 2) {
                DESCR_t idx = interp_eval(child->children[1]);
                if (IS_FAIL_fn(idx)) return FAILDESCR;
                return subscript_get(base, idx);
            }
            DESCR_t i1 = interp_eval(child->children[1]);
            DESCR_t i2 = interp_eval(child->children[2]);
            if (IS_FAIL_fn(i1) || IS_FAIL_fn(i2)) return FAILDESCR;
            return subscript_get2(base, i1, i2);
        }

        if (child->kind == E_CAPT_COND_ASGN && child->nchildren == 1) {
            EXPR_t *inner = child->children[0];
            /* $.var<idx> case: dot child is E_IDX whose base is E_VAR */
            if (inner->kind == E_IDX && inner->nchildren >= 2
                    && inner->children[0]->kind == E_VAR
                    && inner->children[0]->sval) {
                const char *nm = inner->children[0]->sval;
                DESCR_t base = NV_GET_fn(nm);
                if (IS_FAIL_fn(base)) return FAILDESCR;
                if (inner->nchildren == 2) {
                    DESCR_t idx = interp_eval(inner->children[1]);
                    if (IS_FAIL_fn(idx)) return FAILDESCR;
                    return subscript_get(base, idx);
                }
                DESCR_t i1 = interp_eval(inner->children[1]);
                DESCR_t i2 = interp_eval(inner->children[2]);
                if (IS_FAIL_fn(i1) || IS_FAIL_fn(i2)) return FAILDESCR;
                return subscript_get2(base, i1, i2);
            }
            /* $.var case: dot child is plain E_VAR — but only if this is a
             * literal $.var (direct name lookup). For $X (runtime indirect),
             * evaluate X's value and use THAT as the variable name.
             * Distinction: $.var uses the identifier literally; $X uses X's value.
             * Since parser wraps both as E_CAPT_COND_ASGN(E_VAR), we must
             * evaluate the inner var and use its string value as the lookup key. */
            if (inner->kind == E_VAR && inner->sval) {
                DESCR_t xval = NV_GET_fn(inner->sval);
                if (IS_NAMEPTR(xval)) return NAME_DEREF_PTR(xval);
                if (IS_NAMEVAL(xval)) return NV_GET_fn(xval.s);
                const char *nm2 = VARVAL_fn(xval);
                if (!nm2 || !*nm2) return NULVCL;
                DESCR_t named = NV_GET_fn(nm2);
                if (IS_NAMEPTR(named)) return NAME_DEREF_PTR(named);
                if (IS_NAMEVAL(named)) return NV_GET_fn(named.s);
                return named;
            }
            /* fallback: evaluate inner directly */
            DESCR_t nd = interp_eval(inner);
            const char *nm2 = VARVAL_fn(nd);
            if (!nm2 || !*nm2) return NULVCL;
            DESCR_t named2 = NV_GET_fn(nm2);
            if (IS_NAMEPTR(named2)) return NAME_DEREF_PTR(named2);
            if (IS_NAMEVAL(named2)) return NV_GET_fn(named2.s);
            return named2;
        }
        /* $expr — indirect through runtime string/name value */
        DESCR_t nd = interp_eval(child);
        if (IS_NAMEPTR(nd)) return NAME_DEREF_PTR(nd);
        if (IS_NAMEVAL(nd)) return NV_GET_fn(nd.s);
        const char *nm = VARVAL_fn(nd);
        if (!nm || !*nm) return NULVCL;
        /* The named variable might also be a DT_N — dereference one more level */
        DESCR_t named = NV_GET_fn(nm);
        if (IS_NAMEPTR(named)) return NAME_DEREF_PTR(named);
        if (IS_NAMEVAL(named)) return NV_GET_fn(named.s);
        return named;
    }

    case E_FNC: {
        if (!e->sval || !*e->sval) return FAILDESCR;

        /* DEFINE('spec'[,'entry']) — register user function.
         * SIL DEFIFN returns the function name string on success (DIFFER-able).
         * Extract name = everything before '(' or ',' in spec. */
        if (strcasecmp(e->sval, "DEFINE") == 0) {
            const char *spec = define_spec_from_expr(e);
            if (spec && *spec) {
                const char *entry = define_entry_from_expr(e);
                if (entry) DEFINE_fn_entry(spec, NULL, entry);
                else       DEFINE_fn(spec, NULL);
                /* Return function name: chars before '(' or ',' */
                char namebuf[256];
                size_t ni = 0;
                for (; ni < sizeof(namebuf)-1 && spec[ni]
                       && spec[ni] != '(' && spec[ni] != ','; ni++)
                    namebuf[ni] = spec[ni];
                namebuf[ni] = '\0';
                return STRVAL(GC_strdup(namebuf));
            }
            return FAILDESCR;   /* malformed spec → FAIL per SIL */
        }

        int nargs = e->nchildren;
        DESCR_t *args = nargs > 0
            ? (DESCR_t *)alloca((size_t)nargs * sizeof(DESCR_t))
            : NULL;
        for (int i = 0; i < nargs; i++) {
            args[i] = interp_eval(e->children[i]);
            if (IS_FAIL_fn(args[i])) return FAILDESCR;
        }

        /* DYN-70 fix: check for user-defined body label BEFORE calling APPLY_fn.
         * APPLY_fn internally dispatches user functions via call_user_function,
         * so calling APPLY_fn then call_user_function again causes double execution.
         * Rule: if a body label exists in the program → user-defined → skip APPLY_fn.
         * Builtins never have a body label; user functions always do (prescan_defines). */
        {
            /* Resolve body: try as-is, uppercase, then entry_label (OPSYN aliases) */
            STMT_t *body = label_lookup(e->sval);
            if (!body) {
                char ufn[128];
                size_t fl = strlen(e->sval);
                if (fl >= sizeof(ufn)) fl = sizeof(ufn)-1;
                for (size_t i = 0; i <= fl; i++) ufn[i] = (char)toupper((unsigned char)e->sval[i]);
                body = label_lookup(ufn);
            }
            if (!body) {
                const char *el = FUNC_ENTRY_fn(e->sval);
                if (el) body = label_lookup(el);
            }
            if (body) {
                /* User-defined function — call interpreter directly, never via APPLY_fn */
                DESCR_t r = call_user_function(e->sval, args, nargs);
                if (IS_NAME(r)) return NAME_DEREF(r);
                return r;
            }
        }
        /* ── U-22: cross-language fallback in value-context E_FNC ────────────
         * SNO body lookup above found nothing.  Try Icon proc table, then
         * Prolog pred table, before falling through to builtins/APPLY_fn. */
        {
            /* Try Icon proc table (case-sensitive) */
            for (int _ci = 0; _ci < icn_proc_count; _ci++) {
                if (strcmp(icn_proc_table[_ci].name, e->sval) == 0)
                    return icn_call_proc(icn_proc_table[_ci].proc, args, nargs);
            }
            /* Try Prolog pred table: "name/arity" key */
            if (g_pl_active) {
                char _pk[256];
                snprintf(_pk, sizeof _pk, "%s/%d", e->sval, nargs);
                EXPR_t *_choice = pl_pred_table_lookup(&g_pl_pred_table, _pk);
                if (_choice) {
                    Term **_pl_args = (nargs > 0) ? pl_env_new(nargs) : NULL;
                    Term **_saved   = g_pl_env;
                    g_pl_env = _pl_args;
                    bb_node_t _root = pl_box_choice(_choice, g_pl_env, nargs);
                    int _ok = bb_broker(_root, BB_ONCE, NULL, NULL);
                    g_pl_env = _saved;
                    return _ok ? INTVAL(1) : FAILDESCR;
                }
            }
        }
        /* IDENT/DIFFER: per SPITBOL spec, arguments must have SAME data type AND value.
         * IDENT(3, '3') FAILS (integer vs string). IDENT(S) succeeds iff S is null string.
         * DIFFER(S,T) succeeds iff they differ in type OR value. DIFFER(S) succeeds iff S != ''.
         * Both return NULVCL on success. */
        /* EVAL/CODE: binary _EVAL_/_CODE_ are stubs; route through our full impl. */
        if (strcasecmp(e->sval, "EVAL") == 0) {
            if (nargs < 1) return FAILDESCR;
            extern DESCR_t EVAL_fn(DESCR_t);
            DESCR_t _er = EVAL_fn(args[0]);
            return _er;
        }
        if (strcasecmp(e->sval, "CODE") == 0) {
            if (nargs < 1) return FAILDESCR;
            const char *_cs = VARVAL_fn(args[0]);
            extern DESCR_t code(const char *);
            return (_cs && *_cs) ? code(_cs) : FAILDESCR;
        }
        if (strcasecmp(e->sval, "IDENT") == 0) {
            if (nargs == 1) {
                /* IDENT(S) — succeed if S is null string */
                return IS_NULL_fn(args[0]) ? NULVCL : FAILDESCR;
            }
            if (nargs >= 2) {
                /* Normalize: treat DT_SNUL and DT_S("") as same null type for comparison */
                int a_null = IS_NULL_fn(args[0]), b_null = IS_NULL_fn(args[1]);
                if (a_null && b_null) return NULVCL;   /* both null → identical */
                if (a_null || b_null) return FAILDESCR; /* one null, one not → differ */
                /* Same non-null type AND same string value */
                if (args[0].v != args[1].v) return FAILDESCR;
                const char *sa = VARVAL_fn(args[0]);
                const char *sb = VARVAL_fn(args[1]);
                if (!sa) sa = ""; if (!sb) sb = "";
                return strcmp(sa, sb) == 0 ? NULVCL : FAILDESCR;
            }
        }
        if (strcasecmp(e->sval, "DIFFER") == 0) {
            if (nargs == 1) {
                return IS_NULL_fn(args[0]) ? FAILDESCR : NULVCL;
            }
            if (nargs >= 2) {
                int a_null = IS_NULL_fn(args[0]), b_null = IS_NULL_fn(args[1]);
                if (a_null && b_null) return FAILDESCR;  /* both null → identical → DIFFER fails */
                if (a_null || b_null) return NULVCL;     /* one null, one not → differ */
                if (args[0].v != args[1].v) return NULVCL;
                const char *sa = VARVAL_fn(args[0]);
                const char *sb = VARVAL_fn(args[1]);
                if (!sa) sa = ""; if (!sb) sb = "";
                return strcmp(sa, sb) != 0 ? NULVCL : FAILDESCR;
            }
        }

        /* SC-1: DATA constructor/field-accessor dispatch via our registry */
        {
            ScDatType *_dt = sc_dat_find_type(e->sval);
            if (_dt) return sc_dat_construct(_dt, args, nargs);
            int _fi = 0;
            ScDatType *_ft = sc_dat_find_field(e->sval, &_fi);
            if (_ft && nargs >= 1) return sc_dat_field_get(e->sval, args[0]);
        }
        /* No body label → builtin or unknown. APPLY_fn handles both. */
        if (FNCEX_fn(e->sval)) {
            DESCR_t bres = APPLY_fn(e->sval, args, nargs);
            return bres;
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

    case E_DEFER: {
        /* *expr — SIL *X operator. Three sub-cases:
         *
         * *var  (E_VAR): deferred variable reference — fetch value NOW.
         *   "term = *factor" stores factor's current pattern.
         *
         * *func(args) (E_FNC): always builds a deferred T_FUNC/XATP pattern
         *   node via pat_user_call — fires the function at match time.
         *   This applies in ALL contexts (RHS assignment, pattern expr, etc.).
         *   "addop = ANY('+-') . *Push()" — *Push() must be a pattern node.
         *
         * *complex_expr: freeze as DT_E for EVAL() to thaw later. */
        if (e->nchildren < 1) return NULVCL;
        EXPR_t *child = e->children[0];
        /* RUNTIME-6: *X in value context ALWAYS produces DT_E (EXPRESSION).
         * The child EXPR_t* is frozen; EVAL() thaws and executes it.
         * interp_eval_pat handles the pattern-context path (*var, *func). */
        DESCR_t d;
        d.v    = DT_E;
        d.ptr  = child;
        d.slen = 0;
        d.s    = NULL;
        return d;
    }

    case E_NOT: {
        /* \X — o$nta/b/c: succeed (null) iff X fails; fail if X succeeds.
         * Expression-context version; pattern-context uses bb_not. */
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t v = interp_eval(e->children[0]);
        if (IS_FAIL_fn(v)) return NULVCL;
        return FAILDESCR;
    }

    case E_ALT: {
        /* child[0] | child[1] | ... — build pat_alt chain left-to-right */
        if (e->nchildren == 0) return NULVCL;
        DESCR_t acc = interp_eval(e->children[0]);
        for (int i = 1; i < e->nchildren; i++)
            acc = pat_alt(acc, interp_eval(e->children[i]));
        return acc;
    }
    case E_CAPT_COND_ASGN: {
        /* pat . target — conditional assignment on match success.
         * target may be:
         *   E_VAR "name"         → XNME node, assign to named var at flush
         *   E_DEFER(E_FNC(...))  → XCALLCAP node: call func at match time to
         *                          get DT_N lvalue, write matched text at flush.
         *                          Function must NOT be called at build time. */
        if (e->nchildren < 2) return NULVCL;
        DESCR_t pat = interp_eval_pat(e->children[0]);
        EXPR_t *tgt = e->children[1];
        if (tgt->kind == E_DEFER && tgt->nchildren == 1
                && tgt->children[0]->kind == E_FNC && tgt->children[0]->sval) {
            /* Deferred-function target — build XCALLCAP, don't call now */
            EXPR_t *fnc = tgt->children[0];
            int na = fnc->nchildren;
            DESCR_t *av = na > 0 ? GC_malloc(na * sizeof(DESCR_t)) : NULL;
            for (int i = 0; i < na; i++) av[i] = interp_eval(fnc->children[i]);
            return pat_assign_callcap(pat, fnc->sval, av, na);
        }
        const char *nm = tgt->sval;
        if (!nm && tgt->kind == E_INDIRECT && tgt->nchildren > 0) {
            /* REM . $'$B' — target is E_INDIRECT(E_QLIT "$B").
             * We need the *variable name* ("$B"), not the value of $'$B'.
             * The name is the evaluated string of the child expression. */
            EXPR_t *ichild = tgt->children[0];
            if (ichild->kind == E_QLIT || ichild->kind == E_VAR)
                nm = ichild->sval;                     /* literal name: $'$B' or $.X */
            else {
                DESCR_t nd = interp_eval(ichild);      /* runtime indirect: $X */
                nm = VARVAL_fn(nd);
            }
        }
        return nm ? pat_assign_cond(pat, STRVAL((char *)nm)) : pat;
    }
    case E_CAPT_IMMED_ASGN: {
        /* pat $ target — immediate assignment during match */
        if (e->nchildren < 2) return NULVCL;
        DESCR_t pat = interp_eval_pat(e->children[0]);
        EXPR_t *tgt = e->children[1];
        if (tgt->kind == E_DEFER && tgt->nchildren == 1
                && tgt->children[0]->kind == E_FNC && tgt->children[0]->sval) {
            EXPR_t *fnc = tgt->children[0];
            int na = fnc->nchildren;
            DESCR_t *av = na > 0 ? GC_malloc(na * sizeof(DESCR_t)) : NULL;
            for (int i = 0; i < na; i++) av[i] = interp_eval(fnc->children[i]);
            DESCR_t name_d = call_user_function(fnc->sval, av, na);
            if (IS_NAME(name_d) && name_d.ptr)
                return pat_assign_imm(pat, name_d);
            const char *nm2 = VARVAL_fn(name_d);
            return nm2 ? pat_assign_imm(pat, STRVAL((char*)nm2)) : pat;
        }
        EXPR_t *tgt2 = e->children[1];
        const char *nm = tgt2->sval;
        if (!nm && tgt2->kind == E_INDIRECT && tgt2->nchildren > 0) {
            EXPR_t *ichild = tgt2->children[0];
            if (ichild->kind == E_QLIT || ichild->kind == E_VAR)
                nm = ichild->sval;
            else { DESCR_t nd = interp_eval(ichild); nm = VARVAL_fn(nd); }
        }
        return nm ? pat_assign_imm(pat, STRVAL((char *)nm)) : pat;
    }
    case E_CAPT_CURSOR: {
        /* Two forms:
         *   unary:  @var         — E_CAPT_CURSOR(E_VAR)         nchildren==1
         *   binary: pat @ var    — E_CAPT_CURSOR(pat, E_VAR)    nchildren==2
         * Both write the cursor position into var as DT_I at match time. */
        if (e->nchildren == 1) {
            /* unary @var: epsilon left, cursor capture into var */
            const char *nm = e->children[0]->sval;
            if (!nm) return NULVCL;
            return pat_at_cursor(nm);
        }
        if (e->nchildren < 2) return NULVCL;
        DESCR_t left_pat = interp_eval_pat(e->children[0]);
        const char *nm   = e->children[1]->sval;
        if (!nm) return left_pat;
        DESCR_t atp = pat_at_cursor(nm);
        return pat_cat(left_pat, atp);
    }

    /* ── Numeric relational operators ─────────────────────────────────────
     * Each compares two numeric operands; succeeds (returns lhs) or fails.
     * SNOBOL4 relops return the left operand on success (not a boolean). */
#define NUMREL(op) do { \
        if (e->nchildren < 2) return FAILDESCR; \
        DESCR_t l = interp_eval(e->children[0]); \
        DESCR_t r = interp_eval(e->children[1]); \
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR; \
        double lv = (l.v==DT_R) ? l.r : (double)(l.v==DT_I ? l.i : 0); \
        double rv = (r.v==DT_R) ? r.r : (double)(r.v==DT_I ? r.i : 0); \
        if (!(lv op rv)) return FAILDESCR; \
        return l; \
    } while(0)
    case E_LT: NUMREL(<);
    case E_LE: NUMREL(<=);
    case E_GT: NUMREL(>);
    case E_GE: NUMREL(>=);
    case E_EQ: NUMREL(==);
    case E_NE: NUMREL(!=);
#undef NUMREL

    /* ── Lexicographic (string) relational operators ───────────────────────
     * Each compares two string operands; succeeds (returns lhs) or fails. */
#define STRREL(cmpop) do { \
        if (e->nchildren < 2) return FAILDESCR; \
        DESCR_t l = interp_eval(e->children[0]); \
        DESCR_t r = interp_eval(e->children[1]); \
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR; \
        const char *ls = VARVAL_fn(l); if (!ls) ls = ""; \
        const char *rs = VARVAL_fn(r); if (!rs) rs = ""; \
        int cmp = strcmp(ls, rs); \
        if (!(cmp cmpop 0)) return FAILDESCR; \
        return l; \
    } while(0)
    case E_LLT: STRREL(<);
    case E_LLE: STRREL(<=);
    case E_LGT: STRREL(>);
    case E_LGE: STRREL(>=);
    case E_LEQ: STRREL(==);
    case E_LNE: STRREL(!=);
#undef STRREL

    /* ── Zero-argument pattern primitives ────────────────────────────────
     * These IR nodes are emitted directly by the SNOBOL4 parser for
     * bare keywords like ARB, REM, FAIL, SUCCEED, FENCE, ABORT, BAL.
     * Without these cases they fell through to default→NULVCL (DT_S=0),
     * so pat_ctx was satisfied but interp_eval returned DT_S not DT_P.
     * Fix: DYN-55. */
    case E_ARB:     return pat_arb();
    case E_REM:     return pat_rem();
    case E_FAIL:    return pat_fail();
    case E_SUCCEED: return pat_succeed();
    case E_FENCE:   return pat_fence();
    case E_ABORT:   return pat_abort();
    case E_BAL:     return pat_bal();

    /* ── One-argument pattern primitives ─────────────────────────────────
     * POS(n), RPOS(n), TAB(n), RTAB(n), LEN(n) take integer args.
     * ANY(s), NOTANY(s), SPAN(s), BREAK(s), BREAKX(s) take string args. */
    case E_POS: {
        if (e->nchildren < 1) return pat_pos(0);
        DESCR_t a = interp_eval(e->children[0]);
        return pat_pos((int64_t)(a.v==DT_I ? a.i : (int64_t)(a.v==DT_R ? (int64_t)a.r : 0)));
    }
    case E_RPOS: {
        if (e->nchildren < 1) return pat_rpos(0);
        DESCR_t a = interp_eval(e->children[0]);
        return pat_rpos((int64_t)(a.v==DT_I ? a.i : (int64_t)(a.v==DT_R ? (int64_t)a.r : 0)));
    }
    case E_TAB: {
        if (e->nchildren < 1) return pat_tab(0);
        DESCR_t a = interp_eval(e->children[0]);
        return pat_tab((int64_t)(a.v==DT_I ? a.i : (int64_t)(a.v==DT_R ? (int64_t)a.r : 0)));
    }
    case E_RTAB: {
        if (e->nchildren < 1) return pat_rtab(0);
        DESCR_t a = interp_eval(e->children[0]);
        return pat_rtab((int64_t)(a.v==DT_I ? a.i : (int64_t)(a.v==DT_R ? (int64_t)a.r : 0)));
    }
    case E_LEN: {
        if (e->nchildren < 1) return pat_len(0);
        DESCR_t a = interp_eval(e->children[0]);
        return pat_len((int64_t)(a.v==DT_I ? a.i : (int64_t)(a.v==DT_R ? (int64_t)a.r : 0)));
    }
    case E_ANY: {
        if (e->nchildren < 1) return pat_any_cs("");
        DESCR_t a = NAME_DEREF(interp_eval(e->children[0]));
        const char *s = (a.v==DT_S||a.v==DT_SNUL) && a.s ? a.s : "";
        return pat_any_cs(s);
    }
    case E_NOTANY: {
        if (e->nchildren < 1) return pat_notany("");
        DESCR_t a = NAME_DEREF(interp_eval(e->children[0]));
        const char *s = (a.v==DT_S||a.v==DT_SNUL) && a.s ? a.s : "";
        return pat_notany(s);
    }
    case E_SPAN: {
        if (e->nchildren < 1) return pat_span("");
        DESCR_t a = NAME_DEREF(interp_eval(e->children[0]));
        const char *s = (a.v==DT_S||a.v==DT_SNUL) && a.s ? a.s : "";
        return pat_span(s);
    }
    case E_BREAK: {
        if (e->nchildren < 1) return pat_break_("");
        DESCR_t a = NAME_DEREF(interp_eval(e->children[0]));
        const char *s = (a.v==DT_S||a.v==DT_SNUL) && a.s ? a.s : "";
        return pat_break_(s);
    }
    case E_BREAKX: {
        extern DESCR_t pat_breakx(const char *);
        if (e->nchildren < 1) return pat_breakx("");
        DESCR_t a = NAME_DEREF(interp_eval(e->children[0]));
        const char *s = (a.v==DT_S||a.v==DT_SNUL) && a.s ? a.s : "";
        return pat_breakx(s);
    }
    case E_ARBNO: {
        if (e->nchildren < 1) return pat_arb(); /* degenerate */
        DESCR_t inner = interp_eval(e->children[0]);
        return pat_arbno(inner);
    }

    /* ── Prolog IR nodes — S-1C-2/3 ──────────────────────────────────────────
     * Only reached when g_pl_active is set (Prolog program running).
     * E_CHOICE drives clause selection via the Byrd box broker (pl_broker.c).
     * E_UNIFY/E_CUT/E_TRAIL_* are leaf goal nodes evaluated inline. */
    case E_UNIFY: {
        if (!g_pl_active) return NULVCL;
        Term *t1 = pl_unified_term_from_expr(e->children[0], g_pl_env);
        Term *t2 = pl_unified_term_from_expr(e->children[1], g_pl_env);
        int mark = trail_mark(&g_pl_trail);
        if (!unify(t1, t2, &g_pl_trail)) { trail_unwind(&g_pl_trail, mark); return FAILDESCR; }
        return INTVAL(1);
    }
    case E_CUT:
        if (g_pl_active) g_pl_cut_flag = 1;
        return INTVAL(1);
    case E_TRAIL_MARK:
    case E_TRAIL_UNWIND:
        return NULVCL;
    case E_CLAUSE:
        /* Clauses are iterated by E_CHOICE — never dispatched standalone. */
        return NULVCL;
    case E_CHOICE: {
        /* Drive clause selection via the Byrd box broker.
         * pl_box_choice builds the full OR/CAT/head-unify box tree.
         * bb_broker drives α (BB_ONCE) — OR-box retries β internally per clause.
         * g_pl_env holds the caller's arg Term** array (arity slots).
         * Returns INTVAL(1) on first solution (γ), FAILDESCR on ω. */
        if (!g_pl_active) return NULVCL;
        int arity = 0;
        if (e->sval) { const char *sl = strrchr(e->sval, '/'); if (sl) arity = atoi(sl+1); }
        bb_node_t root = pl_box_choice(e, g_pl_env, arity);
        int ok = bb_broker(root, BB_ONCE, NULL, NULL);
        return ok ? INTVAL(1) : FAILDESCR;
    }

    /* ── Icon EKinds — OE-3 ─── */

    case E_CSET: return e->sval ? STRVAL(e->sval) : NULVCL;

    case E_TO: case E_TO_BY: {
        long cur;
        if (icn_gen_lookup(e, &cur)) return INTVAL(cur);
        if (e->nchildren < 1) return NULVCL;
        return interp_eval(e->children[0]);
    }

    case E_EVERY: {
        if (e->nchildren < 1) return NULVCL;
        EXPR_t *gen  = e->children[0];
        EXPR_t *body = (e->nchildren > 1) ? e->children[1] : NULL;
        if (body && gen->kind == E_TO && gen->nchildren >= 2) {
            DESCR_t lo_d = interp_eval(gen->children[0]);
            DESCR_t hi_d = interp_eval(gen->children[1]);
            if (IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)) return NULVCL;
            long lo=lo_d.i, hi=hi_d.i;
            EXPR_t *saved_root = ICN_CUR.body_root;
            ICN_CUR.body_root = body;
            for(long i=lo;i<=hi&&!ICN_CUR.returning;i++) interp_eval(body);
            ICN_CUR.body_root = saved_root;
            return NULVCL;
        }
        int ticks = icn_drive(gen);
        if (!ticks) interp_eval(gen);
        return NULVCL;
    }

    case E_WHILE: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break &&
               !IS_FAIL_fn(interp_eval(e->children[0]))) {
            if (e->nchildren > 1) interp_eval(e->children[1]);
        }
        ICN_CUR.loop_break = saved_brk;
        return NULVCL;
    }

    case E_UNTIL: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break) {
            DESCR_t cv = (e->nchildren > 0) ? interp_eval(e->children[0]) : FAILDESCR;
            if (!IS_FAIL_fn(cv)) break;
            if (e->nchildren > 1) interp_eval(e->children[1]);
        }
        ICN_CUR.loop_break = saved_brk;
        return NULVCL;
    }

    case E_REPEAT: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break) {
            if (e->nchildren > 0) interp_eval(e->children[0]);
        }
        ICN_CUR.loop_break = saved_brk;
        return NULVCL;
    }

    case E_SEQ_EXPR: {
        DESCR_t v = NULVCL;
        for (int i = 0; i < e->nchildren && !ICN_CUR.returning; i++)
            v = interp_eval(e->children[i]);
        return v;
    }

    case E_IF: {
        if (e->nchildren < 1) return NULVCL;
        DESCR_t cv = interp_eval(e->children[0]);
        if (!IS_FAIL_fn(cv))
            return (e->nchildren > 1) ? interp_eval(e->children[1]) : cv;
        return (e->nchildren > 2) ? interp_eval(e->children[2]) : FAILDESCR;
    }

    case E_AUGOP: {
        if (e->nchildren < 2) return NULVCL;
        EXPR_t *lhs = e->children[0];
        DESCR_t lv = interp_eval(lhs);
        DESCR_t rv = interp_eval(e->children[1]);
        if (IS_FAIL_fn(lv)||IS_FAIL_fn(rv)) return FAILDESCR;
        long li=IS_INT_fn(lv)?lv.i:(long)lv.r, ri=IS_INT_fn(rv)?rv.i:(long)rv.r;
        DESCR_t result = NULVCL;
        switch((IcnTkKind)e->ival){
            case TK_AUGPLUS:   result=INTVAL(li+ri); break;
            case TK_AUGMINUS:  result=INTVAL(li-ri); break;
            case TK_AUGSTAR:   result=INTVAL(li*ri); break;
            case TK_AUGSLASH:  result=ri?INTVAL(li/ri):FAILDESCR; break;
            case TK_AUGMOD:    result=ri?INTVAL(li%ri):FAILDESCR; break;
            case TK_AUGCONCAT: {
                const char *ls=VARVAL_fn(lv),*rs=VARVAL_fn(rv);
                if(!ls)ls="";if(!rs)rs="";
                size_t ll=strlen(ls),rl=strlen(rs);
                char *buf=GC_malloc(ll+rl+1);
                memcpy(buf,ls,ll);memcpy(buf+ll,rs,rl);buf[ll+rl]='\0';
                result=STRVAL(buf); break;
            }
            default: result=INTVAL(li+ri); break;
        }
        if (IS_FAIL_fn(result)) return FAILDESCR;
        if (lhs->kind == E_VAR && icn_frame_depth > 0) {
            int slot=(int)lhs->ival;
            if(slot>=0&&slot<ICN_CUR.env_n) ICN_CUR.env[slot]=result;
        }
        return result;
    }

    case E_LOOP_BREAK: {
        ICN_CUR.loop_break = 1;
        return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
    }

    case E_SCAN: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t subj_d = interp_eval(e->children[0]);
        if (IS_FAIL_fn(subj_d)) return FAILDESCR;
        const char *subj_s = VARVAL_fn(subj_d); if (!subj_s) subj_s = "";
        if (icn_scan_depth < ICN_SCAN_STACK_MAX) {
            icn_scan_stack[icn_scan_depth].subj = icn_scan_subj;
            icn_scan_stack[icn_scan_depth].pos  = icn_scan_pos;
            icn_scan_depth++;
        }
        icn_scan_subj = subj_s; icn_scan_pos = 1;
        DESCR_t r = (e->nchildren >= 2) ? interp_eval(e->children[1]) : NULVCL;
        if (icn_scan_depth > 0) {
            icn_scan_depth--;
            icn_scan_subj = icn_scan_stack[icn_scan_depth].subj;
            icn_scan_pos  = icn_scan_stack[icn_scan_depth].pos;
        }
        return r;
    }

    case E_ITERATE: {
        long cur; const char *str;
        if (icn_gen_lookup_sv(e, &cur, &str) && str) {
            char *ch = GC_malloc(2); ch[0] = str[cur]; ch[1] = '\0';
            return STRVAL(ch);
        }
        return FAILDESCR;
    }

    case E_SUSPEND:
        return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;

    default:
        return NULVCL;
    }
}


/* -- interp_eval_ref -- lvalue evaluator → DESCR_t* (SIL NAME semantics) -----
 * Returns a pointer to the live descriptor cell for the given lvalue expression.
 * Mirrors SIL: ARYA10 (array), ASSCR (table), FIELD (DATA), GNVARS (variable).
 * Returns NULL for non-addressable positions (OOB, I/O vars, etc.).
 * The caller wraps the result as NAMEPTR(ptr) to create a DT_N descriptor.
 * --------------------------------------------------------------------- */
static DESCR_t *interp_eval_ref(EXPR_t *e)
{
    if (!e) return NULL;
    switch (e->kind) {

    case E_VAR: {
        /* Simple variable — find-or-create NV cell */
        return NV_PTR_fn(e->sval);
    }

    case E_IDX: {
        /* arr[idx] or arr[i][j] — return interior cell pointer */
        if (e->nchildren < 2) return NULL;
        DESCR_t base = interp_eval(e->children[0]);
        if (IS_FAIL_fn(base)) return NULL;
        DESCR_t idx  = interp_eval(e->children[1]);
        if (IS_FAIL_fn(idx)) return NULL;
        if (IS_ARR(base)) {
            return array_ptr(base.arr, (int)to_int(idx));
        }
        if (IS_TBL(base)) {
            return table_ptr(base.tbl, idx);
        }
        return NULL;
    }

    case E_NAME: {
        /* .expr — dot operator: evaluate child as lvalue */
        if (e->nchildren == 1)
            return interp_eval_ref(e->children[0]);
        /* .var plain (sval set): NV cell */
        if (e->sval)
            return NV_PTR_fn(e->sval);
        return NULL;
    }

    case E_CAPT_COND_ASGN: {
        /* .var (parsed as E_CAPT_COND_ASGN child E_VAR) */
        if (e->nchildren >= 1 && e->children[0]->kind == E_VAR)
            return NV_PTR_fn(e->children[0]->sval);
        if (e->nchildren >= 1)
            return interp_eval_ref(e->children[0]);
        return NULL;
    }

    case E_INDIRECT: {
        /* $expr — evaluate expr to get name string, then return that var's cell */
        DESCR_t name_d = interp_eval(e->nchildren >= 1 ? e->children[0] : NULL);
        const char *nm = IS_NAMEPTR(name_d)
            ? VARVAL_fn(NAME_DEREF_PTR(name_d))
            : VARVAL_fn(name_d);
        if (!nm || !*nm) return NULL;
        return NV_PTR_fn(nm);
    }

    default: return NULL;
    }
}

/* -- DYN-59: interp_eval_pat -- evaluate expr in PATTERN context ----------
 * Like interp_eval but E_SEQ/E_CAT use pat_cat (not CONCAT_fn) and
 * zero-arg pattern keywords in E_VAR go through APPLY_fn first.
 * Called at the pattern call site: interp_eval_pat(s->pattern).
 * ----------------------------------------------------------------------- */
DESCR_t interp_eval_pat(EXPR_t *e)
{
    if (!e) return NULVCL;
    switch (e->kind) {
    case E_SEQ:
    case E_CAT: {
        if (e->nchildren == 0) return NULVCL;
        DESCR_t acc = interp_eval_pat(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t nxt = interp_eval_pat(e->children[i]);
            if (IS_FAIL_fn(nxt)) return FAILDESCR;
            acc = pat_cat(acc, nxt);
        }
        return acc;
    }
    case E_ALT: {
        /* pattern alternation: p1 | p2 | ... — each child evaluated in
         * pattern context so that E_DEFER(E_VAR) children become XDSAR
         * nodes rather than frozen DT_E values. */
        if (e->nchildren == 0) return pat_epsilon();
        DESCR_t acc = interp_eval_pat(e->children[0]);
        if (IS_FAIL_fn(acc)) return FAILDESCR;
        for (int i = 1; i < e->nchildren; i++) {
            DESCR_t nxt = interp_eval_pat(e->children[i]);
            if (IS_FAIL_fn(nxt)) return FAILDESCR;
            acc = pat_alt(acc, nxt);
        }
        return acc;
    }
    case E_VAR:
        if (e->sval && *e->sval) {
            if (_is_pat_fnc_name(e->sval)) {
                DESCR_t _fr = APPLY_fn(e->sval, NULL, 0);
                if (!IS_FAIL_fn(_fr)) return _fr;
            }
            DESCR_t _v = interp_eval(e);
            /* PATVAL coerce: DT_N → deref; DT_E(null) → epsilon; DT_I/DT_R → literal; DT_E → thaw; DT_P/DT_S → pass. */
            if (_v.v == DT_N) {
                if (_v.slen == 1 && _v.ptr) _v = *(DESCR_t *)_v.ptr;
                else if (_v.slen == 0 && _v.s) _v = NV_GET_fn(_v.s);
                else _v = NULVCL;
            }
            if (_v.v == DT_E && !_v.ptr) return NULVCL;  /* null frozen expr → unset var */
            if (_v.v == DT_E || _v.v == DT_I || _v.v == DT_R) return PATVAL_fn(_v);
            return _v;
        }
        return NULVCL;
    case E_DEFER:
        /* *expr in pattern context — two sub-cases:
         *
         * 1. *func(args)  — E_DEFER(E_FNC): build a deferred T_FUNC pattern node
         *    (XATP via pat_user_call) so the function fires at MATCH time as a
         *    zero-width side-effect.  Mirrors SIL *X where X is a user function.
         *
         * 2. *var         — E_DEFER(E_VAR): look up the variable NOW and return
         *    its stored pattern value (the pattern was built at assignment time).
         *    (Contrast: E_DEFER in value context produces DT_E via interp_eval.) */
        if (e->nchildren < 1) return pat_epsilon();
        {
            EXPR_t *child = e->children[0];
            if (child->kind == E_FNC && child->sval) {
                /* *func(args) — build deferred XATP pattern node */
                int na = child->nchildren;
                DESCR_t *av = NULL;
                if (na > 0) {
                    av = GC_malloc(na * sizeof(DESCR_t));
                    for (int i = 0; i < na; i++)
                        av[i] = interp_eval(child->children[i]);
                }
                return pat_user_call(child->sval, av, na);
            }
            /* *var — build XDSAR deferred-ref node so the variable is
             * resolved at MATCH time (not now).  This is required for
             * self-/mutually-recursive patterns like:
             *   factor = addop *factor . *Unary() | *primary
             * where *factor must not be resolved while factor is still
             * being assigned.  pat_ref() creates an XDSAR node; the
             * materialise() path in snobol4_pattern.c resolves it with
             * cycle detection at match time. */
            if (child->kind == E_VAR && child->sval)
                return pat_ref(child->sval);
            /* Non-VAR, non-FNC child: if it contains no pattern-only nodes,
             * it is a pure value expression — freeze as DT_E for EVAL() to
             * thaw later.  E.g. *('abc' 'def') or *'str' → DT_E, not STRING.
             * If it IS a pattern tree (E_ALT etc.) evaluate in pat context. */
            if (!_expr_is_pat(child)) {
                DESCR_t d; d.v = DT_E; d.ptr = child; d.slen = 0;
                return d;
            }
            DESCR_t r = interp_eval_pat(child);
            if (IS_NAMEPTR(r)) r = NAME_DEREF_PTR(r);
            return r;
        }

    default:
        return interp_eval(e);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Prolog IR interpreter block — pl_execute_program_unified + helpers
 * (recovered from scrip.c bca2b79a; removed by 476fd067 accidentally)
 * ══════════════════════════════════════════════════════════════════════════ */

#define PL_PRED_TABLE_SIZE PL_PRED_TABLE_SIZE_FWD



void execute_program(Program *prog)
{
    polyglot_init(prog, polyglot_lang_mask(prog));   /* U-14 / FI-8: language-selective init */
    g_lang = 0;  /* SNOBOL4 mode */

    STMT_t *s = prog->head;
    /* No hardcoded step_limit — &STLIMIT (kw_stlimit) governs via comm_stno().
     * comm_stno() increments kw_stcount and fires sno_runtime_error(22) when
     * kw_stlimit >= 0 && kw_stcount > kw_stlimit.  The setjmp handler below
     * breaks the loop for terminal error codes (22 = STLIMIT exceeded). */

    /* Arm runtime-error longjmp.  sno_runtime_error() prints the message
     * and longjmps here with the error code.  We treat it as statement
     * failure (SNOBOL4 spec: runtime error → fail branch, then END).
     * Terminal errors (code 20-23, 26-27, 29-31, 39) exit the loop. */
    g_sno_err_active = 1;

    /* Hoist per-iteration state above setjmp: C99 forbids goto crossing an
     * initializer, and longjmp re-enters at the setjmp call each iteration. */
    int         stno      = 0;
    int         succeeded = 1;
    DESCR_t     subj_val  = NULVCL;
    const char *subj_name = NULL;
    const char *target    = NULL;

    while (s) {
        if (s->is_end) break;  /* U-23: polyglot multi-section dispatch handles remaining modules */
        comm_stno(++stno);

        /* ── --trace: print statement number to stderr ─────────────── */
        if (g_opt_trace)
            fprintf(stderr, "TRACE stmt %d\n", stno);

        /* Catch runtime errors (longjmp from sno_runtime_error).
         * Terminal errors (stlimit, storage, etc.) break the loop.
         * Soft errors: take :F branch if present, else advance. */
        int _err = setjmp(g_sno_err_jmp);
        if (_err != 0) {
            /* message already printed by sno_runtime_error() */
            if (sno_err_is_terminal(_err)) break;   /* &STLIMIT, storage, etc. */
            succeeded = 0;
            target    = NULL;
            if (s->go && s->go->onfailure && *s->go->onfailure)
                target = s->go->onfailure;
            goto do_goto;
        }

        /* ── U-15: per-statement dispatch by st->lang ─────────────── */
        if (s->lang == LANG_ICN || s->lang == LANG_RAKU) {
            /* Icon / Raku STMT_t nodes are procedure definitions — already registered
             * in icn_proc_table by polyglot_init.  Skip inline; main() is
             * called once after the SNO/PL statement loop completes. */
            s = s->next; continue;
        }
        if (s->lang == LANG_PL) {
            /* Prolog statement: evaluate subject as a goal with pl active */
            if (s->subject) {
                int sv_pl = g_pl_active;
                g_pl_active = 1;
                interp_eval(s->subject);
                g_pl_active = sv_pl;
            }
            s = s->next; continue;
        }
        /* LANG_SNO (0): fall through to existing SNOBOL4 path below.
         * Also skip any stray Prolog/Icon IR nodes that have lang==LANG_SNO
         * (shouldn't happen after U-12/U-13, but keep guard for safety). */
        if (s->subject && (s->subject->kind == E_CHOICE ||
                           s->subject->kind == E_UNIFY  ||
                           s->subject->kind == E_CLAUSE)) {
            s = s->next; continue;
        }

        /* ── evaluate subject ──────────────────────────────────────── */
        subj_val  = NULVCL;
        subj_name = NULL;

        if (s->subject) {
            if (s->subject->kind == E_VAR && s->subject->sval) {
                subj_name = s->subject->sval;
                /* Only read the value when we need to match against it.
                 * Pure assignment (has_eq, no pattern) only needs the name —
                 * calling NV_GET_fn on a function name triggers a spurious
                 * zero-arg call (APPLY_fn → g_user_call_hook), causing Error 5. */
                if (s->pattern)
                    subj_val = NV_GET_fn(subj_name);
            } else if (s->subject->kind == E_INDIRECT && s->subject->nchildren > 0) {
                /* $'$B' or $X as subject — resolve to variable name for write-back */
                EXPR_t *ic = s->subject->children[0];
                if (ic->kind == E_QLIT && ic->sval) {
                    subj_name = ic->sval;
                } else if (ic->kind == E_VAR && ic->sval) {
                    DESCR_t xv = NV_GET_fn(ic->sval);
                    subj_name = VARVAL_fn(xv);
                } else {
                    DESCR_t nd = interp_eval(ic);
                    subj_name = VARVAL_fn(nd);
                }
                if (subj_name && s->pattern) {
                    subj_val = NV_GET_fn(subj_name);
                } else if (!subj_name)
                    subj_val = interp_eval(s->subject);
            } else {
                subj_val = interp_eval(s->subject);
            }
        }

        succeeded = 1;

        /* ── pattern match ─────────────────────────────────────────── */
        if (s->pattern) {
            /* S-10 fix: pattern must be evaluated in pattern context so *func()
             * produces XATP nodes, not frozen DT_E expressions. */
            DESCR_t pat_d = interp_eval_pat(s->pattern);
            /* ── --dump-bb: print PATND tree before match ─────────── */
            if (g_opt_dump_bb && pat_d.v == DT_P && pat_d.p)
                patnd_print((PATND_t *)pat_d.p, stderr);
            if (IS_FAIL_fn(pat_d)) {
                succeeded = 0;
            } else {
                DESCR_t  repl_val;
                int      has_repl = 0;
                if (s->has_eq && s->replacement) {
                    repl_val = interp_eval(s->replacement);
                    has_repl = !IS_FAIL_fn(repl_val);
                } else if (s->has_eq) {
                    /* X ? PAT =   (empty replacement) — replace matched
                     * portion with null string, advancing subject cursor */
                    repl_val = NULVCL;
                    has_repl = 1;
                }
                Σ = subj_name ? subj_name : "";
                succeeded = exec_stmt(
                    subj_name,
                    subj_name ? NULL : &subj_val,
                    pat_d,
                    has_repl ? &repl_val : NULL,
                    has_repl);
            }

        /* ── pure assignment (direct or null) ─────────────────────── */
        } else if (s->has_eq && subj_name) {
            /* X = expr  OR  X =  (null assign, no replacement node).
             * Always value context — *expr produces DT_E (RUNTIME-6). */
            DESCR_t repl_val = s->replacement
                ? interp_eval(s->replacement)
                : NULVCL;
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else {
                set_and_trace(subj_name, repl_val);
                succeeded = 1;
            }

        /* ── subscript assignment: A<i> = expr ─────────────────────── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_IDX) {
            EXPR_t *idx_e = s->subject;
            if (idx_e->nchildren >= 2) {
                DESCR_t base = interp_eval(idx_e->children[0]);
                DESCR_t idx  = interp_eval(idx_e->children[1]);
                DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
                if (IS_FAIL_fn(base) || IS_FAIL_fn(idx) || IS_FAIL_fn(repl_val)) {
                    succeeded = 0;
                } else {
                    if (idx_e->nchildren >= 3) {
                        DESCR_t idx2 = interp_eval(idx_e->children[2]);
                        subscript_set2(base, idx, idx2, repl_val);
                    } else {
                        subscript_set(base, idx, repl_val);
                    }
                    succeeded = 1;
                }
            } else { succeeded = 0; }

        /* ── keyword assignment: &KW = expr ───────────────────────── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_KEYWORD && s->subject->sval) {
            DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else {
                g_kw_ctx = 1;             /* signal Error 7 guard in NV_SET_fn */
                NV_SET_fn(s->subject->sval, repl_val);
                g_kw_ctx = 0;
                succeeded = 1;
            }

        /* ── indirect assignment: $expr = rhs ─────────────────────── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_INDIRECT) {
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
                    set_and_trace(nm, repl_val);
                    succeeded = 1;
                }
            }

        /* ── ITEM setter or DATA field setter: fname(obj[,i]) = expr ── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_FNC && s->subject->sval &&
                   s->subject->nchildren >= 1) {
            DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else if (strcasecmp(s->subject->sval, "ITEM") == 0 &&
                       s->subject->nchildren >= 2) {
                DESCR_t base = interp_eval(s->subject->children[0]);
                DESCR_t idx  = interp_eval(s->subject->children[1]);
                if (IS_FAIL_fn(base) || IS_FAIL_fn(idx)) {
                    succeeded = 0;
                } else if (s->subject->nchildren >= 3) {
                    DESCR_t idx2 = interp_eval(s->subject->children[2]);
                    if (IS_FAIL_fn(idx2)) { succeeded = 0; }
                    else { subscript_set2(base, idx, idx2, repl_val); succeeded = 1; }
                } else {
                    subscript_set(base, idx, repl_val);
                    succeeded = 1;
                }
            } else {
                DESCR_t obj = interp_eval(s->subject->children[0]);
                if (IS_FAIL_fn(obj)) {
                    succeeded = 0;
                } else {
                    FIELD_SET_fn(obj, s->subject->sval, repl_val);
                    succeeded = 1;
                }
            }

        /* ── NRETURN lvalue assign: fn() = expr  (zero-arg fn call as lvalue) ── */
        } else if (s->has_eq && s->subject &&
                   s->subject->kind == E_FNC && s->subject->sval &&
                   s->subject->nchildren == 0) {
            DESCR_t rv = s->replacement ? interp_eval(s->replacement) : NULVCL;
            if (!IS_FAIL_fn(rv)) {
                DESCR_t fres = call_user_function(s->subject->sval, NULL, 0);
                /* Use NAME_SET: slen discriminates NAMEPTR (interior ptr) from NAMEVAL (name string) */
                if (NAME_SET(fres, rv)) { succeeded = 1; }
                else { succeeded = 0; }
            } else succeeded = 0;

        /* ── expression-only (side effects, e.g. bare function call) ─ */
        } else if (s->subject && !s->pattern && !s->has_eq) {
            if (IS_FAIL_fn(subj_val)) succeeded = 0;
        }
        /* ── goto resolution ───────────────────────────────────────── */
        target = NULL;
        if (s->go) {
            if (s->go->uncond && *s->go->uncond)
                target = s->go->uncond;
            else if (s->go->computed_uncond_expr) {
                DESCR_t cv = interp_eval(s->go->computed_uncond_expr);
                target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
            } else if (succeeded && s->go->onsuccess && *s->go->onsuccess)
                target = s->go->onsuccess;
            else if (succeeded && s->go->computed_success_expr) {
                DESCR_t cv = interp_eval(s->go->computed_success_expr);
                target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
            } else if (!succeeded && s->go->onfailure && *s->go->onfailure)
                target = s->go->onfailure;
            else if (!succeeded && s->go->computed_failure_expr) {
                DESCR_t cv = interp_eval(s->go->computed_failure_expr);
                target = (cv.v == DT_S && cv.s) ? cv.s : NULL;
            }
        }

        do_goto:
        if (target) {
            /* Check for END pseudo-label */
            if (strcasecmp(target, "END") == 0) break;
            /* RETURN/FRETURN at top-level (outside a call) → treat as END */
            if (strcasecmp(target, "RETURN") == 0 || strcasecmp(target, "FRETURN") == 0) break;
            STMT_t *dest = label_lookup(target);
            if (dest) { s = dest; continue; }
            /* Unknown label — Error 24: Undefined or erroneous goto */
            sno_runtime_error(24, NULL);
            break;
        }

        s = s->next;
    }

    /* ── U-23: section-ordered polyglot dispatch ──────────────────────────
     * When g_polyglot=1 (multi-section .scrip), execute modules in registry
     * order so interleaved SNO/ICN/PL sections see each other's NV values.
     * The SNO main loop above already ran all SNO stmts (they are NOT
     * re-run here).  For Icon and Prolog sections we call their main/0
     * in source order relative to SNO sections.
     *
     * U-23 multi-section model:
     *   - SNO sections: already executed by main loop (in order, END skipped).
     *   - ICN sections: find the "main" proc for each ICN module in order
     *     and call it.  If multiple ICN modules share a proc table, only one
     *     main will be present — that is correct.
     *   - PL  sections: call main/0 once after all PL clauses are loaded.
     *
     * For single-section .scrip (g_polyglot=0), fall through to legacy dispatch.
     */
    if (g_polyglot && g_registry.nmod > 0) {
        /* U-23: two-pass dispatch. SNO already ran; dispatch ICN/PL in registry order. */
        for (int _mi = 0; _mi < g_registry.nmod; _mi++) {
            ScripModule *_m = &g_registry.mods[_mi];
            if (_m->lang == LANG_ICN || _m->lang == LANG_RAKU) {
                int _pend = _m->icn_proc_start + _m->icn_proc_count;
                int _found = 0;
                g_lang = 1;   /* OE-7: Icon top-level mode required for icn_call_proc */
                for (int _pi = _m->icn_proc_start; _pi < _pend && _pi < icn_proc_count; _pi++) {
                    if (strcmp(icn_proc_table[_pi].name, "main") == 0)
                        { icn_call_proc(icn_proc_table[_pi].proc, NULL, 0); _found=1; break; }
                }
                if (!_found)
                    for (int _pi=0; _pi<icn_proc_count; _pi++)
                        if (strcmp(icn_proc_table[_pi].name,"main")==0)
                            { icn_call_proc(icn_proc_table[_pi].proc,NULL,0); break; }
                g_lang = 0;
            } else if (_m->lang == LANG_PL) {
                EXPR_t *pl_main = pl_pred_table_lookup(&g_pl_pred_table, "main/0");
                if (pl_main) {
                    int sv_pl = g_pl_active; g_pl_active = 1;
                    interp_eval(pl_main);
                    g_pl_active = sv_pl;
                }
            }
        }
        return;
    }

    /* ── Legacy single-section dispatch (U-15 / U-19) ──────────────────── */
    if (icn_proc_count > 0) {
        for (int _i = 0; _i < icn_proc_count; _i++) {
            if (strcmp(icn_proc_table[_i].name, "main") == 0) {
                icn_call_proc(icn_proc_table[_i].proc, NULL, 0);
                break;
            }
        }
    }
    {
        EXPR_t *pl_main = pl_pred_table_lookup(&g_pl_pred_table, "main/0");
        if (pl_main) {
            int sv_pl = g_pl_active;
            g_pl_active = 1;
            interp_eval(pl_main);
            g_pl_active = sv_pl;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

/* _eval_str_impl_fn — EVAL(string) hook for pattern-context strings.
 * Uses bison parse_expr_pat_from_str (snobol4.tab.c) which produces
 * EXPR_t with correct EKind values directly — no CMPILE/CMPND_t bridge. */
DESCR_t _eval_str_impl_fn(const char *s) {
    EXPR_t *tree = parse_expr_pat_from_str(s);
    if (!tree) return FAILDESCR;
    return interp_eval_pat(tree);
}

DESCR_t _eval_pat_impl_fn(DESCR_t pat) {
    /* Run DT_P pattern against empty subject — used by EVAL_fn for *func() patterns.
     * If function fails at match time, EVAL fails. */
    extern int exec_stmt(const char *, DESCR_t *, DESCR_t, DESCR_t *, int);
    DESCR_t subj = STRVAL("");
    int ok = exec_stmt("", &subj, pat, NULL, 0);
    return ok ? NULVCL : FAILDESCR;
}


/* label_exists — called by LABEL() builtin via sno_set_label_exists_hook */
int _label_exists_fn(const char *name) {
    return label_lookup(name) != NULL;
}

/* S-10: forward declarations so _usercall_hook can call them */
DESCR_t _builtin_IDENT(DESCR_t *args, int nargs);
DESCR_t _builtin_DIFFER(DESCR_t *args, int nargs);
DESCR_t _builtin_DATA(DESCR_t *args, int nargs);

/* _usercall_hook: calls user functions via call_user_function;
 * for pure builtins (FNCEX_fn && no body label) uses APPLY_fn directly
 * so FAILDESCR propagates correctly (DYN-74: fixes *ident(1,2) in EVAL).
 * U-22: cross-call extension — if name not found in SNO label table,
 * try icn_proc_table (BB_PUMP one-shot) then g_pl_pred_table (BB_ONCE). */
DESCR_t _usercall_hook(const char *name, DESCR_t *args, int nargs) {
    /* S-10 fix: handle scrip.c-only predicates directly so *IDENT(x)/*DIFFER(x)
     * in pattern context correctly fail/succeed via bb_usercall -> g_user_call_hook. */
    if (strcasecmp(name, "IDENT") == 0)  return _builtin_IDENT(args, nargs);
    if (strcasecmp(name, "DIFFER") == 0) return _builtin_DIFFER(args, nargs);
    if (strcasecmp(name, "DATA") == 0)   return _builtin_DATA(args, nargs);
    /* SC-1: DATA constructor/field-accessor dispatch via sc_dat registry.
     * Must precede label lookup so struct names shadow any same-named labels. */
    {
        ScDatType *_dt = sc_dat_find_type(name);
        if (_dt) return sc_dat_construct(_dt, args, nargs);
        int _fi = 0;
        ScDatType *_ft = sc_dat_find_field(name, &_fi);
        if (_ft && nargs >= 1) return sc_dat_field_get(name, args[0]);
    }
    /* Check for a body label (user-defined function) */
    const char *_entry = FUNC_ENTRY_fn(name);
    STMT_t *_body = _entry ? label_lookup(_entry) : NULL;
    if (!_body) _body = label_lookup(name);
    if (!_body) {
        char _uf[128]; size_t _fl = strlen(name);
        if (_fl >= sizeof(_uf)) _fl = sizeof(_uf) - 1;
        for (size_t _i = 0; _i <= _fl; _i++)
            _uf[_i] = (char)toupper((unsigned char)name[_i]);
        _body = label_lookup(_uf);
    }
    /* Pure builtin (no body) AND registered as builtin: use APPLY_fn for correct failure */
    if (!_body && FNCEX_fn(name)) return APPLY_fn(name, args, nargs);

    /* ── U-22: cross-language fallback ────────────────────────────────
     * Name not found in SNO label/builtin tables.  Try Icon, then Prolog.
     * This lets SNOBOL4 source call Icon procedures and Prolog predicates
     * by name, the same way the linker resolves an undefined symbol. */
    if (!_body) {
        /* Try Icon proc table (case-sensitive — Icon is case-sensitive) */
        for (int _i = 0; _i < icn_proc_count; _i++) {
            if (strcmp(icn_proc_table[_i].name, name) == 0) {
                /* Call as one-shot: drive the Icon proc and return its value.
                 * icn_call_proc returns FAILDESCR if the procedure fails. */
                return icn_call_proc(icn_proc_table[_i].proc, args, nargs);
            }
        }
        /* Try Prolog pred table: name/arity key, e.g. "color/1" */
        if (g_pl_active) {
            char pl_key[256];
            snprintf(pl_key, sizeof pl_key, "%s/%d", name, nargs);
            EXPR_t *choice = pl_pred_table_lookup(&g_pl_pred_table, pl_key);
            if (choice) {
                /* Set up Prolog arg Term** from DESCR_t args, drive BB_ONCE */
                Term **pl_args = (nargs > 0) ? pl_env_new(nargs) : NULL;
                for (int _i = 0; _i < nargs; _i++)
                    pl_args[_i] = pl_unified_term_from_expr(
                        /* wrap DESCR_t as a literal EXPR_t leaf */
                        (args[_i].v == DT_S)
                            ? &(EXPR_t){ .kind = E_QLIT, .sval = (char*)args[_i].s }
                            : &(EXPR_t){ .kind = E_ILIT, .ival = (long)args[_i].s },
                        NULL);
                Term **saved_env = g_pl_env;
                g_pl_env = pl_args;
                bb_node_t root = pl_box_choice(choice, g_pl_env, nargs);
                int ok = bb_broker(root, BB_ONCE, NULL, NULL);
                g_pl_env = saved_env;
                return ok ? INTVAL(1) : FAILDESCR;
            }
        }
    }

    /* User-defined (has body) OR unknown: call_user_function handles both */
    return call_user_function(name, args, nargs);
}


/* ── ir_print_stmt — print one STMT_t as IR sexp for comparison sweep ──────
 * Emits: (STMT [:lbl L] [:subj EXPR] [:pat EXPR] [:repl EXPR] [:go*])
 * Used by --dump-ir and --dump-ir-bison.
 * ir_print_node() is from src/ir/ir_print.c — linked via Makefile.
 * ----------------------------------------------------------------------- */
static void ir_print_stmt(STMT_t *st, FILE *f) {
    fprintf(f, "(STMT");
    if (st->label)       fprintf(f, " :lbl %s", st->label);
    if (st->has_eq)      fprintf(f, " :eq");
    if (st->is_end)      fprintf(f, " :end");
    if (st->subject)   { fprintf(f, " :subj ");  ir_print_node(st->subject,     f); }
    if (st->pattern)   { fprintf(f, " :pat ");   ir_print_node(st->pattern,     f); }
    if (st->replacement){fprintf(f, " :repl ");  ir_print_node(st->replacement, f); }
    if (st->go) {
        SnoGoto *g = st->go;
        if (g->uncond)    fprintf(f, " :go %s",  g->uncond);
        if (g->onsuccess) fprintf(f, " :goS %s", g->onsuccess);
        if (g->onfailure) fprintf(f, " :goF %s", g->onfailure);
        if (g->computed_uncond_expr)  { fprintf(f, " :go $(");  ir_print_node(g->computed_uncond_expr,  f); fprintf(f, ")"); }
        if (g->computed_success_expr) { fprintf(f, " :goS $("); ir_print_node(g->computed_success_expr, f); fprintf(f, ")"); }
        if (g->computed_failure_expr) { fprintf(f, " :goF $("); ir_print_node(g->computed_failure_expr, f); fprintf(f, ")"); }
    }
    fprintf(f, ")\n");
}

/* Dump a full Program* as IR sexp — one line per statement. */
void ir_dump_program(Program *prog, FILE *f) {
    if (!prog) { fprintf(f, "(NULL-PROGRAM)\n"); return; }
    for (STMT_t *st = prog->head; st; st = st->next)
        ir_print_stmt(st, f);
}

/* ── S-10 fix: IDENT/DIFFER wrappers for register_fn ──────────────────────
 * IDENT and DIFFER are scrip.c-only builtins not in the binary runtime's
 * APPLY_fn table.  When *IDENT(x) fires at match time via deferred_call_fn,
 * APPLY_fn("IDENT",...) returns non-FAILDESCR for unknown names → T_FUNC
 * always succeeds.  Registering these wrappers makes APPLY_fn dispatch them
 * correctly so *IDENT(n(x)) fails when n(x) is not the null string. */
DESCR_t _builtin_IDENT(DESCR_t *args, int nargs) {
    if (nargs == 1) return IS_NULL_fn(args[0]) ? NULVCL : FAILDESCR;
    if (nargs >= 2) {
        int a_null = IS_NULL_fn(args[0]), b_null = IS_NULL_fn(args[1]);
        if (a_null && b_null) return NULVCL;
        if (a_null || b_null) return FAILDESCR;
        if (args[0].v != args[1].v) return FAILDESCR;
        const char *sa = VARVAL_fn(args[0]), *sb = VARVAL_fn(args[1]);
        if (!sa) sa = ""; if (!sb) sb = "";
        return strcmp(sa, sb) == 0 ? NULVCL : FAILDESCR;
    }
    return FAILDESCR;
}
DESCR_t _builtin_DIFFER(DESCR_t *args, int nargs) {
    if (nargs == 1) return IS_NULL_fn(args[0]) ? FAILDESCR : NULVCL;
    if (nargs >= 2) {
        int a_null = IS_NULL_fn(args[0]), b_null = IS_NULL_fn(args[1]);
        if (a_null && b_null) return FAILDESCR;
        if (a_null || b_null) return NULVCL;
        if (args[0].v != args[1].v) return NULVCL;
        const char *sa = VARVAL_fn(args[0]), *sb = VARVAL_fn(args[1]);
        if (!sa) sa = ""; if (!sb) sb = "";
        return strcmp(sa, sb) != 0 ? NULVCL : FAILDESCR;
    }
    return FAILDESCR;
}

/* ── EVAL/CODE wrappers ─────────────────────────────────────────────────────
 * EVAL and CODE are not registered in the binary APPLY_fn table.
 * FNCEX_fn("EVAL")=0 so _usercall_hook falls through to call_user_function
 * which finds no body → returns NULVCL (STRING) instead of DT_P.
 * Fix: register wrappers so FNCEX_fn("EVAL")=1 → APPLY_fn dispatches here
 * → EVAL_fn → CONVE_fn → EXPVAL_fn → correct DT_P for pattern expressions. */
extern DESCR_t EVAL_fn(DESCR_t);
extern DESCR_t code(const char *);
DESCR_t _builtin_EVAL(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    return EVAL_fn(args[0]);
}
DESCR_t _builtin_CODE(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    const char *s = VARVAL_fn(args[0]);
    if (!s || !*s) return FAILDESCR;
    return code(s);
}

/* SC-1: print(v...) — Snocone print builtin: outputs each arg on its own line */
DESCR_t _builtin_print(DESCR_t *args, int nargs) {
    if (nargs == 0) { output_str(""); return NULVCL; }
    for (int i = 0; i < nargs; i++) output_val(args[i]);
    return NULVCL;
}

/* ── SC-1: DATA registry — constructor/accessor dispatch for --ir-run ─────────
 * DEFDAT_fn registers in the SPITBOL binary runtime table; APPLY_fn does not
 * expose DATA-defined names via FNCEX_fn.  We maintain our own registry so
 * interp_eval E_FNC can dispatch constructors and field accessors directly. */

#define SC_DAT_MAX_FIELDS 64
#define SC_DAT_MAX_TYPES  128

static ScDatType sc_dat_types[SC_DAT_MAX_TYPES];
static int       sc_dat_ntypes = 0;

/* Parse "name(f1,f2,...)" spec and register in our table + DEFDAT_fn */
static ScDatType *sc_dat_register(const char *spec) {
    if (sc_dat_ntypes >= SC_DAT_MAX_TYPES) return NULL;
    ScDatType *t = &sc_dat_types[sc_dat_ntypes];
    memset(t, 0, sizeof *t);
    /* parse name */
    const char *p = spec;
    int ni = 0;
    while (*p && *p != '(' && ni < 63) t->name[ni++] = *p++;
    t->name[ni] = '\0';
    if (*p == '(') p++; /* skip '(' */
    /* parse fields */
    while (*p && *p != ')') {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == ')') break;
        int fi = 0;
        while (*p && *p != ',' && *p != ')' && fi < 63) t->fields[t->nfields][fi++] = *p++;
        t->fields[t->nfields][fi] = '\0';
        if (t->nfields < SC_DAT_MAX_FIELDS - 1) t->nfields++;
        if (*p == ',') p++;
    }
    sc_dat_ntypes++;
    return t;
}

/* Look up a DATA type by constructor name (case-insensitive) */
static ScDatType *sc_dat_find_type(const char *name) {
    for (int i = 0; i < sc_dat_ntypes; i++)
        if (strcasecmp(sc_dat_types[i].name, name) == 0) return &sc_dat_types[i];
    return NULL;
}

/* Look up which type owns a field accessor name (case-insensitive) */
static ScDatType *sc_dat_find_field(const char *name, int *fidx) {
    for (int i = 0; i < sc_dat_ntypes; i++)
        for (int j = 0; j < sc_dat_types[i].nfields; j++)
            if (strcasecmp(sc_dat_types[i].fields[j], name) == 0) {
                if (fidx) *fidx = j;
                return &sc_dat_types[i];
            }
    return NULL;
}

/* Construct a new DT_DATA instance: name(f0,f1,...) */
static DESCR_t sc_dat_construct(ScDatType *t, DESCR_t *args, int nargs) {
    DATINST_t *inst = GC_malloc(sizeof(DATINST_t));
    /* Find the DATBLK_t — allocated by DEFDAT_fn in the SPITBOL runtime.
     * We locate it by calling DATCON_fn with zero args to get a prototype,
     * then steal its type pointer — or we build our own DATBLK_t. */
    /* Build a minimal DATBLK_t owned by us */
    DATBLK_t *blk = GC_malloc(sizeof(DATBLK_t));
    blk->name    = GC_strdup(t->name);
    blk->nfields = t->nfields;
    blk->fields  = GC_malloc(t->nfields * sizeof(char *));
    for (int i = 0; i < t->nfields; i++) blk->fields[i] = GC_strdup(t->fields[i]);
    blk->next    = NULL;
    inst->type   = blk;
    inst->fields = GC_malloc(t->nfields * sizeof(DESCR_t));
    for (int i = 0; i < t->nfields; i++)
        inst->fields[i] = (i < nargs) ? args[i] : NULVCL;
    DESCR_t r;
    r.v    = DT_DATA;
    r.slen = 0;
    r.u    = inst;   /* must be last union write — .s/.u share storage */
    return r;
}

/* Field accessor: name(obj) → obj.name ; with one arg assumed to be the instance */
static DESCR_t sc_dat_field_get(const char *fname, DESCR_t obj) {
    DESCR_t *cell = data_field_ptr(fname, obj);
    if (!cell) return FAILDESCR;
    return *cell;
}

/* ── DATA() builtin ─────────────────────────────────────────────────────── */
DESCR_t _builtin_DATA(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    const char *spec = VARVAL_fn(args[0]);
    if (!spec || !*spec) return FAILDESCR;
    DEFDAT_fn(spec);              /* register in SPITBOL runtime (for DATATYPE() etc.) */
    sc_dat_register(spec);        /* register in our dispatch table */
    return NULVCL;
}
