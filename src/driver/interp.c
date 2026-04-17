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
#include "frontend/raku/raku_re.h"
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

/* ── RK-25: Raku exception global ─────────────────────────────────────── */
char g_raku_exception[512] = "";   /* set by raku_die, read by raku_try */
static Raku_match g_raku_match;        /* last regex match result */
static const char *g_raku_subject = ""; /* subject of last match */
/* RK-38: file handle table */
#define RAKU_FH_MAX 64
static FILE *raku_fh_table[RAKU_FH_MAX];
static int   raku_fh_init = 0;
static void raku_fh_ensure_init(void) {
    if (raku_fh_init) return;
    memset(raku_fh_table,0,sizeof raku_fh_table);
    raku_fh_table[0]=stdin; raku_fh_table[1]=stdout; raku_fh_table[2]=stderr;
    raku_fh_init=1;
}
static int raku_fh_alloc(FILE *fp) {
    raku_fh_ensure_init();
    for(int i=3;i<RAKU_FH_MAX;i++) if(!raku_fh_table[i]){raku_fh_table[i]=fp;return i;}
    return -1;
}
static FILE *raku_fh_get(int idx){
    raku_fh_ensure_init();
    if(idx<0||idx>=RAKU_FH_MAX) return NULL;
    return raku_fh_table[idx];
}
static void raku_fh_free(int idx){
    if(raku_fh_init&&idx>=3&&idx<RAKU_FH_MAX) raku_fh_table[idx]=NULL;
}

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
        if (strcmp(label_table[i].name, name) == 0)
            return label_table[i].stmt;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * call_stack — RETURN/FRETURN longjmp infrastructure
 * ══════════════════════════════════════════════════════════════════════════ */

#define CALL_STACK_MAX 256

/* SN-3: shadow table for params/locals whose names collide with SPITBOL builtins
 * (e.g. LEN, ANY, SPAN — NV_SET_fn cannot override these in the SPITBOL NV store).
 * Checked in E_VAR and NV_SET before NV_GET_fn/NV_SET_fn. */
#define SHADOW_MAX 32
typedef struct { char name[64]; DESCR_t val; } ShadowEntry;

typedef struct {
    jmp_buf  ret_env;
    char     fname[128];    /* uppercase — also the return-value variable */
    char   **saved_names;
    DESCR_t *saved_vals;
    int      nsaved;
    DESCR_t  retval_cell;  /* return-value capture: bypasses NV keyword collision */
    int      retval_set;   /* 1 if retval_cell was written by body assignment */
    ShadowEntry shadow[SHADOW_MAX]; /* SN-3: per-frame builtin-name shadows */
    int         nshadow;
} CallFrame;

static CallFrame  call_stack[CALL_STACK_MAX];
static int        call_depth = 0;

/* ── IC-5: E_INITIAL persistence — file-scope table keyed on EXPR_t node id ── */
#define ICN_INIT_MAX   64
#define ICN_INIT_SLOTS  8
typedef struct { char nm[64]; DESCR_t val; } IcnInitSlot;
typedef struct { int id; int ns; IcnInitSlot s[ICN_INIT_SLOTS]; } IcnInitEnt;
static IcnInitEnt icn_init_tab[ICN_INIT_MAX];
static int        icn_init_n = 0;

/* Called just before NV restore in call_user_function to update snapshots */
static void icn_init_update_snapshot(char **snames, DESCR_t *svals, int nsaved) {
    /* For each init entry, check if any tracked var appears in snames (locals).
     * If so, capture its current NV value (pre-restore = end-of-call value). */
    for (int ei = 0; ei < icn_init_n; ei++) {
        IcnInitEnt *ent = &icn_init_tab[ei];
        for (int si = 0; si < ent->ns; si++) {
            for (int ni = 0; ni < nsaved; ni++) {
                if (snames[ni] && strcasecmp(snames[ni], ent->s[si].nm) == 0) {
                    ent->s[si].val = NV_GET_fn(ent->s[si].nm);
                    break;
                }
            }
        }
    }
}

/* IC-5: Save current ICN frame's local values back into icn_init_tab snapshots.
 * Called by icn_call_proc just before popping the frame, so initial-block
 * statics (x in "initial x := 10") persist across calls. */
void icn_init_save_frame(void) {
    if (icn_frame_depth <= 0) return;
    IcnFrame *f = &icn_frame_stack[icn_frame_depth - 1];
    for (int ei = 0; ei < icn_init_n; ei++) {
        IcnInitEnt *ent = &icn_init_tab[ei];
        for (int si = 0; si < ent->ns; si++) {
            /* Find this variable's slot in the current frame scope */
            int slot = icn_scope_get(&f->sc, ent->s[si].nm);
            if (slot >= 0 && slot < f->env_n) {
                ent->s[si].val = f->env[slot];
            } else {
                /* Global variable — read from NV */
                ent->s[si].val = NV_GET_fn(ent->s[si].nm);
            }
        }
    }
}


/* SN-3: shadow table helpers — check active frames top-down */
static int shadow_get(const char *name, DESCR_t *out) {
    for (int d = call_depth - 1; d >= 0; d--) {
        CallFrame *fr = &call_stack[d];
        for (int j = 0; j < fr->nshadow; j++)
            if (strcasecmp(fr->shadow[j].name, name) == 0) { *out = fr->shadow[j].val; return 1; }
    }
    return 0;
}
static void shadow_set_cur(const char *name, DESCR_t val) {
    if (call_depth <= 0) return;
    CallFrame *fr = &call_stack[call_depth - 1];
    for (int j = 0; j < fr->nshadow; j++)
        if (strcasecmp(fr->shadow[j].name, name) == 0) { fr->shadow[j].val = val; return; }
    if (fr->nshadow < SHADOW_MAX) {
        strncpy(fr->shadow[fr->nshadow].name, name, 63);
        fr->shadow[fr->nshadow].name[63] = '\0';
        fr->shadow[fr->nshadow].val = val;
        fr->nshadow++;
    }
}
static int shadow_has(const char *name) {
    for (int d = call_depth - 1; d >= 0; d--) {
        CallFrame *fr = &call_stack[d];
        for (int j = 0; j < fr->nshadow; j++)
            if (strcasecmp(fr->shadow[j].name, name) == 0) return 1;
    }
    return 0;
}

/* The program being interpreted (set in main before execute_program) */
Program *g_prog = NULL;
int g_polyglot = 0; /* U-23: 1 when running a fenced polyglot .scrip file */
int g_opt_trace   = 0;  /* --trace:   print STMT N on each statement */
int g_opt_dump_bb = 0;  /* --dump-bb: print PATND tree before each match */

/* IM-3: IR step-limit for in-process sync monitor */
int      g_ir_step_limit = 0;   /* 0 = unlimited; N = stop after N stmts */
int      g_ir_steps_done = 0;
jmp_buf  g_ir_step_jmp;

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
 * names registered via TRACE(var,'VALUE'), never on &-keywords.
 * KW-RETFIX: capture return-value writes into frame->retval_cell to bypass
 * NV keyword collision when user procedure name matches a keyword (e.g. "Trim"). */
static inline void set_and_trace(const char *name, DESCR_t val) {
    /* SN-3: if this name is in the shadow table, update shadow and skip NV_SET_fn
     * (which would be ignored for pattern-primitive names anyway). */
    if (shadow_has(name)) { shadow_set_cur(name, val); goto trace_hook; }
    NV_SET_fn(name, val);
trace_hook:
    if (call_depth > 0) {
        CallFrame *fr = &call_stack[call_depth - 1];
        if (name && fr->fname[0] && strcasecmp(name, fr->fname) == 0) {
            fr->retval_cell = val;
            fr->retval_set  = 1;
        }
    }
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
static ScDatType *sc_dat_register(const char *spec);
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
    fr->nshadow = 0;  /* SN-3: clear shadow table for this frame */
    /* SN-3: register params/locals whose names collide with SPITBOL builtins
     * (e.g. LEN, ANY, SPAN — NV_GET_fn returns the builtin descriptor, ignoring
     * NV_SET_fn writes). Shadow table takes priority in E_VAR lookup. */
    for (int i = 0; i < np; i++)
        if (_is_pat_fnc_name(pnames[i]))
            shadow_set_cur(pnames[i], (i < nargs) ? args[i] : NULVCL);
    for (int i = 0; i < nl; i++)
        if (_is_pat_fnc_name(lnames[i]))
            shadow_set_cur(lnames[i], NULVCL);
    fr->saved_names = snames;
    fr->saved_vals  = svals;
    fr->nsaved      = nsaved;
    fr->retval_cell = STRVAL("");  /* cleared return slot, matches NV_SET clear above */
    fr->retval_set  = 0;

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
                            subj_name = ic->sval;  /* $'name' — literal name, use directly */
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
                        retval = fr->retval_set ? fr->retval_cell : NV_GET_fn(fr->fname);
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
                        retval = fr->retval_set ? fr->retval_cell : NV_GET_fn(fr->fname);
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
        retval = fr->retval_set ? fr->retval_cell : NV_GET_fn(fr->fname);
        strncpy(kw_rtntype, "RETURN",  sizeof(kw_rtntype)-1);
    } else if (ret_kind == 1) {
        retval = fr->retval_set ? fr->retval_cell : NV_GET_fn(fr->fname);
        strncpy(kw_rtntype, "RETURN",  sizeof(kw_rtntype)-1);
    } else {
        retval = FAILDESCR;
        strncpy(kw_rtntype, "FRETURN", sizeof(kw_rtntype)-1);
    }

fn_done:
    comm_return(fname, retval);  /* T-2: FUNCTION trace RETURN event */
    /* ── IC-5: snapshot initial-block locals before they're wiped ── */
    icn_init_update_snapshot(snames, svals, nsaved);
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

/* icn_find_leaf_gen — walk an expr tree and return the first generator-kind node.
 * Used by E_EVERY special-case to find the raw E_TO (or similar) inside
 * compound exprs like E_ADD(E_VAR(total), E_TO(1,n)), so we can drive only
 * the generator and inject via icn_drive_node, letting interp_eval re-read
 * mutable variables (e.g. frame locals) fresh each tick. */
static EXPR_t *icn_find_leaf_gen(EXPR_t *e) {
    if (!e) return NULL;
    switch (e->kind) {
        case E_TO: case E_TO_BY: case E_ITERATE: case E_ALTERNATE:
        case E_SUSPEND: case E_LIMIT: case E_EVERY: case E_BANG_BINARY: case E_SEQ_EXPR:
            return e;
        case E_FNC: return e;   /* user proc or builtin — treat as leaf generator */
        default: break;
    }
    for (int i = 0; i < e->nchildren; i++) {
        EXPR_t *found = icn_find_leaf_gen(e->children[i]);
        if (found) return found;
    }
    return NULL;
}

/* icn_real_str — format a real for Icon output using shortest round-trip representation.
 * Tries precisions 15..17 and picks the shortest that parses back to the same double. */
static const char *icn_real_str(double r, char *buf, int bufsz) {
    for (int p = 15; p <= 17; p++) {
        snprintf(buf, bufsz, "%.*g", p, r);
        char *end; double back = strtod(buf, &end);
        if (back == r) break;   /* shortest precision that round-trips */
    }
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E') && !strchr(buf, 'n') && !strchr(buf, 'N'))
        strncat(buf, ".0", bufsz - strlen(buf) - 1);
    return buf;
}

/* icn_call_builtin — call a builtin E_FNC with pre-resolved args array.
 * Used by icn_bb_fnc_gen to avoid re-evaluating generator children.
 * Dispatches write/writes/upto/find/any/many/upto/tab/move/match by name.
 * For user procs, calls icn_call_proc directly. */
DESCR_t icn_call_builtin(EXPR_t *call, DESCR_t *args, int nargs) {
    if (!call || call->nchildren < 1 || !call->children[0]) return NULVCL;
    const char *fn = call->children[0]->sval;
    if (!fn) return NULVCL;
    DESCR_t a0 = nargs > 0 ? args[0] : NULVCL;
    DESCR_t a1 = nargs > 1 ? args[1] : NULVCL;
    /* write(v) */
    if (!strcmp(fn, "write")) {
        if (IS_FAIL_fn(a0)) return FAILDESCR;
        if (IS_INT_fn(a0))       printf("%lld\n", (long long)a0.i);
        else if (IS_REAL_fn(a0)) { char _rb[64]; printf("%s\n", icn_real_str(a0.r,_rb,sizeof _rb)); }
        else { const char *s = VARVAL_fn(a0); printf("%s\n", s ? s : ""); }
        return a0;
    }
    /* writes(v) */
    if (!strcmp(fn, "writes")) {
        if (IS_INT_fn(a0))       printf("%lld", (long long)a0.i);
        else if (IS_REAL_fn(a0)) { char _rb[64]; printf("%s", icn_real_str(a0.r,_rb,sizeof _rb)); }
        else { const char *s = VARVAL_fn(a0); printf("%s", s ? s : ""); }
        return a0;
    }
    /* User proc — call directly with resolved args */
    for (int i = 0; i < icn_proc_count; i++) {
        if (!strcmp(icn_proc_table[i].name, fn))
            return icn_call_proc(icn_proc_table[i].proc, args, nargs);
    }
    /* Fallback: re-evaluate whole call (args ignored — last resort) */
    return interp_eval(call);
}

/* Forward declaration (also declared above for call_user_function) */

DESCR_t interp_eval(EXPR_t *e)
{
    if (!e) return NULVCL;
    /* icn_drive_node injection: if this exact node is being driven as a generator
     * (set by E_EVERY leaf-gen injection or icn_drive_fnc), return the staged value
     * directly without recursing into children.  Covers E_TO, E_FNC, and any other
     * node kind that icn_find_leaf_gen or icn_drive_fnc selects as the leaf. */
    if (icn_drive_node && e == icn_drive_node) return icn_drive_val;

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
            } else if (lhs && lhs->kind == E_IDX && lhs->nchildren >= 2) {
                /* t["key"] := val  — subscript assignment in Icon frame */
                DESCR_t base = interp_eval(lhs->children[0]);
                if (!IS_FAIL_fn(base)) {
                    DESCR_t idx = interp_eval(lhs->children[1]);
                    if (!IS_FAIL_fn(idx)) subscript_set(base, idx, val);
                }
            } else if (lhs && lhs->kind == E_FIELD && lhs->sval && lhs->nchildren >= 1) {
                /* p.x := val  — record field assignment in Icon frame */
                DESCR_t obj = interp_eval(lhs->children[0]);
                if (!IS_FAIL_fn(obj)) {
                    DESCR_t *cell = data_field_ptr(lhs->sval, obj);
                    if (cell) *cell = val;
                }
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
                else if (IS_REAL_fn(a)) { char _rb[64]; printf("%s\n",icn_real_str(a.r,_rb,sizeof _rb)); }
                else { const char *s=VARVAL_fn(a); printf("%s\n",s?s:""); }
                return a;
            }
            if (!strcmp(fn,"writes")) {
                if (nargs == 0) return NULVCL;
                DESCR_t a = interp_eval(e->children[1]);
                if (IS_INT_fn(a)) printf("%lld",(long long)a.i);
                else if (IS_REAL_fn(a)) { char _rb[64]; printf("%s",icn_real_str(a.r,_rb,sizeof _rb)); }
                else { const char *s=VARVAL_fn(a); printf("%s",s?s:""); }
                return a;
            }
            if (!strcmp(fn,"read") && nargs == 0) {
                /* Icon read() — read one line from stdin, strip trailing newline.
                 * Fails on EOF. */
                char buf[4096];
                if (!fgets(buf, sizeof buf, stdin)) return FAILDESCR;
                size_t len = strlen(buf);
                if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
                if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
                char *r = GC_malloc(len + 1); memcpy(r, buf, len + 1);
                return STRVAL(r);
            }
            if (!strcmp(fn,"reads") && nargs == 1) {
                /* Icon reads(n) — read n bytes from stdin, fail on EOF. */
                DESCR_t nd = interp_eval(e->children[1]);
                int n = (int)to_int(nd);
                if (n <= 0) return FAILDESCR;
                char *buf = GC_malloc(n + 1);
                int got = (int)fread(buf, 1, (size_t)n, stdin);
                if (got <= 0) return FAILDESCR;
                buf[got] = '\0';
                DESCR_t r; r.v = DT_S; r.slen = (uint32_t)got; r.s = buf;
                return r;
            }
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
            /* icn_drive_fnc passthrough: if this E_FNC node is currently being
             * driven by icn_drive_fnc, return the suspended value directly
             * instead of re-calling the procedure (which would recurse). */
            if (e == icn_drive_node) return icn_drive_val;
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
                (!strcmp(fn,"hash_delete") && nargs == 2) ||
                (!strcmp(fn,"hash_keys") && nargs == 1) ||
                (!strcmp(fn,"hash_values") && nargs == 1) ||
                (!strcmp(fn,"hash_pairs") && nargs == 1)) {
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
                if (!strcmp(fn,"hash_pairs")) {
                    if (!*hs) { char *e2=GC_malloc(1); e2[0]='\0'; return STRVAL(e2); }
                    char *out=GC_malloc(strlen(hs)*2+4); out[0]='\0';
                    const char *p=hs;
                    while (*p) {
                        const char *sep=strchr(p,HK); const char *end=strchr(p,HS);
                        if (!sep) break;
                        if (out[0]) { size_t ol=strlen(out); out[ol]='\x01'; out[ol+1]='\0'; }
                        size_t kl=(size_t)(sep-p);
                        const char *vs=sep+1;
                        size_t vl=end?(size_t)(end-vs):strlen(vs);
                        size_t ol=strlen(out);
                        memcpy(out+ol,p,kl); ol+=kl;
                        out[ol++]=':';
                        memcpy(out+ol,vs,vl); ol+=vl;
                        out[ol]='\0';
                        if (!end) break; p=end+1;
                    }
                    return STRVAL(out);
                }
                if (!strcmp(fn,"hash_delete")) {
                    DESCR_t kd = interp_eval(e->children[2]);
                    char kb[64];
                    const char *ks = IS_INT_fn(kd)  ? (snprintf(kb,sizeof kb,"%lld",(long long)kd.i),kb)
                                   : IS_REAL_fn(kd) ? (snprintf(kb,sizeof kb,"%g",kd.r),kb)
                                   : (kd.s&&*kd.s?kd.s:"");
                    size_t kl=strlen(ks);
                    char *out=GC_malloc(strlen(hs)+2); out[0]='\0';
                    const char *p=hs;
                    while (*p) {
                        const char *sep=strchr(p,HK); const char *end=strchr(p,HS);
                        if (!sep) break;
                        size_t pkl=(size_t)(sep-p);
                        if (pkl!=kl || memcmp(p,ks,kl)!=0) {
                            if (out[0]) { size_t ol=strlen(out); out[ol]=HS; out[ol+1]='\0'; }
                            size_t plen=end?(size_t)(end-p):strlen(p);
                            strncat(out,p,plen);
                        }
                        if (!end) break; p=end+1;
                    }
                    if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                        e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                        ICN_CUR.env[e->children[1]->ival] = STRVAL(out);
                    return STRVAL(out);
                }
            }
#undef HS
#undef HK

            /* ── RK-22: Raku string op builtins ────────────────────────────
             * substr($s, $start [, $len])  — 0-based; maps to SNOBOL4 SUBSTR (1-based)
             * index($s, $needle [, $pos])  — 0-based pos of first match, -1 if not found
             * rindex($s, $needle [, $pos]) — 0-based pos of last match, -1 if not found
             * uc($s)   — uppercase via REPLACE(s, &lcase, &ucase)
             * lc($s)   — lowercase via REPLACE(s, &ucase, &lcase)
             * trim($s) — strip leading+trailing whitespace (Raku semantics)
             * chars($s) / length($s) — number of chars                     */
            if (!strcmp(fn,"raku_substr") || (!strcmp(fn,"substr") && nargs >= 2)) {
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t id = interp_eval(e->children[2]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                long slen = (long)strlen(s);
                long start = IS_INT_fn(id) ? id.i : 0;
                if (start < 0) start = slen + start;
                if (start < 0) start = 0;
                if (start > slen) start = slen;
                long len = slen - start;
                if (nargs >= 3) {
                    DESCR_t ld = interp_eval(e->children[3]);
                    len = IS_INT_fn(ld) ? ld.i : len;
                    if (len < 0) len = 0;
                    if (start + len > slen) len = slen - start;
                }
                char *out = GC_malloc((size_t)len + 1);
                memcpy(out, s + start, (size_t)len); out[len] = '\0';
                return STRVAL(out);
            }
            if (!strcmp(fn,"raku_index") || (!strcmp(fn,"index") && nargs >= 2)) {
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t nd = interp_eval(e->children[2]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                const char *needle = VARVAL_fn(nd); if (!needle) needle = "";
                long from = 0;
                if (nargs >= 3) { DESCR_t pd = interp_eval(e->children[3]); from = IS_INT_fn(pd)?pd.i:0; }
                if (from < 0) from = 0;
                if (*needle == '\0') return INTVAL(from);
                const char *found = strstr(s + from, needle);
                return found ? INTVAL((long)(found - s)) : INTVAL(-1);
            }
            if (!strcmp(fn,"raku_rindex") || (!strcmp(fn,"rindex") && nargs >= 2)) {
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t nd = interp_eval(e->children[2]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                const char *needle = VARVAL_fn(nd); if (!needle) needle = "";
                long slen = (long)strlen(s);
                long from = slen;
                if (nargs >= 3) { DESCR_t pd = interp_eval(e->children[3]); from = IS_INT_fn(pd)?pd.i:slen; }
                size_t nlen = strlen(needle);
                if (nlen == 0) return INTVAL(from < slen ? from : slen);
                long best = -1;
                for (long i = 0; i <= from - (long)nlen; i++) {
                    if (memcmp(s + i, needle, nlen) == 0) best = i;
                }
                return INTVAL(best);
            }
            if (!strcmp(fn,"raku_match") && nargs == 2) {
                /* RK-23: $s ~~ /pattern/ — substring search (literal regex subset).
                 * Returns INTVAL(1) on match, FAILDESCR on no match.
                 * If pattern evaluates to DT_P, dispatch through match_pattern. */
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t pd = interp_eval(e->children[2]);
                const char *subj = VARVAL_fn(sd); if (!subj) subj = "";
                if (pd.v == DT_P) {
                    extern int exec_stmt(const char *, DESCR_t *, DESCR_t, DESCR_t *, int);
                    return exec_stmt(NULL, &sd, pd, NULL, 0) ? INTVAL(1) : FAILDESCR;
                }
                const char *pat = VARVAL_fn(pd); if (!pat) pat = "";
                { Raku_nfa *nfa = raku_nfa_build(pat);
                  if (!nfa) return FAILDESCR;
                  raku_nfa_exec(nfa, subj, &g_raku_match);
                  g_raku_subject = subj;
                  raku_nfa_free(nfa);
                  return g_raku_match.matched ? INTVAL(1) : FAILDESCR;
                }
            }
            if (!strcmp(fn,"raku_match_global") && nargs == 2) {
                /* RK-37: $s ~~ m:g/pat/ -- collect all non-overlapping matches */
                /* Returns SOH-delimited list of full-match strings for for-loop */
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t pd = interp_eval(e->children[2]);
                const char *subj = VARVAL_fn(sd); if (!subj) subj = "";
                const char *pat  = VARVAL_fn(pd); if (!pat)  pat  = "";
                Raku_nfa *nfa = raku_nfa_build(pat);
                if (!nfa) return STRVAL(GC_strdup(""));
                int slen = (int)strlen(subj);
                /* collect all matches into a SOH-delimited array string */
                char *out = GC_malloc(slen * 4 + 4); out[0] = '\0';
                int pos = 0, count = 0;
                while (pos <= slen) {
                    Raku_match m;
                    /* build a temporary subject slice via exec on offset */
                    raku_nfa_exec(nfa, subj + pos, &m);
                    if (!m.matched) break;
                    int mlen = m.full_end - m.full_start;
                    if (count > 0) { int ol=strlen(out); out[ol]='\x01'; out[ol+1]='\0'; }
                    strncat(out, subj + pos + m.full_start, (size_t)mlen);
                    /* also update g_raku_match for last match captures */
                    g_raku_match = m;
                    g_raku_match.full_start += pos;
                    g_raku_match.full_end   += pos;
                    for (int g=0;g<m.ngroups;g++) {
                        if (m.group_start[g]>=0) g_raku_match.group_start[g]+=pos;
                        if (m.group_end[g]>=0)   g_raku_match.group_end[g]+=pos;
                    }
                    g_raku_subject = subj;
                    pos += m.full_start + (mlen > 0 ? mlen : 1);
                    count++;
                }
                raku_nfa_free(nfa);
                return count > 0 ? STRVAL(out) : FAILDESCR;
            }
            if (!strcmp(fn,"raku_subst") && nargs == 2) {
                /* RK-37: $s ~~ s/pat/repl/[g] -- substitution */
                /* tok format: "pat\x01repl\x01flag" where flag=g or - */
                DESCR_t sd = interp_eval(e->children[1]);
                DESCR_t td = interp_eval(e->children[2]);
                const char *subj = VARVAL_fn(sd); if (!subj) subj = "";
                const char *tok  = VARVAL_fn(td); if (!tok)  tok  = "";
                /* split tok on \x01 */
                const char *sep1 = strchr(tok, '\x01');
                if (!sep1) return sd;
                const char *sep2 = strchr(sep1+1, '\x01');
                if (!sep2) return sd;
                int plen = (int)(sep1-tok);
                int rlen = (int)(sep2-(sep1+1));
                char *pat  = GC_malloc(plen+1); memcpy(pat, tok, plen); pat[plen]='\0';
                char *repl = GC_malloc(rlen+1); memcpy(repl, sep1+1, rlen); repl[rlen]='\0';
                int global = (*(sep2+1)=='g');
                Raku_nfa *nfa = raku_nfa_build(pat);
                if (!nfa) return sd;
                int slen=(int)strlen(subj);
                char *res = GC_malloc(slen*4+rlen*8+4); res[0]='\0';
                int pos=0, did_one=0;
                while (pos<=slen) {
                    Raku_match m; raku_nfa_exec(nfa, subj+pos, &m);
                    if (!m.matched) { strncat(res, subj+pos, (size_t)(slen-pos)); break; }
                    /* copy pre-match */
                    strncat(res, subj+pos, (size_t)m.full_start);
                    /* copy replacement (TODO: $0/$<n> expansion in repl) */
                    strcat(res, repl);
                    g_raku_match=m; g_raku_subject=subj;
                    int advance=m.full_start+(m.full_end-m.full_start>0?m.full_end-m.full_start:1);
                    pos+=advance; did_one=1;
                    if (!global) { strncat(res, subj+pos, (size_t)(slen-pos)); break; }
                }
                raku_nfa_free(nfa);
                /* update the subject variable in the frame if it was a VAR */
                if (e->children[1]->kind==E_VAR && e->children[1]->ival>=0 &&
                    e->children[1]->ival<ICN_CUR.env_n && icn_frame_depth>0)
                    ICN_CUR.env[e->children[1]->ival] = STRVAL(res);
                return did_one ? STRVAL(res) : sd;
            }
            /* RK-38: file I/O builtins */
            if (!strcmp(fn,"open") && (nargs==1||nargs==2)) {
                DESCR_t pd=interp_eval(e->children[1]);
                const char *path=VARVAL_fn(pd); if(!path||!*path) return FAILDESCR;
                const char *mode="r";
                if(nargs==2){
                    DESCR_t md=interp_eval(e->children[2]);
                    const char *ms=VARVAL_fn(md); if(!ms) ms="";
                    if(strstr(ms,":w")||strstr(ms,"w")) mode="w";
                    else if(strstr(ms,":a")||strstr(ms,"a")) mode="a";
                }
                FILE *fp=fopen(path,mode);
                if(!fp) return FAILDESCR;
                int idx=raku_fh_alloc(fp);
                if(idx<0){fclose(fp);return FAILDESCR;}
                return INTVAL(idx);
            }
            if (!strcmp(fn,"close") && nargs==1) {
                DESCR_t fd=interp_eval(e->children[1]);
                int idx=(int)(IS_INT_fn(fd)?fd.i:0);
                FILE *fp=raku_fh_get(idx);
                if(fp){fclose(fp);raku_fh_free(idx);}
                return INTVAL(0);
            }
            if (!strcmp(fn,"slurp") && nargs==1) {
                DESCR_t ad=interp_eval(e->children[1]);
                FILE *fp=NULL; int need_close=0;
                if(IS_INT_fn(ad)) {
                    fp=raku_fh_get((int)ad.i);
                } else {
                    const char *path=VARVAL_fn(ad); if(!path||!*path) return STRVAL(GC_strdup(""));
                    fp=fopen(path,"r"); need_close=1;
                }
                if(!fp) return STRVAL(GC_strdup(""));
                fseek(fp,0,SEEK_END); long sz=ftell(fp); rewind(fp);
                char *buf=GC_malloc(sz+1);
                size_t nr=fread(buf,1,(size_t)sz,fp); buf[nr]='\0';
                if(need_close) fclose(fp);
                return STRVAL(buf);
            }
            if (!strcmp(fn,"lines") && nargs==1) {
                /* lines(fh|path) -> SOH-delimited line list for for-loop */
                DESCR_t ad=interp_eval(e->children[1]);
                FILE *fp=NULL; int need_close=0;
                if(IS_INT_fn(ad)) {
                    fp=raku_fh_get((int)ad.i);
                } else {
                    const char *path=VARVAL_fn(ad); if(!path||!*path) return STRVAL(GC_strdup(""));
                    fp=fopen(path,"r"); need_close=1;
                }
                if(!fp) return STRVAL(GC_strdup(""));
                char *out=GC_malloc(65536); out[0]='\0'; size_t cap=65536, used=0;
                char line[4096]; int first=1;
                while(fgets(line,sizeof line,fp)){
                    size_t ll=strlen(line);
                    while(ll>0&&(line[ll-1]=='\n'||line[ll-1]=='\r')) line[--ll]='\0';
                    size_t need=used+ll+2;
                    if(need>cap){cap=need*2;char*nb=GC_malloc(cap);memcpy(nb,out,used);out=nb;}
                    if(!first){out[used++]='\x01';}
                    memcpy(out+used,line,ll); used+=ll; out[used]='\0'; first=0;
                }
                if(need_close) fclose(fp);
                return STRVAL(out);
            }
            if ((!strcmp(fn,"raku_print_fh")||!strcmp(fn,"raku_say_fh")) && nargs==2) {
                /* RK-39: print/say to file handle */
                DESCR_t fd=interp_eval(e->children[1]);
                DESCR_t vd=interp_eval(e->children[2]);
                int idx=(int)(IS_INT_fn(fd)?fd.i:1);
                FILE *fp=raku_fh_get(idx); if(!fp) fp=stdout;
                const char *s=VARVAL_fn(vd); if(!s) s="";
                fputs(s,fp);
                if(!strcmp(fn,"raku_say_fh")) fputc('\n',fp);
                return INTVAL(0);
            }
            if (!strcmp(fn,"spurt") && nargs==2) {
                /* RK-56: spurt(path, content) -- write string to file */
                DESCR_t pd=interp_eval(e->children[1]);
                DESCR_t cd=interp_eval(e->children[2]);
                const char *path=VARVAL_fn(pd); if(!path||!*path) return FAILDESCR;
                const char *content=VARVAL_fn(cd); if(!content) content="";
                FILE *fp=fopen(path,"w"); if(!fp) return FAILDESCR;
                fputs(content,fp); fclose(fp);
                return INTVAL(0);
            }
            if (!strcmp(fn,"raku_nfa_compile") && nargs == 1) {
                /* RK-32: compile pattern string -> NFA, print state count, return 0 */
                DESCR_t pd = interp_eval(e->children[1]);
                const char *pat = VARVAL_fn(pd); if (!pat) pat = "";
                { Raku_nfa *nfa = raku_nfa_build(pat);
                  if (!nfa) { printf("NFA:%s:ERROR\n", pat); return INTVAL(0); }
                  printf("NFA:%s:states=%d\n", pat, raku_nfa_state_count(nfa));
                  raku_nfa_free(nfa);
                }
                return INTVAL(0);
            }
            if (!strcmp(fn,"raku_named_capture") && nargs == 1) {
                /* RK-35: $<n> named capture from last ~~ match */
                DESCR_t nd = interp_eval(e->children[1]);
                const char *name = VARVAL_fn(nd); if (!name) name = "";
                if (!g_raku_match.matched) return STRVAL(GC_strdup(""));
                int g = -1;
                for (int i=0;i<g_raku_match.ngroups;i++)
                    if (strcmp(g_raku_match.group_name[i],name)==0){g=i;break;}
                if (g<0||g_raku_match.group_start[g]<0) return STRVAL(GC_strdup(""));
                int gs=g_raku_match.group_start[g], ge=g_raku_match.group_end[g];
                if (ge<gs) return STRVAL(GC_strdup(""));
                int len=ge-gs; char *out=GC_malloc(len+1);
                memcpy(out,g_raku_subject+gs,(size_t)len); out[len]='\0';
                return STRVAL(out);
            }
            if (!strcmp(fn,"raku_capture") && nargs == 1) {
                /* RK-34: $N positional capture from last ~~ match */
                DESCR_t nd = interp_eval(e->children[1]);
                int n = (int)(IS_INT_fn(nd) ? nd.i : 0);
                if (!g_raku_match.matched || n < 0 || n >= g_raku_match.ngroups
                    || g_raku_match.group_start[n] < 0) return STRVAL(GC_strdup(""));
                int gs = g_raku_match.group_start[n];
                int ge = g_raku_match.group_end[n];
                if (ge < gs) return STRVAL(GC_strdup(""));
                int len = ge - gs;
                char *out = GC_malloc(len + 1);
                memcpy(out, g_raku_subject + gs, (size_t)len);
                out[len] = '\0';
                return STRVAL(out);
            }
            if (!strcmp(fn,"raku_uc") || (!strcmp(fn,"uc") && nargs == 1)) {
                DESCR_t sd = interp_eval(e->children[1]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                char *out = GC_strdup(s);
                for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
                return STRVAL(out);
            }
            if (!strcmp(fn,"raku_lc") || (!strcmp(fn,"lc") && nargs == 1)) {
                DESCR_t sd = interp_eval(e->children[1]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                char *out = GC_strdup(s);
                for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
                return STRVAL(out);
            }
            if (!strcmp(fn,"raku_trim")) {
                DESCR_t sd = interp_eval(e->children[1]);
                const char *s = VARVAL_fn(sd); if (!s) s = "";
                while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
                size_t len = strlen(s);
                while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r')) len--;
                char *out = GC_malloc(len + 1); memcpy(out, s, len); out[len] = '\0';
                return STRVAL(out);
            }
            if (!strcmp(fn,"chars") || !strcmp(fn,"length")) {
                if (nargs == 1) {
                    DESCR_t sd = interp_eval(e->children[1]);
                    const char *s = VARVAL_fn(sd); if (!s) s = "";
                    return INTVAL((long)strlen(s));
                }
            }

            /* ── RK-25: Raku try/CATCH/die exception handling ───────────────
             * raku_die(msg)         — store msg in g_raku_exception, return FAILDESCR
             * raku_try(body)        — eval body; if FAIL, clear exception, return NULVCL
             * raku_try(body, catch) — eval body; if FAIL, eval catch block, return result */
            if (!strcmp(fn,"raku_die") && nargs >= 1) {
                DESCR_t md = interp_eval(e->children[1]);
                const char *msg = VARVAL_fn(md); if (!msg) msg = "Died";
                extern char g_raku_exception[512];
                snprintf(g_raku_exception, sizeof g_raku_exception, "%s", msg);
                return FAILDESCR;
            }
            if (!strcmp(fn,"raku_try") && (nargs == 1 || nargs == 2)) {
                extern char g_raku_exception[512];
                g_raku_exception[0] = '\0';
                DESCR_t r = interp_eval(e->children[1]);   /* try body */
                int body_failed = IS_FAIL_fn(r);
                int real_die    = (g_raku_exception[0] != '\0'); /* only raku_die sets this */
                if (!body_failed) { g_raku_exception[0]='\0'; return r; } /* success */
                /* body failed */
                if (nargs == 2 && real_die) {
                    /* CATCH block: only fires on explicit die, not on fall-off-end */
                    EXPR_t *catch_blk = e->children[2];
                    int _sl2 = -1;
                    EXPR_t *_stk2[64]; int _sn2=0; _stk2[_sn2++]=catch_blk;
                    while (_sn2>0 && _sl2<0) {
                        EXPR_t *_n=_stk2[--_sn2]; if(!_n) continue;
                        if(_n->kind==E_VAR && _n->sval &&
                           (strcmp(_n->sval,"$!")==0||strcmp(_n->sval,"!")==0))
                            _sl2=(int)_n->ival;
                        for(int _ci=0;_ci<_n->nchildren&&_sn2<62;_ci++) _stk2[_sn2++]=_n->children[_ci];
                    }
                    DESCR_t exc_d = STRVAL(GC_strdup(g_raku_exception));
                    if (_sl2 >= 0 && _sl2 < ICN_CUR.env_n) ICN_CUR.env[_sl2] = exc_d;
                    else NV_SET_fn("$!", exc_d);
                    g_raku_exception[0] = '\0';
                    return interp_eval(catch_blk);
                }
                g_raku_exception[0] = '\0';
                return NULVCL;   /* swallow failure (no CATCH, or non-die failure) */
            }

            /* ── RK-24: Raku map/grep/sort higher-order list ops ────────────
             * raku_map(block, @arr)  — apply block to each elem, collect results
             * raku_grep(block, @arr) — collect elems where block is truthy
             * raku_sort(@arr)        — lexicographic sort
             * raku_sort(block, @arr) — sort with comparator (block uses $a/$b)
             *
             * Arrays are SOH (\x01) delimited strings.
             * Block is an E_EXPR subtree (child[1]); array is child[2] (or child[1] for sort).
             * $_ is bound into env slot via icn_scope_set for each iteration.  */

            /* helper: split SOH string → char** of elems (GC-allocated) */
#define SOH '\x01'

            if (!strcmp(fn,"raku_map") && nargs == 2) {
                EXPR_t *blk = e->children[1];          /* block EXPR_t* */
                DESCR_t arrd = interp_eval(e->children[2]);
                const char *as = VARVAL_fn(arrd); if (!as) as = "";
                /* iterate elems, eval block with $_ bound, collect */
                char *out = GC_strdup("");
                const char *seg = as;
                int first = 1;
                do {
                    const char *nx = strchr(seg, SOH);
                    size_t elen = nx ? (size_t)(nx - seg) : strlen(seg);
                    char *elem = GC_malloc(elen + 1);
                    memcpy(elem, seg, elen); elem[elen] = '\0';
                    /* bind $_ — walk closure tree; $_ has sval="$_" or "_", use ival as slot */
                    { /* elem: INTVAL if numeric, else STRVAL */
                      char *_ep_ev; long _iv_ev = strtol(elem, &_ep_ev, 10);
                      DESCR_t _ev = (*_ep_ev == '\0' && _ep_ev > elem) ? INTVAL(_iv_ev) : STRVAL(elem);
                      int _sl = -1;
                      EXPR_t *_stk[64]; int _sn=0; _stk[_sn++]=blk;
                      while (_sn>0 && _sl<0) {
                          EXPR_t *_n=_stk[--_sn];
                          if (!_n) continue;
                          if (_n->kind==E_VAR && _n->sval) {
                              const char *_sv = _n->sval;
                              /* match "$_" or "_" (sigil may be stripped) */
                              if (strcmp(_sv,"$_")==0 || strcmp(_sv,"_")==0)
                                  _sl=(int)_n->ival;
                          }
                          for(int _ci=0;_ci<_n->nchildren&&_sn<62;_ci++) _stk[_sn++]=_n->children[_ci];
                      }
                      if (_sl >= 0 && _sl < ICN_CUR.env_n) ICN_CUR.env[_sl] = _ev;
                      else NV_SET_fn("$_", _ev); }
                    DESCR_t r = interp_eval(blk);
                    if (!IS_FAIL_fn(r)) {
                        const char *rv; char rb[64];
                        if (IS_INT_fn(r))       { snprintf(rb,sizeof rb,"%lld",(long long)r.i); rv=rb; }
                        else if (IS_REAL_fn(r)) { snprintf(rb,sizeof rb,"%g",r.r); rv=rb; }
                        else                    { rv = VARVAL_fn(r); if (!rv) rv = ""; }
                        size_t ol = strlen(out), rl = strlen(rv);
                        char *nout = GC_malloc(ol + rl + 2);
                        memcpy(nout, out, ol);
                        if (!first) { nout[ol] = SOH; memcpy(nout+ol+1, rv, rl); nout[ol+1+rl]='\0'; }
                        else        { memcpy(nout+ol, rv, rl); nout[ol+rl]='\0'; first=0; }
                        out = nout;
                    }
                    seg = nx ? nx + 1 : NULL;
                } while (seg);
                return STRVAL(out);
            }

            if (!strcmp(fn,"raku_grep") && nargs == 2) {
                EXPR_t *blk = e->children[1];
                DESCR_t arrd = interp_eval(e->children[2]);
                const char *as = VARVAL_fn(arrd); if (!as) as = "";
                char *out = GC_strdup("");
                const char *seg = as;
                int first = 1;
                do {
                    const char *nx = strchr(seg, SOH);
                    size_t elen = nx ? (size_t)(nx - seg) : strlen(seg);
                    char *elem = GC_malloc(elen + 1);
                    memcpy(elem, seg, elen); elem[elen] = '\0';
                    /* bind $_ — walk closure tree; $_ has sval="$_" or "_", use ival as slot */
                    { /* elem: INTVAL if numeric, else STRVAL */
                      char *_ep_ev; long _iv_ev = strtol(elem, &_ep_ev, 10);
                      DESCR_t _ev = (*_ep_ev == '\0' && _ep_ev > elem) ? INTVAL(_iv_ev) : STRVAL(elem);
                      int _sl = -1;
                      EXPR_t *_stk[64]; int _sn=0; _stk[_sn++]=blk;
                      while (_sn>0 && _sl<0) {
                          EXPR_t *_n=_stk[--_sn];
                          if (!_n) continue;
                          if (_n->kind==E_VAR && _n->sval) {
                              const char *_sv = _n->sval;
                              /* match "$_" or "_" (sigil may be stripped) */
                              if (strcmp(_sv,"$_")==0 || strcmp(_sv,"_")==0)
                                  _sl=(int)_n->ival;
                          }
                          for(int _ci=0;_ci<_n->nchildren&&_sn<62;_ci++) _stk[_sn++]=_n->children[_ci];
                      }
                      if (_sl >= 0 && _sl < ICN_CUR.env_n) ICN_CUR.env[_sl] = _ev;
                      else NV_SET_fn("$_", _ev); }
                    DESCR_t r = interp_eval(blk);
                    /* RK-24: grep truthy = block did not fail (SNOBOL4 success/fail semantics).
                     * E_EQ/E_LT etc return FAILDESCR on false, non-fail on true. */
                    int truthy = !IS_FAIL_fn(r);
                    if (truthy) {
                        size_t ol = strlen(out), el = strlen(elem);
                        char *nout = GC_malloc(ol + el + 2);
                        memcpy(nout, out, ol);
                        if (!first) { nout[ol] = SOH; memcpy(nout+ol+1,elem,el); nout[ol+1+el]='\0'; }
                        else        { memcpy(nout+ol,elem,el); nout[ol+el]='\0'; first=0; }
                        out = nout;
                    }
                    seg = nx ? nx + 1 : NULL;
                } while (seg);
                return STRVAL(out);
            }

            if (!strcmp(fn,"raku_sort") && (nargs == 1 || nargs == 2)) {
                /* Simple lexicographic sort; numeric if all-integer elements.
                 * With block (nargs==2): use $a/$b comparator block. */
                DESCR_t arrd = interp_eval(e->children[nargs == 2 ? 2 : 1]);
                const char *as = VARVAL_fn(arrd); if (!as || !*as) return STRVAL(GC_strdup(""));
                EXPR_t *blk = (nargs == 2) ? e->children[1] : NULL;
                /* split into array of strings */
                int cnt = 1; for (const char *p=as;*p;p++) if(*p==SOH) cnt++;
                char **elems = GC_malloc((size_t)cnt * sizeof(char*));
                int idx = 0; const char *seg = as;
                do {
                    const char *nx = strchr(seg, SOH);
                    size_t elen = nx ? (size_t)(nx-seg) : strlen(seg);
                    char *elem = GC_malloc(elen+1); memcpy(elem,seg,elen); elem[elen]='\0';
                    elems[idx++] = elem;
                    seg = nx ? nx+1 : NULL;
                } while (seg && idx < cnt);
                /* sort: comparator block or default lexicographic */
                if (blk) {
                    /* insertion sort using block with $a/$b */
                    for (int i=1;i<cnt;i++) {
                        char *key = elems[i]; int j=i-1;
                        while (j>=0) {
                            NV_SET_fn("$a", STRVAL(elems[j]));
                            NV_SET_fn("$b", STRVAL(key));
                            DESCR_t r = interp_eval(blk);
                            long cmp = IS_INT_fn(r) ? r.i : 0;
                            if (cmp <= 0) break;
                            elems[j+1]=elems[j]; j--;
                        }
                        elems[j+1]=key;
                    }
                } else {
                    /* check if all-integer */
                    int all_int = 1;
                    for (int i=0;i<cnt&&all_int;i++) {
                        char *ep; strtol(elems[i],&ep,10);
                        if (*ep) all_int=0;
                    }
                    /* qsort */
                    if (all_int) {
                        /* numeric sort via simple insertion */
                        for (int i=1;i<cnt;i++) {
                            char *key=elems[i]; long kv=atol(key); int j=i-1;
                            while (j>=0 && atol(elems[j])>kv) { elems[j+1]=elems[j]; j--; }
                            elems[j+1]=key;
                        }
                    } else {
                        for (int i=1;i<cnt;i++) {
                            char *key=elems[i]; int j=i-1;
                            while (j>=0 && strcmp(elems[j],key)>0) { elems[j+1]=elems[j]; j--; }
                            elems[j+1]=key;
                        }
                    }
                }
                /* rejoin */
                size_t total=0; for(int i=0;i<cnt;i++) total+=strlen(elems[i])+1;
                char *out=GC_malloc(total+1); out[0]='\0';
                for (int i=0;i<cnt;i++) {
                    if (i) { size_t ol=strlen(out); out[ol]=SOH; out[ol+1]='\0'; }
                    strcat(out,elems[i]);
                }
                return STRVAL(out);
            }
#undef SOH

            /* ── RK-26: Raku OO builtins ────────────────────────────────────
             * raku_new(classname, key1, val1, key2, val2, ...)
             *   → find registered ScDatType by name, construct instance,
             *     assign named args to matching fields.
             * raku_mcall(obj, methname, arg1, arg2, ...)
             *   → look up obj's datatype name, find "TypeName__methname" proc
             *     in icn_proc_table, call it with (obj, arg1, arg2, ...).
             * ──────────────────────────────────────────────────────────────*/
            if (!strcmp(fn,"raku_new")) {
                /* children: [fn_name_var, classname_qlit, key1, val1, ...] */
                /* e->children[0] = E_VAR("raku_new") (make_call layout)
                 * e->children[1] = E_QLIT(classname)
                 * e->children[2..] = alternating key, val */
                if (e->nchildren < 2) return NULVCL;
                DESCR_t cnameD = interp_eval(e->children[1]);
                const char *cname = VARVAL_fn(cnameD);
                if (!cname || !*cname) return FAILDESCR;
                ScDatType *t = sc_dat_find_type(cname);
                if (!t) return FAILDESCR;
                /* Build field array in order matching type definition */
                DESCR_t fvals[64];
                for (int i=0;i<t->nfields && i<64;i++) fvals[i]=NULVCL;
                /* Walk named pairs: children[2],children[3] = key,val ... */
                for (int ci=2; ci+1 < e->nchildren; ci+=2) {
                    DESCR_t kD = interp_eval(e->children[ci]);
                    DESCR_t vD = interp_eval(e->children[ci+1]);
                    const char *kname = VARVAL_fn(kD);
                    if (!kname) continue;
                    for (int fi=0;fi<t->nfields;fi++) {
                        if (strcasecmp(t->fields[fi], kname)==0) { fvals[fi]=vD; break; }
                    }
                }
                return sc_dat_construct(t, fvals, t->nfields);
            }

            if (!strcmp(fn,"raku_mcall")) {
                /* children: [fn_var, obj, methname_qlit, arg1, arg2, ...] */
                if (e->nchildren < 3) return FAILDESCR;
                DESCR_t obj    = interp_eval(e->children[1]);
                DESCR_t mnameD = interp_eval(e->children[2]);
                const char *mname = VARVAL_fn(mnameD);
                if (!mname || !*mname) return FAILDESCR;
                /* Determine class name from obj's DT_DATA type */
                const char *cname = NULL;
                if (obj.v == DT_DATA && obj.u) {
                    DATINST_t *inst = (DATINST_t *)obj.u;
                    if (inst->type) cname = inst->type->name;
                }
                if (!cname) return FAILDESCR;
                /* Build proc name: "ClassName__methname" */
                char procname[256];
                snprintf(procname, sizeof procname, "%s__%s", cname, mname);
                /* Find in icn_proc_table */
                int pi;
                for (pi = 0; pi < icn_proc_count; pi++)
                    if (strcmp(icn_proc_table[pi].name, procname) == 0) break;
                if (pi >= icn_proc_count) return FAILDESCR;
                /* Build arg array: self=obj, then extra args */
                int nextra = e->nchildren - 3;
                int total  = 1 + nextra;
                DESCR_t *callargs = GC_malloc((size_t)total * sizeof(DESCR_t));
                callargs[0] = obj;
                for (int i=0;i<nextra;i++) callargs[i+1] = interp_eval(e->children[3+i]);
                return icn_call_proc(icn_proc_table[pi].proc, callargs, total);
            }

            /* ── IC-3: Icon table builtins (DT_T native hash table) ────────
             * table()         → new empty table (default value = &null)
             * insert(T,k,v)   → set T[k]=v, return T
             * delete(T,k)     → remove T[k], return T
             * member(T,k)     → return T[k] if present, else fail
             * key(T)          → generator: yields each key (via every)     */
            if (!strcmp(fn,"table") && nargs <= 2) {
                /* Icon: table()      → empty table, default=&null
                 *       table(x)     → empty table, default=x
                 *       table(n,inc) → SNOBOL4 compat (ignored for Icon) */
                TBBLK_t *tbl = table_new();
                if (nargs == 1) {
                    /* Icon table(dflt) — one arg is the default value */
                    tbl->dflt = interp_eval(e->children[1]);
                } else {
                    tbl->dflt = NULVCL;
                }
                DESCR_t d; d.v = DT_T; d.slen = 0; d.tbl = tbl;
                return d;
            }
            if (!strcmp(fn,"insert") && nargs >= 2) {
                DESCR_t td = interp_eval(e->children[1]);
                if (td.v != DT_T) return FAILDESCR;
                DESCR_t kd = interp_eval(e->children[2]);
                DESCR_t vd = (nargs >= 3) ? interp_eval(e->children[3]) : NULVCL;
                char kb[64]; const char *ks;
                if (IS_INT_fn(kd))       { snprintf(kb,sizeof kb,"%lld",(long long)kd.i); ks=kb; }
                else if (IS_REAL_fn(kd)) { snprintf(kb,sizeof kb,"%g",kd.r); ks=kb; }
                else                     { ks = VARVAL_fn(kd); if (!ks) ks=""; }
                table_set_descr(td.tbl, ks, kd, vd);
                return td;
            }
            if (!strcmp(fn,"delete") && nargs == 2) {
                DESCR_t td = interp_eval(e->children[1]);
                if (td.v != DT_T) return FAILDESCR;
                DESCR_t kd = interp_eval(e->children[2]);
                char kb[64]; const char *ks;
                if (IS_INT_fn(kd))       { snprintf(kb,sizeof kb,"%lld",(long long)kd.i); ks=kb; }
                else if (IS_REAL_fn(kd)) { snprintf(kb,sizeof kb,"%g",kd.r); ks=kb; }
                else                     { ks = VARVAL_fn(kd); if (!ks) ks=""; }
                /* walk bucket using same djb2 hash as _tbl_hash in binary:
                 * init=0x1505, hash = hash*33 ^ c, result & 0xFF */
                unsigned h = 0x1505;
                { const char *p=ks; while(*p) { h=(h<<5)+h^(unsigned char)*p++; } h&=0xFF; }
                TBPAIR_t **pp = &td.tbl->buckets[h];
                while (*pp) {
                    if (strcmp((*pp)->key, ks)==0) { TBPAIR_t *del=*pp; *pp=del->next; td.tbl->size--; break; }
                    pp = &(*pp)->next;
                }
                return td;
            }
            if (!strcmp(fn,"member") && nargs == 2) {
                DESCR_t td = interp_eval(e->children[1]);
                if (td.v != DT_T) return FAILDESCR;
                DESCR_t kd = interp_eval(e->children[2]);
                char kb[64]; const char *ks;
                if (IS_INT_fn(kd))       { snprintf(kb,sizeof kb,"%lld",(long long)kd.i); ks=kb; }
                else if (IS_REAL_fn(kd)) { snprintf(kb,sizeof kb,"%g",kd.r); ks=kb; }
                else                     { ks = VARVAL_fn(kd); if (!ks) ks=""; }
                if (!table_has(td.tbl, ks)) return FAILDESCR;
                return table_get(td.tbl, ks);
            }

            /* ── IC-5: key(T) — generator yielding each key of a table ───── */
            if (!strcmp(fn,"key") && nargs == 1) {
                DESCR_t td = interp_eval(e->children[1]);
                if (td.v != DT_T || !td.tbl) return FAILDESCR;
                /* oneshot: return first key; every uses icn_bb_tbl_iterate for keys */
                for (int _bi = 0; _bi < TABLE_BUCKETS; _bi++)
                    if (td.tbl->buckets[_bi])
                        return td.tbl->buckets[_bi]->key_descr;
                return FAILDESCR;
            }

            /* ── IC-5: integer(x), real(x), string(x), numeric(x) ──────────*/
            if (!strcmp(fn,"integer") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                if (IS_INT_fn(av)) return av;
                if (IS_REAL_fn(av)) return INTVAL((long long)av.r);
                const char *s = VARVAL_fn(av); if (!s) return FAILDESCR;
                char *end; long long iv = strtoll(s, &end, 10);
                if (end != s && (*end=='\0'||*end==' ')) return INTVAL(iv);
                /* try real→int */
                double rv = strtod(s, &end);
                if (end != s && (*end=='\0'||*end==' ')) return INTVAL((long long)rv);
                return FAILDESCR;
            }
            if (!strcmp(fn,"real") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                if (IS_REAL_fn(av)) return av;
                if (IS_INT_fn(av)) return REALVAL((double)av.i);
                const char *s = VARVAL_fn(av); if (!s) return FAILDESCR;
                char *end; double rv = strtod(s, &end);
                if (end != s && (*end=='\0'||*end==' ')) return REALVAL(rv);
                return FAILDESCR;
            }
            if (!strcmp(fn,"string") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                if (IS_STR_fn(av)) return av;
                char *buf = GC_malloc(64);
                if (IS_INT_fn(av))       snprintf(buf,64,"%lld",(long long)av.i);
                else if (IS_REAL_fn(av)) { icn_real_str(av.r,buf,64); }
                else return NULVCL;
                return STRVAL(buf);
            }
            if (!strcmp(fn,"numeric") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                if (IS_INT_fn(av)||IS_REAL_fn(av)) return av;
                const char *s = VARVAL_fn(av); if (!s||!*s) return FAILDESCR;
                char *end; long long iv = strtoll(s, &end, 10);
                if (end!=s && (*end=='\0'||*end==' ')) return INTVAL(iv);
                double rv = strtod(s, &end);
                if (end!=s && (*end=='\0'||*end==' ')) return REALVAL(rv);
                return FAILDESCR;
            }

            /* ── IC-5: Icon list builtins: push/pull/put/get ───────────────
             * Icon lists stored as DT_DATA with type name "icnlist" and a
             * DT_A array field "elems".  We use a simple GC array of DESCR_t. */
            if ((!strcmp(fn,"push")||!strcmp(fn,"put")||!strcmp(fn,"get")||!strcmp(fn,"pull")) && nargs >= 1) {
                DESCR_t ld = interp_eval(e->children[1]);
                /* push(L, v) — prepend */
                if (!strcmp(fn,"push") && nargs == 2) {
                    DESCR_t vd = interp_eval(e->children[2]);
                    if (ld.v != DT_DATA) return FAILDESCR;
                    int n = (int)FIELD_GET_fn(ld,"icn_size").i;
                    DESCR_t ea = FIELD_GET_fn(ld,"icn_elems");
                    DESCR_t *old = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
                    DESCR_t *nb = GC_malloc((n+1)*sizeof(DESCR_t));
                    nb[0] = vd;
                    if (old && n > 0) memcpy(nb+1,old,n*sizeof(DESCR_t));
                    FIELD_SET_fn(ld,"icn_elems",(DESCR_t){.v=DT_DATA,.ptr=nb});
                    FIELD_SET_fn(ld,"icn_size",INTVAL(n+1));
                    return ld;
                }
                /* put(L, v) — append */
                if (!strcmp(fn,"put") && nargs == 2) {
                    DESCR_t vd = interp_eval(e->children[2]);
                    if (ld.v != DT_DATA) return FAILDESCR;
                    int n = (int)FIELD_GET_fn(ld,"icn_size").i;
                    DESCR_t ea = FIELD_GET_fn(ld,"icn_elems");
                    DESCR_t *old = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
                    DESCR_t *nb = GC_malloc((n+1)*sizeof(DESCR_t));
                    if (old && n > 0) memcpy(nb,old,n*sizeof(DESCR_t));
                    nb[n] = vd;
                    FIELD_SET_fn(ld,"icn_elems",(DESCR_t){.v=DT_DATA,.ptr=nb});
                    FIELD_SET_fn(ld,"icn_size",INTVAL(n+1));
                    return ld;
                }
                /* get(L) — remove and return first element */
                if (!strcmp(fn,"get") && nargs == 1) {
                    if (ld.v != DT_DATA) return FAILDESCR;
                    DESCR_t ea = FIELD_GET_fn(ld,"icn_elems");
                    int n = (int)FIELD_GET_fn(ld,"icn_size").i;
                    DESCR_t *arr = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
                    if (!arr || n <= 0) return FAILDESCR;
                    DESCR_t ret = arr[0];
                    FIELD_SET_fn(ld,"icn_elems",(DESCR_t){.v=DT_DATA,.ptr=arr+1});
                    FIELD_SET_fn(ld,"icn_size",INTVAL(n-1));
                    /* write back to var if possible */
                    if (e->children[1]->kind==E_VAR) {
                        int sl=(int)e->children[1]->ival;
                        if(sl>=0&&sl<ICN_CUR.env_n) ICN_CUR.env[sl]=ld;
                    }
                    return ret;
                }
                /* pull(L) — remove and return last element */
                if (!strcmp(fn,"pull") && nargs == 1) {
                    if (ld.v != DT_DATA) return FAILDESCR;
                    DESCR_t ea = FIELD_GET_fn(ld,"icn_elems");
                    int n = (int)FIELD_GET_fn(ld,"icn_size").i;
                    DESCR_t *arr = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
                    if (!arr || n <= 0) return FAILDESCR;
                    DESCR_t ret = arr[n-1];
                    FIELD_SET_fn(ld,"icn_size",INTVAL(n-1));
                    if (e->children[1]->kind==E_VAR) {
                        int sl=(int)e->children[1]->ival;
                        if(sl>=0&&sl<ICN_CUR.env_n) ICN_CUR.env[sl]=ld;
                    }
                    return ret;
                }
            }

            /* ── IC-5: char(n), ord(s) ──────────────────────────────────── */
            if (!strcmp(fn,"char") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                int n = (int)(IS_INT_fn(av) ? av.i : (long long)strtol(VARVAL_fn(av)?VARVAL_fn(av):"0",NULL,10));
                char *buf = GC_malloc(2); buf[0]=(char)(n&0xFF); buf[1]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"ord") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                const char *s = VARVAL_fn(av); if (!s||!*s) return FAILDESCR;
                return INTVAL((unsigned char)s[0]);
            }

            /* ── IC-5: left/right/center/repl/reverse/map/trim ─────────── */
            if (!strcmp(fn,"left") && nargs >= 2) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                int n=(int)to_int(interp_eval(e->children[2])); if(n<0)n=0;
                const char *fill=" "; if(nargs>=3){DESCR_t fd=interp_eval(e->children[3]);const char*fs=VARVAL_fn(fd);if(fs&&*fs)fill=fs;}
                char *buf=GC_malloc(n+1); int sl=(int)strlen(s);
                for(int i=0;i<n;i++) buf[i]=(i<sl)?s[i]:fill[0]; buf[n]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"right") && nargs >= 2) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                int n=(int)to_int(interp_eval(e->children[2])); if(n<0)n=0;
                const char *fill=" "; if(nargs>=3){DESCR_t fd=interp_eval(e->children[3]);const char*fs=VARVAL_fn(fd);if(fs&&*fs)fill=fs;}
                int sl=(int)strlen(s); char *buf=GC_malloc(n+1);
                int pad=n-sl; if(pad<0)pad=0;
                for(int i=0;i<pad;i++) buf[i]=fill[0];
                for(int i=0;i<n-pad;i++) buf[pad+i]=s[sl-(n-pad)+i];
                buf[n]='\0'; return STRVAL(buf);
            }
            if (!strcmp(fn,"center") && nargs >= 2) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                int n=(int)to_int(interp_eval(e->children[2])); if(n<0)n=0;
                const char *fill=" "; if(nargs>=3){DESCR_t fd=interp_eval(e->children[3]);const char*fs=VARVAL_fn(fd);if(fs&&*fs)fill=fs;}
                int sl=(int)strlen(s); char *buf=GC_malloc(n+1);
                int lpad=(n-sl)/2; if(lpad<0)lpad=0;
                int rpad=n-sl-lpad; if(rpad<0)rpad=0;
                for(int i=0;i<lpad;i++) buf[i]=fill[0];
                for(int i=0;i<sl&&lpad+i<n;i++) buf[lpad+i]=s[i];
                for(int i=0;i<rpad;i++) buf[lpad+sl+i]=fill[0];
                buf[n]='\0'; return STRVAL(buf);
            }
            if (!strcmp(fn,"repl") && nargs == 2) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                int n=(int)to_int(interp_eval(e->children[2])); if(n<0)n=0;
                int sl=(int)strlen(s); char *buf=GC_malloc(sl*n+1); buf[0]='\0';
                for(int i=0;i<n;i++) memcpy(buf+i*sl,s,sl); buf[sl*n]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"reverse") && nargs == 1) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                int sl=(int)strlen(s); char *buf=GC_malloc(sl+1);
                for(int i=0;i<sl;i++) buf[i]=s[sl-1-i]; buf[sl]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"map") && nargs == 3) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                DESCR_t fv=interp_eval(e->children[2]); const char *from=VARVAL_fn(fv); if(!from)from="";
                DESCR_t tv=interp_eval(e->children[3]); const char *to=VARVAL_fn(tv); if(!to)to="";
                int sl=(int)strlen(s); char *buf=GC_malloc(sl+1);
                int fl=(int)strlen(from),tl=(int)strlen(to);
                for(int i=0;i<sl;i++){
                    char c=s[i]; int found=0;
                    for(int j=0;j<fl;j++) if(from[j]==c){buf[i]=(j<tl)?to[j]:'\0';found=1;break;}
                    if(!found) buf[i]=c;
                }
                buf[sl]='\0'; return STRVAL(buf);
            }
            if (!strcmp(fn,"trim") && nargs == 1) {
                DESCR_t sv=interp_eval(e->children[1]); const char *s=VARVAL_fn(sv); if(!s)s="";
                if (g_lang == 1) {
                    /* Icon: trim trailing whitespace only */
                    int sl=(int)strlen(s); while(sl>0&&isspace((unsigned char)s[sl-1]))sl--;
                    char *buf=GC_malloc(sl+1); memcpy(buf,s,sl); buf[sl]='\0';
                    return STRVAL(buf);
                } else {
                    /* Raku/other: trim both ends */
                    while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r') s++;
                    size_t len=strlen(s);
                    while(len>0&&(s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r')) len--;
                    char *buf=GC_malloc(len+1); memcpy(buf,s,len); buf[len]='\0';
                    return STRVAL(buf);
                }
            }
            /* ── IC-5: type(x), image(x), copy(x) ──────────────────────── */
            if (!strcmp(fn,"type") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                const char *t;
                if (IS_INT_fn(av))       t="integer";
                else if (IS_REAL_fn(av)) t="real";
                else if (av.v==DT_T)     t="table";
                else if (av.v==DT_A)     t="list";
                else if (av.v==DT_DATA)  {
                    /* check if icnlist tag */
                    DESCR_t tag = FIELD_GET_fn(av,"icn_type");
                    t = (tag.v==DT_S && tag.s) ? tag.s : "record";
                }
                else t="string";
                return STRVAL(t);
            }
            if (!strcmp(fn,"image") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                char *buf = GC_malloc(128);
                if (IS_INT_fn(av))       snprintf(buf,128,"%lld",(long long)av.i);
                else if (IS_REAL_fn(av)) icn_real_str(av.r,buf,128);
                else if (av.v==DT_T)     snprintf(buf,128,"table(%d)",av.tbl?av.tbl->size:0);
                else if (av.v==DT_DATA)  snprintf(buf,128,"record");
                else { const char *s=VARVAL_fn(av); return STRVAL(s?s:""); }
                return STRVAL(buf);
            }
            if (!strcmp(fn,"copy") && nargs == 1) {
                /* shallow copy — for our purposes return same value */
                return interp_eval(e->children[1]);
            }

            /* ── IC-5: swap(L, k) is actually handled as E_SWAP op ─────── */
            /* ── IC-5: size *L for DT_DATA lists ──────────────────────────
             * E_SIZE is handled below; nothing to add in E_FNC.           */

            /* ── IC-7: math builtins: abs, max, min, sqrt ──────────────── */
            if (!strcmp(fn,"abs") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                if (IS_REAL_fn(av)) return REALVAL(fabs(av.r));
                return INTVAL(av.i < 0 ? -av.i : av.i);
            }
            if (!strcmp(fn,"max") && nargs >= 2) {
                DESCR_t best = interp_eval(e->children[1]);
                for (int _j = 2; _j <= nargs; _j++) {
                    DESCR_t cv = interp_eval(e->children[_j]);
                    int gt = (IS_REAL_fn(best)||IS_REAL_fn(cv))
                        ? ((IS_REAL_fn(best)?best.r:(double)best.i) < (IS_REAL_fn(cv)?cv.r:(double)cv.i))
                        : (best.i < cv.i);
                    if (gt) best = cv;
                }
                return best;
            }
            if (!strcmp(fn,"min") && nargs >= 2) {
                DESCR_t best = interp_eval(e->children[1]);
                for (int _j = 2; _j <= nargs; _j++) {
                    DESCR_t cv = interp_eval(e->children[_j]);
                    int lt = (IS_REAL_fn(best)||IS_REAL_fn(cv))
                        ? ((IS_REAL_fn(best)?best.r:(double)best.i) > (IS_REAL_fn(cv)?cv.r:(double)cv.i))
                        : (best.i > cv.i);
                    if (lt) best = cv;
                }
                return best;
            }
            if (!strcmp(fn,"sqrt") && nargs == 1) {
                DESCR_t av = interp_eval(e->children[1]);
                double v = IS_REAL_fn(av) ? av.r : (double)av.i;
                return REALVAL(sqrt(v));
            }
            /* seq(i) / seq(i,j) — generator: i, i+1, i+2, ... (up to j if given).
             * Returns first value here; icn_eval_gen handles E_FNC "seq" as a box. */
            if (!strcmp(fn,"seq") && nargs >= 1) {
                DESCR_t start = interp_eval(e->children[1]);
                return IS_INT_fn(start) ? start : INTVAL(1);
            }

            /* ── IC-7: sort(L) / sortf(L, n) ───────────────────────────── */
            if ((!strcmp(fn,"sort") && nargs == 1) || (!strcmp(fn,"sortf") && nargs == 2)) {
                DESCR_t ld = interp_eval(e->children[1]);
                if (ld.v != DT_DATA) return FAILDESCR;
                DESCR_t ea = FIELD_GET_fn(ld,"icn_elems");
                int n = (int)FIELD_GET_fn(ld,"icn_size").i;
                if (n <= 0) return ld;
                DESCR_t *arr = (ea.v==DT_DATA) ? (DESCR_t*)ea.ptr : NULL;
                if (!arr) return ld;
                /* copy into new array for sort */
                DESCR_t *sorted = GC_malloc(n * sizeof(DESCR_t));
                memcpy(sorted, arr, n * sizeof(DESCR_t));
                int field_idx = (!strcmp(fn,"sortf") && nargs == 2)
                    ? (int)to_int(interp_eval(e->children[2])) - 1 : -1;
                /* insertion sort — small lists only; correct semantics */
                for (int _i = 1; _i < n; _i++) {
                    DESCR_t key = sorted[_i]; int _j = _i - 1;
                    while (_j >= 0) {
                        DESCR_t a = sorted[_j], b = key;
                        if (field_idx >= 0) {
                            /* sortf: compare field field_idx of record via DATINST_t */
                            if (a.v==DT_DATA && a.u) { DATINST_t *_ia=(DATINST_t*)a.u; if(_ia->type&&field_idx<_ia->type->nfields) a=_ia->fields[field_idx]; }
                            if (b.v==DT_DATA && b.u) { DATINST_t *_ib=(DATINST_t*)b.u; if(_ib->type&&field_idx<_ib->type->nfields) b=_ib->fields[field_idx]; }
                        }
                        int cmp;
                        if (IS_INT_fn(a) && IS_INT_fn(b)) cmp = (a.i > b.i) ? 1 : (a.i < b.i) ? -1 : 0;
                        else { const char *sa=VARVAL_fn(a),*sb=VARVAL_fn(b); cmp=strcmp(sa?sa:"",sb?sb:""); }
                        if (cmp <= 0) break;
                        sorted[_j+1] = sorted[_j]; _j--;
                    }
                    sorted[_j+1] = key;
                }
                /* build new icnlist with sorted elements */
                DESCR_t res = ld; /* same type tag */
                FIELD_SET_fn(res,"icn_elems",(DESCR_t){.v=DT_DATA,.ptr=sorted});
                FIELD_SET_fn(res,"icn_size",INTVAL(n));
                return res;
            }

            /* ── IC-5: record constructor — Icon puts name in children[0]->sval,
             * not in e->sval, so the shared E_FNC handler misses it.
             * Look up fn in sc_dat registry; if found, construct instance. */
            {
                ScDatType *_dt = sc_dat_find_type(fn);
                if (_dt) {
                    DESCR_t _args[ICN_SLOT_MAX];
                    for (int _j = 0; _j < nargs && _j < ICN_SLOT_MAX; _j++)
                        _args[_j] = interp_eval(e->children[1+_j]);
                    return sc_dat_construct(_dt, _args, nargs);
                }
            }

            return NULVCL;
        }
        case E_ALT:
        case E_ALTERNATE: {
            /* Icon value alternation: expr1 | expr2 | ... — return leftmost non-fail */
            if (e->nchildren < 1) return FAILDESCR;
            for (int i = 0; i < e->nchildren; i++) {
                DESCR_t v = interp_eval(e->children[i]);
                if (!IS_FAIL_fn(v)) return v;
            }
            return FAILDESCR;
        }
        case E_EVERY: {
            if (e->nchildren < 1) return NULVCL;
            EXPR_t *gen  = e->children[0];
            EXPR_t *body = (e->nchildren > 1) ? e->children[1] : NULL;
            /* IC-2a: icn_eval_gen + BB_PUMP — all goal-directed ops through Byrd boxes.
             * Special case: E_ASSIGN with generative RHS — drive the leaf generator
             * and re-evaluate the full assignment each tick so frame locals are read fresh.
             * e.g.: every total := total + (1 to n) inside a proc body.
             * NOTE: E_AUGOP is NOT special-cased here — it has its own icn_is_gen path
             * in the E_AUGOP handler that correctly drives the generator per-tick. */
            if (gen->kind == E_ASSIGN &&
                gen->nchildren >= 2 && icn_is_gen(gen->children[1])) {
                EXPR_t *leaf = icn_find_leaf_gen(gen->children[1]);
                if (!leaf) leaf = gen->children[1];
                bb_node_t rbox = icn_eval_gen(leaf);
                DESCR_t tick = rbox.fn(rbox.ζ, α);
                while (!IS_FAIL_fn(tick) && !ICN_CUR.returning && !ICN_CUR.loop_break) {
                    icn_drive_node = leaf;
                    icn_drive_val  = tick;
                    interp_eval(gen);
                    icn_drive_node = NULL;
                    if (body) interp_eval(body);
                    if (ICN_CUR.returning || ICN_CUR.loop_break) break;
                    tick = rbox.fn(rbox.ζ, β);
                }
                ICN_CUR.loop_break = 0;
                return NULVCL;
            }
            /* IC-6: E_SEQ conjunction — every (gen_expr & body_expr).
             * E_SEQ is Icon's & operator. Drive gen (children[0]) as generator;
             * evaluate remaining children as body per successful tick.
             * e.g.: every (x := (1|2|3|4|5)) > 2 & write(x)
             *   gen  = E_SEQ(E_GT(E_ASSIGN(x,alt), 2), E_FNC(write,x))
             * We split: filter_gen = children[0], seq_body = children[1..n-1]. */
            if (gen->kind == E_SEQ && gen->nchildren >= 2 && icn_is_gen(gen->children[0])) {
                EXPR_t *filter = gen->children[0];
                bb_node_t fbox = icn_eval_gen(filter);
                DESCR_t tick = fbox.fn(fbox.ζ, α);
                while (!IS_FAIL_fn(tick) && !ICN_CUR.returning && !ICN_CUR.loop_break) {
                    /* Execute remaining seq children as body */
                    for (int _si = 1; _si < gen->nchildren; _si++)
                        interp_eval(gen->children[_si]);
                    if (body) interp_eval(body);
                    if (ICN_CUR.returning || ICN_CUR.loop_break) break;
                    tick = fbox.fn(fbox.ζ, β);
                }
                ICN_CUR.loop_break = 0;
                return NULVCL;
            }
            /* When body==NULL, the box's own side-effects ARE the work (e.g. every write(1 to 5)).
             * When body!=NULL, box produces a value; body runs separately each tick. */
            bb_node_t box = icn_eval_gen(gen);
            /* RK-21: save caller frame depth before pumping — icn_bb_suspend (gather coroutine)
             * leaves its frame on the stack during suspend, so icn_frame_depth increases by 1
             * after box.fn(α). Binding must target the CALLER's frame, not ICN_CUR. */
            int caller_depth = icn_frame_depth;
            DESCR_t val = box.fn(box.ζ, α);
            while (!IS_FAIL_fn(val) && !ICN_CUR.returning && !ICN_CUR.loop_break) {
                /* RK-21: bind loop variable into the CALLER's frame (depth saved before pump). */
                if (gen->sval && *gen->sval && caller_depth >= 1) {
                    IcnFrame *cf = &icn_frame_stack[caller_depth - 1];
                    int slot = icn_scope_get(&cf->sc, gen->sval);
                    if (slot >= 0 && slot < cf->env_n)
                        cf->env[slot] = val;
                    else
                        NV_SET_fn(gen->sval, val);
                }
                if (body) {
                    icn_gen_push(gen, val.v == DT_I ? val.i : 0, val.v == DT_I ? NULL : val.s);
                    /* RK-21: if a coroutine (gather) frame is suspended on the stack,
                     * step icn_frame_depth back to caller_depth so ICN_CUR is the caller's
                     * frame during body execution. The coroutine frame is preserved at
                     * icn_frame_stack[caller_depth] and restored after body runs. */
                    int saved_depth = icn_frame_depth;
                    icn_frame_depth = caller_depth;
                    interp_eval(body);
                    icn_frame_depth = saved_depth;
                    icn_gen_pop();
                }
                if (ICN_CUR.returning || ICN_CUR.loop_break) break;
                val = box.fn(box.ζ, β);
            }
            ICN_CUR.loop_break = 0;
            return NULVCL;
        }
        case E_WHILE: {
            int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
            while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending &&
                   !IS_FAIL_fn(interp_eval(e->children[0]))) {
                if (e->nchildren > 1) interp_eval(e->children[1]);
                if (ICN_CUR.suspending) break;   /* suspend yield — exit loop, return to icn_call_proc */
            }
            ICN_CUR.loop_break = saved_brk;
            return NULVCL;
        }
        case E_UNTIL: {
            int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
            while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending) {
                DESCR_t cv = (e->nchildren > 0) ? interp_eval(e->children[0]) : FAILDESCR;
                if (!IS_FAIL_fn(cv)) break;
                if (e->nchildren > 1) interp_eval(e->children[1]);
                if (ICN_CUR.suspending) break;
            }
            ICN_CUR.loop_break = saved_brk;
            return NULVCL;
        }
        case E_REPEAT: {
            int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
            while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending)
                if (e->nchildren > 0) { interp_eval(e->children[0]); if (ICN_CUR.suspending) break; }
            ICN_CUR.loop_break = saved_brk;
            return NULVCL;
        }
        case E_SUSPEND: {
            /* Icon suspend: yield a value to icn_drive_fnc loop. */
            DESCR_t val = (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
            if (!IS_FAIL_fn(val)) {
                ICN_CUR.suspending  = 1;
                ICN_CUR.suspend_val = val;
                ICN_CUR.suspend_do  = (e->nchildren > 1) ? e->children[1] : NULL;
            }
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
            if (!IS_FAIL_fn(cv)) { if (e->nchildren > 1) return interp_eval(e->children[1]); }
            else                  { if (e->nchildren > 2) return interp_eval(e->children[2]); }
            return NULVCL;
        }
        case E_BREAK: {
            ICN_CUR.loop_break = 1;
            return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
        }
        case E_RETURN: {
            DESCR_t rv = (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
            ICN_CUR.returning  = 1;
            ICN_CUR.return_val = rv;
            return rv;
        }
        case E_FAIL: {
            ICN_CUR.returning  = 1;
            ICN_CUR.return_val = FAILDESCR;
            return FAILDESCR;
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
            /* SN-3: shadow table takes priority — param/local named after a pattern
             * primitive (LEN, ANY, SPAN, …) is invisible to NV_GET_fn. */
            { DESCR_t _sv; if (shadow_get(e->sval, &_sv)) return _sv; }
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
        /* Keywords are case-insensitive; NV stores them uppercase (e.g. "LCASE","UCASE").
         * Lexer strips '&' and preserves original case — uppercase before lookup. */
        char uc[64]; int _ki;
        for (_ki = 0; e->sval[_ki] && _ki < 63; _ki++)
            uc[_ki] = toupper((unsigned char)e->sval[_ki]);
        uc[_ki] = '\0';
        return NV_GET_fn(uc);
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
        /* SNOBOL4: int**int with non-negative exponent → integer; any real operand → real */
        if (IS_INT_fn(l) && IS_INT_fn(r) && r.i >= 0) {
            long base = l.i, result = 1; int exp = (int)r.i;
            for (int k = 0; k < exp; k++) result *= base;
            return INTVAL(result);
        }
        double base = IS_REAL_fn(l) ? l.r : (double)l.i;
        double exp  = IS_REAL_fn(r) ? r.r : (double)r.i;
        return (DESCR_t){ .v = DT_R, .r = pow(base, exp) };
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
                /* SNOBOL4 spec: DEFINE returns null string on success */
                return NULVCL;
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

    case E_SIZE: {
        /* *E — size of string, list, or table.
         * String: number of characters.  List/table (SOH-delimited): element count.
         * DT_T: native table → tbl->size. */
        if (e->nchildren < 1) return INTVAL(0);
        DESCR_t v = interp_eval(e->children[0]);
        if (IS_FAIL_fn(v)) return FAILDESCR;
        if (v.v == DT_T) return INTVAL(v.tbl ? v.tbl->size : 0);
        /* IC-5: DT_DATA icnlist */
        if (v.v == DT_DATA) {
            DESCR_t tag = FIELD_GET_fn(v,"icn_type");
            if (tag.v==DT_S && tag.s && strcmp(tag.s,"list")==0)
                return INTVAL((int)FIELD_GET_fn(v,"icn_size").i);
        }
        if (IS_INT_fn(v)) return INTVAL(0);   /* integer has no size */
        if (IS_REAL_fn(v)) return INTVAL(0);
        /* String: count chars, or SOH-delimited elements for arrays */
        const char *s = VARVAL_fn(v);
        if (!s) return INTVAL(0);
        /* If string contains SOH (\x01) it is a Raku/Icon array — count elements */
        if (strchr(s, '\x01')) {
            long n = 1;
            for (const char *p = s; *p; p++) if (*p == '\x01') n++;
            return INTVAL(n);
        }
        long len = v.slen > 0 ? v.slen : (long)strlen(s);
        return INTVAL(len);
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
        /* Snocone *fn() lowers as E_INDIRECT(E_FNC(...)) — same semantics as
         * E_DEFER(E_FNC(...)): call fn at flush time to get lvalue, assign then.
         * SC-26: route this through pat_assign_callcap so it joins the unified
         * NAM list and fires in left-to-right order after preceding captures. */
        if (tgt->kind == E_INDIRECT && tgt->nchildren == 1
                && tgt->children[0]->kind == E_FNC && tgt->children[0]->sval) {
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
     * Each compares two numeric operands; succeeds (returns rhs) or fails.
     * Icon relops return the RIGHT operand on success — this lets them act
     * as filters in generator chains: every write(2 < (1 to 4)) → 3, 4.
     * (SNOBOL4 uses a separate path; these cases are Icon-only.) */
#define NUMREL(op) do { \
        if (e->nchildren < 2) return FAILDESCR; \
        DESCR_t l = interp_eval(e->children[0]); \
        DESCR_t r = interp_eval(e->children[1]); \
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR; \
        double lv = (l.v==DT_R) ? l.r : (double)(l.v==DT_I ? l.i : 0); \
        double rv = (r.v==DT_R) ? r.r : (double)(r.v==DT_I ? r.i : 0); \
        if (!(lv op rv)) return FAILDESCR; \
        return r; \
    } while(0)
    case E_LT: NUMREL(<);
    case E_LE: NUMREL(<=);
    case E_GT: NUMREL(>);
    case E_GE: NUMREL(>=);
    case E_EQ: NUMREL(==);
    case E_NE: NUMREL(!=);
#undef NUMREL

    /* ── Lexicographic (string) relational operators ───────────────────────
     * Each compares two string operands; succeeds (returns rhs) or fails.
     * Icon string relops return the RIGHT operand on success — oracle:
     * ocomp.r StrComp macro: "Return y as the result of the comparison." */
#define STRREL(cmpop) do { \
        if (e->nchildren < 2) return FAILDESCR; \
        DESCR_t l = interp_eval(e->children[0]); \
        DESCR_t r = interp_eval(e->children[1]); \
        if (IS_FAIL_fn(l) || IS_FAIL_fn(r)) return FAILDESCR; \
        const char *ls = VARVAL_fn(l); if (!ls) ls = ""; \
        const char *rs = VARVAL_fn(r); if (!rs) rs = ""; \
        int cmp = strcmp(ls, rs); \
        if (!(cmp cmpop 0)) return FAILDESCR; \
        return r; \
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
    case E_FENCE:
        if (e->nchildren > 0) {
            DESCR_t _inner = interp_eval_pat(e->children[0]);
            if (IS_FAIL_fn(_inner)) return FAILDESCR;
            return pat_fence_p(_inner);
        }
        return pat_fence();
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
        DESCR_t inner = interp_eval_pat(e->children[0]);
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
        /* IC-2a: icn_eval_gen + BB_PUMP — all goal-directed ops through Byrd boxes.
         * Special case: if gen is E_ASSIGN or E_AUGOP with a generative RHS,
         * drive the LEAF generator inside the RHS and re-evaluate gen each tick so
         * that mutable frame locals (e.g. `total`) are read fresh.  e.g.:
         *   every total := total + (1 to n)   -- E_ASSIGN(E_VAR(total), E_ADD(E_VAR(total), E_TO(1,n)))
         *   every total +:= (1 to n)           -- E_AUGOP with E_TO rhs
         * We find the leaf generator node (e.g. E_TO), drive only that via icn_eval_gen,
         * and inject each raw tick value via icn_drive_node passthrough.  interp_eval(gen)
         * then re-reads the current value of `total` from the frame slot each iteration. */
        if ((gen->kind == E_ASSIGN) &&
            gen->nchildren >= 2 && icn_is_gen(gen->children[1])) {
            EXPR_t *leaf = icn_find_leaf_gen(gen->children[1]);
            if (!leaf) leaf = gen->children[1];   /* fallback: treat whole RHS as gen */
            bb_node_t rbox = icn_eval_gen(leaf);
            DESCR_t tick = rbox.fn(rbox.ζ, α);
            while (!IS_FAIL_fn(tick) && !ICN_CUR.returning && !ICN_CUR.loop_break) {
                /* Inject the raw generator tick value via drive passthrough,
                 * then re-evaluate gen (the assign/augop) — interp_eval re-reads
                 * the current frame value of any E_VAR in the expression. */
                icn_drive_node = leaf;
                icn_drive_val  = tick;
                interp_eval(gen);
                icn_drive_node = NULL;
                if (body) interp_eval(body);
                if (ICN_CUR.returning || ICN_CUR.loop_break) break;
                tick = rbox.fn(rbox.ζ, β);
            }
            ICN_CUR.loop_break = 0;
            return NULVCL;
        }
        EXPR_t *do_expr = body ? body : gen;
        bb_node_t box = icn_eval_gen(gen);
        DESCR_t val = box.fn(box.ζ, α);
        while (!IS_FAIL_fn(val) && !ICN_CUR.returning && !ICN_CUR.loop_break) {
            icn_gen_push(gen, val.v == DT_I ? val.i : 0, val.v == DT_I ? NULL : val.s);
            interp_eval(do_expr);
            icn_gen_pop();
            if (ICN_CUR.returning || ICN_CUR.loop_break) break;
            val = box.fn(box.ζ, β);
        }
        ICN_CUR.loop_break = 0;
        return NULVCL;
    }

    case E_WHILE: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending &&
               !IS_FAIL_fn(interp_eval(e->children[0]))) {
            if (e->nchildren > 1) interp_eval(e->children[1]);
            if (ICN_CUR.suspending) break;
        }
        ICN_CUR.loop_break = saved_brk;
        return NULVCL;
    }

    case E_UNTIL: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending) {
            DESCR_t cv = (e->nchildren > 0) ? interp_eval(e->children[0]) : FAILDESCR;
            if (!IS_FAIL_fn(cv)) break;
            if (e->nchildren > 1) interp_eval(e->children[1]);
            if (ICN_CUR.suspending) break;
        }
        ICN_CUR.loop_break = saved_brk;
        return NULVCL;
    }

    case E_REPEAT: {
        int saved_brk = ICN_CUR.loop_break; ICN_CUR.loop_break = 0;
        while (!ICN_CUR.returning && !ICN_CUR.loop_break && !ICN_CUR.suspending)
            if (e->nchildren > 0) { interp_eval(e->children[0]); if (ICN_CUR.suspending) break; }
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

    case E_CASE: {
        /* Two layouts:
         *   Icon:  child[0]=topic, then pairs [val, body]..., optional trailing default_body
         *   Raku:  child[0]=topic, then triples [cmpnode(E_ILIT|E_NUL), val, body]
         * Detect by child[1]: if it's E_ILIT or E_NUL it's a Raku cmpnode (triples).
         * Icon case values are always full expressions — never bare E_ILIT/E_NUL. */
        if (e->nchildren < 1) return NULVCL;
        DESCR_t topic = interp_eval(e->children[0]);
        /* Detect layout by child count:
         *   Raku triples: nchildren = 1 + 3*N  (topic + N×[cmpnode,val,body])  → (nchildren-1) % 3 == 0
         *   Icon pairs:   nchildren = 1 + 2*N  or  1 + 2*N + 1 (with default)  → (nchildren-1) % 2 == 0 or 1
         * Additionally verify child[1] is E_ILIT or E_NUL for Raku (cmpnode marker). */
        int is_raku_layout = (e->nchildren >= 4 && (e->nchildren - 1) % 3 == 0 &&
            e->children[1] && (e->children[1]->kind == E_ILIT || e->children[1]->kind == E_NUL));
        if (is_raku_layout) {
            /* Raku: triples [cmpnode(E_ILIT), val, body] */
            int i = 1;
            while (i + 2 < e->nchildren) {
                EXPR_t *cmpnode = e->children[i];
                EXPR_t *val     = e->children[i+1];
                EXPR_t *body    = e->children[i+2];
                i += 3;
                if (cmpnode->kind == E_NUL) return interp_eval(body);
                EKind cmp = (EKind)(cmpnode->ival);
                DESCR_t wval = interp_eval(val);
                int match = 0;
                if (cmp == E_LEQ) {
                    const char *ts = IS_STR_fn(topic)?topic.s:VARVAL_fn(topic);
                    const char *ws = IS_STR_fn(wval)?wval.s:VARVAL_fn(wval);
                    match = (ts && ws && strcmp(ts,ws)==0);
                } else {
                    if (IS_INT_fn(topic) && IS_INT_fn(wval)) match = (topic.i == wval.i);
                    else { const char *ts=VARVAL_fn(topic),*ws=VARVAL_fn(wval); match=(ts&&ws&&strcmp(ts,ws)==0); }
                }
                if (match) return interp_eval(body);
            }
            if (i+1 < e->nchildren && e->children[i]->kind==E_NUL)
                return interp_eval(e->children[i+1]);
            return NULVCL;
        }
        /* Icon: pairs [val, body] then optional trailing default body */
        int nc = e->nchildren;
        int i = 1;
        while (i + 1 < nc) {
            DESCR_t wval = interp_eval(e->children[i]);
            EXPR_t *body = e->children[i+1];
            i += 2;
            int match;
            if (IS_INT_fn(topic) && IS_INT_fn(wval)) match = (topic.i == wval.i);
            else {
                const char *ts = VARVAL_fn(topic), *ws = VARVAL_fn(wval);
                match = (ts && ws && strcmp(ts, ws) == 0);
            }
            if (match) return interp_eval(body);
        }
        /* trailing default body (odd child count) */
        if (i < nc) return interp_eval(e->children[i]);
        return NULVCL;
    }

    case E_NULL: {
        /* /E — succeeds (yields &null) if E fails; fails if E succeeds */
        if (e->nchildren < 1) return NULVCL;
        DESCR_t v = interp_eval(e->children[0]);
        return IS_FAIL_fn(v) ? NULVCL : FAILDESCR;
    }

    case E_NONNULL: {
        /* \E — succeeds (yields E) if E succeeds and is non-null; fails otherwise */
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t v = interp_eval(e->children[0]);
        if (IS_FAIL_fn(v)) return FAILDESCR;
        /* null in Icon is the empty string / zero-length result of &null */
        if (v.v == DT_S && (!v.s || v.s[0] == '\0')) return FAILDESCR;
        if (v.v == DT_I && v.i == 0 && !(IS_INT_fn(v))) return FAILDESCR;
        return v;
    }

    case E_AUGOP: {
        if (e->nchildren < 2) return NULVCL;
        EXPR_t *lhs = e->children[0];
        EXPR_t *rhs = e->children[1];
        /* Helper lambda: apply augop to (lv, rv), write back to lhs slot, return result */
        #define AUGOP_APPLY(lv_, rv_) do { \
            DESCR_t _lv = (lv_), _rv = (rv_); \
            if (IS_FAIL_fn(_lv)||IS_FAIL_fn(_rv)) break; \
            long _li = IS_INT_fn(_lv)?_lv.i:(long)_lv.r; \
            long _ri = IS_INT_fn(_rv)?_rv.i:(long)_rv.r; \
            DESCR_t _res = NULVCL; \
            switch((IcnTkKind)e->ival){ \
                case TK_AUGPLUS:   _res=INTVAL(_li+_ri); break; \
                case TK_AUGMINUS:  _res=INTVAL(_li-_ri); break; \
                case TK_AUGSTAR:   _res=INTVAL(_li*_ri); break; \
                case TK_AUGSLASH:  _res=_ri?INTVAL(_li/_ri):FAILDESCR; break; \
                case TK_AUGMOD:    _res=_ri?INTVAL(_li%_ri):FAILDESCR; break; \
                case TK_AUGCONCAT: { \
                    const char *_ls=VARVAL_fn(_lv),*_rs=VARVAL_fn(_rv); \
                    if(!_ls)_ls="";if(!_rs)_rs=""; \
                    size_t _ll=strlen(_ls),_rl=strlen(_rs); \
                    char *_buf=GC_malloc(_ll+_rl+1); \
                    memcpy(_buf,_ls,_ll);memcpy(_buf+_ll,_rs,_rl);_buf[_ll+_rl]='\0'; \
                    _res=STRVAL(_buf); break; \
                } \
                default: _res=INTVAL(_li+_ri); break; \
            } \
            if (!IS_FAIL_fn(_res) && lhs->kind == E_VAR && icn_frame_depth > 0) { \
                int _slot=(int)lhs->ival; \
                if(_slot>=0&&_slot<ICN_CUR.env_n) ICN_CUR.env[_slot]=_res; \
            } \
            _augop_result = _res; \
        } while(0)

        /* If RHS is a generator: apply augop once per tick, re-reading lhs each time.
         * This implements  every sum +:= (1 to 5)  →  sum=1,3,6,10,15
         * and  every result ||:= !s  →  result="x","xy"  */
        DESCR_t _augop_result = NULVCL;
        if (rhs && icn_is_gen(rhs)) {
            bb_node_t rbox = icn_eval_gen(rhs);
            DESCR_t tick = rbox.fn(rbox.ζ, α);
            while (!IS_FAIL_fn(tick) && !ICN_CUR.loop_break && !ICN_CUR.returning) {
                DESCR_t cur_lv = interp_eval(lhs);   /* re-read lhs each tick */
                AUGOP_APPLY(cur_lv, tick);
                tick = rbox.fn(rbox.ζ, β);
            }
        } else {
            DESCR_t lv = interp_eval(lhs);
            DESCR_t rv = interp_eval(rhs);
            AUGOP_APPLY(lv, rv);
        }
        #undef AUGOP_APPLY
        return _augop_result;
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
        /* IC-3: DT_T table — return first value (oneshot; every uses icn_bb_tbl_iterate) */
        if (e->nchildren >= 1) {
            DESCR_t sv = interp_eval(e->children[0]);
            if (sv.v == DT_T && sv.tbl) {
                for (int bi = 0; bi < TABLE_BUCKETS; bi++) {
                    if (sv.tbl->buckets[bi]) return sv.tbl->buckets[bi]->val;
                }
                return FAILDESCR;
            }
            /* IC-5: DT_DATA icnlist — !L returns first element (every drives the rest) */
            if (sv.v == DT_DATA) {
                DESCR_t tag = FIELD_GET_fn(sv,"icn_type");
                if (tag.v==DT_S && tag.s && strcmp(tag.s,"list")==0) {
                    int n=(int)FIELD_GET_fn(sv,"icn_size").i;
                    DESCR_t ea=FIELD_GET_fn(sv,"icn_elems");
                    DESCR_t *elems=(ea.v==DT_DATA)?(DESCR_t*)ea.ptr:NULL;
                    if(!elems||n<=0) return FAILDESCR;
                    return elems[0];
                }
            }
        }
        long cur; const char *str;
        if (icn_gen_lookup_sv(e, &cur, &str) && str) {
            char *ch = GC_malloc(2); ch[0] = str[cur]; ch[1] = '\0';
            return STRVAL(ch);
        }
        return FAILDESCR;
    }

    case E_SUSPEND: {
        /* Icon suspend: yield a value to the driving icn_drive E_FNC loop.
         * Signal by setting ICN_CUR.suspending=1; icn_drive sees the flag,
         * runs the every-body, executes the do-clause, clears the flag, and
         * re-enters the procedure body loop.  For non-generator (bare call)
         * contexts there is no driver, so we just return the value. */
        DESCR_t val = (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
        if (icn_frame_depth > 0) {
            ICN_CUR.suspending  = 1;
            ICN_CUR.suspend_val = val;
            ICN_CUR.suspend_do  = (e->nchildren > 1) ? e->children[1] : NULL;
        }
        return val;
    }

    /* ── IC-5: E_SWAP — x :=: y  (swap two lvalues) ───────────────────── */
    case E_SWAP: {
        if (e->nchildren < 2 || icn_frame_depth <= 0) return NULVCL;
        EXPR_t *lhs = e->children[0], *rhs = e->children[1];
        DESCR_t lv = interp_eval(lhs), rv = interp_eval(rhs);
        /* write rv→lhs slot, lv→rhs slot */
        if (lhs && lhs->kind == E_VAR) {
            int sl=(int)lhs->ival;
            if (sl>=0&&sl<ICN_CUR.env_n) ICN_CUR.env[sl]=rv;
            else if (sl<0&&lhs->sval) NV_SET_fn(lhs->sval,rv);
        }
        if (rhs && rhs->kind == E_VAR) {
            int sl=(int)rhs->ival;
            if (sl>=0&&sl<ICN_CUR.env_n) ICN_CUR.env[sl]=lv;
            else if (sl<0&&rhs->sval) NV_SET_fn(rhs->sval,lv);
        }
        return rv;
    }

    /* ── IC-5: E_LCONCAT — s1 ||| s2  (list concatenation = string concat alias) */
    case E_LCONCAT: {
        if (e->nchildren < 2) return NULVCL;
        DESCR_t a = interp_eval(e->children[0]);
        DESCR_t b = interp_eval(e->children[1]);
        /* For string operands, behave like string concat */
        char ab[64], bb[64];
        const char *as = IS_INT_fn(a)?(snprintf(ab,64,"%lld",(long long)a.i),ab):IS_REAL_fn(a)?(icn_real_str(a.r,ab,64),ab):VARVAL_fn(a);
        const char *bs = IS_INT_fn(b)?(snprintf(bb,64,"%lld",(long long)b.i),bb):IS_REAL_fn(b)?(icn_real_str(b.r,bb,64),bb):VARVAL_fn(b);
        if (!as) as=""; if (!bs) bs="";
        size_t al=strlen(as),bl=strlen(bs);
        char *buf=GC_malloc(al+bl+1); memcpy(buf,as,al); memcpy(buf+al,bs,bl); buf[al+bl]='\0';
        return STRVAL(buf);
    }

    /* ── IC-5: E_MAKELIST — [e1,e2,...] list constructor ───────────────── */
    case E_MAKELIST: {
        int n = e->nchildren;
        /* Register icnlist type once if needed, using a global flag */
        static int icnlist_registered = 0;
        if (!icnlist_registered) { DEFDAT_fn("icnlist(icn_elems,icn_size,icn_type)"); icnlist_registered=1; }
        DESCR_t *elems = GC_malloc((n>0?n:1)*sizeof(DESCR_t));
        for (int i = 0; i < n; i++) elems[i] = interp_eval(e->children[i]);
        DESCR_t eptr; eptr.v=DT_DATA; eptr.slen=0; eptr.ptr=(void*)elems;
        DESCR_t ld = DATCON_fn("icnlist", eptr, INTVAL(n), STRVAL("list"));
        return ld;
    }

    /* ── IC-5: E_SECTION — s[i:j] string section ───────────────────────── */
    case E_SECTION: {
        if (e->nchildren < 3) return NULVCL;
        DESCR_t sd = interp_eval(e->children[0]);
        const char *s = VARVAL_fn(sd); if (!s) s="";
        int slen = (int)strlen(s);
        int i = (int)to_int(interp_eval(e->children[1]));
        int j = (int)to_int(interp_eval(e->children[2]));
        /* Icon 1-based, negative wraps: -1 = slen+1 */
        if (i < 0) i = slen + 1 + i + 1;
        if (j < 0) j = slen + 1 + j + 1;
        if (i < 1) i = 1; if (j > slen+1) j = slen+1;
        if (i > j) { char *e2=GC_malloc(1); e2[0]='\0'; return STRVAL(e2); }
        int len = j - i;
        char *buf = GC_malloc(len+1); memcpy(buf, s+i-1, len); buf[len]='\0';
        return STRVAL(buf);
    }

    /* ── IC-5: E_INITIAL — once-only block; persists local values across calls ─ */
    case E_INITIAL: {
        /* Uses file-scope icn_init_tab[] keyed on e->id.
         * First call: run block, snapshot assigned locals.
         * Subsequent calls: restore snapshot (updated at call exit by icn_init_update_snapshot). */
        IcnInitEnt *ent = NULL;
        for (int _i = 0; _i < icn_init_n; _i++)
            if (icn_init_tab[_i].id == e->id) { ent = &icn_init_tab[_i]; break; }

        if (!ent) {
            /* ── First call: run the block ── */
            for (int i = 0; i < e->nchildren; i++) interp_eval(e->children[i]);
            if (icn_init_n < ICN_INIT_MAX) {
                ent = &icn_init_tab[icn_init_n++];
                ent->id = e->id; ent->ns = 0;
                for (int i = 0; i < e->nchildren && ent->ns < ICN_INIT_SLOTS; i++) {
                    EXPR_t *ch = e->children[i];
                    if (!ch || ch->kind != E_ASSIGN || ch->nchildren < 1) continue;
                    EXPR_t *lhs = ch->children[0];
                    if (!lhs || lhs->kind != E_VAR || !lhs->sval) continue;
                    IcnInitSlot *sl = &ent->s[ent->ns++];
                    strncpy(sl->nm, lhs->sval, 63); sl->nm[63] = '\0';
                    if (icn_frame_depth > 0 && lhs->ival >= 0 && lhs->ival < ICN_CUR.env_n)
                        sl->val = ICN_CUR.env[lhs->ival];
                    else
                        sl->val = NV_GET_fn(lhs->sval);
                }
            }
            e->ival = 1;
        } else {
            /* ── Subsequent calls: restore snapshot into frame/NV ── */
            for (int si = 0; si < ent->ns; si++) {
                int restored = 0;
                if (icn_frame_depth > 0) {
                    for (int i = 0; i < e->nchildren && !restored; i++) {
                        EXPR_t *ch = e->children[i];
                        if (!ch || ch->kind != E_ASSIGN || ch->nchildren < 1) continue;
                        EXPR_t *lhs = ch->children[0];
                        if (!lhs || lhs->kind != E_VAR || !lhs->sval) continue;
                        if (strcasecmp(lhs->sval, ent->s[si].nm) == 0
                            && lhs->ival >= 0 && lhs->ival < ICN_CUR.env_n) {
                            ICN_CUR.env[lhs->ival] = ent->s[si].val;
                            restored = 1;
                        }
                    }
                }
                if (!restored) NV_SET_fn(ent->s[si].nm, ent->s[si].val);
            }
        }
        return NULVCL;
    }

    /* ── IC-5: E_RECORD — register record type ──────────────────────────── */
    case E_RECORD: {
        /* e->sval = type name; children = field name E_VAR nodes.
         * Build spec string "typename(f1,f2,...)" and call DEFDAT_fn + sc_dat_register. */
        if (!e->sval) return NULVCL;
        /* Only register once (EXPR_t node persists across calls) */
        if (e->ival != 0) return NULVCL;
        e->ival = 1;
        char spec[256]; int pos=0;
        pos += snprintf(spec+pos, sizeof(spec)-pos, "%s(", e->sval);
        for (int i = 0; i < e->nchildren && pos < (int)sizeof(spec)-2; i++) {
            if (i > 0) spec[pos++]=',';
            const char *fn2 = (e->children[i] && e->children[i]->sval) ? e->children[i]->sval : "";
            pos += snprintf(spec+pos, sizeof(spec)-pos, "%s", fn2);
        }
        if (pos < (int)sizeof(spec)-1) spec[pos++]=')';
        spec[pos]='\0';
        DEFDAT_fn(spec);
        sc_dat_register(spec);   /* IC-5: also register in our dispatch table */
        return NULVCL;
    }

    /* ── IC-5: E_FIELD — field access: obj.fieldname ────────────────────── */
    case E_FIELD: {
        /* e->sval = field name; child[0] = object expression */
        if (!e->sval || e->nchildren < 1) return NULVCL;
        DESCR_t obj = interp_eval(e->children[0]);
        if (IS_FAIL_fn(obj)) return FAILDESCR;
        DESCR_t *cell = data_field_ptr(e->sval, obj);
        if (!cell) return FAILDESCR;
        return *cell;
    }

    /* ── IC-5: E_GLOBAL (declaration, skip at eval time) ───────────────── */
    case E_GLOBAL:
        return NULVCL;

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

    case E_FIELD: {
        /* obj.fieldname as lvalue — return interior ptr to field cell */
        if (!e->sval || e->nchildren < 1) return NULL;
        DESCR_t obj = interp_eval(e->children[0]);
        if (IS_FAIL_fn(obj)) return NULL;
        return data_field_ptr(e->sval, obj);
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

    case E_FENCE:
        /* FENCE(P) in pattern context — must be handled here so the child
         * is evaluated in pat context, not value context (default path). */
        if (e->nchildren > 0) {
            DESCR_t _inner = interp_eval_pat(e->children[0]);
            if (IS_FAIL_fn(_inner)) return FAILDESCR;
            return pat_fence_p(_inner);
        }
        return pat_fence();

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

        /* IM-3: step-limit — stop after exactly g_ir_step_limit statements */
        if (g_ir_step_limit > 0 && g_ir_steps_done++ >= g_ir_step_limit)
            longjmp(g_ir_step_jmp, 1);

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
                    subj_name = ic->sval;  /* $'name' — literal name, use directly */
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
            EXPR_t *ichild = s->subject->nchildren > 0 ? s->subject->children[0] : NULL;
            DESCR_t repl_val = s->replacement ? interp_eval(s->replacement) : NULVCL;
            if (IS_FAIL_fn(repl_val)) {
                succeeded = 0;
            } else {
                /* $expr = rhs: evaluate expr to get string name, assign rhs to that var.
                 * $UTF_Array[i,2] means: get string value of UTF_Array[i,2], use as var name.
                 * This is always string-name indirection — never a subscript lvalue. */
                DESCR_t name_d = ichild ? interp_eval(ichild) : NULVCL;
                const char *nm = VARVAL_fn(name_d);
                if (!nm || !*nm) {
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
    /* ITEM(arr,i) read and ITEM_SET(rhs,arr,i) write — SM emits these for ITEM() syntax.
     * ITEM read: args[0]=arr, args[1]=i, [args[2]=j for 2D].
     * ITEM_SET write: args[0]=rhs, args[1]=arr, args[2]=i, [args[3]=j for 2D]. */
    if (strcasecmp(name, "ITEM") == 0 && nargs >= 2) {
        if (nargs >= 3) return subscript_get2(args[0], args[1], args[2]);
        return subscript_get(args[0], args[1]);
    }
    if (strcasecmp(name, "ITEM_SET") == 0 && nargs >= 3) {
        DESCR_t rhs = args[0], arr = args[1], idx = args[2];
        if (nargs >= 4) { subscript_set2(arr, idx, args[3], rhs); }
        else            { subscript_set(arr, idx, rhs); }
        return rhs;
    }
    /* SC-1: DATA constructor/field-accessor/field-mutator dispatch via sc_dat registry.
     * Must precede label lookup so struct names shadow any same-named labels. */
    {
        ScDatType *_dt = sc_dat_find_type(name);
        if (_dt) return sc_dat_construct(_dt, args, nargs);
        int _fi = 0;
        ScDatType *_ft = sc_dat_find_field(name, &_fi);
        if (_ft && nargs >= 1) return sc_dat_field_get(name, args[0]);
        /* Field mutator: fname_SET(rhs, obj) — sm_lower emits this for fname(obj)=rhs.
         * Strip the _SET suffix, look up the field, write through data_field_ptr. */
        size_t _nlen = strlen(name);
        if (_nlen > 4 && strcasecmp(name + _nlen - 4, "_SET") == 0 && nargs >= 2) {
            char _fname[128];
            size_t _flen = _nlen - 4;
            if (_flen >= sizeof(_fname)) _flen = sizeof(_fname) - 1;
            memcpy(_fname, name, _flen); _fname[_flen] = '\0';
            DESCR_t *_cell = data_field_ptr(_fname, args[1]);
            if (_cell) { *_cell = args[0]; return args[0]; }
        }
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


/* IM-3: execute_program_steps — run at most N statements then return.
 * Sets up g_ir_step_jmp so the step-limit longjmp lands here safely. */
void execute_program_steps(Program *prog, int n) {
    g_ir_step_limit = n;
    g_ir_steps_done = 0;
    if (setjmp(g_ir_step_jmp) == 0)
        execute_program(prog);
    g_ir_step_limit = 0;
    g_ir_steps_done = 0;
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

/* Public wrapper: register an Icon/SNOBOL4 record type spec from polyglot_init.
 * Also calls DEFDAT_fn so the SPITBOL runtime knows the type. */
void icn_record_register(const char *spec) {
    if (!spec || !*spec) return;
    /* Skip if already registered (polyglot_init may see the same file twice) */
    const char *p = spec;
    char name[64]; int ni = 0;
    while (*p && *p != '(' && ni < 63) name[ni++] = *p++;
    name[ni] = '\0';
    if (sc_dat_find_type(name)) return;   /* already registered */
    DEFDAT_fn(spec);
    sc_dat_register(spec);
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

/* sc_dat_field_call — public entry for SM-run DATA dispatch.
 * Called from sm_interp SM_CALL handler when args[0] is DT_DATA, to give
 * DATA field accessors/mutators/constructors priority over same-named builtins
 * (e.g. DATA field named 'real' must win over REAL() builtin when arg is DT_DATA).
 * Returns FAILDESCR if name is not a DATA constructor/field — caller falls through
 * to normal INVOKE_fn dispatch.
 * Convention for mutators: name ends in _SET, args[0]=rhs, args[1]=obj. */
DESCR_t sc_dat_field_call(const char *name, DESCR_t *args, int nargs) {
    if (!name || nargs < 1) return FAILDESCR;
    /* Constructor: name matches a DATA type name */
    ScDatType *_dt = sc_dat_find_type(name);
    if (_dt) return sc_dat_construct(_dt, args, nargs);
    /* Field accessor: name matches a field, arg is DT_DATA instance */
    int _fi = 0;
    ScDatType *_ft = sc_dat_find_field(name, &_fi);
    if (_ft) return sc_dat_field_get(name, args[0]);
    /* Field mutator: name ends in _SET — strip suffix, check field */
    size_t _nlen = strlen(name);
    if (_nlen > 4 && strcasecmp(name + _nlen - 4, "_SET") == 0 && nargs >= 2) {
        char _fname[128];
        size_t _flen = _nlen - 4;
        if (_flen >= sizeof(_fname)) _flen = sizeof(_fname) - 1;
        memcpy(_fname, name, _flen); _fname[_flen] = '\0';
        DESCR_t *_cell = data_field_ptr(_fname, args[1]);
        if (_cell) { *_cell = args[0]; return args[0]; }
    }
    return FAILDESCR;
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
