/*
 * scrip.c — unified SCRIP driver
 *
 * One binary, all modes. Frontend inferred from file extension.
 *
 * Usage:
 *   scrip [mode] [bb] [target] [options] <file> [-- program-args...]
 *
 * Execution modes (default: --ir-run):
 *   --ir-run         interpret via IR tree-walk (correctness reference) [DEFAULT]
 *   --sm-run         interpret SM_Program via dispatch loop
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
 *   --dump-parse     dump CMPILE parse tree (CMPILE hand-written parser; diagnostic only)
 *   --dump-parse-flat  same, one line per stmt
 *   --dump-ir        print IR after frontend (uses CMPILE path)
 *   --dump-ir-bison  print IR after frontend (uses Bison/Flex path — matches execution)
 *
 * Parser note:
 *   Execution (--ir-run, --sm-run, --jit-run, --jit-emit) uses sno_parse()
 *   — the Bison/Flex generated parser (snobol4.tab.c / snobol4.lex.c).
 *   CMPILE (hand-written recursive descent) is used only for --dump-parse
 *   and --dump-ir. Pattern primitives (E_ANY, E_SPAN, etc.) are emitted
 *   as typed IR nodes by the Bison parser; CMPILE still emits E_FNC.
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
#include <ucontext.h>
#include <time.h>
#include <gc.h>

/* ── frontend ─────────────────────────────────────────────────────────── */
#include "../frontend/snobol4/scrip_cc.h"
extern Program *sno_parse(FILE *f, const char *filename);
#include "../frontend/snocone/snocone_driver.h"
#include "../frontend/prolog/prolog_driver.h"
#include "../frontend/prolog/prolog_runtime.h"
#include "../frontend/prolog/prolog_atom.h"
#include "../frontend/prolog/prolog_builtin.h"
#include "../frontend/prolog/term.h"
#include "../frontend/icon/icon_driver.h"
#include "../frontend/icon/icon_lex.h"    /* IcnTkKind — TK_AUG* for E_AUGOP in unified interp */

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

/* ── Prolog type forward declarations ───────────────────────────────────────
 * Full definitions appear in the pl_ block below; globals declared here so
 * they sit alongside the Icon globals at file scope (same pattern). */
#define PL_PRED_TABLE_SIZE_FWD 256
typedef struct Pl_PredEntry_t { const char *key; EXPR_t *choice; struct Pl_PredEntry_t *next; } Pl_PredEntry;
typedef struct { Pl_PredEntry *buckets[PL_PRED_TABLE_SIZE_FWD]; } Pl_PredTable;
/* Trail is defined in prolog_runtime.h (already included) */

/* ── Icon unified interpreter state ────────────────────────────────────────
 * Icon procedures use name-based NV store, same as SNOBOL4.
 * On procedure entry each param/local name is saved from NV and replaced;
 * on exit all are restored. No slot arrays, no icn_env.
 *
 * Icon procedure table: built from Program* at execute_program time.
 * Each entry maps procname → the E_FNC node (from STMT_t subject).
 * ────────────────────────────────────────────────────────────────────────── */
#define ICN_SLOT_MAX   64   /* max params+locals per procedure */
#define ICN_PROC_MAX  256
typedef struct { const char *name; EXPR_t *proc; } IcnProcEntry;
static IcnProcEntry icn_proc_table[ICN_PROC_MAX];
static int          icn_proc_count = 0;
static int          icn_returning  = 0;     /* 1 = Icon return in progress */
static DESCR_t      icn_return_val;         /* value being returned */
static EXPR_t      *g_icn_root     = NULL;  /* current Icon drive root */

/* Generator substitution stack for E_EVERY/E_TO β re-entry (DESCR_t version).
 * Mirrors icn_exec_driven logic but uses DESCR_t instead of IcnVal. */
#define ICN_GEN_MAX 16
typedef struct { EXPR_t *node; long cur; const char *sval; } IcnGenEntry_d;
static IcnGenEntry_d icn_gen_stack[ICN_GEN_MAX];
static int           icn_gen_depth = 0;
static void icn_gen_push(EXPR_t *n, long v, const char *sv) { if(icn_gen_depth<ICN_GEN_MAX){icn_gen_stack[icn_gen_depth].node=n;icn_gen_stack[icn_gen_depth].cur=v;icn_gen_stack[icn_gen_depth].sval=sv;icn_gen_depth++;} }
static void icn_gen_pop(void)               { if(icn_gen_depth>0)icn_gen_depth--; }
static int  icn_gen_lookup(EXPR_t *n, long *out) { for(int i=icn_gen_depth-1;i>=0;i--) if(icn_gen_stack[i].node==n){*out=icn_gen_stack[i].cur;return 1;} return 0; }
static int  icn_gen_lookup_sv(EXPR_t *n, long *out, const char **sv) { for(int i=icn_gen_depth-1;i>=0;i--) if(icn_gen_stack[i].node==n){*out=icn_gen_stack[i].cur;*sv=icn_gen_stack[i].sval;return 1;} return 0; }
static int  icn_gen_active(EXPR_t *n)            { for(int i=0;i<icn_gen_depth;i++) if(icn_gen_stack[i].node==n) return 1; return 0; }

/* Icon scan state globals */
#define ICN_SCAN_STACK_MAX 16
static const char *icn_scan_subj  = NULL;
static int         icn_scan_pos   = 0;
static struct { const char *subj; int pos; } icn_scan_stack[ICN_SCAN_STACK_MAX];
static int         icn_scan_depth = 0;
static int         icn_loop_break = 0; /* E_LOOP_BREAK signal for repeat/while/until */

/* S-11: ucontext-based coroutine for suspend/resume.
 * Each generator proc call gets its own stack via makecontext/swapcontext.
 * The generator swapcontext's back to the caller on each yield, and the caller
 * swapcontext's back to resume. No stack-smash risk — each side has its own stack. */
#define ICN_CORO_STACK  (256*1024)   /* 256KB stack per generator */
#define ICN_CORO_MAX    32
typedef struct {
    EXPR_t         *call_node;       /* E_FNC call node — unique key per call site */
    ucontext_t      gen_ctx;         /* generator's execution context */
    ucontext_t      caller_ctx;      /* caller's context (restored on yield) */
    char           *stack;           /* generator stack (GC_malloc'd) */
    DESCR_t         yielded;         /* value from generator → caller */
    int             exhausted;       /* 1 = generator finished */
    int             active;          /* 1 = slot in use */
    /* Current args/proc for the trampoline */
    EXPR_t         *proc;
    DESCR_t         args[ICN_SLOT_MAX];
    int             nargs;
} Icn_coro_entry;
static Icn_coro_entry icn_coro_table[ICN_CORO_MAX];
static int            icn_coro_count = 0;
static Icn_coro_entry *icn_cur_coro  = NULL; /* coro currently running */

static Icn_coro_entry *icn_coro_find(EXPR_t *call_node) {
    for (int i = 0; i < icn_coro_count; i++)
        if (icn_coro_table[i].call_node == call_node && icn_coro_table[i].active)
            return &icn_coro_table[i];
    return NULL;
}
static void icn_coro_clear(EXPR_t *call_node) {
    for (int i = 0; i < icn_coro_count; i++)
        if (icn_coro_table[i].call_node == call_node) { icn_coro_table[i].active = 0; return; }
}

static int icn_has_suspend(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_SUSPEND) return 1;
    for (int i = 0; i < e->nchildren; i++) if (icn_has_suspend(e->children[i])) return 1;
    return 0;
}
static int icn_has_suspend_call(EXPR_t *e) {
    if (!e) return 0;
    if (e->kind == E_FNC && e->nchildren >= 1 && e->children[0] && e->children[0]->sval) {
        const char *fn = e->children[0]->sval;
        for (int i = 0; i < icn_proc_count; i++)
            if (strcmp(icn_proc_table[i].name, fn) == 0 && icn_has_suspend(icn_proc_table[i].proc))
                return 1;
    }
    for (int i = 0; i < e->nchildren; i++) if (icn_has_suspend_call(e->children[i])) return 1;
    return 0;
}

static DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs); /* forward */
static DESCR_t  interp_eval(EXPR_t *e); /* forward — needed by icn_drive + trampoline */
static Term    *pl_unified_term_from_expr(EXPR_t *e, Term **env); /* forward */
static Term   **pl_env_new(int n); /* forward */
static EXPR_t  *pl_pred_table_lookup(Pl_PredTable *pt, const char *key); /* forward */
static int      is_pl_user_call(EXPR_t *goal); /* forward */
static int      pl_unified_exec_goal(EXPR_t *goal, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag); /* forward */

/* ── Prolog global execution state ─────────────────────────────────────────
 * Initialised by pl_execute_program_unified() at program start. */
static Pl_PredTable g_pl_pred_table;
static Trail        g_pl_trail;
static int          g_pl_cut_flag = 0;
static Term       **g_pl_env      = NULL;
static int          g_pl_active   = 0;

/* Trampoline: entry point for generator coroutine stack */
static void icn_coro_trampoline(void) {
    Icn_coro_entry *ce = icn_cur_coro;
    int nparams = (int)ce->proc->ival, bs = 1+nparams, nb = ce->proc->nchildren-bs;
    for (int i = 0; i < nparams && i < ICN_SLOT_MAX; i++) {
        EXPR_t *pn = ce->proc->children[1+i];
        if (pn && pn->sval) NV_SET_fn(pn->sval, (i < ce->nargs) ? ce->args[i] : NULVCL);
    }
    int saved_ret = icn_returning; icn_returning = 0;
    for (int i = 0; i < nb && !icn_returning; i++) {
        EXPR_t *st = ce->proc->children[bs+i];
        if (!st || st->kind == E_GLOBAL) continue;
        interp_eval(st);
    }
    icn_returning = saved_ret;
    ce->exhausted = 1;
    swapcontext(&ce->gen_ctx, &ce->caller_ctx);
}

/* icn_drive: drive generators embedded in e, re-executing root each tick.
 * Returns tick count. Mirrors icn_exec_driven in icon_interp.c but uses DESCR_t. */
static int icn_drive(EXPR_t *root, EXPR_t *e) {
    if (!e) return 0;
    if (icn_gen_active(e)) return 0;
    if (e->kind == E_TO && e->nchildren >= 2) {
        int ticks = 0;
        /* Both lo and hi may be generators — collect all values, cross-product. */
        EXPR_t *lo_node = e->children[0];
        EXPR_t *hi_node = e->children[1];
        long lo_vals[256]; int nlo = 0;
        long hi_vals[256]; int nhi = 0;
        /* Collect lo values */
        if (lo_node->kind == E_TO && lo_node->nchildren >= 2) {
            DESCR_t llo = interp_eval(lo_node->children[0]);
            DESCR_t lhi = interp_eval(lo_node->children[1]);
            if (!IS_FAIL_fn(llo) && !IS_FAIL_fn(lhi))
                for (long v = llo.i; v <= lhi.i && nlo < 256; v++) lo_vals[nlo++] = v;
        } else {
            DESCR_t lo_d = interp_eval(lo_node);
            if (!IS_FAIL_fn(lo_d)) lo_vals[nlo++] = lo_d.i;
        }
        /* Collect hi values */
        if (hi_node->kind == E_TO && hi_node->nchildren >= 2) {
            DESCR_t hlo = interp_eval(hi_node->children[0]);
            DESCR_t hhi = interp_eval(hi_node->children[1]);
            if (!IS_FAIL_fn(hlo) && !IS_FAIL_fn(hhi))
                for (long v = hlo.i; v <= hhi.i && nhi < 256; v++) hi_vals[nhi++] = v;
        } else {
            DESCR_t hi_d = interp_eval(hi_node);
            if (!IS_FAIL_fn(hi_d)) hi_vals[nhi++] = hi_d.i;
        }
        if (nlo == 0 || nhi == 0) return 0;
        /* Cross-product: for each lo tick, for each hi tick, iterate lo..hi */
        for (int li = 0; li < nlo && !icn_returning; li++) {
            long lo = lo_vals[li];
            for (int hi_idx = 0; hi_idx < nhi && !icn_returning; hi_idx++) {
                long hi = hi_vals[hi_idx];
                for (long i = lo; i <= hi && !icn_returning; i++) {
                    icn_gen_push(e, i, NULL);
                    int inner = icn_drive(root, root);
                    if (!inner) interp_eval(root);
                    icn_gen_pop(); ticks++;
                    if (icn_returning) break;
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
        if(st>0){for(long i=lo;i<=hi&&!icn_returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root,root);if(!inner)interp_eval(root);icn_gen_pop();ticks++;if(icn_returning)break;}}
        else    {for(long i=lo;i>=hi&&!icn_returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root,root);if(!inner)interp_eval(root);icn_gen_pop();ticks++;if(icn_returning)break;}}
        return ticks;
    }
    /* S-6: E_ITERATE (!str) — generate each character of a string */
    if (e->kind == E_ITERATE && e->nchildren >= 1) {
        DESCR_t sv_d = interp_eval(e->children[0]);
        if (IS_FAIL_fn(sv_d) || !IS_STR_fn(sv_d)) return 0;
        const char *str = sv_d.s ? sv_d.s : "";
        long len = (long)strlen(str); int ticks = 0;
        for (long i = 0; i < len && !icn_returning; i++) {
            icn_gen_push(e, i, str);
            int inner = icn_drive(root, root);
            if (!inner) interp_eval(root);
            icn_gen_pop(); ticks++;
            if (icn_returning) break;
        }
        return ticks;
    }
    /* S-7: find(pat,str) as generator — successive 1-based positions.
     * ICN_CALL nodes have sval=NULL; name lives in children[0]->sval. */
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
        while (!icn_returning) {
            char *hit = strstr(p, needle);
            if (!hit) break;
            long pos1 = (long)(hit - hay) + 1;  /* 1-based */
            icn_gen_push(e, pos1, NULL);
            int inner = icn_drive(root, root);
            if (!inner) interp_eval(root);
            icn_gen_pop(); ticks++;
            if (icn_returning) break;
            p = hit + (nlen > 0 ? nlen : 1);    /* advance past this match */
        }
        return ticks;
    }
    for(int i=0;i<e->nchildren;i++){int t=icn_drive(root,e->children[i]);if(t>0)return t;}
    return 0;
}

/* icn_interp_eval: thin delegator — all Icon evaluation now in interp_eval. */
static DESCR_t icn_interp_eval(EXPR_t *root, EXPR_t *e) {
    g_icn_root = root;
    return interp_eval(e);
}

/* icn_collect_names: walk proc body to collect all E_VAR names into names[].
 * Returns count. Used by icn_call_proc to build save/restore list. */
static int icn_collect_names(EXPR_t *e, const char **names, int *n, int max) {
    if (!e) return 0;
    if (e->kind == E_VAR && e->sval) {
        for (int i = 0; i < *n; i++) if (strcmp(names[i], e->sval) == 0) return 0;
        if (*n < max) names[(*n)++] = e->sval;
    }
    for (int i = 0; i < e->nchildren; i++) icn_collect_names(e->children[i], names, n, max);
    return 0;
}

/* icn_call_proc: call an Icon procedure using name-based NV store.
 * Saves NV values for all params/locals, installs new values, executes body,
 * then restores saved values. No slot arrays — pure name/NV. */
static DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs) {
    int nparams    = (int)proc->ival;
    int body_start = 1 + nparams;
    int nbody      = proc->nchildren - body_start;

    /* Collect all variable names used in this procedure */
    const char *names[ICN_SLOT_MAX];
    int nnames = 0;

    /* Params first (in order) */
    for (int i = 0; i < nparams && i < ICN_SLOT_MAX; i++) {
        EXPR_t *pn = proc->children[1+i];
        if (pn && pn->sval) {
            int dup = 0;
            for (int j = 0; j < nnames; j++) if (strcmp(names[j], pn->sval)==0) { dup=1; break; }
            if (!dup) names[nnames++] = pn->sval;
        }
    }
    /* Locals from E_GLOBAL decls, then any undeclared vars in body */
    for (int i = 0; i < nbody; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (!st) continue;
        if (st->kind == E_GLOBAL)
            for (int j = 0; j < st->nchildren; j++)
                if (st->children[j] && st->children[j]->sval) {
                    const char *nm = st->children[j]->sval;
                    int dup = 0;
                    for (int k = 0; k < nnames; k++) if (strcmp(names[k],nm)==0){dup=1;break;}
                    if (!dup && nnames < ICN_SLOT_MAX) names[nnames++] = nm;
                }
        icn_collect_names(st, names, &nnames, ICN_SLOT_MAX);
    }

    /* Save current NV values for all names */
    DESCR_t saved[ICN_SLOT_MAX];
    for (int i = 0; i < nnames; i++) saved[i] = NV_GET_fn(names[i]);

    /* Install params; zero-init locals */
    for (int i = 0; i < nnames; i++) {
        DESCR_t val = (i < nparams && i < nargs) ? args[i] : NULVCL;
        NV_SET_fn(names[i], val);
    }

    /* Save/reset return state */
    int saved_ret = icn_returning;
    icn_returning = 0;

    /* Execute body */
    DESCR_t result = NULVCL;
    for (int i = 0; i < nbody && !icn_returning; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (!st || st->kind == E_GLOBAL) continue;
        result = interp_eval(st);
    }
    if (icn_returning) result = icn_return_val;

    /* Restore all saved NV values */
    for (int i = 0; i < nnames; i++) NV_SET_fn(names[i], saved[i]);
    icn_returning = saved_ret;
    return result;
}

/* icn_execute_program_unified: entry point for Icon via unified interpreter.
 * Replaces icon_execute_program. Builds proc table, calls main/0. */
static void icn_execute_program_unified(Program *prog) {
    icn_proc_count = 0;
    icn_returning = 0;
    icn_gen_depth = 0;
    icn_scan_subj = NULL; icn_scan_pos = 0; icn_scan_depth = 0;
    icn_loop_break = 0; g_icn_root = NULL;
    icn_coro_count = 0; icn_cur_coro = NULL;

    /* Build procedure table from STMT_t subjects */
    for (STMT_t *st = prog->head; st; st = st->next) {
        EXPR_t *proc = st->subject;
        if (!proc || proc->kind != E_FNC || proc->nchildren < 1) continue;
        const char *name = proc->children[0]->sval;
        if (!name || icn_proc_count >= ICN_PROC_MAX) continue;
        icn_proc_table[icn_proc_count].name = name;
        icn_proc_table[icn_proc_count].proc = proc;
        icn_proc_count++;
    }

    /* Find and call main */
    for (int i = 0; i < icn_proc_count; i++) {
        if (strcmp(icn_proc_table[i].name, "main") == 0) {
            icn_call_proc(icn_proc_table[i].proc, NULL, 0);
            return;
        }
    }
    fprintf(stderr, "icon: no main procedure\n");
}

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
            /* Icon: &keyword lowered as E_VAR("&name") — dispatch to keyword logic */
            if (e->sval[0] == '&' && e->sval[1]) {
                const char *kw = e->sval + 1;
                if (strcmp(kw,"subject")==0) return icn_scan_subj ? STRVAL(icn_scan_subj) : NULVCL;
                if (strcmp(kw,"pos")    ==0) return INTVAL(icn_scan_pos);
                return NV_GET_fn(kw);
            }
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
        /* Icon scan keywords live in icn_scan globals, not the NV store */
        if (strcmp(e->sval,"subject")==0)
            return icn_scan_subj ? STRVAL(icn_scan_subj) : NULVCL;
        if (strcmp(e->sval,"pos")==0) return INTVAL(icn_scan_pos);
        /* All other keywords stored without & prefix in NV store */
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
        /* Icon E_FNC: sval is NULL; name is in children[0]->sval. */
        if (!e->sval && e->nchildren >= 1 && e->children[0] && e->children[0]->sval) {
            const char *fn = e->children[0]->sval;
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
            if (!strcmp(fn,"any") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t cs = interp_eval(e->children[1]);
                const char *s=icn_scan_subj, *cv=VARVAL_fn(cs); int p=icn_scan_pos-1;
                if (!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
                icn_scan_pos++; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"many") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t cs = interp_eval(e->children[1]);
                const char *s=icn_scan_subj, *cv=VARVAL_fn(cs); int p=icn_scan_pos-1;
                if (!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
                while(p<(int)strlen(s)&&strchr(cv,s[p])) p++;
                icn_scan_pos=p+1; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"upto") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t cs = interp_eval(e->children[1]);
                const char *s=icn_scan_subj, *cv=VARVAL_fn(cs); if (!s||!cv) return FAILDESCR;
                int p=icn_scan_pos-1;
                while(p<(int)strlen(s)&&!strchr(cv,s[p])) p++;
                if(p>=(int)strlen(s)) return FAILDESCR;
                icn_scan_pos=p+1; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"move") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t nv = interp_eval(e->children[1]); int newp=icn_scan_pos+(int)nv.i;
                if (!icn_scan_subj||newp<1||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
                int old=icn_scan_pos; icn_scan_pos=newp; size_t len=(size_t)nv.i;
                char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"tab") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t nv = interp_eval(e->children[1]); int newp=(int)nv.i;
                if (!icn_scan_subj||newp<icn_scan_pos||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
                int old=icn_scan_pos; icn_scan_pos=newp; size_t len=(size_t)(newp-old);
                char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
                return STRVAL(buf);
            }
            if (!strcmp(fn,"match") && nargs >= 1 && icn_scan_pos > 0) {
                DESCR_t sv = interp_eval(e->children[1]);
                const char *needle=VARVAL_fn(sv), *hay=icn_scan_subj?icn_scan_subj:"";
                if (!needle) return FAILDESCR;
                int p=icn_scan_pos-1, nl=(int)strlen(needle);
                if (strncmp(hay+p,needle,nl)!=0) return FAILDESCR;
                icn_scan_pos+=nl; return INTVAL(icn_scan_pos);
            }
            if (!strcmp(fn,"find") && nargs >= 2) {
                long pos1; if (icn_gen_lookup(e, &pos1)) return INTVAL(pos1);
                DESCR_t s1=interp_eval(e->children[1]), s2=interp_eval(e->children[2]);
                const char *needle=VARVAL_fn(s1), *hay=VARVAL_fn(s2);
                if (!needle||!hay) return FAILDESCR;
                char *p = strstr(hay, needle);
                return p ? INTVAL((long long)(p-hay)+1) : FAILDESCR;
            }
            /* User Icon procedure */
            for (int i = 0; i < icn_proc_count; i++) {
                if (strcmp(icn_proc_table[i].name, fn) == 0) {
                    EXPR_t *proc = icn_proc_table[i].proc;
                    /* S-11: generator proc — use ucontext coroutine */
                    if (icn_has_suspend(proc)) {
                        Icn_coro_entry *ce = icn_coro_find(e);
                        if (!ce) {
                            /* Allocate new coro slot */
                            if (icn_coro_count >= ICN_CORO_MAX) return FAILDESCR;
                            ce = &icn_coro_table[icn_coro_count++];
                            ce->call_node = e; ce->active = 1; ce->exhausted = 0;
                            ce->proc = proc; ce->nargs = nargs;
                            for (int j = 0; j < nargs && j < ICN_SLOT_MAX; j++)
                                ce->args[j] = interp_eval(e->children[j+1]);
                            /* Set up new stack and context */
                            ce->stack = GC_malloc(ICN_CORO_STACK);
                            getcontext(&ce->gen_ctx);
                            ce->gen_ctx.uc_stack.ss_sp   = ce->stack;
                            ce->gen_ctx.uc_stack.ss_size  = ICN_CORO_STACK;
                            ce->gen_ctx.uc_link           = NULL;
                            makecontext(&ce->gen_ctx, icn_coro_trampoline, 0);
                        }
                        if (ce->exhausted) { icn_coro_clear(e); return FAILDESCR; }
                        /* Switch into generator */
                        Icn_coro_entry *prev = icn_cur_coro; icn_cur_coro = ce;
                        swapcontext(&ce->caller_ctx, &ce->gen_ctx);
                        icn_cur_coro = prev;
                        if (ce->exhausted) { icn_coro_clear(e); return FAILDESCR; }
                        return ce->yielded;
                    }
                    DESCR_t icn_args[ICN_SLOT_MAX];
                    for (int j = 0; j < nargs && j < ICN_SLOT_MAX; j++)
                        icn_args[j] = interp_eval(e->children[j+1]);
                    return icn_call_proc(icn_proc_table[i].proc, icn_args, nargs);
                }
            }
            return NULVCL;
        }
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
    case E_FENCE:
        if (e->nchildren >= 1) { DESCR_t inner = interp_eval(e->children[0]); return pat_fence_p(inner); }
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
        DESCR_t inner = interp_eval(e->children[0]);
        return pat_arbno(inner);
    }

    /* ── Icon-only nodes ────────────────────────────────────────────────────
     * These opcodes are emitted only by the Icon frontend. Variable access
     * uses the NV store by name — icn_call_proc saves/restores NV around calls. */

    case E_CSET:
        return e->sval ? STRVAL(e->sval) : NULVCL;

    case E_TO: case E_TO_BY: {
        long cur;
        if (icn_gen_lookup(e, &cur)) return INTVAL(cur);
        return (e->nchildren >= 1) ? interp_eval(e->children[0]) : NULVCL;
    }

    case E_EVERY: {
        if (e->nchildren < 1) return NULVCL;
        EXPR_t *gen  = e->children[0];
        EXPR_t *body = (e->nchildren > 1) ? e->children[1] : NULL;
        if (body && gen->kind == E_TO && gen->nchildren >= 2) {
            DESCR_t lo_d = interp_eval(gen->children[0]);
            DESCR_t hi_d = interp_eval(gen->children[1]);
            if (IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)) return NULVCL;
            for (long i=lo_d.i; i<=hi_d.i && !icn_returning; i++) interp_eval(body);
            return NULVCL;
        }
        /* S-11: check if gen tree contains a suspend-based generator proc call.
         * If so, loop calling interp_eval(gen) until it returns FAILDESCR. */
        int has_gen = icn_has_suspend_call(gen);
        if (has_gen) {
            while (!icn_returning && !icn_loop_break) {
                DESCR_t v = interp_eval(gen);
                if (IS_FAIL_fn(v)) break;
                if (body) interp_eval(body);
            }
            return NULVCL;
        }
        int ticks = icn_drive(gen, gen);
        if (!ticks) interp_eval(gen);
        return NULVCL;
    }

    case E_WHILE: {
        int sb = icn_loop_break; icn_loop_break = 0;
        while (!icn_returning && !icn_loop_break &&
               !IS_FAIL_fn(interp_eval(e->children[0])))
            if (e->nchildren > 1) interp_eval(e->children[1]);
        icn_loop_break = sb; return NULVCL;
    }

    case E_UNTIL: {
        int sb = icn_loop_break; icn_loop_break = 0;
        while (!icn_returning && !icn_loop_break) {
            DESCR_t cv = (e->nchildren > 0) ? interp_eval(e->children[0]) : FAILDESCR;
            if (!IS_FAIL_fn(cv)) break;
            if (e->nchildren > 1) interp_eval(e->children[1]);
        }
        icn_loop_break = sb; return NULVCL;
    }

    case E_REPEAT: {
        int sb = icn_loop_break; icn_loop_break = 0;
        while (!icn_returning && !icn_loop_break)
            if (e->nchildren > 0) interp_eval(e->children[0]);
        icn_loop_break = sb; return NULVCL;
    }

    case E_IF: {
        if (e->nchildren < 1) return NULVCL;
        DESCR_t cv = interp_eval(e->children[0]);
        if (!IS_FAIL_fn(cv))
            return (e->nchildren > 1) ? interp_eval(e->children[1]) : cv;
        return (e->nchildren > 2) ? interp_eval(e->children[2]) : FAILDESCR;
    }

    case E_RETURN: {
        icn_return_val = (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
        icn_returning = 1; return icn_return_val;
    }

    case E_LOOP_BREAK: {
        icn_loop_break = 1;
        return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
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
        if (lhs->kind == E_VAR && lhs->sval) NV_SET_fn(lhs->sval, result);
        return result;
    }

    case E_ALTERNATE: {
        /* Icon | — value alternation: try left, if FAIL try right */
        for (int i = 0; i < e->nchildren; i++) {
            DESCR_t v = interp_eval(e->children[i]);
            if (!IS_FAIL_fn(v)) return v;
        }
        return FAILDESCR;
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

    case E_SUSPEND: {
        /* S-11: yield to caller via swapcontext. icn_cur_coro must be set. */
        if (!icn_cur_coro) {
            return (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
        }
        DESCR_t val = (e->nchildren > 0) ? interp_eval(e->children[0]) : NULVCL;
        if (IS_FAIL_fn(val)) { icn_cur_coro->exhausted = 1; swapcontext(&icn_cur_coro->gen_ctx, &icn_cur_coro->caller_ctx); return FAILDESCR; }
        icn_cur_coro->yielded = val;
        /* Yield to caller; resume here when caller swapcontext's back */
        swapcontext(&icn_cur_coro->gen_ctx, &icn_cur_coro->caller_ctx);
        /* Resumed — run 'do body' if present */
        if (e->nchildren > 1) interp_eval(e->children[1]);
        return NULVCL;
    }

    /* ── Prolog IR nodes — S-1C-2/3 ───────────────────────────────────────
     * Only reached when g_pl_active is set.
     * E_UNIFY/E_CUT/E_TRAIL_* are leaf goal nodes.
     * E_CHOICE iterates clauses with backtracking via g_pl_trail.
     * E_CLAUSE unifies head args from g_pl_env, runs body via interp_eval. */
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
        /* Clauses are iterated inline by E_CHOICE — never dispatched standalone. */
        return NULVCL;

    case E_CHOICE: {
        /* Iterate clauses of a predicate with backtracking.
         * e->sval = "functor/arity", e->children[i] = E_CLAUSE nodes.
         * Caller has placed Term** args in g_pl_env (arity slots). */
        if (!g_pl_active) return NULVCL;
        int nclauses = e->nchildren;
        /* parse arity from sval "name/N" */
        int arity = 0;
        if (e->sval) { const char *sl = strrchr(e->sval, '/'); if (sl) arity = atoi(sl+1); }
        int mark = trail_mark(&g_pl_trail);
        int saved_cut = g_pl_cut_flag; g_pl_cut_flag = 0;
        int result = 0;
        for (int ci = 0; ci < nclauses && !g_pl_cut_flag; ci++) {
            if (ci > 0) trail_unwind(&g_pl_trail, mark);
            EXPR_t *ec = e->children[ci];
            if (!ec || ec->kind != E_CLAUSE) continue;
            int n_vars = (int)ec->ival;
            Term **cenv = pl_env_new(n_vars);
            /* unify head args */
            int head_mark = trail_mark(&g_pl_trail); int head_ok = 1;
            for (int i = 0; i < arity && i < ec->nchildren; i++) {
                Term *ha = pl_unified_term_from_expr(ec->children[i], cenv);
                Term *ca = (g_pl_env && i < arity) ? g_pl_env[i] : term_new_var(i);
                if (!unify(ca, ha, &g_pl_trail)) { head_ok = 0; break; }
            }
            if (!head_ok) { trail_unwind(&g_pl_trail, head_mark); free(cenv); continue; }
            /* run body — E_FNC (Prolog builtins/user calls) via pl_unified_exec_goal,
             * structural nodes (E_UNIFY, E_CUT, E_TRAIL_*) via interp_eval */
            Term **saved_env = g_pl_env; g_pl_env = cenv;
            int clause_cut = 0; int saved_cf = g_pl_cut_flag; g_pl_cut_flag = 0;
            int ok = 1;
            for (int i = arity; i < ec->nchildren && ok && !g_pl_cut_flag; i++) {
                EXPR_t *goal = ec->children[i];
                if (!goal) continue;
                /* All Prolog goal nodes now go through interp_eval:
                 * E_UNIFY/E_CUT/E_TRAIL_* handled by cases above.
                 * E_CHOICE handled by case above (user predicate call).
                 * E_FNC: builtins and user calls — route through pl_unified_exec_goal
                 * for now; S-1C-4 final step will inline builtins into interp_eval. */
                if (goal->kind == E_FNC) {
                    /* user-defined predicate: look up E_CHOICE, call interp_eval */
                    if (is_pl_user_call(goal)) {
                        char key[256]; snprintf(key, sizeof key, "%s/%d", goal->sval ? goal->sval : "", goal->nchildren);
                        EXPR_t *choice = pl_pred_table_lookup(&g_pl_pred_table, key);
                        if (!choice) { fprintf(stderr, "prolog: undefined predicate %s\n", key); ok = 0; continue; }
                        /* build caller arg terms into a fresh env slot for E_CHOICE */
                        int carity = goal->nchildren;
                        Term **call_args = carity ? malloc(carity * sizeof(Term *)) : NULL;
                        for (int a = 0; a < carity; a++) call_args[a] = pl_unified_term_from_expr(goal->children[a], cenv);
                        Term **saved_env2 = g_pl_env; g_pl_env = call_args;
                        DESCR_t r = interp_eval(choice);
                        g_pl_env = saved_env2;
                        if (call_args) free(call_args);
                        if (IS_FAIL_fn(r)) ok = 0;
                    } else {
                        /* builtin — still through pl_unified_exec_goal until inlined */
                        if (!pl_unified_exec_goal(goal, cenv, &g_pl_pred_table, &g_pl_trail, &g_pl_cut_flag))
                            ok = 0;
                    }
                } else {
                    DESCR_t r = interp_eval(goal);
                    if (IS_FAIL_fn(r)) ok = 0;
                }
            }
            clause_cut = g_pl_cut_flag;
            g_pl_env = saved_env; g_pl_cut_flag = saved_cf;
            free(cenv);
            if (ok) { result = 1; if (clause_cut) g_pl_cut_flag = 1; break; }
            if (clause_cut) break;
        }
        if (!result) { trail_unwind(&g_pl_trail, mark); g_pl_cut_flag = saved_cut; }
        return result ? INTVAL(1) : FAILDESCR;
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
 * pl_execute_program_unified — Prolog IR interpreter (Phase 1B)
 * All logic moved from prolog_interp.c into scrip.c.
 * prolog_interp.c and prolog_interp.h are deleted after this.
 * ══════════════════════════════════════════════════════════════════════════ */

/*---- Predicate table — typedefs hoisted to file scope (see forward decls above) ----*/
#define PL_PRED_TABLE_SIZE PL_PRED_TABLE_SIZE_FWD

static unsigned pl_pred_hash(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h % PL_PRED_TABLE_SIZE;
}
static void pl_pred_table_insert(Pl_PredTable *pt, const char *key, EXPR_t *choice) {
    unsigned h = pl_pred_hash(key);
    Pl_PredEntry *e = malloc(sizeof(Pl_PredEntry));
    e->key = key; e->choice = choice; e->next = pt->buckets[h]; pt->buckets[h] = e;
}
static EXPR_t *pl_pred_table_lookup(Pl_PredTable *pt, const char *key) {
    for (Pl_PredEntry *e = pt->buckets[pl_pred_hash(key)]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e->choice;
    return NULL;
}

/*---- Choice point stack ----*/
#define PL_CP_STACK_MAX 4096
typedef struct {
    jmp_buf     jb;
    Pl_PredTable *pt;
    const char *key;
    int         arity;
    Trail      *trail;
    int         trail_mark;
    int         next_clause;
    int         cut;
} Pl_ChoicePoint;
static Pl_ChoicePoint pl_cp_stack[PL_CP_STACK_MAX];
static int            pl_cp_top = 0;

static Term **pl_env_new(int n) {
    if (n <= 0) return NULL;
    Term **env = malloc(n * sizeof(Term *));
    for (int i = 0; i < n; i++) env[i] = term_new_var(i);
    return env;
}

/*---- Continuation type ----*/
typedef struct Pl_Cont Pl_Cont;
struct Pl_Cont { int (*fn)(Pl_Cont *self); };
static int pl_cont_done_fn(Pl_Cont *self) { (void)self; return 1; }
static Pl_Cont pl_cont_done_val = { pl_cont_done_fn };
static Pl_Cont *pl_cont_done = &pl_cont_done_val;

/*---- Forward declarations ----*/
static Term *pl_unified_term_from_expr(EXPR_t *e, Term **env);
static Term *pl_unified_deep_copy(Term *t);
static int pl_unified_exec_goal(EXPR_t *goal, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag);
static int pl_unified_exec_body(EXPR_t **goals, int ngoals, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag);
static int pl_unified_exec_body_k(EXPR_t **goals, int ngoals, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag, Pl_Cont *cont);
static int pl_unified_call(Pl_PredTable *pt, const char *key, int arity, Term **args, Trail *trail, int start);
static int pl_unified_exec_clause(EXPR_t *ec, int n_args, Term **call_args, Pl_PredTable *pt, Trail *trail, int *cut_flag);

/*---- findall support ----*/
typedef struct {
    Pl_Cont     base;
    EXPR_t     *tmpl_expr;
    Term      **env;
    Term      **solutions;
    int         nsol;
    int         sol_cap;
} Pl_Findall_cont;

static int pl_findall_cont_fn(Pl_Cont *self) {
    Pl_Findall_cont *fc = (Pl_Findall_cont *)self;
    Term *snap = pl_unified_deep_copy(pl_unified_term_from_expr(fc->tmpl_expr, fc->env));
    if (fc->nsol >= fc->sol_cap) {
        fc->sol_cap = fc->sol_cap ? fc->sol_cap * 2 : 8;
        fc->solutions = realloc(fc->solutions, fc->sol_cap * sizeof(Term *));
    }
    fc->solutions[fc->nsol++] = snap;
    return 0;
}

/*---- pl_unified_term_from_expr ----*/
static Term *pl_unified_term_from_expr(EXPR_t *e, Term **env) {
    if (!e) return term_new_atom(prolog_atom_intern("[]"));
    switch (e->kind) {
        case E_QLIT: return term_new_atom(prolog_atom_intern(e->sval ? e->sval : ""));
        case E_ILIT: return term_new_int((long)e->ival);
        case E_FLIT: return term_new_float(e->dval);
        case E_VAR:  return (env && e->ival >= 0) ? env[e->ival] : term_new_var(e->ival);
        case E_ADD: case E_SUB: case E_MUL: case E_DIV: case E_MOD: {
            /* arithmetic ops used as terms (e.g. K-V): wrap as compound */
            const char *op = e->kind==E_ADD?"+":e->kind==E_SUB?"-":e->kind==E_MUL?"*":e->kind==E_DIV?"/":"%";
            int atom = prolog_atom_intern(op);
            Term *args2[2]; args2[0]=pl_unified_term_from_expr(e->children[0],env); args2[1]=pl_unified_term_from_expr(e->children[1],env);
            return term_new_compound(atom, 2, args2);
        }
        case E_FNC: {
            int arity = e->nchildren;
            int atom  = prolog_atom_intern(e->sval ? e->sval : "f");
            if (arity == 0) return term_new_atom(atom);
            Term **args = malloc(arity * sizeof(Term *));
            for (int i = 0; i < arity; i++) args[i] = pl_unified_term_from_expr(e->children[i], env);
            Term *t = term_new_compound(atom, arity, args);
            free(args);
            return t;
        }
        default: return term_new_atom(prolog_atom_intern("?"));
    }
}

/*---- pl_unified_deep_copy ----*/
static Term *pl_unified_deep_copy(Term *t) {
    t = term_deref(t);
    if (!t || t->tag == TT_VAR) return term_new_atom(prolog_atom_intern("_"));
    if (t->tag == TT_ATOM)  return term_new_atom(t->atom_id);
    if (t->tag == TT_INT)   return term_new_int(t->ival);
    if (t->tag == TT_FLOAT) return term_new_float(t->fval);
    if (t->tag == TT_COMPOUND) {
        Term **args = malloc(t->compound.arity * sizeof(Term *));
        for (int i = 0; i < t->compound.arity; i++) args[i] = pl_unified_deep_copy(t->compound.args[i]);
        Term *r = term_new_compound(t->compound.functor, t->compound.arity, args);
        free(args);
        return r;
    }
    return term_new_atom(prolog_atom_intern("_"));
}

/*---- pl_unified_eval_arith ----*/
static long pl_unified_eval_arith(EXPR_t *e, Term **env) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ILIT: return (long)e->ival;
        case E_FLIT: return (long)e->dval;
        case E_VAR: { Term *t = term_deref(env && e->ival >= 0 ? env[e->ival] : NULL);
                      return (t && t->tag == TT_INT) ? t->ival : 0; }
        case E_ADD: return pl_unified_eval_arith(e->children[0],env) + pl_unified_eval_arith(e->children[1],env);
        case E_SUB: return pl_unified_eval_arith(e->children[0],env) - pl_unified_eval_arith(e->children[1],env);
        case E_MUL: return pl_unified_eval_arith(e->children[0],env) * pl_unified_eval_arith(e->children[1],env);
        case E_DIV: { long d=pl_unified_eval_arith(e->children[1],env); return d?pl_unified_eval_arith(e->children[0],env)/d:0; }
        case E_MOD: { long d=pl_unified_eval_arith(e->children[1],env); return d?pl_unified_eval_arith(e->children[0],env)%d:0; }
        case E_FNC: {
            const char *fn = e->sval ? e->sval : "";
            if (strcmp(fn,"mod")==0&&e->nchildren==2){long d=pl_unified_eval_arith(e->children[1],env);return d?pl_unified_eval_arith(e->children[0],env)%d:0;}
            if (strcmp(fn,"abs")==0&&e->nchildren==1){long v=pl_unified_eval_arith(e->children[0],env);return v<0?-v:v;}
            if (strcmp(fn,"max")==0&&e->nchildren==2){long a=pl_unified_eval_arith(e->children[0],env),b=pl_unified_eval_arith(e->children[1],env);return a>b?a:b;}
            if (strcmp(fn,"min")==0&&e->nchildren==2){long a=pl_unified_eval_arith(e->children[0],env),b=pl_unified_eval_arith(e->children[1],env);return a<b?a:b;}
            if (strcmp(fn,"rem")==0&&e->nchildren==2){long d=pl_unified_eval_arith(e->children[1],env);return d?pl_unified_eval_arith(e->children[0],env)%d:0;}
            Term *t=term_deref(pl_unified_term_from_expr(e,env));
            return (t&&t->tag==TT_INT)?t->ival:0;
        }
        default: return 0;
    }
}

/*---- is_pl_user_call ----*/
static int is_pl_user_call(EXPR_t *goal) {
    if (!goal || goal->kind != E_FNC || !goal->sval) return 0;
    static const char *builtins[] = {
        "true","fail","halt","nl","write","writeln","print","tab","is",
        "<",">","=<",">=","=:=","=\\=","=","\\=","==","\\==",
        "@<","@>","@=<","@>=",
        "var","nonvar","atom","integer","float","compound","atomic","callable","is_list",
        "functor","arg","=..","\\+","not",",",";","->","findall",
        "assert","assertz","asserta","retract","retractall","abolish",
        NULL
    };
    for (int i = 0; builtins[i]; i++) if (strcmp(goal->sval, builtins[i]) == 0) return 0;
    return 1;
}

/*---- pl_unified_exec_goal ----*/
static int pl_unified_exec_goal(EXPR_t *goal, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag) {
    if (!goal) return 1;
    switch (goal->kind) {
        case E_UNIFY: {
            Term *t1=pl_unified_term_from_expr(goal->children[0],env);
            Term *t2=pl_unified_term_from_expr(goal->children[1],env);
            int mark=trail_mark(trail);
            if (!unify(t1,t2,trail)){trail_unwind(trail,mark);return 0;}
            return 1;
        }
        case E_CUT: if (cut_flag) *cut_flag=1; return 1;
        case E_TRAIL_MARK: case E_TRAIL_UNWIND: return 1;
        case E_FNC: {
            const char *fn = goal->sval ? goal->sval : "true";
            int arity = goal->nchildren;
            if (strcmp(fn,"true")==0&&arity==0) return 1;
            if (strcmp(fn,"fail")==0&&arity==0) return 0;
            if (strcmp(fn,"halt")==0&&arity==0) exit(0);
            if (strcmp(fn,"halt")==0&&arity==1){Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));exit(t&&t->tag==TT_INT?(int)t->ival:0);}
            if (strcmp(fn,"nl")==0&&arity==0){putchar('\n');return 1;}
            if (strcmp(fn,"write")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));return 1;}
            if (strcmp(fn,"writeln")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));putchar('\n');return 1;}
            if (strcmp(fn,"print")==0&&arity==1){pl_write(pl_unified_term_from_expr(goal->children[0],env));return 1;}
            if (strcmp(fn,"tab")==0&&arity==1){
                Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                long n=(t&&t->tag==TT_INT)?t->ival:0;
                for(long i=0;i<n;i++) putchar(' ');
                return 1;
            }
            if (strcmp(fn,"is")==0&&arity==2){
                long val=pl_unified_eval_arith(goal->children[1],env);
                Term *lhs=pl_unified_term_from_expr(goal->children[0],env);
                int mark=trail_mark(trail);
                if(!unify(lhs,term_new_int(val),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* arithmetic comparisons */
            { struct{const char *n;int op;}cmps[]={{"<",0},{">",1},{"=<",2},{">=",3},{"=:=",4},{"=\\=",5},{NULL,0}};
              for(int ci=0;cmps[ci].n;ci++) if(strcmp(fn,cmps[ci].n)==0&&arity==2){
                  long a=pl_unified_eval_arith(goal->children[0],env),b=pl_unified_eval_arith(goal->children[1],env);
                  switch(cmps[ci].op){case 0:return a<b;case 1:return a>b;case 2:return a<=b;case 3:return a>=b;case 4:return a==b;case 5:return a!=b;}
              }
            }
            if (strcmp(fn,"=")==0&&arity==2){
                int mark=trail_mark(trail);
                if(!unify(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            if (strcmp(fn,"\\=")==0&&arity==2){
                int mark=trail_mark(trail);
                int ok=unify(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail);
                trail_unwind(trail,mark);return !ok;
            }
            if (strcmp(fn,"==")==0&&arity==2){
                Term *t1=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_unified_term_from_expr(goal->children[1],env));
                if(!t1||!t2)return t1==t2;
                if(t1->tag!=t2->tag)return 0;
                if(t1->tag==TT_ATOM)return t1->atom_id==t2->atom_id;
                if(t1->tag==TT_INT) return t1->ival==t2->ival;
                if(t1->tag==TT_VAR) return t1==t2;
                return 0;
            }
            if (strcmp(fn,"\\==")==0&&arity==2){
                Term *t1=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                Term *t2=term_deref(pl_unified_term_from_expr(goal->children[1],env));
                if(!t1||!t2)return t1!=t2;
                if(t1->tag!=t2->tag)return 1;
                if(t1->tag==TT_ATOM)return t1->atom_id!=t2->atom_id;
                if(t1->tag==TT_INT) return t1->ival!=t2->ival;
                if(t1->tag==TT_VAR) return t1!=t2;
                return 1;
            }
            /* type tests */
            if (arity==1){
                Term *t=term_deref(pl_unified_term_from_expr(goal->children[0],env));
                if(strcmp(fn,"var"     )==0)return !t||t->tag==TT_VAR;
                if(strcmp(fn,"nonvar"  )==0)return  t&&t->tag!=TT_VAR;
                if(strcmp(fn,"atom"    )==0)return  t&&t->tag==TT_ATOM;
                if(strcmp(fn,"integer" )==0)return  t&&t->tag==TT_INT;
                if(strcmp(fn,"float"   )==0)return  t&&t->tag==TT_FLOAT;
                if(strcmp(fn,"compound")==0)return  t&&t->tag==TT_COMPOUND;
                if(strcmp(fn,"atomic"  )==0)return  t&&(t->tag==TT_ATOM||t->tag==TT_INT||t->tag==TT_FLOAT);
                if(strcmp(fn,"callable")==0)return  t&&(t->tag==TT_ATOM||t->tag==TT_COMPOUND);
                if(strcmp(fn,"is_list" )==0){
                    int nil=prolog_atom_intern("[]"),dot=prolog_atom_intern(".");
                    for(Term *c=t;;){c=term_deref(c);if(!c)return 0;if(c->tag==TT_ATOM&&c->atom_id==nil)return 1;if(c->tag!=TT_COMPOUND||c->compound.arity!=2||c->compound.functor!=dot)return 0;c=c->compound.args[1];}
                }
            }
            /* ,/N conjunction — thread through cont via exec_body */
            if (strcmp(fn,",")==0){
                return pl_unified_exec_body(goal->children,goal->nchildren,env,pt,trail,cut_flag);
            }
            /* ;/N disjunction */
            if (strcmp(fn,";")==0&&arity>=2){
                EXPR_t *left=goal->children[0],*right=goal->children[1];
                if(left&&left->kind==E_FNC&&left->sval&&strcmp(left->sval,"->")==0&&left->nchildren>=2){
                    Trail save=*trail;int mark=trail_mark(trail);int cut2=0;
                    if(pl_unified_exec_goal(left->children[0],env,pt,trail,&cut2)){
                        return pl_unified_exec_body(left->children+1,left->nchildren-1,env,pt,trail,cut_flag);
                    }
                    trail_unwind(trail,mark);*trail=save;
                    return pl_unified_exec_goal(right,env,pt,trail,cut_flag);
                }
                {Trail save=*trail;int mark=trail_mark(trail);int cut2=0;
                 if(pl_unified_exec_goal(left,env,pt,trail,&cut2))return 1;
                 trail_unwind(trail,mark);*trail=save;
                 return pl_unified_exec_goal(right,env,pt,trail,cut_flag);}
            }
            /* ->/N if-then */
            if (strcmp(fn,"->")==0&&arity>=2){
                int cut2=0;
                if(!pl_unified_exec_goal(goal->children[0],env,pt,trail,&cut2))return 0;
                return pl_unified_exec_body(goal->children+1,goal->nchildren-1,env,pt,trail,cut_flag);
            }
            /* \+/not */
            if ((strcmp(fn,"\\+")==0||strcmp(fn,"not")==0)&&arity==1){
                Trail save=*trail;int mark=trail_mark(trail);int cut2=0;
                int ok=pl_unified_exec_goal(goal->children[0],env,pt,trail,&cut2);
                trail_unwind(trail,mark);*trail=save;return !ok;
            }
            /* functor/3 */
            if (strcmp(fn,"functor")==0&&arity==3){
                int mark=trail_mark(trail);
                if(!pl_functor(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),pl_unified_term_from_expr(goal->children[2],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* arg/3 */
            if (strcmp(fn,"arg")==0&&arity==3){
                int mark=trail_mark(trail);
                if(!pl_arg(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),pl_unified_term_from_expr(goal->children[2],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* =../2 */
            if (strcmp(fn,"=..")==0&&arity==2){
                int mark=trail_mark(trail);
                if(!pl_univ(pl_unified_term_from_expr(goal->children[0],env),pl_unified_term_from_expr(goal->children[1],env),trail)){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            /* assert/assertz/asserta — dynamic predicate addition */
            if ((strcmp(fn,"assert")==0||strcmp(fn,"assertz")==0||strcmp(fn,"asserta")==0)&&arity==1){
                /* no dynamic pred support yet — succeed silently */
                return 1;
            }
            /* retract/retractall/abolish — dynamic predicate removal */
            if ((strcmp(fn,"retract")==0||strcmp(fn,"retractall")==0||strcmp(fn,"abolish")==0)&&arity==1){
                return 1;
            }
            /* findall/3 */
            if (strcmp(fn,"findall")==0&&arity==3){
                EXPR_t *tmpl_expr=goal->children[0];
                EXPR_t *goal_expr=goal->children[1];
                EXPR_t *list_expr=goal->children[2];
                Pl_Findall_cont fc;
                fc.base.fn=pl_findall_cont_fn;
                fc.tmpl_expr=tmpl_expr;
                fc.env=env;
                fc.solutions=NULL; fc.nsol=0; fc.sol_cap=0;
                Trail fa_trail; trail_init(&fa_trail);
                int saved_cp=pl_cp_top; int cut2=0;
                /* flatten conjunction into goals array for proper cont threading */
                if (goal_expr&&goal_expr->kind==E_FNC&&goal_expr->sval&&strcmp(goal_expr->sval,",")==0) {
                    pl_unified_exec_body_k(goal_expr->children,goal_expr->nchildren,env,pt,&fa_trail,&cut2,(Pl_Cont*)&fc);
                } else {
                    EXPR_t *goals1[1]; goals1[0]=goal_expr;
                    pl_unified_exec_body_k(goals1,1,env,pt,&fa_trail,&cut2,(Pl_Cont*)&fc);
                }
                pl_cp_top=saved_cp;
                int nil_id=prolog_atom_intern("[]"),dot_id=prolog_atom_intern(".");
                Term *lst=term_new_atom(nil_id);
                for(int i=fc.nsol-1;i>=0;i--){Term *a2[2];a2[0]=fc.solutions[i];a2[1]=lst;lst=term_new_compound(dot_id,2,a2);}
                free(fc.solutions);
                Term *list_term=pl_unified_term_from_expr(list_expr,env);
                int u_mark=trail_mark(trail);
                if(!unify(list_term,lst,trail)){trail_unwind(trail,u_mark);return 0;}
                return 1;
            }
            /* user-defined */
            if (is_pl_user_call(goal)){
                char key[256]; snprintf(key,sizeof key,"%s/%d",fn,arity);
                Term **args=malloc(arity*sizeof(Term*));
                for(int i=0;i<arity;i++) args[i]=pl_unified_term_from_expr(goal->children[i],env);
                int mark=trail_mark(trail);
                int r=pl_unified_call(pt,key,arity,args,trail,0);
                free(args);
                if(r<0){trail_unwind(trail,mark);return 0;}
                return 1;
            }
            fprintf(stderr,"prolog: undefined predicate %s/%d\n",fn,arity);
            return 0;
        }
        default: return 1;
    }
}

/*---- pl_unified_exec_clause ----*/
static int pl_unified_exec_clause(EXPR_t *ec, int n_args, Term **call_args, Pl_PredTable *pt, Trail *trail, int *cut_flag) {
    if(!ec||ec->kind!=E_CLAUSE)return 0;
    int n_vars=(int)ec->ival;
    Term **env=pl_env_new(n_vars);
    int head_mark=trail_mark(trail);
    for(int i=0;i<n_args&&i<ec->nchildren;i++){
        Term *ha=pl_unified_term_from_expr(ec->children[i],env);
        if(!unify(call_args[i],ha,trail)){trail_unwind(trail,head_mark);free(env);return 0;}
    }
    int nbody=ec->nchildren-n_args;
    EXPR_t **body=ec->children+n_args;
    int ok=nbody==0?1:pl_unified_exec_body(body,nbody,env,pt,trail,cut_flag);
    free(env);
    return ok;
}

/*---- pl_unified_call ----*/
static int pl_unified_call(Pl_PredTable *pt, const char *key, int arity, Term **args, Trail *trail, int start) {
    EXPR_t *choice=pl_pred_table_lookup(pt,key);
    if(!choice){fprintf(stderr,"prolog: undefined predicate %s\n",key);return -1;}
    int nclauses=choice->nchildren;
    int mark=trail_mark(trail);
    for(int ci=start;ci<nclauses;ci++){
        if(ci>start)trail_unwind(trail,mark);
        EXPR_t *ec=choice->children[ci];if(!ec)continue;
        int clause_cut=0;
        if(pl_unified_exec_clause(ec,arity,args,pt,trail,&clause_cut))return ci;
        if(clause_cut)break;
    }
    trail_unwind(trail,mark);
    return -1;
}

/*---- Body continuation ----*/
typedef struct {
    Pl_Cont      base;
    EXPR_t     **goals;
    int          ngoals;
    Term       **env;
    Pl_PredTable *pt;
    Trail       *trail;
    int         *cut_flag;
    Pl_Cont     *next;
} Pl_Body_cont;

static int pl_body_cont_fn(Pl_Cont *self) {
    Pl_Body_cont *bc=(Pl_Body_cont *)self;
    return pl_unified_exec_body_k(bc->goals,bc->ngoals,bc->env,bc->pt,bc->trail,bc->cut_flag,bc->next);
}

/*---- pl_unified_exec_body_k ----*/
static int pl_unified_exec_body_k(EXPR_t **goals, int ngoals, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag, Pl_Cont *cont) {
    if(ngoals==0)return cont->fn(cont);
    EXPR_t *g=goals[0];

    if(is_pl_user_call(g)){
        const char *fn=g->sval; int arity=g->nchildren;
        char key[256]; snprintf(key,sizeof key,"%s/%d",fn,arity);
        EXPR_t *choice=pl_pred_table_lookup(pt,key);
        if(!choice){fprintf(stderr,"prolog: undefined predicate %s\n",key);return 0;}
        int nclauses=choice->nchildren;
        if(pl_cp_top>=PL_CP_STACK_MAX){fprintf(stderr,"prolog: CP stack overflow\n");return 0;}
        Pl_ChoicePoint *cp=&pl_cp_stack[pl_cp_top++];
        cp->pt=pt; cp->key=key; cp->arity=arity; cp->trail=trail;
        cp->trail_mark=trail_mark(trail); cp->next_clause=0; cp->cut=0;
        setjmp(cp->jb);
        int result=0;
        while(!cp->cut&&cp->next_clause<nclauses){
            int ci=cp->next_clause;
            trail_unwind(trail,cp->trail_mark);
            Term **args=malloc(arity*sizeof(Term*));
            for(int i=0;i<arity;i++) args[i]=pl_unified_term_from_expr(g->children[i],env);
            cp->next_clause=ci+1;
            EXPR_t *ec=choice->children[ci];
            if(!ec||ec->kind!=E_CLAUSE){free(args);continue;}
            int n_vars=(int)ec->ival;
            Term **cenv=pl_env_new(n_vars);
            int head_mark=trail_mark(trail); int head_ok=1;
            for(int i=0;i<arity&&i<ec->nchildren;i++){
                Term *ha=pl_unified_term_from_expr(ec->children[i],cenv);
                if(!unify(args[i],ha,trail)){head_ok=0;break;}
            }
            free(args);
            if(!head_ok){trail_unwind(trail,head_mark);free(cenv);continue;}
            Pl_Body_cont suffix_bc;
            suffix_bc.base.fn=pl_body_cont_fn;
            suffix_bc.goals=goals+1; suffix_bc.ngoals=ngoals-1;
            suffix_bc.env=env; suffix_bc.pt=pt; suffix_bc.trail=trail;
            suffix_bc.cut_flag=cut_flag; suffix_bc.next=cont;
            int nbody=ec->nchildren-arity;
            EXPR_t **body=ec->children+arity;
            int clause_cut=0; int ok;
            if(nbody==0) ok=suffix_bc.base.fn(&suffix_bc.base);
            else ok=pl_unified_exec_body_k(body,nbody,cenv,pt,trail,&clause_cut,&suffix_bc.base);
            free(cenv);
            if(clause_cut){if(cut_flag)*cut_flag=1;result=ok;break;}
            if(ok){result=1;break;}
        }
        pl_cp_top--;
        return result;
    }
    /* deterministic goal */
    if(!pl_unified_exec_goal(g,env,pt,trail,cut_flag))return 0;
    return pl_unified_exec_body_k(goals+1,ngoals-1,env,pt,trail,cut_flag,cont);
}

static int pl_unified_exec_body(EXPR_t **goals, int ngoals, Term **env, Pl_PredTable *pt, Trail *trail, int *cut_flag) {
    return pl_unified_exec_body_k(goals,ngoals,env,pt,trail,cut_flag,pl_cont_done);
}

/*---- pl_execute_program_unified — entry point ----*/
/* S-1C-3: calls interp_eval() on the main/0 E_CHOICE node directly.
 * pl_unified_call no longer drives top-level execution.
 * User-defined predicate calls within body goals still go through
 * pl_unified_exec_goal → pl_unified_call (S-1C-4 will eliminate those). */
static void pl_execute_program_unified(Program *prog) {
    if (!prog) return;
    prolog_atom_init();
    memset(&g_pl_pred_table, 0, sizeof g_pl_pred_table);
    for (STMT_t *s = prog->head; s; s = s->next) {
        EXPR_t *subj = s->subject;
        if (subj && (subj->kind==E_CHOICE||subj->kind==E_CLAUSE) && subj->sval)
            pl_pred_table_insert(&g_pl_pred_table, subj->sval, subj);
    }
    trail_init(&g_pl_trail);
    g_pl_cut_flag = 0;
    g_pl_env      = NULL;
    g_pl_active   = 1;
    /* Find main/0 E_CHOICE node and dispatch through interp_eval */
    EXPR_t *main_choice = pl_pred_table_lookup(&g_pl_pred_table, "main/0");
    if (main_choice) {
        interp_eval(main_choice);
    } else {
        fprintf(stderr, "prolog: no main/0 predicate\n");
    }
    g_pl_active = 0;
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
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

/* _eval_str_impl_fn — EVAL(string) hook for pattern-context strings.
 * Uses bison parse_expr_pat_from_str (snobol4.tab.c) which produces
 * EXPR_t with correct EKind values directly — no CMPILE/CMPND_t bridge. */
static DESCR_t _eval_str_impl_fn(const char *s) {
    EXPR_t *tree = parse_expr_pat_from_str(s);
    if (!tree) return FAILDESCR;
    return interp_eval_pat(tree);
}

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

/* S-10: forward declarations so _usercall_hook can call them */
static DESCR_t _builtin_IDENT(DESCR_t *args, int nargs);
static DESCR_t _builtin_DIFFER(DESCR_t *args, int nargs);

/* _usercall_hook: calls user functions via call_user_function;
 * for pure builtins (FNCEX_fn && no body label) uses APPLY_fn directly
 * so FAILDESCR propagates correctly (DYN-74: fixes *ident(1,2) in EVAL). */
static DESCR_t _usercall_hook(const char *name, DESCR_t *args, int nargs) {
    /* S-10 fix: handle scrip.c-only predicates directly so *IDENT(x)/*DIFFER(x)
     * in pattern context correctly fail/succeed via bb_usercall -> g_user_call_hook. */
    if (strcasecmp(name, "IDENT") == 0)  return _builtin_IDENT(args, nargs);
    if (strcasecmp(name, "DIFFER") == 0) return _builtin_DIFFER(args, nargs);
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

/* ── S-10 fix: IDENT/DIFFER wrappers for register_fn ──────────────────────
 * IDENT and DIFFER are scrip.c-only builtins not in the binary runtime's
 * APPLY_fn table.  When *IDENT(x) fires at match time via deferred_call_fn,
 * APPLY_fn("IDENT",...) returns non-FAILDESCR for unknown names → T_FUNC
 * always succeeds.  Registering these wrappers makes APPLY_fn dispatch them
 * correctly so *IDENT(n(x)) fails when n(x) is not the null string. */
static DESCR_t _builtin_IDENT(DESCR_t *args, int nargs) {
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
static DESCR_t _builtin_DIFFER(DESCR_t *args, int nargs) {
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
static DESCR_t _builtin_EVAL(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    return EVAL_fn(args[0]);
}
static DESCR_t _builtin_CODE(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    const char *s = VARVAL_fn(args[0]);
    if (!s || !*s) return FAILDESCR;
    return code(s);
}

int main(int argc, char **argv)
{
    /* ── flag parsing ─────────────────────────────────────────────────── */

    /* Execution modes — mutually exclusive (default: --ir-run) */
    int mode_ir_run        = 0;  /* --ir-run   : interpret via IR tree-walk (correctness ref) [DEFAULT] */
    int mode_sm_run        = 0;  /* --sm-run   : interpret SM_Program via dispatch loop */
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

    /* Default execution mode: --ir-run */
    if (!mode_ir_run && !mode_sm_run && !mode_jit_run && !mode_jit_emit)
        mode_ir_run = 1;

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
            "Execution modes (default: --ir-run):\n"
            "  --ir-run         interpret via IR tree-walk (correctness reference)  [DEFAULT]\n"
            "  --sm-run         interpret SM_Program via dispatch loop\n"
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

    struct timespec _t0, _t1, _t2, _t3;
    if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t0);

    /* ── parse ──────────────────────────────────────────────────────────────
     * --dump-parse / --dump-parse-flat / --dump-ir  →  sno_parse (Bison/Flex)
     * --dump-ir-bison / --ir-run / --sm-run / etc.  →  sno_parse (Bison/Flex)
     * sno_parse is the execution path. Pattern prims emit typed IR nodes (E_ANY etc).
     * .sc extension  →  snocone_compile()  .pl  →  prolog_compile()  .icn  →  icon_compile() */
    /* detect Snocone frontend by file extension */
    int lang_snocone = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".sc") == 0) lang_snocone = 1; }
    int lang_prolog = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".pl") == 0) lang_prolog = 1; }
    int lang_icon = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".icn") == 0) lang_icon = 1; }

    Program *prog = NULL;
    if (lang_snocone || lang_prolog || lang_icon) {
        /* Read whole file into buffer */
        fseek(f, 0, SEEK_END); long flen = ftell(f); rewind(f);
        char *src = malloc(flen + 1);
        if (!src) { fprintf(stderr, "scrip: out of memory\n"); return 1; }
        fread(src, 1, flen, f); src[flen] = '\0'; fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        prog = lang_prolog ? prolog_compile(src, input_path)
                           : lang_icon   ? icon_compile(src, input_path)
                           : snocone_compile(src, input_path);
        free(src);
    } else if (dump_parse || dump_parse_flat || dump_ir) {
        fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        FILE *f2 = fopen(input_path, "r");
        if (!f2) { fprintf(stderr, "scrip: cannot re-open '%s'\n", input_path); return 1; }
        prog = sno_parse(f2, input_path);
        fclose(f2);
        if (prog) { ir_dump_program(prog, stdout); }
        return 0;
    } else {
        fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        FILE *f3 = fopen(input_path, "r");
        if (!f3) { fprintf(stderr, "scrip: cannot re-open '%s'\n", input_path); return 1; }
        prog = sno_parse(f3, input_path);
        fclose(f3);
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

    /* S-10 fix: register scrip.c-only builtins so APPLY_fn can dispatch them
     * at match time (used by *IDENT(x) / *DIFFER(x) in pattern position). */
    register_fn("IDENT",  _builtin_IDENT,  1, 2);
    register_fn("DIFFER", _builtin_DIFFER, 1, 2);
    register_fn("EVAL",   _builtin_EVAL,   1, 1);
    register_fn("CODE",   _builtin_CODE,   1, 1);

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

    /* Wire DT_S eval hook: EVAL(string) containing complex operators
     * (E_DEFER, cursor-assign) routes through interp_eval_pat. */
    {
        extern DESCR_t (*g_eval_str_hook)(const char *s);
        g_eval_str_hook = _eval_str_impl_fn;
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
    } else if (lang_prolog) {
        pl_execute_program_unified(prog);
    } else if (lang_icon) {
        icn_execute_program_unified(prog);   /* unified IR interpreter — one interp_eval */
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
