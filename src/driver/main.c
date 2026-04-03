/*
 * main.c — scrip-cc driver
 *
 * Usage (gcc-style):
 *   scrip-cc -asm file1.sno file2.sno ...   → file1.s  file2.s  ...
 *   scrip-cc -jvm file1.sno file2.sno ...   → file1.j  file2.j  ...
 *   scrip-cc -net file1.sno file2.sno ...   → file1.il file2.il ...
 *   scrip-cc -wasm file1.sno file2.sno ...  → file1.wat file2.wat ...
 *   scrip-cc -asm -o out.s file.sno         → out.s  (single file, explicit output)
 *   scrip-cc -asm                           → stdout (stdin mode)
 *
 * Multiple source files: each input produces one output alongside the source
 * (suffix replaced). -o is an error with more than one input file.
 * Frontend auto-detected from suffix: .sc→Snocone .pl→Prolog .icn→Icon.
 *
 * Milestone: M-G-INV-EMIT — multi-file support for emit-diff invariant check
 */
#include "scrip_cc.h"
#include "emit_x64_snocone.h"
#include "prolog_atom.h"
#include "prolog_parse.h"
#include "prolog_lower.h"
#include "icon_lex.h"
#include "icon_ast.h"
#include "icon_parse.h"
#include "icon_emit.h"
#include "icon_lower.h"
#include "../frontend/rebus/rebus.h"
#include "../frontend/rebus/rebus_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int trampoline_mode;
void asm_emit(Program *prog, FILE *f);
void asm_emit_prolog(Program *prog, FILE *f);
extern int asm_body_mode;
void jvm_emit(Program *prog, FILE *f, const char *filename);
void net_emit(Program *prog, FILE *f, const char *filename);
void emit_wasm(Program *prog, FILE *f, const char *filename);
void pl_emit(Program *prog, FILE *f);
void prolog_emit_jvm(Program *prog, FILE *f, const char *filename);
void prolog_emit_net(Program *prog, FILE *f, const char *filename);
void prolog_emit_wasm(Program *prog, FILE *f, const char *filename);
void pl_linker_prescan(PlProgram *pl_prog);
ImportEntry *icn_prescan_imports(const char *src);
void emit_jvm_icon_file(EXPR_t **nodes, int count, FILE *out,
                  const char *filename, const char *outpath, ImportEntry *imports);
void emit_wasm_icon_file(EXPR_t **nodes, int count, FILE *out,
                  const char *filename);
void js_emit(Program *prog, FILE *f);

static int asm_mode  = 0;
static int dump_ir   = 0;
static int jvm_mode  = 0;
static int net_mode  = 0;
static int wasm_mode = 0;
static int js_mode   = 0;
static int sc_mode   = 0;
static int pl_mode   = 0;
static int icn_mode  = 0;
static int reb_mode  = 0;
static int fold_mode = 1;

static int ends_with(const char *f, const char *s) {
    if (!f) return 0;
    size_t fl = strlen(f), sl = strlen(s);
    return fl >= sl && strcmp(f + fl - sl, s) == 0;
}

static char *read_all(FILE *f) {
    size_t cap = 4096, used = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf+used, 1, cap-used-1, f)) > 0) {
        used += n;
        if (used+1 >= cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) return NULL; }
    }
    buf[used] = '\0';
    return buf;
}

/* Replace suffix of infile with out_ext (e.g. ".s"). Caller frees. */
static char *derive_outname(const char *infile, const char *out_ext) {
    const char *dot = strrchr(infile, '.');
    size_t base = dot ? (size_t)(dot - infile) : strlen(infile);
    size_t elen  = strlen(out_ext);
    char *out = malloc(base + elen + 1);
    if (!out) { perror("malloc"); exit(1); }
    memcpy(out, infile, base);
    memcpy(out + base, out_ext, elen + 1);
    return out;
}

static const char *backend_ext(void) {
    if (asm_mode)  return ".s";
    if (jvm_mode)  return ".j";
    if (net_mode)  return ".il";
    if (wasm_mode) return ".wat";
    if (js_mode)   return ".js";
    return ".c";
}

/* Compile infile (NULL = stdin) and emit to out. outpath used for JVM class name. */
static int compile_one(const char *infile, const char *outpath, FILE *out) {
    sno_reset();   /* clear per-file parser state: nerrors, include dirs */

    int file_sc  = sc_mode  || ends_with(infile, ".sc");
    int file_pl  = pl_mode  || ends_with(infile, ".pl") || ends_with(infile, ".pro");
    int file_icn = icn_mode || ends_with(infile, ".icn");
    int file_reb = reb_mode || ends_with(infile, ".reb");

    FILE *in = stdin;
    if (infile) {
        in = fopen(infile, "r");
        if (!in) { perror(infile); return 1; }
        if (!file_sc && !file_pl && !file_icn) {
            char *dir = strdup(infile);
            char *sl  = strrchr(dir, '/');
            if (sl) { *sl = '\0'; sno_add_include_dir(dir); }
            else sno_add_include_dir(".");
            free(dir);
        }
    }

    int rc = 0;
    Program *prog = NULL;

    if (file_icn) {
        char *src = read_all(in);
        if (!src) { fprintf(stderr, "scrip-cc: read error\n"); rc = 1; goto done; }
        ImportEntry *imports = icn_prescan_imports(src);
        IcnLexer lx; icn_lex_init(&lx, src);
        IcnParser parser; icn_parse_init(&parser, &lx);
        int count = 0;
        IcnNode **procs = icn_parse_file(&parser, &count);
        free(src);
        if (parser.had_error) {
            fprintf(stderr, "scrip-cc: Icon parse error: %s\n", parser.errmsg);
            rc = 1; goto done;
        }
        {
            int lcount = 0;
            EXPR_t **lowered = icon_lower_file(procs, count, &lcount);
            if (jvm_mode)       emit_jvm_icon_file(lowered, lcount, out, infile, outpath, imports);
            else if (wasm_mode) emit_wasm_icon_file(lowered, lcount, out, infile);
            else                { icn_emit_file(lowered, lcount, out); }
            free(lowered);
        }
        for (int i = 0; i < count; i++) icn_node_free(procs[i]);
        free(procs);
        goto done;
    }

    if (file_pl) {
        char *src = read_all(in);
        if (!src) { fprintf(stderr, "scrip-cc: read error\n"); rc = 1; goto done; }
        prolog_atom_init();
        PlProgram *pl_prog = prolog_parse(src, infile ? infile : "<stdin>");
        free(src);
        if (pl_prog->nerrors) {
            fprintf(stderr, "scrip-cc: %d Prolog parse error(s)\n", pl_prog->nerrors);
            rc = 1; goto done;
        }
        prog = prolog_lower(pl_prog);
        if (jvm_mode) pl_linker_prescan(pl_prog);
        prolog_program_free(pl_prog);
        if (!prog) { rc = 1; goto done; }
        if      (dump_ir)   prolog_lower_pretty(prog, out);
        else if (asm_mode)  asm_emit_prolog(prog, out);
        else if (jvm_mode)  prolog_emit_jvm(prog, out, infile);
        else if (net_mode)  prolog_emit_net(prog, out, infile);
        else if (wasm_mode) prolog_emit_wasm(prog, out, infile);
        else                pl_emit(prog, out);
        goto done;
    }

    if (file_reb) {
        RProgram *rp = rebus_parse(in, infile ? infile : "<stdin>");
        if (rebus_nerrors) {
            fprintf(stderr, "scrip-cc: %d rebus error(s)\n", rebus_nerrors);
            rc = 1; goto done;
        }
        prog = rebus_lower(rp);
        if (!prog) { rc = 1; goto done; }
        if      (asm_mode)  asm_emit(prog, out);
        else if (jvm_mode)  jvm_emit(prog, out, infile);
        else if (net_mode)  net_emit(prog, out, infile);
        else if (wasm_mode) emit_wasm(prog, out, infile);
        else                c_emit(prog, out);
        goto done;
    }

    if (file_sc) {
        char *src = read_all(in);
        if (!src) { fprintf(stderr, "scrip-cc: read error\n"); rc = 1; goto done; }
        /* emit_x64_snocone_compile is the Snocone lowering pass (SC-1).
           Always use it regardless of backend (M-G5-LOWER-SNOCONE-FIX). */
        prog = emit_x64_snocone_compile(src, infile ? infile : "<stdin>");
        free(src);
        if (!prog) { rc = 1; goto done; }
    } else {
        prog = sno_parse(in, infile ? infile : "<stdin>");
        if (sno_nerrors) {
            fprintf(stderr, "scrip-cc: %d error(s)\n", sno_nerrors); rc = 1; goto done;
        }
    }

    if      (asm_mode)  asm_emit(prog, out);
    else if (jvm_mode)  jvm_emit(prog, out, infile);
    else if (net_mode)  net_emit(prog, out, infile);
    else if (wasm_mode) emit_wasm(prog, out, infile);
    else if (js_mode)   js_emit(prog, out);
    else                c_emit(prog, out);

done:
    if (infile && in != stdin) fclose(in);
    return rc;
}

int main(int argc, char *argv[]) {
    const char *explicit_out = NULL;
    const char *files[1024];
    int nfiles = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-I") && i+1 < argc) {
            sno_add_include_dir(argv[++i]);
        } else if (!strncmp(argv[i], "-I", 2)) {
            sno_add_include_dir(argv[i]+2);
        } else if (!strcmp(argv[i], "-o") && i+1 < argc) {
            explicit_out = argv[++i];
        } else if (!strcmp(argv[i], "-trampoline")) { trampoline_mode = 1;
        } else if (!strcmp(argv[i], "-dump-ir"))      { dump_ir = 1;
        } else if (!strcmp(argv[i], "-asm"))         { asm_mode = 1;
        } else if (!strcmp(argv[i], "-jvm"))         { jvm_mode = 1;
        } else if (!strcmp(argv[i], "-icn"))         { icn_mode = 1;
        } else if (!strcmp(argv[i], "-net"))         { net_mode = 1;
        } else if (!strcmp(argv[i], "-wasm"))        { wasm_mode = 1;
        } else if (!strcmp(argv[i], "-js"))          { js_mode = 1;
        } else if (!strcmp(argv[i], "-asm-body"))    { asm_mode = 1; asm_body_mode = 1;
        } else if (!strcmp(argv[i], "-pl"))          { pl_mode = 1;
        } else if (!strcmp(argv[i], "-sc"))          { sc_mode = 1;
        } else if (!strcmp(argv[i], "-reb"))         { reb_mode = 1;
        } else if (!strcmp(argv[i], "-F"))           { fold_mode = 1;
        } else if (!strcmp(argv[i], "-f"))           { fold_mode = 0;
        } else if (argv[i][0] != '-') {
            if (nfiles >= 1024) { fprintf(stderr, "scrip-cc: too many input files\n"); return 1; }
            files[nfiles++] = argv[i];
        } else {
            fprintf(stderr, "scrip-cc: unknown option %s\n", argv[i]); return 1;
        }
    }

    /* stdin mode */
    if (nfiles == 0) {
        FILE *out = stdout;
        if (explicit_out) { out = fopen(explicit_out, "w"); if (!out) { perror(explicit_out); return 1; } }
        int rc = compile_one(NULL, explicit_out, out);
        if (explicit_out) fclose(out);
        return rc;
    }

    /* -o with multiple files: error */
    if (explicit_out && nfiles > 1) {
        fprintf(stderr, "scrip-cc: -o cannot be used with multiple input files\n"); return 1;
    }

    /* Single file, explicit -o */
    if (nfiles == 1 && explicit_out) {
        FILE *out = fopen(explicit_out, "w");
        if (!out) { perror(explicit_out); return 1; }
        int rc = compile_one(files[0], explicit_out, out);
        fclose(out);
        return rc;
    }

    /* One or more files: gcc-style, derive output name per input */
    const char *ext = backend_ext();
    int any_error = 0;
    for (int i = 0; i < nfiles; i++) {
        char *outname = derive_outname(files[i], ext);
        FILE *out = fopen(outname, "w");
        if (!out) { perror(outname); free(outname); any_error = 1; continue; }
        int rc = compile_one(files[i], outname, out);
        fclose(out);
        if (rc) { fprintf(stderr, "scrip-cc: error compiling %s\n", files[i]); any_error = 1; }
        free(outname);
    }
    return any_error ? 1 : 0;
}
