/*
 * icn_main.c — Tiny-ICON compiler driver
 *
 * Originally a standalone binary.  Now integrated into sno2c:
 *   - icn_main() is the old main(), kept for reference / standalone use.
 *   - icn_prescan_imports() pre-scans raw source for $import/-IMPORT control lines
 *     and returns an ImportEntry* list (same type as SNOBOL4 lex uses).
 *     Called by sno2c main.c before icn_lex_init(), mirrors pj_linker_prescan().
 *
 * Usage (standalone):  sno2c [-jvm] [-o out] file.icn
 * Usage (via sno2c):   sno2c -icn [-jvm] [-o out] file.icn
 */

#include "icon_lex.h"
#include "icon_ast.h"
#include "icon_parse.h"
#include "icon_emit.h"
#include "sno2c.h"          /* ExportEntry, ImportEntry */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declaration for JVM emitter */
void ij_emit_file(IcnNode **nodes, int count, FILE *out, const char *filename,
                  const char *outpath, ImportEntry *imports);

/* =========================================================================
 * icn_prescan_imports — scan raw Icon source for $import / -IMPORT lines.
 *
 * Icon control line forms accepted:
 *   $import assembly.METHOD
 *   -IMPORT assembly.METHOD      (SNOBOL4-style, also accepted for symmetry)
 *
 * Returns a singly-linked ImportEntry* list (caller owns; never freed in
 * practice — lives for the duration of compilation).
 * ======================================================================= */
ImportEntry *icn_prescan_imports(const char *src) {
    ImportEntry *head = NULL;
    const char *p = src;
    while (*p) {
        /* skip to start of line */
        /* scan for $import or -IMPORT at column 0 */
        /* skip leading whitespace on line */
        const char *line = p;
        while (*p && *p != '\n') p++;
        /* process [line, p) */
        const char *lp = line;
        while (*lp == ' ' || *lp == '\t') lp++;
        int is_import = 0;
        if (*lp == '$' && strncasecmp(lp+1, "import", 6) == 0 &&
            (lp[7] == ' ' || lp[7] == '\t' || lp[7] == '\0' || lp[7] == '\n'))
            { is_import = 1; lp += 7; }
        else if (*lp == '-' && strncasecmp(lp+1, "IMPORT", 6) == 0 &&
            (lp[7] == ' ' || lp[7] == '\t' || lp[7] == '\0' || lp[7] == '\n'))
            { is_import = 1; lp += 7; }
        if (is_import) {
            while (*lp == ' ' || *lp == '\t') lp++;
            /* collect token up to whitespace/newline/end */
            char tok[256]; int ti = 0;
            while (*lp && *lp != ' ' && *lp != '\t' && *lp != '\n' && ti < 255)
                tok[ti++] = *lp++;
            tok[ti] = '\0';
            if (ti > 0) {
                ImportEntry *e = calloc(1, sizeof *e);
                /* tok is "assembly.METHOD" */
                char *dot = strchr(tok, '.');
                if (dot) {
                    int alen = (int)(dot - tok);
                    char asmname[256] = {0};
                    strncpy(asmname, tok, alen < 255 ? alen : 255);
                    e->name   = strdup(asmname);
                    e->method = strdup(dot + 1);
                } else {
                    e->name   = strdup(tok);
                    e->method = strdup(tok);
                }
                e->lang = strdup("ICON");
                e->next = head;
                head = e;
            }
        }
        if (*p == '\n') p++;
    }
    return head;
}

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

/* icn_main — standalone entry point; not called by sno2c; not called by sno2c */
int icn_main(int argc, char **argv) {
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
    if (!input) { fprintf(stderr, "usage: sno2c [-jvm] [-o out.j/.asm] [-run] file.icn\n"); return 1; }

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
        ij_emit_file(procs, count, out_file, input, output, NULL);
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
