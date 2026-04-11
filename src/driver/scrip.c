/*
 * scrip.c — unified SCRIP driver
 *
 * One binary, all modes. Frontend inferred from file extension.
 *
 * Usage:
 *   scrip [mode] [bb] [target] [options] <file> [-- program-args...]
 *
 * Execution modes (default: --sm-run):
 *   --ir-run         interpret via IR tree-walk (correctness reference)
 *   --sm-run         interpret SM_Program via dispatch loop  [DEFAULT]
 *   --jit-run        lower SM_Program to x86 bytes -> mmap slab -> jump in
 *   --jit-emit       lower SM_Program -> emit to file (target selects format)
 *
 * Byrd Box pattern mode (default: --bb-driver):
 *   --bb-driver      pattern matching via driver/broker
 *   --bb-live        pattern matching live-wired in exec memory
 *                    (only meaningful with --jit-run or --jit-emit)
 *
 * Target (for --jit-emit or --jit-run, default: --x64):
 *   --x64  --jvm  --net  --js  --wasm
 *
 * Diagnostic options:
 *   --dump-ir        print IR after frontend
 *   --dump-sm        print SM_Program after lowering
 *   --dump-bb        print BB-GRAPH for each statement
 *   --trace          MONITOR trace output (for two-way diff vs SPITBOL)
 *   --bench          print wall-clock time after execution
 *
 * Frontend inferred from extension:
 *   .sno=SNOBOL4  .icn=Icon  .pl=Prolog  .sc=Snocone  .reb=Rebus  .spt=SPITBOL
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * SPRINT:  M-SCRIP-U0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <gc.h>

/* ── frontend ─────────────────────────────────────────────────────────── */
#include "../frontend/snobol4/scrip_cc.h"
#include "../frontend/snobol4/CMPILE.h"
extern Program *sno_parse(FILE *f, const char *filename);

/* ir_print_node — from src/ir/ir_print.c (linked via Makefile) */
extern void ir_print_node   (const EXPR_t *e, FILE *f);
extern void ir_print_node_nl(const EXPR_t *e, FILE *f);

/* ── runtime ──────────────────────────────────────────────────────────── */
#include "../runtime/x86/snobol4.h"
#include "../runtime/x86/sil_macros.h"   /* SIL macro translations — both RT and SM axes */
#include "../runtime/x86/snobol4_runtime_shim.h"

/* ── SM stack machine (M-SCRIP-U3) ───────────────────────────────────── */
#include "../runtime/x86/sm_lower.h"
#include "../runtime/x86/sm_interp.h"
#include "../runtime/x86/sm_prog.h"
#include "../runtime/x86/bb_build.h"    /* M-BB-LIVE-WIRE: bb_mode_t, g_bb_mode */
#include "../runtime/x86/sm_codegen.h"  /* M-JIT-RUN: sm_codegen, sm_jit_run */
#include "../runtime/x86/sm_image.h"    /* M-JIT-RUN: sm_image_init */

/* pat_at_cursor not exposed in snobol4.h — forward-declare here */
extern DESCR_t pat_at_cursor(const char *varname);

/* stmt_init — stubbed: SM/IR paths init via SNO_INIT_fn() in snobol4.c */
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
static Program *g_prog = NULL;

/* ── Diagnostic flags (set in main, read by execute_program / sm_interp) ── */
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
static void prescan_defines(Program *prog)
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

/* ── call_user_function — forward decl (needs interp_eval, declared below) ── */
static DESCR_t  interp_eval(EXPR_t *e);      /* forward */
static DESCR_t  interp_eval_pat(EXPR_t *e);  /* forward — pattern context */
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

static DESCR_t interp_eval(EXPR_t *e)
{
    if (!e) return NULVCL;

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
                if (IS_NAME(r)) return NAME_DEREF(r);  /* NRETURN: IS_NAMEPTR/IS_NAMEVAL via NAME_DEREF */
                return r;
            }
        }
        /* IDENT/DIFFER: per SPITBOL spec, arguments must have SAME data type AND value.
         * IDENT(3, '3') FAILS (integer vs string). IDENT(S) succeeds iff S is null string.
         * DIFFER(S,T) succeeds iff they differ in type OR value. DIFFER(S) succeeds iff S != ''.
         * Both return NULVCL on success. */
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
        const char *nm = e->children[1]->sval;
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

    default:
        return NULL;
    }
}

/* -- DYN-59: interp_eval_pat -- evaluate expr in PATTERN context ----------
 * Like interp_eval but E_SEQ/E_CAT use pat_cat (not CONCAT_fn) and
 * zero-arg pattern keywords in E_VAR go through APPLY_fn first.
 * Called at the pattern call site: interp_eval_pat(s->pattern).
 * ----------------------------------------------------------------------- */
static DESCR_t interp_eval_pat(EXPR_t *e)
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
 * execute_program — full-program executor with label-table goto resolution
 * ══════════════════════════════════════════════════════════════════════════ */

static void execute_program(Program *prog)
{
    label_table_build(prog);
    prescan_defines(prog);

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
        if (s->is_end) break;
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

        /* Skip Prolog/Icon nodes that sneak in via shared parser */
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
            } else {
                subj_val = interp_eval(s->subject);
            }
        }

        succeeded = 1;

        /* ── pattern match ─────────────────────────────────────────── */
        if (s->pattern) {
            /* Build pattern descriptor via interp_eval then exec_stmt */
            DESCR_t pat_d = interp_eval(s->pattern);
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
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

static DESCR_t _eval_pat_impl_fn(DESCR_t pat) {
    /* Run DT_P pattern against empty subject — used by EVAL_fn for *func() patterns.
     * If function fails at match time, EVAL fails. */
    extern int exec_stmt(const char *, DESCR_t *, DESCR_t, DESCR_t *, int);
    DESCR_t subj = STRVAL("");
    int ok = exec_stmt("", &subj, pat, NULL, 0);
    return ok ? NULVCL : FAILDESCR;
}


/* label_exists — called by LABEL() builtin via sno_set_label_exists_hook */
static int _label_exists_fn(const char *name) {
    return label_lookup(name) != NULL;
}

/* _usercall_hook: calls user functions via call_user_function;
 * for pure builtins (FNCEX_fn && no body label) uses APPLY_fn directly
 * so FAILDESCR propagates correctly (DYN-74: fixes *ident(1,2) in EVAL). */
static DESCR_t _usercall_hook(const char *name, DESCR_t *args, int nargs) {
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
    /* User-defined (has body) OR unknown: call_user_function handles both */
    return call_user_function(name, args, nargs);
}

/* ── cmpile_lower: CMPILE_t list → Program* IR ───────────────────────
 * Walks the CMPILE_t linked list from cmpile_file() and builds the
 * Program* / STMT_t IR that execute_program() expects.
 * Bridge that makes CMPILE.c the authoritative top-level parser.
 * --------------------------------------------------------------------- */
Program *cmpile_lower(CMPILE_t *cl)
{
    if (!cl) return NULL;
    Program *prog = GC_malloc(sizeof *prog);
    prog->head = prog->tail = NULL;

    for (CMPILE_t *s = cl; s; s = s->next) {
        STMT_t *st = GC_malloc(sizeof *st);
        memset(st, 0, sizeof *st);

        /* ── label + subject wiring ─────────────────────────────────────
         * CMPILE LBLTB always puts the column-1 identifier into s->label
         * and leaves s->subject NULL.  In SNOBOL4, a column-1 label IS a
         * valid variable name: "OUTPUT = 'x'" has label="OUTPUT", subj=NULL,
         * and the assignment target is the variable OUTPUT.
         * STMT_t needs BOTH: label kept for goto resolution AND a synthesised
         * E_VAR subject so the execution loop can find subj_name.
         * Rule: if s->subject is NULL and s->label is set and there is a body
         * (replacement, pattern, or has_eq), synthesise subject from label.
         * Bare label lines (no body) and END: subject stays NULL. */
        st->label = s->label ? GC_strdup(s->label) : NULL;
        if (s->subject) {
            st->subject = cmpnd_to_expr(s->subject);
        } else if (s->label && !s->is_end
                   && (s->replacement || s->pattern || s->has_eq)) {
            EXPR_t *sv = GC_malloc(sizeof *sv);
            memset(sv, 0, sizeof *sv);
            sv->kind = E_VAR;
            sv->sval = GC_strdup(s->label);
            st->subject = sv;
        } else {
            st->subject = NULL;
        }
        st->pattern     = s->pattern     ? cmpnd_to_expr(s->pattern)     : NULL;
        st->replacement = s->replacement ? cmpnd_to_expr(s->replacement) : NULL;
        st->has_eq      = s->has_eq;
        st->is_end      = s->is_end;
        /* Wire goto: CMPILE_t has go_s/go_f/go_u; STMT_t uses SnoGoto */
        if (s->go_s || s->go_f || s->go_u) {
            SnoGoto *sg = GC_malloc(sizeof *sg);
            memset(sg, 0, sizeof *sg);
            sg->onsuccess = s->go_s ? GC_strdup(s->go_s) : NULL;
            sg->onfailure = s->go_f ? GC_strdup(s->go_f) : NULL;
            sg->uncond    = s->go_u ? GC_strdup(s->go_u) : NULL;
            st->go = sg;
        }
        st->next        = NULL;

        if (!prog->head) prog->head = st;
        else             prog->tail->next = st;
        prog->tail = st;
    }
    return prog;
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
static void ir_dump_program(Program *prog, FILE *f) {
    if (!prog) { fprintf(f, "(NULL-PROGRAM)\n"); return; }
    for (STMT_t *st = prog->head; st; st = st->next)
        ir_print_stmt(st, f);
}

int main(int argc, char **argv)
{
    /* ── flag parsing ─────────────────────────────────────────────────── */

    /* Execution modes — mutually exclusive (default: --sm-run) */
    int mode_ir_run        = 0;  /* --ir-run   : interpret via IR tree-walk (correctness ref) */
    int mode_sm_run        = 0;  /* --sm-run   : interpret SM_Program via dispatch loop [DEFAULT] */
    int mode_jit_run       = 0;  /* --jit-run  : SM_Program -> x86 bytes -> mmap slab -> jump in */
    int mode_jit_emit      = 0;  /* --jit-emit : SM_Program -> emit to file (target selects format) */

    /* Byrd Box pattern mode — independent switch (default: --bb-driver) */
    int bb_driver          = 0;  /* --bb-driver : pattern matching via driver/broker */
    int bb_live            = 0;  /* --bb-live   : live-wired in exec memory */

    /* Emit targets (meaningful with --jit-emit, default: --x64) */
    int target_x64         = 0;  /* --x64  */
    int target_jvm         = 0;  /* --jvm  */
    int target_net         = 0;  /* --net  */
    int target_js          = 0;  /* --js   */
    int target_c           = 0;  /* --c    */
    int target_wasm        = 0;  /* --wasm */

    /* Diagnostic options */
    int dump_parse         = 0;  /* --dump-parse      */
    int dump_parse_flat    = 0;  /* --dump-parse-flat */
    int dump_ir            = 0;  /* --dump-ir   : print IR after frontend */
    int dump_ir_bison      = 0;  /* --dump-ir-bison : IR via old Bison/Flex parser */
    int dump_sm            = 0;  /* --dump-sm   : print SM_Program after lowering */
    int dump_bb            = 0;  /* --dump-bb   : print BB-GRAPH per statement */
    int opt_trace          = 0;  /* --trace     : MONITOR trace output */
    int opt_bench          = 0;  /* --bench     : print wall-clock time after execution */

    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] == '-') {
        /* execution modes */
        if      (strcmp(argv[argi], "--ir-run")        == 0) { mode_ir_run        = 1; argi++; }
        else if (strcmp(argv[argi], "--sm-run")        == 0) { mode_sm_run        = 1; argi++; }
        else if (strcmp(argv[argi], "--jit-run")       == 0) { mode_jit_run       = 1; argi++; }
        else if (strcmp(argv[argi], "--jit-emit")      == 0) { mode_jit_emit      = 1; argi++; }
        /* BB pattern mode */
        else if (strcmp(argv[argi], "--bb-driver")     == 0) { bb_driver          = 1; argi++; }
        else if (strcmp(argv[argi], "--bb-live")       == 0) { bb_live            = 1; argi++; }
        /* emit targets */
        else if (strcmp(argv[argi], "--x64")           == 0) { target_x64         = 1; argi++; }
        else if (strcmp(argv[argi], "--jvm")           == 0) { target_jvm         = 1; argi++; }
        else if (strcmp(argv[argi], "--net")           == 0) { target_net         = 1; argi++; }
        else if (strcmp(argv[argi], "--js")            == 0) { target_js          = 1; argi++; }
        else if (strcmp(argv[argi], "--c")             == 0) { target_c           = 1; argi++; }
        else if (strcmp(argv[argi], "--wasm")          == 0) { target_wasm        = 1; argi++; }
        /* diagnostic */
        else if (strcmp(argv[argi], "--dump-parse")      == 0) { dump_parse      = 1; argi++; }
        else if (strcmp(argv[argi], "--dump-parse-flat") == 0) { dump_parse_flat = 1; argi++; }
        else if (strcmp(argv[argi], "--dump-ir")         == 0) { dump_ir         = 1; argi++; }
        else if (strcmp(argv[argi], "--dump-ir-bison")   == 0) { dump_ir_bison   = 1; argi++; }
        else if (strcmp(argv[argi], "--dump-sm")         == 0) { dump_sm         = 1; argi++; }
        else if (strcmp(argv[argi], "--dump-bb")         == 0) { dump_bb         = 1; argi++; }
        else if (strcmp(argv[argi], "--trace")           == 0) { opt_trace       = 1; argi++; }
        else if (strcmp(argv[argi], "--bench")           == 0) { opt_bench       = 1; argi++; }
        else break;
    }

    /* Default execution mode: --sm-run */
    if (!mode_ir_run && !mode_sm_run && !mode_jit_run && !mode_jit_emit)
        mode_sm_run = 1;

    /* Default BB mode: --bb-driver unless --bb-live explicitly set */
    if (!bb_driver && !bb_live) bb_driver = 1;

    /* Default emit target: --x64 */
    if (mode_jit_emit && !target_x64 && !target_jvm && !target_net &&
        !target_js && !target_c && !target_wasm)
        target_x64 = 1;

    /* Suppress unused warnings for modes/targets not yet wired to codegen */
    (void)bb_driver;
    (void)target_x64; (void)target_jvm; (void)target_net;
    (void)target_js; (void)target_c;

    /* M-BB-LIVE-WIRE: propagate BB mode to stmt_exec.c */
    if (bb_live) g_bb_mode = BB_MODE_LIVE;

    if (argi >= argc) {
        fprintf(stderr,
            "usage: scrip [mode] [bb] [target] [options] <file> [-- program-args...]\n"
            "\n"
            "Execution modes (default: --sm-run):\n"
            "  --ir-run         interpret via IR tree-walk (correctness reference)\n"
            "  --sm-run         interpret SM_Program via dispatch loop  [DEFAULT]\n"
            "  --jit-run        SM_Program -> x86 bytes -> mmap slab -> jump in\n"
            "  --jit-emit       SM_Program -> emit to file (target selects format)\n"
            "\n"
            "Byrd Box pattern mode (default: --bb-driver):\n"
            "  --bb-driver      pattern matching via driver/broker\n"
            "  --bb-live        live-wired BB blobs in exec memory (requires M-DYN-B* blobs)\n"
            "\n"
            "Target (default: --x64):\n"
            "  --x64  --jvm  --net  --js  --c  --wasm\n"
            "\n"
            "Diagnostic options:\n"
            "  --dump-ir        print IR after frontend\n"
            "  --dump-sm        print SM_Program after lowering\n"
            "  --dump-bb        print BB-GRAPH for each statement\n"
            "  --trace          MONITOR trace output (diff vs SPITBOL)\n"
            "  --bench          print wall-clock time after execution\n"
            "  --dump-parse     dump CMPILE parse tree\n"
            "  --dump-parse-flat  dump CMPILE parse tree (one line)\n"
            "  --dump-ir-bison  dump IR via old Bison/Flex parser\n"
            "\n"
            "Frontend inferred from file extension:\n"
            "  .sno=SNOBOL4  .spt=SPITBOL  .icn=Icon  .pl=Prolog  .sc=Snocone  .reb=Rebus\n"
        );
        return 1;
    }
    const char *input_path = argv[argi];

    /* Set up include search dirs before parsing:
     * 1. Directory of the input file itself
     * 2. SNO_LIB env var (corpus root for 'lib/xxx.sno' includes)
     * 3. Current working directory */
    {
        extern void sno_add_include_dir(const char *d);
        /* dir of input file */
        char dirbuf[4096];
        strncpy(dirbuf, input_path, sizeof dirbuf - 1);
        dirbuf[sizeof dirbuf - 1] = '\0';
        char *sl = strrchr(dirbuf, '/');
        if (sl) { *sl = '\0'; sno_add_include_dir(dirbuf); }
        else     { sno_add_include_dir("."); }
        /* SNO_LIB env var */
        const char *sno_lib = getenv("SNO_LIB");
        if (sno_lib && *sno_lib) sno_add_include_dir(sno_lib);
        /* Auto-detect corpus root: walk up from file dir looking for lib/ subdir.
         * Handles 'lib/math.sno' style includes without requiring SNO_LIB. */
        {
            char walk[4096];
            strncpy(walk, input_path, sizeof walk - 1);
            walk[sizeof walk - 1] = '\0';
            char *p = strrchr(walk, '/');
            while (p) {
                *p = '\0';
                char probe[4096];
                snprintf(probe, sizeof probe, "%s/lib", walk);
                struct stat st;
                if (stat(probe, &st) == 0 && S_ISDIR(st.st_mode)) {
                    sno_add_include_dir(walk);
                    break;
                }
                p = strrchr(walk, '/');
            }
        }
        /* cwd fallback */
        sno_add_include_dir(".");
    }

    FILE *f = fopen(input_path, "r");
    if (!f) {
        fprintf(stderr, "scrip: cannot open '%s'\n", input_path);
        return 1;
    }

    /* ── parse ──────────────────────────────────────────────────────────
     * --dump-parse / --dump-parse-flat: use CMPILE path, emit, exit.
     * Normal execution: sno_parse() (proven path, PASS=190 baseline).
     * cmpile_lower() wiring for execution is RT-105 step 2 — requires
     * cmpnd_to_expr() coverage audit before it can replace sno_parse().
     * ----------------------------------------------------------------- */
    cmpile_init();
    /* Mirror the same include dirs used by sno_add_include_dir above */
    {
        char dirbuf2[4096];
        strncpy(dirbuf2, input_path, sizeof dirbuf2 - 1);
        dirbuf2[sizeof dirbuf2 - 1] = '\0';
        char *sl2 = strrchr(dirbuf2, '/');
        if (sl2) { *sl2 = '\0'; cmpile_add_include(dirbuf2); }
        else      { cmpile_add_include("."); }
        const char *sno_lib2 = getenv("SNO_LIB");
        if (sno_lib2 && *sno_lib2) cmpile_add_include(sno_lib2);
        /* corpus root auto-detect */
        char walk2[4096];
        strncpy(walk2, input_path, sizeof walk2 - 1);
        walk2[sizeof walk2 - 1] = '\0';
        char *p2 = strrchr(walk2, '/');
        while (p2) {
            *p2 = '\0';
            char probe2[4096];
            snprintf(probe2, sizeof probe2, "%s/lib", walk2);
            struct stat st2;
            if (stat(probe2, &st2) == 0 && S_ISDIR(st2.st_mode)) {
                cmpile_add_include(walk2);
                break;
            }
            p2 = strrchr(walk2, '/');
        }
        cmpile_add_include(".");
    }
    struct timespec _t0, _t1, _t2, _t3;
    if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t0);

    /* ── parse ──────────────────────────────────────────────────────────────
     * --dump-parse / --dump-parse-flat / --dump-ir  →  CMPILE (hand-written)
     * everything else (--ir-run, --sm-run, --dump-ir-bison)  →  sno_parse (Bison/Flex)
     * sno_parse is the proven path: PASS=190 baseline. */
    Program *prog = NULL;
    if (dump_parse || dump_parse_flat || dump_ir) {
        CMPILE_t *cl = cmpile_file(f, input_path);
        fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        if (dump_parse || dump_parse_flat) {
            int oneline = dump_parse_flat ? 1 : 0;
            int idx = 0;
            for (CMPILE_t *s = cl; s; s = s->next)
                cmpile_print(s, stdout, oneline, idx++);
            cmpile_free(cl);
            return 0;
        }
        /* dump_ir */
        Program *cprog = cmpile_lower(cl);
        cmpile_free(cl);
        ir_dump_program(cprog, stdout);
        return 0;
    } else {
        fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        FILE *f2 = fopen(input_path, "r");
        if (!f2) { fprintf(stderr, "scrip: cannot re-open '%s'\n", input_path); return 1; }
        prog = sno_parse(f2, input_path);
        fclose(f2);
        if (dump_ir_bison) { ir_dump_program(prog, stdout); return 0; }
    }

    if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t2);

    if (!prog || !prog->head) {
        fprintf(stderr, "scrip: parse failed for '%s'\n", input_path);
        return 1;
    }

    /* Initialise binary box pool (M-DYN-B1) */
    {
        extern void bb_pool_init(void);
        bb_pool_init();
    }

    /* Initialise all builtins (GT, LT, SIZE, DATATYPE, etc.) registered in snobol4.c */
    extern void SNO_INIT_fn(void);
    SNO_INIT_fn();

    stmt_init();
    g_prog = prog;

    /* Wire user-function dispatch hook (wrapper defined above main) */
    extern DESCR_t (*g_user_call_hook)(const char *, DESCR_t *, int);
    g_user_call_hook = _usercall_hook;

    /* Wire LABEL() predicate hook */
    {
        extern void sno_set_label_exists_hook(int (*fn)(const char *));
        sno_set_label_exists_hook(_label_exists_fn);
    }

    /* Wire DT_P eval hook: EVAL(*func(args)) runs pattern against empty subject */
    {
        extern DESCR_t (*g_eval_pat_hook)(DESCR_t pat);
        g_eval_pat_hook = _eval_pat_impl_fn;
    }

    /* ── Set diagnostic globals ─────────────────────────────────────── */
    g_opt_trace   = opt_trace;
    g_opt_dump_bb = dump_bb;

    /* ── --dump-sm with --ir-run: lower-only, no execution ─────────── */
    if (dump_sm && !mode_sm_run) {
        label_table_build(prog);
        prescan_defines(prog);
        SM_Program *sm0 = sm_lower(prog);
        if (!sm0) { fprintf(stderr, "scrip: sm_lower failed\n"); return 1; }
        sm_prog_print(sm0, stdout);
        sm_prog_free(sm0);
        return 0;
    }

    /* ── --jit-emit --wasm: REMOVED (2026-04-08) ────────────────────── */
    if (mode_jit_emit && target_wasm) {
        fprintf(stderr, "scrip: --wasm emit removed from scrip build\n");
        return 1;
    }

    if (mode_sm_run) {
        /* --sm-run: SM-LOWER path — IR → SM_Program → sm_interp_run.
         * Must mirror execute_program setup: build label table and register
         * DEFINE'd functions so call_user_function can find bodies via
         * g_user_call_hook → _usercall_hook → label_lookup. */
        label_table_build(prog);
        prescan_defines(prog);
        g_sno_err_active = 1;   /* arm so sno_runtime_error longjmps safely */
        SM_Program *sm = sm_lower(prog);
        if (!sm) {
            fprintf(stderr, "scrip: sm_lower failed\n");
            return 1;
        }
        /* ── --dump-sm: print SM_Program and exit ───────────────────── */
        if (dump_sm) {
            sm_prog_print(sm, stdout);
            sm_prog_free(sm);
            return 0;
        }
        SM_State st;
        sm_state_init(&st);
        /* Arm g_sno_err_jmp: sno_runtime_error longjmps here on error.
         * We treat each error as statement failure: mark last_ok=0, advance pc,
         * and re-enter the interp loop.  This mirrors execute_program's per-stmt
         * setjmp pattern and prevents longjmp into an uninitialized jmp_buf. */
        int hybrid_err;
        while (1) {
            hybrid_err = setjmp(g_sno_err_jmp);
            if (hybrid_err != 0) {
                /* runtime error fired mid-statement: mark fail, advance past
                 * the current instruction and continue */
                st.last_ok = 0;
                st.sp = 0;  /* reset value stack — state is undefined after error */
                if (st.pc < sm->count) st.pc++;  /* skip offending instruction */
                /* drain to next SM_STNO boundary so we resume cleanly */
                while (st.pc < sm->count &&
                       sm->instrs[st.pc].op != SM_STNO &&
                       sm->instrs[st.pc].op != SM_HALT)
                    st.pc++;
            }
            int rc = sm_interp_run(sm, &st);
            if (rc == 0 || rc < -1) break;  /* halted or fatal */
            if (st.pc >= sm->count) break;
        }
        sm_prog_free(sm);
    } else if (mode_jit_run) {
        /* --jit-run: SM-LOWER → sm_codegen → sm_jit_run.
         * Same preamble as --sm-run; codegen replaces sm_interp_run. */
        label_table_build(prog);
        prescan_defines(prog);
        g_sno_err_active = 1;
        SM_Program *sm = sm_lower(prog);
        if (!sm) { fprintf(stderr, "scrip: sm_lower failed\n"); return 1; }
        if (dump_sm) { sm_prog_print(sm, stdout); sm_prog_free(sm); return 0; }
        if (sm_image_init() != 0) {
            fprintf(stderr, "scrip: sm_image_init failed\n");
            sm_prog_free(sm); return 1;
        }
        if (sm_codegen(sm) != 0) {
            fprintf(stderr, "scrip: sm_codegen failed\n");
            sm_prog_free(sm); return 1;
        }
        SM_State st;
        sm_state_init(&st);
        int hybrid_err;
        while (1) {
            hybrid_err = setjmp(g_sno_err_jmp);
            if (hybrid_err != 0) {
                st.last_ok = 0;
                st.sp = 0;
                if (st.pc < sm->count) st.pc++;
                while (st.pc < sm->count &&
                       sm->instrs[st.pc].op != SM_STNO &&
                       sm->instrs[st.pc].op != SM_HALT)
                    st.pc++;
            }
            int rc = sm_jit_run(sm, &st);
            if (rc == 0 || rc < -1) break;
            if (st.pc >= sm->count) break;
        }
        sm_prog_free(sm);
    } else {
        execute_program(prog);
    }
    if (opt_bench) {
        clock_gettime(CLOCK_MONOTONIC, &_t3);
        double parse_ms = (_t1.tv_sec - _t0.tv_sec)*1e3 + (_t1.tv_nsec - _t0.tv_nsec)/1e6;
        double lower_ms = (_t2.tv_sec - _t1.tv_sec)*1e3 + (_t2.tv_nsec - _t1.tv_nsec)/1e6;
        double exec_ms  = (_t3.tv_sec - _t2.tv_sec)*1e3 + (_t3.tv_nsec - _t2.tv_nsec)/1e6;
        fprintf(stderr, "BENCH parse=%.2fms lower=%.2fms exec=%.2fms total=%.2fms\n",
                parse_ms, lower_ms, exec_ms, parse_ms + lower_ms + exec_ms);
    }
    /* M-DYN-B13: BINARY_AUDIT=1 is canonical; SNO_BINARY_BOXES=1 is legacy alias */
    if (getenv("BINARY_AUDIT") || getenv("SNO_BINARY_BOXES")) {
        extern void bin_audit_print(void);
        bin_audit_print();
    }
    return 0;
}
