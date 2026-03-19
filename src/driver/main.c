/*
 * main.c — sno2c driver
 * Usage: sno2c [-I dir] [-o out.c] [-trampoline] [-sc] input.sno|input.sc
 *
 * Sprint SC3: -sc flag (or .sc suffix) routes through the Snocone frontend
 * (sc_lex → sc_parse per-stmt → sc_lower) instead of the SNOBOL4 frontend.
 * The resulting Program* is passed to the same snoc_emit / asm_emit backend.
 */
#include "sno2c.h"
#include "sc_driver.h"   /* sc_compile() — Snocone expression-only pipeline */
#include "sc_cf.h"       /* sc_cf_compile() — full control-flow lowering (SC4-ASM) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int trampoline_mode;  /* defined in emit.c */
void asm_emit(Program *prog, FILE *f); /* defined in emit_byrd_asm.c */
extern int asm_body_mode;    /* defined in emit_byrd_asm.c */
void jvm_emit(Program *prog, FILE *f, const char *filename); /* emit_byrd_jvm.c */
void net_emit(Program *prog, FILE *f, const char *filename); /* net_emit.c */

static int asm_mode = 0;    /* -asm flag: emit x64 NASM instead of C */
static int jvm_mode = 0;    /* -jvm flag: emit JVM Jasmin text */
static int net_mode = 0;    /* -net flag: emit .NET CIL text */
static int sc_mode  = 0;    /* -sc  flag: Snocone frontend */

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
        } else if (!strcmp(argv[i],"-sc")) {
            sc_mode = 1;
        } else if (argv[i][0]!='-') {
            infile = argv[i];
        } else {
            fprintf(stderr,"sno2c: unknown option %s\n",argv[i]); return 1;
        }
    }

    /* Auto-detect Snocone by .sc suffix */
    if (!sc_mode && ends_with(infile, ".sc"))
        sc_mode = 1;

    FILE *in = stdin;
    if (infile) {
        in = fopen(infile,"r");
        if (!in) { perror(infile); return 1; }
        if (!sc_mode) {
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

    if (sc_mode) {
        /* ---- Snocone frontend ------------------------------------------ */
        char *src = read_all(in);
        if (!src) { fprintf(stderr,"sno2c: read error\n"); return 1; }
        if (asm_mode) {
            /* ASM backend: use full control-flow lowering (SC4-ASM) */
            prog = sc_cf_compile(src, infile ? infile : "<stdin>");
        } else {
            /* C backend: expression-only pipeline (SC3, legacy) */
            prog = sc_compile(src, infile ? infile : "<stdin>");
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
        snoc_emit(prog, out);

    if (infile)  fclose(in);
    if (outfile) fclose(out);
    return 0;
}
