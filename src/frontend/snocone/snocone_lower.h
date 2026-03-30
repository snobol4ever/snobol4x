/*
 * snocone_lower.h -- Snocone postfix → EXPR_t/STMT_t IR  (Sprint SC2)
 *
 * Step 3 of the Snocone frontend pipeline:
 *
 *   snocone_lex()     -> ScTokenArray    (snocone_lex.c)
 *   snocone_parse()   -> ScParseResult   (snocone_parse.c)
 *   snocone_lower()   -> ScLowerResult   (this file)
 *
 * snocone_lower() walks the postfix ScPToken[] from snocone_parse, evaluates it
 * using an operand stack of EXPR_t*, and assembles STMT_t IR nodes at
 * each SNOCONE_NEWLINE boundary.
 *
 * Operator mapping (from bconv table in snocone.sc / JVM snocone_emitter.clj):
 *
 *   SNOCONE_ASSIGN    ->  subject=lhs, replacement=rhs  (assignment stmt)
 *   SNOCONE_PLUS      ->  E_ADD
 *   SNOCONE_MINUS     ->  E_SUB (binary) / E_NEG (unary)
 *   SNOCONE_STAR      ->  E_MPY
 *   SNOCONE_SLASH     ->  E_DIV
 *   SNOCONE_CARET     ->  E_POW
 *   SNOCONE_CONCAT    ->  E_CONC  (blank concat — juxtaposition)
 *   SNOCONE_OR        ->  E_ALT    (pattern alternation || )
 *   SNOCONE_PIPE      ->  E_SEQ / E_CONCAT  (single | — pattern→E_SEQ, value→E_CONCAT)
 *   SNOCONE_PERIOD    ->  E_CAPT_COND   (conditional capture  .)
 *   SNOCONE_DOLLAR    ->  E_CAPT_IMM   (immediate capture    $)
 *   SNOCONE_AT        ->  E_CAPT_CUR   (@var — cursor position)
 *   SNOCONE_AMPERSAND ->  E_KW    (unary & — keyword reference)
 *   SNOCONE_TILDE     ->  E_FNC("NOT",1)  (logical negation)
 *   SNOCONE_QUESTION  ->  E_FNC("DIFFER",2) or unary: E_FNC("DIFFER",1)
 *   SNOCONE_EQ        ->  E_FNC("EQ",2)
 *   SNOCONE_NE        ->  E_FNC("NE",2)
 *   SNOCONE_LT        ->  E_FNC("LT",2)
 *   SNOCONE_GT        ->  E_FNC("GT",2)
 *   SNOCONE_LE        ->  E_FNC("LE",2)
 *   SNOCONE_GE        ->  E_FNC("GE",2)
 *   SNOCONE_STR_IDENT  -> E_FNC("IDENT",2)
 *   SNOCONE_STR_DIFFER -> E_FNC("DIFFER",2)
 *   SNOCONE_STR_LT    ->  E_FNC("LLT",2)
 *   SNOCONE_STR_GT    ->  E_FNC("LGT",2)
 *   SNOCONE_STR_LE    ->  E_FNC("LLE",2)
 *   SNOCONE_STR_GE    ->  E_FNC("LGE",2)
 *   SNOCONE_STR_EQ    ->  E_FNC("LEQ",2)
 *   SNOCONE_STR_NE    ->  E_FNC("LNE",2)
 *   SNOCONE_PERCENT   ->  E_FNC("REMDR",2)
 *   SNOCONE_STAR (unary) -> E_INDR (indirect reference)
 *   SNOCONE_CALL      ->  E_FNC(name, nargs)
 *   SNOCONE_ARRAY_REF ->  E_IDX(name, nargs)
 *
 * Statement assembly at SNOCONE_NEWLINE:
 *   Stack top is the expression for this line.
 *   If the expression root is E_ASSIGN or the top SNOCONE_ASSIGN produced a
 *   two-operand form, split into subject + replacement fields of STMT_t.
 *   Otherwise the expression is the subject field (pattern match or
 *   expression-only statement).
 */

#ifndef SNOCONE_LOWER_H
#define SNOCONE_LOWER_H

#include "snocone_parse.h"
#include "scrip_cc.h"      /* EXPR_t, STMT_t, Program */

/* ---------------------------------------------------------------------------
 * Lower result
 * ------------------------------------------------------------------------- */
typedef struct {
    Program *prog;      /* heap-allocated IR program; caller frees */
    int      nerrors;   /* number of lowering errors (0 = clean)   */
} ScLowerResult;

/* ---------------------------------------------------------------------------
 * Public API
 *
 * snocone_lower(ptoks, count, filename)
 *   Walk the postfix ScPToken array from snocone_parse() and produce IR.
 *   filename is used for error messages (may be NULL).
 *   On success: result.nerrors == 0, result.prog != NULL.
 *   On error:   result.nerrors > 0, result.prog may be partial.
 *
 * sc_lower_free(r)
 *   Free the Program (STMT_t/EXPR_t chain) owned by the result.
 * ------------------------------------------------------------------------- */
ScLowerResult snocone_lower(const ScPToken *ptoks, int count, const char *filename);
void          sc_lower_free(ScLowerResult *r);

#endif /* SNOCONE_LOWER_H */
