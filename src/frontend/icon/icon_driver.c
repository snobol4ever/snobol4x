/*
 * icon_driver.c — Tiny-ICON compiler driver
 *
 * Usage: icon_driver <file.icn>
 *   Reads Icon source, emits x64 NASM to stdout (or -o file.asm).
 *   With -run: assembles + links + executes.
 */

#include "icon_lex.h"
#include "icon_ast.h"
#include "icon_parse.h"
#include "icon_emit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration for JVM emitter */
void ij_emit_file(IcnNode **nodes, int count, FILE *out, const char *filename, const char *outpath);

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    const char *input  = NULL;
    const char *output = NULL;
    int do_run = 0;
    int do_jvm = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "-run") == 0) do_run = 1;
        else if (strcmp(argv[i], "-jvm") == 0) do_jvm = 1;
        else input = argv[i];
    }
    if (!input) { fprintf(stderr, "usage: icon_driver [-jvm] [-o out.j/.asm] [-run] file.icn\n"); return 1; }

    char *src = read_file(input);
    if (!src) return 1;

    /* Lex */
    IcnLexer lx;
    icn_lex_init(&lx, src);

    /* Parse */
    IcnParser parser;
    icn_parse_init(&parser, &lx);
    int count = 0;
    IcnNode **procs = icn_parse_file(&parser, &count);
    if (parser.had_error) {
        fprintf(stderr, "parse error: %s\n", parser.errmsg);
        free(src); return 1;
    }

    /* Emit */
    FILE *out_file = stdout;
    if (output) { out_file = fopen(output, "w"); if (!out_file) { perror(output); return 1; } }

    if (do_jvm) {
        ij_emit_file(procs, count, out_file, input, output);
    } else {
        IcnEmitter em;
        icn_emit_init(&em, out_file);
        icn_emit_file(&em, procs, count);
    }

    if (output) fclose(out_file);

    /* Free AST */
    for (int i = 0; i < count; i++) icn_node_free(procs[i]);
    free(procs);
    free(src);

    /* Assemble and run */
    if (do_run && output) {
        char obj[256], bin[256], cmd[1024];
        snprintf(obj, sizeof obj, "%s.o", output);
        snprintf(bin, sizeof bin, "%s.bin", output);
        snprintf(cmd, sizeof cmd,
            "nasm -f elf64 %s -o %s && "
            "gcc -nostdlib -no-pie -Wl,--no-warn-execstack %s "
            "src/frontend/icon/icon_runtime.c "
            "-o %s",
            output, obj, obj, bin);
        int r = system(cmd);
        if (r != 0) { fprintf(stderr, "assemble/link failed\n"); return 1; }
        return system(bin);
    }
    return 0;
}
