#ifndef PL_PARSE_H
#define PL_PARSE_H
/*
 * pl_parse.h — ClauseAST for the Prolog frontend
 *
 * The parser produces a linked list of PlClause nodes (one per clause
 * or directive).  Each clause has a head Term* and a body Term** array.
 * The body is a flat array of goals; conjunction is represented by
 * multiple entries (not a nested tree).
 *
 * Directives like  :- initialization(main).  are stored as fact clauses
 * with head == NULL and body[0] == the directive goal.
 */

#include "term.h"
#include "pl_atom.h"
#include <stdio.h>

/* -------------------------------------------------------------------------
 * PlClause — one Horn clause or directive
 * ---------------------------------------------------------------------- */
typedef struct PlClause PlClause;
struct PlClause {
    Term     *head;         /* NULL for directives                        */
    Term    **body;         /* array of goal Terms (may be NULL if fact)  */
    int       nbody;        /* number of goals in body                    */
    int       lineno;       /* source line of the neck :-                 */
    PlClause *next;         /* linked list                                */
};

/* -------------------------------------------------------------------------
 * PlProgram — result of parsing a .pl file
 * ---------------------------------------------------------------------- */
typedef struct {
    PlClause *head;    /* first clause                                    */
    PlClause *tail;    /* last clause (for O(1) append)                   */
    int       nclauses;
    int       nerrors;
} PlProgram;

/* -------------------------------------------------------------------------
 * Parser API
 * ---------------------------------------------------------------------- */

/*
 * pl_parse(src, filename) — parse a NUL-terminated Prolog source string.
 *
 * Returns a heap-allocated PlProgram*.  The caller owns it.
 * prog->nerrors > 0 means parse errors were reported to stderr.
 * The program is still returned (partial parse); caller decides whether
 * to abort on errors.
 */
PlProgram *pl_parse(const char *src, const char *filename);

/*
 * pl_program_pretty(prog, out) — pretty-print parsed program for
 * round-trip verification.
 */
void pl_program_pretty(PlProgram *prog, FILE *out);

/*
 * pl_program_free(prog) — release all memory.
 */
void pl_program_free(PlProgram *prog);

/* -------------------------------------------------------------------------
 * Term pretty-printer (also used by pl_lower.c diagnostics)
 * ---------------------------------------------------------------------- */
void term_pretty(Term *t, FILE *out);

#endif /* PL_PARSE_H */
