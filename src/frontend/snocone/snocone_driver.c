/*
 * sc_driver.c — Snocone frontend pipeline driver  (Sprint SC3)
 *
 * Ported directly from the pipeline() helper in
 * test/frontend/snocone/sc_lower_test.c (50/50 PASS, session185).
 *
 * The logic is unchanged; only the API boundary is different:
 *   - reads from a char* buffer instead of a string literal
 *   - returns Program* instead of being a static test helper
 *   - propagates errors through snocone_lower nerrors
 */

#include "snocone_driver.h"
#include "snocone_lex.h"
#include "snocone_parse.h"
#include "snocone_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENTS 1024

Program *snocone_compile(const char *source, const char *filename)
{
    if (!filename) filename = "<stdin>";

    /* ---- 1. Lex --------------------------------------------------------- */
    ScTokenArray ta = snocone_lex(source);

    /* ---- 2. Per-stmt parse (shunting-yard, one segment at a time) ------- */
    ScParseResult *segments = malloc(MAX_SEGMENTS * sizeof(ScParseResult));
    if (!segments) { fprintf(stderr, "snocone_compile: out of memory\n"); return NULL; }
    int nseg = 0;

    /* Combined postfix buffer — will hold tokens from all segments */
    int cap = ta.count * 2 + 8;
    ScPToken *combined = malloc(cap * sizeof(ScPToken));
    if (!combined) { free(segments); return NULL; }
    int ccount = 0;

    int start = 0;
    for (int i = 0; i <= ta.count; i++) {
        int end_kind = (i < ta.count) ? (int)ta.tokens[i].kind : (int)SNOCONE_EOF;
        int is_sep = (end_kind == (int)SNOCONE_NEWLINE  ||
                      end_kind == (int)SNOCONE_SEMICOLON ||
                      end_kind == (int)SNOCONE_EOF);
        if (is_sep) {
            int seg_len = i - start;
            if (seg_len > 0 && nseg < MAX_SEGMENTS) {
                segments[nseg] = snocone_parse(ta.tokens + start, seg_len);
                ScParseResult *pr = &segments[nseg++];
                /* grow combined if needed */
                if (ccount + pr->count + 1 >= cap) {
                    cap = (ccount + pr->count + 4) * 2;
                    combined = realloc(combined, cap * sizeof(ScPToken));
                    if (!combined) {
                        fprintf(stderr, "snocone_compile: out of memory\n");
                        free(segments); return NULL;
                    }
                }
                for (int k = 0; k < pr->count; k++)
                    combined[ccount++] = pr->tokens[k];
            }
            /* append newline separator between segments (not after final EOF) */
            if (end_kind != (int)SNOCONE_EOF && seg_len > 0) {
                ScPToken nl;
                memset(&nl, 0, sizeof nl);
                nl.kind = SNOCONE_NEWLINE;
                nl.text = (char *)"\n";
                nl.line = (i < ta.count) ? ta.tokens[i].line : 0;
                if (ccount >= cap) {
                    cap *= 2;
                    combined = realloc(combined, cap * sizeof(ScPToken));
                    if (!combined) {
                        fprintf(stderr, "snocone_compile: out of memory\n");
                        free(segments); return NULL;
                    }
                }
                combined[ccount++] = nl;
            }
            start = i + 1;
        }
    }

    /* ---- 3. Lower combined postfix → EXPR_t/STMT_t IR ------------------ */
    ScLowerResult lr = snocone_lower(combined, ccount, filename);

    /* ---- 4. Cleanup ----------------------------------------------------- */
    free(combined);
    for (int i = 0; i < nseg; i++)
        sc_parse_free(&segments[i]);
    free(segments);
    sc_tokens_free(&ta);

    if (lr.nerrors > 0) {
        fprintf(stderr, "snocone_compile: %d lower error(s) in %s\n",
                lr.nerrors, filename);
        sc_lower_free(&lr);
        return NULL;
    }

    return lr.prog;   /* caller owns; consistent with snoc_parse() lifetime */
}
