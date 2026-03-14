/*
 * main.c — sno2c driver
 * Usage: sno2c [-I dir] [-o out.c] input.sno
 */
#include "sno2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    const char *outfile = NULL, *infile = NULL;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"-I") && i+1<argc) {
            snoc_add_include_dir(argv[++i]);
        } else if (!strncmp(argv[i],"-I",2)) {
            snoc_add_include_dir(argv[i]+2);
        } else if (!strcmp(argv[i],"-o") && i+1<argc) {
            outfile = argv[++i];
        } else if (argv[i][0]!='-') {
            infile = argv[i];
        } else {
            fprintf(stderr,"sno2c: unknown option %s\n",argv[i]); return 1;
        }
    }

    FILE *in = stdin;
    if (infile) {
        in = fopen(infile,"r");
        if (!in) { perror(infile); return 1; }
        char *dir = strdup(infile);
        char *sl  = strrchr(dir,'/');
        if (sl) { *sl='\0'; snoc_add_include_dir(dir); }
        else snoc_add_include_dir(".");
        free(dir);
    }

    FILE *out = stdout;
    if (outfile) {
        out = fopen(outfile,"w");
        if (!out) { perror(outfile); return 1; }
    }

    Program *prog = snoc_parse(in, infile ? infile : "<stdin>");

    if (snoc_nerrors) {
        fprintf(stderr,"sno2c: %d error(s)\n", snoc_nerrors); return 1;
    }

    snoc_emit(prog, out);

    if (infile)  fclose(in);
    if (outfile) fclose(out);
    return 0;
}
