#ifndef SNOBOL4_LEX_H
#define SNOBOL4_LEX_H

#include <stdio.h>

/* TK_* token values are defined in snobol4.tab.h (bison-generated, 258+).
 * Token.kind holds those values as a plain int — no separate enum here. */

typedef struct {
    int         kind;   /* T_* from snobol4.tab.h */
    const char *sval;
    long        ival;
    double      dval;
    int         lineno;
} Token;

typedef struct Lex {
    int    lineno;
    Token  peek;
    int    peeked;
    void  *_scanner;
    void  *_extra;
} Lex;

void  lex_open_str(Lex *lx, const char *s, int len, int lineno);
Token lex_next    (Lex *lx);
Token lex_peek    (Lex *lx);
int   lex_at_end  (Lex *lx);
void  lex_destroy (Lex *lx);

void  flex_lex_open   (Lex *lx, FILE *f, const char *fname);
Token flex_lex_next   (Lex *lx);
void  flex_lex_destroy(Lex *lx);

typedef struct SnoLine {
    char *label; char *body; char *goto_str; int lineno; int is_end;
} SnoLine;

typedef struct { SnoLine *a; int n, cap; } LineArray;

#endif /* SNOBOL4_LEX_H */
