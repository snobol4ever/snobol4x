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
 *   --trace          MONITOR trace output (for two-way diff vs CSNOBOL4)
 *   --bench          print wall-clock time after execution
 *
 * Frontend inferred from extension:
 *   .sno=SNOBOL4  .icn=Icon  .pl=Prolog  .sc=Snocone  .reb=Rebus
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
#include "../frontend/snocone/snocone_cf.h"
#include "../frontend/prolog/prolog_driver.h"
#include "../frontend/prolog/term.h"            /* Term — needed by Prolog globals block */
#include "../frontend/prolog/prolog_runtime.h"  /* Trail — needed by Prolog globals block */
#include "../frontend/prolog/prolog_atom.h"     /* prolog_atom_name — U-23: 64-bit ptr, must be declared */
#include "../frontend/prolog/prolog_builtin.h"  /* interp_exec_pl_builtin declaration */
#include "../frontend/prolog/pl_broker.h"       /* pl_box_choice, pl_box_* — S-BB-7; pl_exec_goal removed U-11 */
#include "../frontend/icon/icon_driver.h"
#include "../frontend/raku/raku_driver.h"
#include "../frontend/rebus/rebus_lower.h"
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
#include "sync_monitor.h"               /* IM-7: --monitor in-process comparator */
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

#include "../runtime/interp/icn_runtime.h"
#include "../runtime/interp/pl_runtime.h"
#include "interp.h"   /* FI-6: interp loop extracted to interp.c */

#include "driver/polyglot.h"   /* FI-7: polyglot layer extracted to polyglot.c */

int main(int argc, char **argv)
{
    /* ── flag parsing ─────────────────────────────────────────────────── */

    /* Execution modes — mutually exclusive (default: --sm-run) */
    int mode_ir_run        = 0;  /* --ir-run   : interpret via IR tree-walk (correctness ref) */
    int mode_sm_run        = 0;  /* --sm-run   : interpret SM_Program via dispatch loop [DEFAULT] */
    int mode_jit_run       = 0;  /* --jit-run  : SM_Program -> x86 bytes -> mmap slab -> jump in */
    int mode_monitor       = 0;  /* --monitor  : in-process sync comparator (IR vs SM vs JIT) */

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
        else if (strcmp(argv[argi], "--monitor")       == 0) { mode_monitor       = 1; argi++; }
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

    /* Default execution mode: --sm-run (not when --monitor) */
    if (!mode_ir_run && !mode_sm_run && !mode_jit_run && !mode_monitor)
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
            "  --monitor        in-process sync comparator (IR vs SM vs JIT)\n"
            "\n"
            "Byrd Box pattern mode (default: --bb-driver):\n"
            "  --bb-driver      pattern matching via driver/broker\n"
            "  --bb-live        live-wired BB blobs in exec memory (requires M-DYN-B* blobs)\n"
            "\n"
            "Diagnostic options:\n"
            "  --dump-ir        print IR after frontend\n"
            "  --dump-sm        print SM_Program after lowering\n"
            "  --dump-bb        print BB-GRAPH for each statement\n"
            "  --trace          MONITOR trace output (diff vs CSNOBOL4)\n"
            "  --bench          print wall-clock time after execution\n"
            "  --dump-parse     dump CMPILE parse tree\n"
            "  --dump-parse-flat  dump CMPILE parse tree (one line)\n"
            "  --dump-ir-bison  dump IR via old Bison/Flex parser\n"
            "\n"
            "Frontend inferred from file extension:\n"
            "  .sno=SNOBOL4  .icn=Icon  .pl=Prolog  .sc=Snocone  .reb=Rebus\n"
        );
        return 1;
    }
    /* ── Multi-file load (U-MULTIFILE) ─────────────────────────────────────
     * All remaining argv entries are input files.  Each is compiled with the
     * appropriate frontend (by extension) and merged into one Program* in
     * source order.  This enables:
     *   scrip --ir-run lib.pl main.pl
     *   scrip --ir-run shim.pl test_arith.pl
     *   scrip --ir-run base.sno ext.icn main.pl   (polyglot multi-file)
     * A single .scrip/.md file is still handled via parse_scrip_polyglot.
     * --dump-parse/--dump-ir only act on the first file (unchanged). */

    extern void sno_add_include_dir(const char *d);

    struct timespec _t0, _t1, _t2, _t3;
    if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t0);

    /* Scan all input files to detect if any non-SNO language is present */
    int first_file_argi = argi; (void)first_file_argi;
    int has_non_sno = 0;
    for (int fi = argi; fi < argc; fi++) {
        const char *d = strrchr(argv[fi], '.');
        if (d && (strcmp(d,".pl")==0 || strcmp(d,".icn")==0 ||
                  strcmp(d,".raku")==0 || strcmp(d,".reb")==0 ||
                  strcmp(d,".sc")==0 || strcmp(d,".scrip")==0 || strcmp(d,".md")==0))
            has_non_sno = 1;
    }

    Program *prog = NULL;

    for (; argi < argc; argi++) {
        const char *input_path = argv[argi];

        /* Add include dirs for each file's directory */
        {
            char dirbuf[4096];
            strncpy(dirbuf, input_path, sizeof dirbuf - 1);
            dirbuf[sizeof dirbuf - 1] = '\0';
            char *sl = strrchr(dirbuf, '/');
            if (sl) { *sl = '\0'; sno_add_include_dir(dirbuf); }
            else     { sno_add_include_dir("."); }
            const char *sno_lib = getenv("SNO_LIB");
            if (sno_lib && *sno_lib) sno_add_include_dir(sno_lib);
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
            sno_add_include_dir(".");
        }

        /* Detect language from extension */
        const char *dot = strrchr(input_path, '.');
        int lang_snocone  = dot && strcmp(dot, ".sc")   == 0;
        int lang_prolog   = dot && strcmp(dot, ".pl")   == 0;
        int lang_icon     = dot && strcmp(dot, ".icn")  == 0;
        int lang_raku     = dot && strcmp(dot, ".raku") == 0;
        int lang_rebus    = dot && strcmp(dot, ".reb")  == 0;
        int lang_polyglot = dot && (strcmp(dot, ".scrip") == 0 || strcmp(dot, ".md") == 0);

        Program *sub = NULL;

        if (lang_polyglot) {
            g_polyglot = 1;
            FILE *f = fopen(input_path, "r");
            if (!f) { fprintf(stderr, "scrip: cannot open '%s'\n", input_path); return 1; }
            fseek(f, 0, SEEK_END); long flen = ftell(f); rewind(f);
            char *src = malloc(flen + 1);
            if (!src) { fprintf(stderr, "scrip: out of memory\n"); return 1; }
            fread(src, 1, flen, f); src[flen] = '\0'; fclose(f);
            sub = parse_scrip_polyglot(src, input_path);
            free(src);
        } else if (lang_snocone || lang_prolog || lang_icon || lang_raku || lang_rebus) {
            FILE *f = fopen(input_path, "r");
            if (!f) { fprintf(stderr, "scrip: cannot open '%s'\n", input_path); return 1; }
            fseek(f, 0, SEEK_END); long flen = ftell(f); rewind(f);
            char *src = malloc(flen + 1);
            if (!src) { fprintf(stderr, "scrip: out of memory\n"); return 1; }
            fread(src, 1, flen, f); src[flen] = '\0'; fclose(f);
            sub = lang_raku   ? raku_compile(src, input_path)
                : lang_prolog ? prolog_compile(src, input_path)
                : lang_icon   ? icon_compile(src, input_path)
                : lang_rebus  ? rebus_compile(src, input_path)
                :               snocone_cf_compile(src, input_path);
            free(src);
        } else if (dump_parse || dump_parse_flat || dump_ir) {
            /* Dump modes only process the first file */
            FILE *f = fopen(input_path, "r");
            if (!f) { fprintf(stderr, "scrip: cannot open '%s'\n", input_path); return 1; }
            if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);
            Program *dprog = sno_parse(f, input_path);
            fclose(f);
            ir_dump_program(dprog, stdout);
            return 0;
        } else {
            FILE *f = fopen(input_path, "r");
            if (!f) { fprintf(stderr, "scrip: cannot open '%s'\n", input_path); return 1; }
            sub = sno_parse(f, input_path);
            fclose(f);
            if (dump_ir_bison) { ir_dump_program(sub, stdout); return 0; }
        }

        if (!sub || !sub->head) {
            fprintf(stderr, "scrip: parse failed for '%s'\n", input_path);
            return 1;
        }

        /* Merge sub into accumulated prog */
        if (!prog) {
            prog = sub;
        } else {
            prog->tail->next = sub->head;
            prog->tail       = sub->tail;
            prog->nstmts    += sub->nstmts;
            free(sub);
        }
    }

    if (opt_bench) clock_gettime(CLOCK_MONOTONIC, &_t1);

    /* Primary input path (last file, used for --monitor label) */
    const char *input_path = argv[argc - 1];

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
    register_fn("print",  _builtin_print,  0, 99);

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

    if (mode_monitor) {
        /* IM-7: --monitor — in-process sync comparator.
         * Runs IR, SM, and JIT step-by-step over the same Program,
         * snapshot/restoring state between each run.
         * Returns 0 if all three agree; exits non-zero on first divergence. */
        label_table_build(prog);
        prescan_defines(prog);
        g_sno_err_active = 1;
        int div_stmt = sync_monitor_run(prog, 1 /* verbose */, input_path);
        if (div_stmt != 0) {
            fprintf(stderr, "scrip --monitor: DIVERGE at stmt %d\n", div_stmt);
            return 1;
        }
        return 0;
    } else if (has_non_sno) {
        polyglot_execute(prog);   /* OE-7: polyglot takes priority */
    } else if (mode_sm_run) {
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
    } else if (has_non_sno) {
        polyglot_execute(prog);
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
