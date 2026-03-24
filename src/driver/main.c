/*
 * main.c — sno2c driver
 * Usage: sno2c [-I dir] [-o out.c] [-trampoline] [-sc] [-pl] input.sno|input.sc|input.pl
 *
 * Sprint SC3: -sc flag (or .sc suffix) routes through the Snocone frontend.
 * Sprint PL1: -pl flag (or .pl suffix) routes through the Prolog frontend
 *   (prolog_lex -> prolog_parse -> prolog_lower -> prolog_emit).
 *   Produces C with Byrd box α/β/γ/ω four-port clause selection.
 */
#include "sno2c.h"
#include "snocone_driver.h"   /* snocone_compile() */
#include "snocone_cf.h"       /* snocone_cf_compile() */
#include "prolog_atom.h"
#include "prolog_parse.h"
#include "prolog_lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int trampoline_mode;
void asm_emit(Program *prog, FILE *f);
void asm_emit_prolog(Program *prog, FILE *f);
extern int asm_body_mode;
void jvm_emit(Program *prog, FILE *f, const char *filename);
void net_emit(Program *prog, FILE *f, const char *filename);
void pl_emit(Program *prog, FILE *f);   /* defined in prolog_emit.c */
void prolog_emit_jvm(Program *prog, FILE *f, const char *filename); /* prolog_emit_jvm.c */

static int asm_mode = 0;
static int jvm_mode = 0;
static int net_mode = 0;
static int sc_mode  = 0;
static int pl_mode  = 0;    /* -pl flag: Prolog frontend */
/* Case folding (SPITBOL-compatible switch names):
 *   -F = fold identifiers to uppercase (DEFAULT; matches SPITBOL/CSNOBOL4 default).
 *   -f = do not fold (case-sensitive).
 * Today: accepted, recorded in fold_mode, but lexer does not yet fold at parse
 * time — runtime folds on lookup so correctness is preserved.
 * Full parse-time fold is milestone M-SNO2C-FOLD. */
static int fold_mode = 1;   /* 1=fold(default,-F)  0=no-fold(-f) */

/* Return 1 if filename ends with suffix (case-sensitive). */
static int ends_with(const char *filename, const char *suffix) {
    if (!filename) return 0;
    size_t fl = strlen(filename), sl = strlen(suffix);
    return fl >= sl && strcmp(filename + fl - sl, suffix) == 0;
}

/* Read all of FILE *f into a heap-allocated NUL-terminated string. */
static char *read_all(FILE *f) {
    size_t cap = 4096, used = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + used, 1, cap - used - 1, f)) > 0) {
        used += n;
        if (used + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) return NULL;
        }
    }
    buf[used] = '\0';
    return buf;
}

int main(int argc, char *argv[]) {
    const char *outfile = NULL, *infile = NULL;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"-I") && i+1<argc) {
            snoc_add_include_dir(argv[++i]);
        } else if (!strncmp(argv[i],"-I",2)) {
            snoc_add_include_dir(argv[i]+2);
        } else if (!strcmp(argv[i],"-o") && i+1<argc) {
            outfile = argv[++i];
        } else if (!strcmp(argv[i],"-trampoline")) {
            trampoline_mode = 1;
        } else if (!strcmp(argv[i],"-asm")) {
            asm_mode = 1;
        } else if (!strcmp(argv[i],"-jvm")) {
            jvm_mode = 1;
        } else if (!strcmp(argv[i],"-net")) {
            net_mode = 1;
        } else if (!strcmp(argv[i],"-asm-body")) {
            asm_mode = 1;
            asm_body_mode = 1;
        } else if (!strcmp(argv[i],"-pl")) {
            pl_mode = 1;
        } else if (!strcmp(argv[i],"-sc")) {
            sc_mode = 1;
        } else if (!strcmp(argv[i],"-F")) {
            fold_mode = 1;   /* fold ON  — default; explicit -F is a no-op */
        } else if (!strcmp(argv[i],"-f")) {
            fold_mode = 0;   /* fold OFF — no-op until M-SNO2C-FOLD */
        } else if (argv[i][0]!='-') {
            infile = argv[i];
        } else {
            fprintf(stderr,"sno2c: unknown option %s\n",argv[i]); return 1;
        }
    }

    /* Auto-detect Snocone by .sc suffix */
    if (!sc_mode && ends_with(infile, ".sc"))
        sc_mode = 1;

    /* Auto-detect Prolog by .pl suffix */
    if (!pl_mode && ends_with(infile, ".pl"))
        pl_mode = 1;

    FILE *in = stdin;
    if (infile) {
        in = fopen(infile,"r");
        if (!in) { perror(infile); return 1; }
        if (!sc_mode && !pl_mode) {
            /* SNOBOL4 frontend: add directory to include search path */
            char *dir = strdup(infile);
            char *sl  = strrchr(dir,'/');
            if (sl) { *sl='\0'; snoc_add_include_dir(dir); }
            else snoc_add_include_dir(".");
            free(dir);
        }
    }

    FILE *out = stdout;
    if (outfile) {
        out = fopen(outfile,"w");
        if (!out) { perror(outfile); return 1; }
    }

    Program *prog;

    if (pl_mode) {
        /* ---- Prolog frontend (M-PROLOG-R1+) ----------------------------- */
        char *src = read_all(in);
        if (!src) { fprintf(stderr,"sno2c: read error\n"); return 1; }
        prolog_atom_init();
        PlProgram *pl_prog = prolog_parse(src, infile ? infile : "<stdin>");
        free(src);
        if (pl_prog->nerrors) {
            fprintf(stderr,"sno2c: %d Prolog parse error(s)\n", pl_prog->nerrors);
            return 1;
        }
        prog = prolog_lower(pl_prog);
        prolog_program_free(pl_prog);
        if (!prog) { return 1; }
        /* Route: -pl -asm  -> x64 ASM backend
         *        -pl -jvm  -> JVM Jasmin backend (M-PJ-SCAFFOLD+)
         *        -pl       -> C backend (existing pl_emit) */
        if (asm_mode)
            asm_emit_prolog(prog, out);
        else if (jvm_mode)
            prolog_emit_jvm(prog, out, infile);
        else
            pl_emit(prog, out);
        if (infile)  fclose(in);
        if (outfile) fclose(out);
        return 0;
    } else if (sc_mode) {
        /* ---- Snocone frontend ------------------------------------------ */
        char *src = read_all(in);
        if (!src) { fprintf(stderr,"sno2c: read error\n"); return 1; }
        if (asm_mode) {
            /* ASM backend: use full control-flow lowering (SC4-ASM) */
            prog = snocone_cf_compile(src, infile ? infile : "<stdin>");
        } else {
            /* C backend: expression-only pipeline (SC3, legacy) */
            prog = snocone_compile(src, infile ? infile : "<stdin>");
        }
        free(src);
        if (!prog) { return 1; }
    } else {
        /* ---- SNOBOL4 frontend ------------------------------------------ */
        prog = snoc_parse(in, infile ? infile : "<stdin>");
        if (snoc_nerrors) {
            fprintf(stderr,"sno2c: %d error(s)\n", snoc_nerrors); return 1;
        }
    }

    if (asm_mode)
        asm_emit(prog, out);
    else if (jvm_mode)
        jvm_emit(prog, out, infile);
    else if (net_mode)
        net_emit(prog, out, infile);
    else
        c_emit(prog, out);

    if (infile)  fclose(in);
    if (outfile) fclose(out);
    return 0;
}
