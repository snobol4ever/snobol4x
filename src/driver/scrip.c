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
 *
 * Byrd Box pattern mode (default: --bb-driver):
 *   --bb-driver      pattern matching via driver/broker
 *   --bb-live        pattern matching live-wired in exec memory
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
#include <ucontext.h>
#include <sys/stat.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>
#include <gc.h>

/* ── frontend ─────────────────────────────────────────────────────────── */
#include "../frontend/snobol4/scrip_cc.h"
/* CMPILE.h removed — bison/flex path only (GOAL-REMOVE-CMPILE S-5) */
extern Program *sno_parse(FILE *f, const char *filename);
#include "../frontend/snocone/snocone_driver.h"
#include "../frontend/prolog/prolog_driver.h"
#include "../frontend/prolog/term.h"            /* Term — needed by Prolog globals block */
#include "../frontend/prolog/prolog_runtime.h"  /* Trail — needed by Prolog globals block */
#include "../frontend/prolog/prolog_builtin.h"  /* interp_exec_pl_builtin declaration */
#include "../frontend/prolog/pl_broker.h"       /* pl_box_choice, pl_box_* — S-BB-7; pl_exec_goal removed U-11 */
#include "../frontend/icon/icon_driver.h"
#include "../frontend/icon/icon_gen.h"    /* icn_bb_to/by/iterate/suspend, state types — U-17 */
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

/* ── Icon unified interpreter state ────────────────────────────────────────
 * Icon procedures use slot-indexed locals (e->ival on E_VAR nodes).
 * When interp_eval is running inside an Icon procedure call, icn_env points
 * to the current frame's slot array. E_VAR case checks icn_env first.
 * icn_env_n is the slot count. Both are NULL/0 when in SNOBOL4 context.
 *
 * Icon procedure table: built from Program* at execute_program time.
 * Each entry maps procname → the E_FNC node (from STMT_t subject).
 * ────────────────────────────────────────────────────────────────────────── */
#define ICN_SLOT_MAX   64
#define ICN_PROC_MAX  256
typedef struct { const char *name; EXPR_t *proc; } IcnProcEntry;
static IcnProcEntry icn_proc_table[ICN_PROC_MAX];
static int          icn_proc_count = 0;
static DESCR_t     *icn_env        = NULL;  /* current Icon frame slot array */
static int          icn_env_n      = 0;     /* slot count */
static int          icn_returning  = 0;     /* 1 = Icon return in progress */
static DESCR_t      icn_return_val;         /* value being returned */
static int          g_lang         = 0;     /* 0=SNOBOL4 1=Icon */
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
static DESCR_t icn_interp_eval(EXPR_t *root, EXPR_t *e); /* forward */
static DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs); /* forward */

/* ── Prolog type forward declarations ────────────────────────────────────────
 * Full definitions appear in the pl_ block before execute_program below. */
#define PL_PRED_TABLE_SIZE_FWD 256
typedef struct Pl_PredEntry_t { const char *key; EXPR_t *choice; struct Pl_PredEntry_t *next; } Pl_PredEntry;
typedef struct { Pl_PredEntry *buckets[PL_PRED_TABLE_SIZE_FWD]; } Pl_PredTable;

Term    *pl_unified_term_from_expr(EXPR_t *e, Term **env); /* forward */
Term   **pl_env_new(int n); /* forward */
static EXPR_t  *pl_pred_table_lookup(Pl_PredTable *pt, const char *key); /* forward */
static void     pl_pred_table_insert(Pl_PredTable *pt, const char *key, EXPR_t *choice); /* forward U-14 */
static int      is_pl_user_call(EXPR_t *goal); /* forward */
int             interp_exec_pl_builtin(EXPR_t *goal, Term **env); /* forward — defined in pl_ block */
static void     polyglot_init(Program *prog); /* forward U-14 */

/* ── Prolog global execution state ──────────────────────────────────────────
 * Initialised by pl_execute_program_unified() at program start. */
static Pl_PredTable g_pl_pred_table;
       Trail        g_pl_trail;        /* non-static: used by pl_broker.c (pl_interp.h) */
       int          g_pl_cut_flag = 0; /* non-static: used by pl_broker.c (pl_interp.h) */
static Term       **g_pl_env      = NULL;
static int          g_pl_active   = 0;

/* icn_drive: drive generators embedded in e, re-executing root each tick.
 * Returns tick count. Mirrors icn_exec_driven in icon_interp.c but uses DESCR_t. */
static int icn_drive(EXPR_t *root, EXPR_t *e) {
    if (!e) return 0;
    if (icn_gen_active(e)) return 0;
    if (e->kind == E_TO && e->nchildren >= 2) {
        DESCR_t lo_d = icn_interp_eval(root, e->children[0]);
        DESCR_t hi_d = icn_interp_eval(root, e->children[1]);
        if (IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)) return 0;
        long lo=lo_d.i, hi=hi_d.i; int ticks=0;
        for(long i=lo;i<=hi&&!icn_returning;i++){
            icn_gen_push(e,i,NULL);
            int inner=icn_drive(root,root);
            if(!inner) icn_interp_eval(root,root);
            icn_gen_pop(); ticks++;
            if(icn_returning) break;
        }
        return ticks;
    }
    if (e->kind == E_TO_BY && e->nchildren >= 3) {
        DESCR_t lo_d=icn_interp_eval(root,e->children[0]);
        DESCR_t hi_d=icn_interp_eval(root,e->children[1]);
        DESCR_t st_d=icn_interp_eval(root,e->children[2]);
        if(IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)||IS_FAIL_fn(st_d)) return 0;
        long lo=lo_d.i,hi=hi_d.i,st=st_d.i?st_d.i:1; int ticks=0;
        if(st>0){for(long i=lo;i<=hi&&!icn_returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root,root);if(!inner)icn_interp_eval(root,root);icn_gen_pop();ticks++;if(icn_returning)break;}}
        else    {for(long i=lo;i>=hi&&!icn_returning;i+=st){icn_gen_push(e,i,NULL);int inner=icn_drive(root,root);if(!inner)icn_interp_eval(root,root);icn_gen_pop();ticks++;if(icn_returning)break;}}
        return ticks;
    }
    /* S-6: E_ITERATE (!str) — generate each character of a string */
    if (e->kind == E_ITERATE && e->nchildren >= 1) {
        DESCR_t sv_d = icn_interp_eval(root, e->children[0]);
        if (IS_FAIL_fn(sv_d) || !IS_STR_fn(sv_d)) return 0;
        const char *str = sv_d.s ? sv_d.s : "";
        long len = (long)strlen(str); int ticks = 0;
        for (long i = 0; i < len && !icn_returning; i++) {
            icn_gen_push(e, i, str);
            int inner = icn_drive(root, root);
            if (!inner) icn_interp_eval(root, root);
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
        DESCR_t s1 = icn_interp_eval(root, e->children[1]);
        DESCR_t s2 = icn_interp_eval(root, e->children[2]);
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
            if (!inner) icn_interp_eval(root, root);
            icn_gen_pop(); ticks++;
            if (icn_returning) break;
            p = hit + (nlen > 0 ? nlen : 1);    /* advance past this match */
        }
        return ticks;
    }
    for(int i=0;i<e->nchildren;i++){int t=icn_drive(root,e->children[i]);if(t>0)return t;}
    return 0;
}

/* icn_interp_eval: evaluate one Icon IR node, returning DESCR_t.
 * FAILDESCR = Icon fail. NULVCL = null/void result.
 * Uses icn_env[slot] for E_VAR, icn_gen_stack for E_TO substitution. */
static DESCR_t icn_interp_eval(EXPR_t *root, EXPR_t *e) {
    (void)root;
    if (!e) return NULVCL;
    switch (e->kind) {

    case E_ILIT: return INTVAL(e->ival);
    case E_FLIT: return REALVAL(e->dval);
    case E_QLIT: return e->sval ? STRVAL(e->sval) : NULVCL;
    case E_CSET: return e->sval ? STRVAL(e->sval) : NULVCL;
    case E_NUL:  return NULVCL;

    case E_VAR: {
        /* Icon &keywords are lowered as E_VAR with sval starting with '&' */
        if (e->sval && e->sval[0] == '&') {
            const char *kw = e->sval + 1;  /* strip leading & */
            if (strcmp(kw,"subject")==0)
                return icn_scan_subj ? STRVAL(icn_scan_subj) : NULVCL;
            if (strcmp(kw,"pos")==0)    return INTVAL(icn_scan_pos);
            if (strcmp(kw,"letters")==0)
                return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
            if (strcmp(kw,"ucase")==0)  return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            if (strcmp(kw,"lcase")==0)  return STRVAL("abcdefghijklmnopqrstuvwxyz");
            if (strcmp(kw,"digits")==0) return STRVAL("0123456789");
            if (strcmp(kw,"null")==0)   return NULVCL;
            if (strcmp(kw,"fail")==0)   return FAILDESCR;
            return NULVCL;
        }
        int slot = (int)e->ival;
        if (slot >= 0 && slot < icn_env_n && icn_env) return icn_env[slot];
        return NULVCL;
    }

    case E_ASSIGN: {
        if (e->nchildren < 2) return NULVCL;
        DESCR_t val = icn_interp_eval(root, e->children[1]);
        if (IS_FAIL_fn(val)) return FAILDESCR;
        EXPR_t *lhs = e->children[0];
        if (lhs && lhs->kind == E_VAR) {
            int slot = (int)lhs->ival;
            if (slot >= 0 && slot < icn_env_n && icn_env) icn_env[slot] = val;
        }
        return val;
    }

    case E_MNS: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t v = icn_interp_eval(root, e->children[0]);
        if (IS_FAIL_fn(v)) return FAILDESCR;
        return INTVAL(-v.i);
    }

    case E_ADD: case E_SUB: case E_MUL: case E_DIV:
    case E_LT:  case E_LE:  case E_GT:  case E_GE:
    case E_EQ:  case E_NE: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = icn_interp_eval(root, e->children[0]);
        DESCR_t r = icn_interp_eval(root, e->children[1]);
        if (IS_FAIL_fn(l)||IS_FAIL_fn(r)) return FAILDESCR;
        long li = IS_INT_fn(l)?l.i:(long)l.r, ri = IS_INT_fn(r)?r.i:(long)r.r;
        switch(e->kind){
            case E_ADD: return INTVAL(li+ri);
            case E_SUB: return INTVAL(li-ri);
            case E_MUL: return INTVAL(li*ri);
            case E_DIV: return ri?INTVAL(li/ri):FAILDESCR;
            case E_LT:  return li< ri?r:FAILDESCR;
            case E_LE:  return li<=ri?r:FAILDESCR;
            case E_GT:  return li> ri?r:FAILDESCR;
            case E_GE:  return li>=ri?r:FAILDESCR;
            case E_EQ:  return li==ri?r:FAILDESCR;
            case E_NE:  return li!=ri?r:FAILDESCR;
            default:    return FAILDESCR;
        }
    }

    case E_CAT: case E_LCONCAT: {
        if (e->nchildren < 2) return NULVCL;
        DESCR_t l = icn_interp_eval(root, e->children[0]);
        DESCR_t r = icn_interp_eval(root, e->children[1]);
        if (IS_FAIL_fn(l)||IS_FAIL_fn(r)) return FAILDESCR;
        const char *ls = VARVAL_fn(l), *rs = VARVAL_fn(r);
        if (!ls) ls=""; if (!rs) rs="";
        size_t ll=strlen(ls), rl=strlen(rs);
        char *buf = GC_malloc(ll+rl+1);
        memcpy(buf,ls,ll); memcpy(buf+ll,rs,rl); buf[ll+rl]='\0';
        return STRVAL(buf);
    }

    /* E_TO: in scalar context return lo; in driven context return substituted cur */
    case E_TO: case E_TO_BY: {
        long cur;
        if (icn_gen_lookup(e, &cur)) return INTVAL(cur);
        if (e->nchildren < 1) return NULVCL;
        return icn_interp_eval(root, e->children[0]);
    }

    case E_EVERY: {
        if (e->nchildren < 1) return NULVCL;
        EXPR_t *gen  = e->children[0];
        EXPR_t *body = (e->nchildren > 1) ? e->children[1] : NULL;
        if (body) {
            /* Two-child form: every gen do body */
            if (gen->kind == E_TO && gen->nchildren >= 2) {
                DESCR_t lo_d = icn_interp_eval(root, gen->children[0]);
                DESCR_t hi_d = icn_interp_eval(root, gen->children[1]);
                if (IS_FAIL_fn(lo_d)||IS_FAIL_fn(hi_d)) return NULVCL;
                long lo=lo_d.i, hi=hi_d.i;
                for(long i=lo;i<=hi&&!icn_returning;i++) icn_interp_eval(root,body);
                return NULVCL;
            }
        }
        /* One-child form: every expr — drive embedded generator */
        int ticks = icn_drive(gen, gen);
        if (!ticks) icn_interp_eval(root, gen);
        return NULVCL;
    }

    case E_WHILE: {
        int saved_brk = icn_loop_break; icn_loop_break = 0;
        DESCR_t cv;
        while (!icn_returning && !icn_loop_break &&
               !IS_FAIL_fn(cv = icn_interp_eval(root, e->children[0]))) {
            if (e->nchildren > 1) icn_interp_eval(root, e->children[1]);
        }
        icn_loop_break = saved_brk;
        return NULVCL;
    }

    case E_UNTIL: {
        int saved_brk = icn_loop_break; icn_loop_break = 0;
        DESCR_t cv;
        while (!icn_returning && !icn_loop_break) {
            cv = (e->nchildren > 0) ? icn_interp_eval(root, e->children[0]) : FAILDESCR;
            if (!IS_FAIL_fn(cv)) break;
            if (e->nchildren > 1) icn_interp_eval(root, e->children[1]);
        }
        icn_loop_break = saved_brk;
        return NULVCL;
    }

    case E_REPEAT: {
        int saved_brk = icn_loop_break; icn_loop_break = 0;
        while (!icn_returning && !icn_loop_break) {
            if (e->nchildren > 0) icn_interp_eval(root, e->children[0]);
        }
        icn_loop_break = saved_brk;
        return NULVCL;
    }

    case E_SEQ_EXPR: {
        /* (stmt1; stmt2; ...; stmtN) — evaluate all in order, return last value.
         * Unlike E_SEQ (string concat), this is a statement block sequence.
         * Failure in intermediate statements does NOT abort the block. */
        DESCR_t v = NULVCL;
        for (int i = 0; i < e->nchildren && !icn_returning; i++)
            v = icn_interp_eval(root, e->children[i]);
        return v;
    }

    case E_SEQ: {
        DESCR_t v = NULVCL;
        for (int i = 0; i < e->nchildren && !icn_returning; i++)
            v = icn_interp_eval(root, e->children[i]);
        return v;
    }

    case E_IF: {
        if (e->nchildren < 1) return NULVCL;
        DESCR_t cv = icn_interp_eval(root, e->children[0]);
        if (!IS_FAIL_fn(cv))
            return (e->nchildren > 1) ? icn_interp_eval(root, e->children[1]) : cv;
        return (e->nchildren > 2) ? icn_interp_eval(root, e->children[2]) : FAILDESCR;
    }

    case E_NOT: {
        DESCR_t v = (e->nchildren > 0) ? icn_interp_eval(root, e->children[0]) : FAILDESCR;
        return IS_FAIL_fn(v) ? NULVCL : FAILDESCR;
    }

    case E_RETURN: {
        icn_return_val = (e->nchildren > 0)
            ? icn_interp_eval(root, e->children[0]) : NULVCL;
        icn_returning = 1;
        return icn_return_val;
    }

    case E_AUGOP: {
        if (e->nchildren < 2) return NULVCL;
        EXPR_t *lhs = e->children[0];
        DESCR_t lv = icn_interp_eval(root, lhs);
        DESCR_t rv = icn_interp_eval(root, e->children[1]);
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
        if (lhs->kind == E_VAR) {
            int slot=(int)lhs->ival;
            if(slot>=0&&slot<icn_env_n&&icn_env) icn_env[slot]=result;
        }
        return result;
    }

    /* String relational operators */
    case E_LEQ: case E_LNE: case E_LLT: case E_LLE: case E_LGT: case E_LGE: {
        if (e->nchildren < 2) return FAILDESCR;
        DESCR_t l = icn_interp_eval(root, e->children[0]);
        DESCR_t r = icn_interp_eval(root, e->children[1]);
        if (IS_FAIL_fn(l)||IS_FAIL_fn(r)) return FAILDESCR;
        const char *ls = IS_INT_fn(l)?NULL:VARVAL_fn(l);
        const char *rs = IS_INT_fn(r)?NULL:VARVAL_fn(r);
        if (!ls) ls=""; if (!rs) rs="";
        int cmp = strcmp(ls, rs);
        int ok;
        switch(e->kind){
            case E_LEQ: ok=(cmp==0);  break;
            case E_LNE: ok=(cmp!=0);  break;
            case E_LLT: ok=(cmp<0);   break;
            case E_LLE: ok=(cmp<=0);  break;
            case E_LGT: ok=(cmp>0);   break;
            case E_LGE: ok=(cmp>=0);  break;
            default:    ok=0;
        }
        return ok ? r : FAILDESCR;
    }

    /* E_LOOP_BREAK: break from repeat/while/until — use icn_loop_break flag */
    case E_LOOP_BREAK: {
        icn_loop_break = 1;
        return (e->nchildren > 0) ? icn_interp_eval(root, e->children[0]) : NULVCL;
    }

    /* E_SCAN: s ? expr — string scanning context */
    case E_SCAN: {
        if (e->nchildren < 1) return FAILDESCR;
        DESCR_t subj_d = icn_interp_eval(root, e->children[0]);
        if (IS_FAIL_fn(subj_d)) return FAILDESCR;
        const char *subj_s = VARVAL_fn(subj_d);
        if (!subj_s) subj_s = "";
        if (icn_scan_depth < ICN_SCAN_STACK_MAX) {
            icn_scan_stack[icn_scan_depth].subj = icn_scan_subj;
            icn_scan_stack[icn_scan_depth].pos  = icn_scan_pos;
            icn_scan_depth++;
        }
        icn_scan_subj = subj_s; icn_scan_pos = 1;
        DESCR_t r = (e->nchildren >= 2)
            ? icn_interp_eval(root, e->children[1]) : NULVCL;
        if (icn_scan_depth > 0) {
            icn_scan_depth--;
            icn_scan_subj = icn_scan_stack[icn_scan_depth].subj;
            icn_scan_pos  = icn_scan_stack[icn_scan_depth].pos;
        }
        return r;
    }

    /* E_KEYWORD: &subject, &pos, &letters, &ucase, &lcase, &digits, &ascii */
    case E_KEYWORD: {
        if (!e->sval) return NULVCL;
        /* Icon parser stores sval with & prefix (e.g. "&letters"); SNOBOL4 without */
        const char *kw = e->sval;
        if (*kw == '&') kw++;   /* strip leading & for uniform comparison */
        if (strcmp(kw,"subject")==0)
            return icn_scan_subj ? STRVAL(icn_scan_subj) : NULVCL;
        if (strcmp(kw,"pos")==0) return INTVAL(icn_scan_pos);
        /* Icon cset keywords */
        if (strcmp(kw,"letters")==0)
            return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        if (strcmp(kw,"ucase")==0)
            return STRVAL("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        if (strcmp(kw,"lcase")==0)
            return STRVAL("abcdefghijklmnopqrstuvwxyz");
        if (strcmp(kw,"digits")==0)
            return STRVAL("0123456789");
        if (strcmp(kw,"ascii")==0) {
            static char ascii_all[129];
            if (!ascii_all[0]) { for(int _i=1;_i<128;_i++) ascii_all[_i-1]=(char)_i; ascii_all[127]='\0'; }
            return STRVAL(ascii_all);
        }
        return NULVCL;
    }
    case E_ITERATE: {
        /* S-6: !str — when driven, return single-char string at current position */
        long cur; const char *str;
        if (icn_gen_lookup_sv(e, &cur, &str) && str) {
            char *ch = GC_malloc(2); ch[0] = str[cur]; ch[1] = '\0';
            return STRVAL(ch);
        }
        return FAILDESCR;
    }
    case E_SUSPEND: /* simplified: return value (S-11 adds setjmp) */
        return (e->nchildren > 0) ? icn_interp_eval(root, e->children[0]) : NULVCL;

    case E_FNC: {
        /* Icon E_FNC: children[0] = E_VAR(name), children[1..] = args.
         * sval may be NULL — always read name from children[0]->sval. */
        if (e->nchildren < 1) return NULVCL;
        const char *fn = e->children[0]->sval;
        if (!fn) return NULVCL;
        int nargs = e->nchildren - 1;  /* children[1..nchildren-1] are args */

        /* Builtins */
        if (!strcmp(fn,"write")) {
            if (nargs == 0) { printf("\n"); return NULVCL; }
            DESCR_t a = icn_interp_eval(root, e->children[1]);
            if (IS_FAIL_fn(a)) return FAILDESCR;
            if (IS_INT_fn(a)) printf("%lld\n",(long long)a.i);
            else if (IS_REAL_fn(a)) printf("%g\n",a.r);
            else { const char *s=VARVAL_fn(a); printf("%s\n",s?s:""); }
            return a;
        }
        if (!strcmp(fn,"writes")) {
            if (nargs == 0) return NULVCL;
            DESCR_t a = icn_interp_eval(root, e->children[1]);
            if (IS_INT_fn(a)) printf("%lld",(long long)a.i);
            else if (IS_REAL_fn(a)) printf("%g",a.r);
            else { const char *s=VARVAL_fn(a); printf("%s",s?s:""); }
            return a;
        }
        if (!strcmp(fn,"read"))  return NULVCL;
        if (!strcmp(fn,"stop"))  { exit(0); }

        /* Scan-context builtins — mirror icon_interp.c */
        if (!strcmp(fn,"any") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t cs = icn_interp_eval(root, e->children[1]);
            const char *s=icn_scan_subj, *cv=VARVAL_fn(cs);
            int p=icn_scan_pos-1;
            if (!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
            icn_scan_pos++; return INTVAL(icn_scan_pos);
        }
        if (!strcmp(fn,"many") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t cs = icn_interp_eval(root, e->children[1]);
            const char *s=icn_scan_subj, *cv=VARVAL_fn(cs);
            int p=icn_scan_pos-1;
            if (!s||!cv||p>=(int)strlen(s)||!strchr(cv,s[p])) return FAILDESCR;
            while(p<(int)strlen(s)&&strchr(cv,s[p])) p++;
            icn_scan_pos=p+1; return INTVAL(icn_scan_pos);
        }
        if (!strcmp(fn,"upto") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t cs = icn_interp_eval(root, e->children[1]);
            const char *s=icn_scan_subj, *cv=VARVAL_fn(cs);
            if (!s||!cv) return FAILDESCR;
            int p=icn_scan_pos-1;
            while(p<(int)strlen(s)&&!strchr(cv,s[p])) p++;
            if(p>=(int)strlen(s)) return FAILDESCR;
            /* upto returns the position WITHOUT advancing — tab() does the advance */
            return INTVAL(p+1);
        }
        if (!strcmp(fn,"move") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t nv = icn_interp_eval(root, e->children[1]);
            int newp = icn_scan_pos+(int)nv.i;
            if (!icn_scan_subj||newp<1||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
            int old=icn_scan_pos; icn_scan_pos=newp;
            size_t len=(size_t)nv.i;
            char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
            return STRVAL(buf);
        }
        if (!strcmp(fn,"tab") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t nv = icn_interp_eval(root, e->children[1]);
            if (IS_FAIL_fn(nv)) return FAILDESCR;  /* propagate failure from arg */
            int newp=(int)nv.i;
            if (!icn_scan_subj||newp<icn_scan_pos||newp>(int)strlen(icn_scan_subj)+1) return FAILDESCR;
            int old=icn_scan_pos; icn_scan_pos=newp;
            size_t len=(size_t)(newp-old);
            char *buf=GC_malloc(len+1); memcpy(buf,icn_scan_subj+old-1,len); buf[len]='\0';
            return STRVAL(buf);
        }
        if (!strcmp(fn,"match") && nargs >= 1 && icn_scan_pos > 0) {
            DESCR_t sv = icn_interp_eval(root, e->children[1]);
            const char *needle=VARVAL_fn(sv), *hay=icn_scan_subj?icn_scan_subj:"";
            if (!needle) return FAILDESCR;
            int p=icn_scan_pos-1, nl=(int)strlen(needle);
            if (strncmp(hay+p,needle,nl)!=0) return FAILDESCR;
            icn_scan_pos+=nl; return INTVAL(icn_scan_pos);
        }

        /* find(pat,str) — generator via icn_drive; scalar fallback for non-driven context */
        if (!strcmp(fn,"find") && nargs >= 2) {
            long pos1; if (icn_gen_lookup(e, &pos1)) return INTVAL(pos1);
            DESCR_t s1 = icn_interp_eval(root, e->children[1]);
            DESCR_t s2 = icn_interp_eval(root, e->children[2]);
            const char *needle=VARVAL_fn(s1), *hay=VARVAL_fn(s2);
            if (!needle||!hay) return FAILDESCR;
            char *p = strstr(hay, needle);
            return p ? INTVAL((long long)(p-hay)+1) : FAILDESCR;
        }

        /* User Icon procedure lookup */
        for (int i = 0; i < icn_proc_count; i++) {
            if (strcmp(icn_proc_table[i].name, fn) == 0) {
                DESCR_t args[ICN_SLOT_MAX];
                for (int j = 0; j < nargs && j < ICN_SLOT_MAX; j++)
                    args[j] = icn_interp_eval(root, e->children[j+1]);
                return icn_call_proc(icn_proc_table[i].proc, args, nargs);
            }
        }
        return NULVCL;
    }

    default: return NULVCL;
    }
}

/* icn_scope_add/patch: mirror of scope_add/scope_patch in icon_interp.c.
 * Assigns slot indices to E_VAR nodes by name, in-place on the AST. */
typedef struct { const char *name; int slot; } IcnScopeEnt;
typedef struct { IcnScopeEnt e[ICN_SLOT_MAX]; int n; } IcnScope;

static int icn_scope_add(IcnScope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    if (sc->n >= ICN_SLOT_MAX) return -1;
    int slot = sc->n;
    sc->e[sc->n].name=name; sc->e[sc->n].slot=slot; sc->n++;
    return slot;
}
static int icn_scope_get(IcnScope *sc, const char *name) {
    if (!name) return -1;
    for (int i=0;i<sc->n;i++) if(strcmp(sc->e[i].name,name)==0) return sc->e[i].slot;
    return -1;
}
static void icn_scope_patch(IcnScope *sc, EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_GLOBAL) {
        for (int i=0;i<e->nchildren;i++)
            if(e->children[i]&&e->children[i]->sval) icn_scope_add(sc, e->children[i]->sval);
        return;
    }
    if (e->kind == E_VAR && e->sval) {
        /* Add var to scope if not already present (handles undeclared vars) */
        int s = icn_scope_add(sc, e->sval);
        if (s >= 0) e->ival = s;
    }
    for (int i=0;i<e->nchildren;i++) icn_scope_patch(sc, e->children[i]);
}

/* icn_call_proc: call an Icon procedure node (E_FNC with body children).
 * Mirrors icn_call() in icon_interp.c exactly, but uses DESCR_t and icn_env. */
static DESCR_t icn_call_proc(EXPR_t *proc, DESCR_t *args, int nargs) {
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

    /* Allocate fresh slot array, load params */
    DESCR_t frame[ICN_SLOT_MAX];
    memset(frame, 0, sizeof frame);
    for (int i = 0; i < nparams && i < nargs && i < ICN_SLOT_MAX; i++)
        frame[i] = args[i];

    /* Save caller's env, install this frame */
    DESCR_t *saved_env   = icn_env;
    int       saved_n    = icn_env_n;
    int       saved_ret  = icn_returning;
    icn_env      = frame;
    icn_env_n    = nslots;
    icn_returning = 0;

    /* Execute body statements */
    DESCR_t result = NULVCL;
    for (int i = 0; i < nbody && !icn_returning; i++) {
        EXPR_t *st = proc->children[body_start+i];
        if (!st || st->kind == E_GLOBAL) continue;
        result = icn_interp_eval(st, st);
    }
    if (icn_returning) result = icn_return_val;

    /* Restore caller's env */
    icn_env       = saved_env;
    icn_env_n     = saved_n;
    icn_returning = saved_ret;
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
 *   fallback    → one-shot box returning icn_interp_eval(e,e)
 *
 * Visible here: icn_interp_eval, icn_call_proc, icn_proc_table, icn_proc_count.
 *============================================================================================================================*/

/* One-shot fallback box state — holds a pre-evaluated DESCR_t, fires γ once then ω. */
typedef struct { DESCR_t val; int fired; } icn_oneshot_state_t;
static DESCR_t icn_oneshot_box(void *zeta, int entry) {
    icn_oneshot_state_t *z = (icn_oneshot_state_t *)zeta;
    if (entry == α && !z->fired && !IS_FAIL_fn(z->val)) { z->fired = 1; return z->val; }
    return FAILDESCR;
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
    DESCR_t result = icn_call_proc(st.proc, st.args, st.nargs);
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
        DESCR_t lo_d = icn_interp_eval(e, e->children[0]);
        DESCR_t hi_d = icn_interp_eval(e, e->children[1]);
        icn_to_state_t *z = calloc(1, sizeof(*z));
        z->lo = IS_FAIL_fn(lo_d) ? 0 : lo_d.i;
        z->hi = IS_FAIL_fn(hi_d) ? 0 : hi_d.i;
        return (bb_node_t){ icn_bb_to, z, 0 };
    }

    /* ── E_TO_BY: (lo to hi by step) ─────────────────────────────────────── */
    if (e->kind == E_TO_BY && e->nchildren >= 3) {
        DESCR_t lo_d   = icn_interp_eval(e, e->children[0]);
        DESCR_t hi_d   = icn_interp_eval(e, e->children[1]);
        DESCR_t step_d = icn_interp_eval(e, e->children[2]);
        icn_to_by_state_t *z = calloc(1, sizeof(*z));
        z->lo   = IS_FAIL_fn(lo_d)   ? 0 : lo_d.i;
        z->hi   = IS_FAIL_fn(hi_d)   ? 0 : hi_d.i;
        z->step = IS_FAIL_fn(step_d) ? 1 : step_d.i;
        return (bb_node_t){ icn_bb_to_by, z, 0 };
    }

    /* ── E_ITERATE: (!str) ───────────────────────────────────────────────── */
    if (e->kind == E_ITERATE && e->nchildren >= 1) {
        DESCR_t sv = icn_interp_eval(e, e->children[0]);
        icn_iterate_state_t *z = calloc(1, sizeof(*z));
        if (!IS_FAIL_fn(sv) && sv.s) {
            z->str = sv.s;
            z->len = sv.slen > 0 ? sv.slen : (long)strlen(sv.s);
        }
        return (bb_node_t){ icn_bb_iterate, z, 0 };
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
                args[j] = icn_interp_eval(e, e->children[1+j]);
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
    }

    /* ── Fallback: one-shot box wrapping icn_interp_eval ─────────────────── */
    icn_oneshot_state_t *z = calloc(1, sizeof(*z));
    z->val = icn_interp_eval(e, e);
    return (bb_node_t){ icn_oneshot_box, z, 0 };
}

/* icn_execute_program_unified: entry point for Icon via unified interpreter.
 * Replaces icon_execute_program. Builds proc table, calls main/0. */
static void icn_execute_program_unified(Program *prog) {
    polyglot_init(prog);   /* U-14: one pass, all three runtime tables */
    g_lang = 1;            /* Icon top-level mode */

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

/* ══════════════════════════════════════════════════════════════════════════
 * polyglot_init — one pass, all three runtime tables  (U-14)
 *
 * Replaces the three separate init sequences that lived in execute_program,
 * icn_execute_program_unified, and pl_execute_program_unified.
 * Safe to call for single-language Programs — the lang-tagged tables are
 * simply empty when no statements of that language are present.
 * ══════════════════════════════════════════════════════════════════════════ */
static void polyglot_init(Program *prog)
{
    if (!prog) return;

    /* ── SNO: label table + DEFINE prescan ─────────────────────────── */
    label_table_build(prog);
    prescan_defines(prog);

    /* ── ICN: proc table ────────────────────────────────────────────── */
    icn_proc_count = 0;
    icn_env = NULL; icn_env_n = 0; icn_returning = 0;
    icn_gen_depth = 0;
    icn_scan_subj = NULL; icn_scan_pos = 0; icn_scan_depth = 0;
    icn_loop_break = 0; g_icn_root = NULL;

    /* ── PL: pred table + trail ─────────────────────────────────────── */
    prolog_atom_init();
    memset(&g_pl_pred_table, 0, sizeof g_pl_pred_table);
    trail_init(&g_pl_trail);
    g_pl_cut_flag = 0;
    g_pl_env      = NULL;
    g_pl_active   = 0;

    /* ── Single pass: populate ICN proc table and PL pred table ─────── */
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (!s->subject) continue;
        if (s->lang == LANG_ICN) {
            /* Icon: collect E_FNC procedure definitions */
            EXPR_t *proc = s->subject;
            if (proc->kind == E_FNC && proc->nchildren >= 1 && proc->children[0]) {
                const char *name = proc->children[0]->sval;
                if (name && icn_proc_count < ICN_PROC_MAX) {
                    icn_proc_table[icn_proc_count].name = name;
                    icn_proc_table[icn_proc_count].proc = proc;
                    icn_proc_count++;
                }
            }
        } else if (s->lang == LANG_PL) {
            /* Prolog: collect E_CHOICE / E_CLAUSE predicate definitions */
            EXPR_t *subj = s->subject;
                if ((subj->kind == E_CHOICE || subj->kind == E_CLAUSE) && subj->sval) {
                pl_pred_table_insert(&g_pl_pred_table, subj->sval, subj);
                g_pl_active = 1;
                    }
        }
    }
}


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
 * Prolog IR interpreter block — pl_execute_program_unified + helpers
 * (recovered from scrip.c bca2b79a; removed by 476fd067 accidentally)
 * ══════════════════════════════════════════════════════════════════════════ */

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

/* pl_pred_table_lookup_global — non-static wrapper for pl_broker.c (pl_interp.h) */
EXPR_t *pl_pred_table_lookup_global(const char *key) {
    return pl_pred_table_lookup(&g_pl_pred_table, key);
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

Term **pl_env_new(int n) {
    if (n <= 0) return NULL;
    Term **env = malloc(n * sizeof(Term *));
    for (int i = 0; i < n; i++) env[i] = term_new_var(i);
    return env;
}

/*---- Continuation type ----*/
/*---- Forward declarations ----*/
Term *pl_unified_term_from_expr(EXPR_t *e, Term **env);
static Term *pl_unified_deep_copy(Term *t);
int          interp_exec_pl_builtin(EXPR_t *goal, Term **env);



/*---- pl_unified_term_from_expr ----*/
Term *pl_unified_term_from_expr(EXPR_t *e, Term **env) {
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

/*---- interp_exec_pl_builtin — execute one Prolog builtin goal ----*/
/* Uses file-scope globals g_pl_trail, g_pl_cut_flag, g_pl_pred_table, g_pl_env.
 * Returns 1=success, 0=fail. Called by pl_box_builtin in pl_broker.c. */
int interp_exec_pl_builtin(EXPR_t *goal, Term **env) {
    if (!goal) return 1;
    Trail *trail = &g_pl_trail;
    int *cut_flag = &g_pl_cut_flag;
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
            /* ,/N conjunction — run each child goal in sequence */
            if (strcmp(fn,",")==0){
                for(int i=0;i<goal->nchildren;i++){
                    EXPR_t *g=goal->children[i];
                    if(!g) continue;
                    int ok = is_pl_user_call(g) ? ({
                        char key[256]; snprintf(key,sizeof key,"%s/%d",g->sval?g->sval:"",g->nchildren);
                        EXPR_t *ch=pl_pred_table_lookup(&g_pl_pred_table,key);
                        int r=0;
                        if(ch){ int ca=g->nchildren; Term **cargs=ca?malloc(ca*sizeof(Term*)):NULL;
                                 for(int a=0;a<ca;a++) cargs[a]=pl_unified_term_from_expr(g->children[a],env);
                                 Term **sv=g_pl_env; g_pl_env=cargs;
                                 DESCR_t rd=interp_eval(ch); g_pl_env=sv; if(cargs)free(cargs);
                                 r=!IS_FAIL_fn(rd); }
                        r; }) : interp_exec_pl_builtin(g, env);
                    if(!ok) return 0;
                }
                return 1;
            }
            /* ;/N disjunction */
            if (strcmp(fn,";")==0&&arity>=2){
                EXPR_t *left=goal->children[0],*right=goal->children[1];
                /* if-then-else: (Cond -> Then ; Else) */
                if(left&&left->kind==E_FNC&&left->sval&&strcmp(left->sval,"->")==0&&left->nchildren>=2){
                    int mark=trail_mark(trail); int cut2=0;
                    if(interp_exec_pl_builtin(left->children[0],env)){
                        for(int i=1;i<left->nchildren;i++) if(!interp_exec_pl_builtin(left->children[i],env)) return 0;
                        return 1;
                    }
                    trail_unwind(trail,mark);
                    return interp_exec_pl_builtin(right,env);
                }
                /* plain disjunction */
                {int mark=trail_mark(trail);
                 if(interp_exec_pl_builtin(left,env)) return 1;
                 trail_unwind(trail,mark);
                 return interp_exec_pl_builtin(right,env);}
            }
            /* ->/N if-then */
            if (strcmp(fn,"->")==0&&arity>=2){
                if(!interp_exec_pl_builtin(goal->children[0],env)) return 0;
                for(int i=1;i<goal->nchildren;i++) if(!interp_exec_pl_builtin(goal->children[i],env)) return 0;
                return 1;
            }
            /* \+/not */
            if ((strcmp(fn,"\\+")==0||strcmp(fn,"not")==0)&&arity==1){
                int mark=trail_mark(trail);
                int ok=interp_exec_pl_builtin(goal->children[0],env);
                trail_unwind(trail,mark);return !ok;
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
            /* assert/assertz/asserta/retract/retractall/abolish — stubs */
            if ((strcmp(fn,"assert")==0||strcmp(fn,"assertz")==0||strcmp(fn,"asserta")==0)&&arity==1) return 1;
            if ((strcmp(fn,"retract")==0||strcmp(fn,"retractall")==0||strcmp(fn,"abolish")==0)&&arity==1) return 1;
            /* findall/3 — collect all solutions via interp_eval on each goal */
            if (strcmp(fn,"findall")==0&&arity==3){
                EXPR_t *tmpl_expr=goal->children[0];
                EXPR_t *goal_expr=goal->children[1];
                EXPR_t *list_expr=goal->children[2];
                /* Run goal_expr collecting template snapshots on each success.
                 * We drive it as a user call if it is one, else as a builtin.
                 * findall always succeeds (empty list if no solutions). */
                Term **solutions=NULL; int nsol=0,sol_cap=0;
                /* Wrap execution in a sub-trail so findall does not pollute parent */
                Trail fa_trail; trail_init(&fa_trail);
                Trail *saved_trail=trail; (void)saved_trail; /* trail ptr is local alias */
                /* To collect all solutions we need a retry loop.
                 * For user-defined goal_expr, invoke via interp_eval which handles backtracking.
                 * For builtins, a single call suffices (builtins are det or semi-det here).
                 * Full multi-solution findall requires BB broker — deferred to GOAL-PROLOG-BB-BYRD.
                 * For now: one-shot collection (correct for det goals, partial for non-det). */
                Trail *old_global_trail=&g_pl_trail;
                g_pl_trail=fa_trail;
                int ok2;
                if(is_pl_user_call(goal_expr)){
                    char key[256]; snprintf(key,sizeof key,"%s/%d",goal_expr->sval?goal_expr->sval:"",goal_expr->nchildren);
                    EXPR_t *ch=pl_pred_table_lookup(&g_pl_pred_table,key);
                    if(ch){ int ca=goal_expr->nchildren; Term **cargs=ca?malloc(ca*sizeof(Term*)):NULL;
                             for(int a=0;a<ca;a++) cargs[a]=pl_unified_term_from_expr(goal_expr->children[a],env);
                             Term **sv=g_pl_env; g_pl_env=cargs;
                             DESCR_t rd=interp_eval(ch); g_pl_env=sv; if(cargs)free(cargs);
                             ok2=!IS_FAIL_fn(rd); }
                    else ok2=0;
                } else { ok2=interp_exec_pl_builtin(goal_expr,env); }
                if(ok2){
                    Term *snap=pl_unified_deep_copy(pl_unified_term_from_expr(tmpl_expr,env));
                    if(nsol>=sol_cap){sol_cap=sol_cap?sol_cap*2:8;solutions=realloc(solutions,sol_cap*sizeof(Term*));}
                    solutions[nsol++]=snap;
                }
                g_pl_trail=*old_global_trail;
                int nil_id=prolog_atom_intern("[]"),dot_id=prolog_atom_intern(".");
                Term *lst=term_new_atom(nil_id);
                for(int i=nsol-1;i>=0;i--){Term *a2[2];a2[0]=solutions[i];a2[1]=lst;lst=term_new_compound(dot_id,2,a2);}
                free(solutions);
                Term *list_term=pl_unified_term_from_expr(list_expr,env);
                int u_mark=trail_mark(trail);
                if(!unify(list_term,lst,trail)){trail_unwind(trail,u_mark);return 0;}
                return 1;
            }
            fprintf(stderr,"prolog: undefined predicate %s/%d\n",fn,arity);
            return 0;
        }
        default: return 1;
    }
}


/*---- pl_execute_program_unified — entry point ----*/
/* S-BB-7: top-level dispatch now routes main/0 through the Byrd box broker.
 * pl_box_choice(main_choice, NULL, 0) builds the OR-box; bb_broker(BB_ONCE) drives it.
 * The old interp_eval(main_choice) call is removed from the top-level entry.
 * Body goals within clauses still use the old interp_eval path until S-BB-8. */
static void pl_execute_program_unified(Program *prog) {
    if (!prog) return;
    polyglot_init(prog);   /* U-14: one pass, all three runtime tables */
    g_pl_active   = 1;
    /* S-1C-3 restored: main/0 dispatches through interp_eval() — the ONE interpreter.
     * E_CHOICE case in interp_eval uses pl_box_choice+bb_broker(BB_ONCE) for backtracking. */
    EXPR_t *main_choice = pl_pred_table_lookup(&g_pl_pred_table, "main/0");
    if (main_choice) {
        interp_eval(main_choice);
    } else {
        fprintf(stderr, "prolog: no main/0 predicate\n");
    }
    g_pl_active = 0;
}


static void execute_program(Program *prog)
{
    polyglot_init(prog);   /* U-14: one pass, all three runtime tables */
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

        /* ── U-15: per-statement dispatch by st->lang ─────────────── */
        if (s->lang == LANG_ICN) {
            /* Icon STMT_t nodes are procedure definitions — already registered
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

    /* ── U-15: post-loop Icon dispatch ────────────────────────────────────
     * Icon procedure definitions were skipped inline (they're registered in
     * icn_proc_table by polyglot_init).  If the polyglot program has Icon
     * sections, call main/0 now — mirroring icn_execute_program_unified. */
    if (icn_proc_count > 0) {
        for (int _i = 0; _i < icn_proc_count; _i++) {
            if (strcmp(icn_proc_table[_i].name, "main") == 0) {
                icn_call_proc(icn_proc_table[_i].proc, NULL, 0);
                break;
            }
        }
    }

    /* ── U-19: post-loop Prolog dispatch ───────────────────────────────────
     * Prolog clause STMT_t nodes were skipped inline (registered in
     * g_pl_pred_table by polyglot_init).  If the polyglot program has Prolog
     * sections, call main/0 now — mirroring pl_execute_program_unified. */
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
static DESCR_t _builtin_DATA(DESCR_t *args, int nargs);

/* _usercall_hook: calls user functions via call_user_function;
 * for pure builtins (FNCEX_fn && no body label) uses APPLY_fn directly
 * so FAILDESCR propagates correctly (DYN-74: fixes *ident(1,2) in EVAL). */
static DESCR_t _usercall_hook(const char *name, DESCR_t *args, int nargs) {
    /* S-10 fix: handle scrip.c-only predicates directly so *IDENT(x)/*DIFFER(x)
     * in pattern context correctly fail/succeed via bb_usercall -> g_user_call_hook. */
    if (strcasecmp(name, "IDENT") == 0)  return _builtin_IDENT(args, nargs);
    if (strcasecmp(name, "DIFFER") == 0) return _builtin_DIFFER(args, nargs);
    if (strcasecmp(name, "DATA") == 0)   return _builtin_DATA(args, nargs);
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

/* ── SC-1: DATA() builtin for Snocone struct lowering ────────────────────────
 * DATA('name(field1,field2,...)') — register a user-defined type.
 * Calls DEFDAT_fn to register the type + field accessor functions.
 * The constructor name() is registered via register_fn so interp_eval
 * can dispatch it through APPLY_fn → _builtin_DATA_constructor.
 * Returns null string on success (matches SNOBOL4 DATA() convention). */
static DESCR_t _builtin_DATA(DESCR_t *args, int nargs) {
    if (nargs < 1) return FAILDESCR;
    const char *spec = VARVAL_fn(args[0]);
    if (!spec || !*spec) return FAILDESCR;
    DEFDAT_fn(spec);
    return NULVCL;
}

/*============================================================================================================================
 * parse_scrip_polyglot — parse a fenced polyglot .scrip/.md file into one Program*  (U-13)
 *
 * Scans the source for fenced code blocks:
 *   ```SNOBOL4  ...  ```
 *   ```Icon     ...  ```
 *   ```Prolog   ...  ```
 * Each block is compiled with its own frontend.  All resulting STMT_t chains
 * are appended in source order into one Program*, with st->lang already set
 * by each frontend (U-12).  Unrecognised fence languages are skipped silently.
 *============================================================================================================================*/
static Program *parse_scrip_polyglot(const char *src, const char *filename)
{
    Program *result = calloc(1, sizeof(Program));
    if (!result) return NULL;

    const char *p = src;

    while (*p) {
        /* Find next ``` fence open */
        const char *fence = strstr(p, "```");
        if (!fence) break;

        /* Read the language tag on the same line as the fence open */
        const char *tag_start = fence + 3;
        const char *tag_end   = tag_start;
        while (*tag_end && *tag_end != '\n' && *tag_end != '\r') tag_end++;

        /* Trim trailing whitespace from tag */
        while (tag_end > tag_start && (tag_end[-1] == ' ' || tag_end[-1] == '\t')) tag_end--;

        int tag_len = (int)(tag_end - tag_start);

        /* Advance past the fence-open line */
        p = tag_end;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        /* Detect language (case-insensitive) */
        int lang = -1;
        if      (tag_len == 7 && strncasecmp(tag_start, "SNOBOL4", 7) == 0) lang = LANG_SNO;
        else if (tag_len == 4 && strncasecmp(tag_start, "Icon",    4) == 0) lang = LANG_ICN;
        else if (tag_len == 6 && strncasecmp(tag_start, "Prolog",  6) == 0) lang = LANG_PL;

        /* Find the matching fence close ``` */
        const char *block_start = p;
        const char *close = strstr(p, "```");
        if (!close) break;   /* unterminated block — stop */

        /* Extract block text */
        int   blen = (int)(close - block_start);
        char *block = malloc(blen + 1);
        if (!block) { p = close + 3; continue; }
        memcpy(block, block_start, blen);
        block[blen] = '\0';

        /* Advance past fence close */
        p = close + 3;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (lang < 0) { free(block); continue; }   /* unknown language — skip */

        /* Compile block with the appropriate frontend */
        Program *sub = NULL;
        if (lang == LANG_SNO) {
            sub = sno_parse_string(block);
            /* sno_parse_string sets lang=LANG_SNO=0 via calloc — already correct */
        } else if (lang == LANG_ICN) {
            sub = icon_compile(block, filename);
            /* icon_driver.c sets st->lang=LANG_ICN (U-12) */
        } else if (lang == LANG_PL) {
            sub = prolog_compile(block, filename);
            /* prolog_lower.c sets st->lang=LANG_PL (U-12) */
        }
        free(block);

        if (!sub || !sub->head) { free(sub); continue; }

        /* Append sub's STMT_t chain to result */
        if (!result->head) {
            result->head = sub->head;
            result->tail = sub->tail;
        } else {
            result->tail->next = sub->head;
            result->tail       = sub->tail;
        }
        result->nstmts += sub->nstmts;
        free(sub);   /* free the wrapper, not the STMT_t chain */
    }

    return result;
}

int main(int argc, char **argv)
{
    /* ── flag parsing ─────────────────────────────────────────────────── */

    /* Execution modes — mutually exclusive (default: --sm-run) */
    int mode_ir_run        = 0;  /* --ir-run   : interpret via IR tree-walk (correctness ref) */
    int mode_sm_run        = 0;  /* --sm-run   : interpret SM_Program via dispatch loop [DEFAULT] */
    int mode_jit_run       = 0;  /* --jit-run  : SM_Program -> x86 bytes -> mmap slab -> jump in */

    /* Byrd Box pattern mode — independent switch (default: --bb-driver) */
    int bb_driver          = 0;  /* --bb-driver : pattern matching via driver/broker */
    int bb_live            = 0;  /* --bb-live   : live-wired in exec memory */



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
        /* BB pattern mode */
        else if (strcmp(argv[argi], "--bb-driver")     == 0) { bb_driver          = 1; argi++; }
        else if (strcmp(argv[argi], "--bb-live")       == 0) { bb_live            = 1; argi++; }
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
    if (!mode_ir_run && !mode_sm_run && !mode_jit_run)
        mode_sm_run = 1;

    /* Default BB mode: --bb-driver unless --bb-live explicitly set */
    if (!bb_driver && !bb_live) bb_driver = 1;

    /* Suppress unused warning for bb_driver (not yet wired to stmt_exec.c guard) */
    (void)bb_driver;

    /* M-BB-LIVE-WIRE: propagate BB mode to stmt_exec.c */
    if (bb_live) g_bb_mode = BB_MODE_LIVE;

    if (argi >= argc) {
        fprintf(stderr,
            "usage: scrip [mode] [bb] [options] <file> [-- program-args...]\n"
            "\n"
            "Execution modes (default: --sm-run):\n"
            "  --ir-run         interpret via IR tree-walk (correctness reference)\n"
            "  --sm-run         interpret SM_Program via dispatch loop  [DEFAULT]\n"
            "  --jit-run        SM_Program -> x86 bytes -> mmap slab -> jump in\n"
            "\n"
            "Byrd Box pattern mode (default: --bb-driver):\n"
            "  --bb-driver      pattern matching via driver/broker\n"
            "  --bb-live        live-wired BB blobs in exec memory (requires M-DYN-B* blobs)\n"
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
     * --dump-parse / --dump-parse-flat / --dump-ir  →  CMPILE (hand-written)
     * everything else (--ir-run, --sm-run, --dump-ir-bison)  →  sno_parse (Bison/Flex)
     * sno_parse is the proven path: PASS=190 baseline.
     * .sc extension  →  snocone_compile() (lex+parse+lower in one call) */
    /* detect Snocone frontend by file extension */
    int lang_snocone = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".sc") == 0) lang_snocone = 1; }
    int lang_prolog = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".pl") == 0) lang_prolog = 1; }
    int lang_icon = 0;
    { const char *dot = strrchr(input_path, '.'); if (dot && strcmp(dot, ".icn") == 0) lang_icon = 1; }
    int lang_polyglot = 0;  /* U-13: .scrip or .md → fenced polyglot */
    { const char *dot = strrchr(input_path, '.');
      if (dot && (strcmp(dot, ".scrip") == 0 || strcmp(dot, ".md") == 0)) lang_polyglot = 1; }

    Program *prog = NULL;
    if (lang_polyglot) {
        /* U-13: read whole file, split fenced blocks, compile each, merge into one Program* */
        fseek(f, 0, SEEK_END); long flen = ftell(f); rewind(f);
        char *src = malloc(flen + 1);
        if (!src) { fprintf(stderr, "scrip: out of memory\n"); return 1; }
        fread(src, 1, flen, f); src[flen] = '\0'; fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        prog = parse_scrip_polyglot(src, input_path);
        free(src);
    } else if (lang_snocone || lang_prolog || lang_icon) {
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
        /* --dump-parse / --dump-parse-flat / --dump-ir: bison path (CMPILE removed) */
        fclose(f);
        if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
        FILE *f3 = fopen(input_path, "r");
        if (!f3) { fprintf(stderr, "scrip: cannot re-open '%s'\n", input_path); return 1; }
        Program *dprog = sno_parse(f3, input_path);
        fclose(f3);
        ir_dump_program(dprog, stdout);
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

    /* S-10 fix: register scrip.c-only builtins so APPLY_fn can dispatch them
     * at match time (used by *IDENT(x) / *DIFFER(x) in pattern position). */
    register_fn("IDENT",  _builtin_IDENT,  1, 2);
    register_fn("DIFFER", _builtin_DIFFER, 1, 2);
    register_fn("EVAL",   _builtin_EVAL,   1, 1);
    register_fn("CODE",   _builtin_CODE,   1, 1);
    register_fn("DATA",   _builtin_DATA,   1, 1);

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
    } else if (lang_polyglot) {
        execute_program(prog);   /* U-13: polyglot — SNO path for now; U-15 adds per-stmt dispatch */
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
