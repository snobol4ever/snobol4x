/*
 * snocone_parse.h -- Snocone expression parser public API  (Sprint SC1)
 *
 * Step 2 of the Snocone frontend pipeline:
 *
 *   snocone_lex()     -> ScTokenArray      (snocone_lex.c)
 *   snocone_parse()   -> ScParseResult     (this file)
 *   snocone_lower()   -> EXPR_t/STMT_t IR  (snocone_lower.c, Sprint SC2)
 *
 * SnoconeParser implements the shunting-yard algorithm from snocone.sc
 * (binop / endexp / begexp) and returns a postfix (RPN) token list.
 *
 * Precedence table (lp / rp from bconv in snocone.sc):
 *   SNOCONE_ASSIGN    lp=1  rp=2   right-assoc  (x = y = z  ->  x y z = =)
 *   SNOCONE_QUESTION  lp=2  rp=2   left
 *   SNOCONE_PIPE      lp=3  rp=3   left
 *   SNOCONE_OR        lp=4  rp=4   left
 *   SNOCONE_CONCAT    lp=5  rp=5   left
 *   comparisons  lp=6  rp=6   left
 *   SNOCONE_PLUS/-    lp=7  rp=7   left
 *   SNOCONE_SLASH/STAR/PERCENT  lp=8  rp=8  left
 *   SNOCONE_CARET     lp=9  rp=10  right-assoc  (a^b^c -> a b c ^ ^)
 *   SNOCONE_PERIOD/.  lp=10 rp=10  left
 *   SNOCONE_DOLLAR/$  lp=10 rp=10  left
 *
 * Reduce condition (from binop() in snocone.sc):
 *   while existing_op.lp >= incoming_op.rp  -> reduce
 *
 * Unary operators: ANY("+-*&@~?.$")  -- applied right-to-left, tightest binding.
 * Each SnoconeToken in the output carries the sc_is_unary flag when used as unary.
 *
 * Synthetic token kinds added to SnoconeKind (snocone_lex.h):
 *   SNOCONE_CALL       -- function call:  f x y... CALL(n)
 *   SNOCONE_ARRAY_REF  -- array index:    a i... ARRAY_REF(n)
 * These are emitted at the call/index site and carry arg_count.
 */

#ifndef SNOCONE_PARSE_H
#define SNOCONE_PARSE_H

#include "snocone_lex.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Extended token -- wraps SnoconeToken with parser-level metadata
 * ------------------------------------------------------------------------- */
typedef struct {
    SnoconeKind  kind;
    char   *text;        /* same pointer as lexer (not duplicated)         */
    int     line;        /* 1-based source line                            */
    int     is_unary;    /* 1 if this op was used as a unary prefix        */
    int     arg_count;   /* for SNOCONE_CALL / SNOCONE_ARRAY_REF: number of args     */
} ScPToken;

/* Synthetic token kinds emitted by the parser.
 * These values must not collide with SnoconeKind.  They are added above SNOCONE_UNKNOWN. */
#define SNOCONE_CALL       ((SnoconeKind)(SNOCONE_UNKNOWN + 1))
#define SNOCONE_ARRAY_REF  ((SnoconeKind)(SNOCONE_UNKNOWN + 2))

/* ---------------------------------------------------------------------------
 * Parse result
 * ------------------------------------------------------------------------- */
typedef struct {
    ScPToken *tokens;   /* heap-allocated postfix token array */
    int       count;    /* number of tokens                   */
} ScParseResult;

/* ---------------------------------------------------------------------------
 * Public API
 *
 * snocone_parse(toks, count)
 *   Parse an infix SnoconeToken array (from snocone_lex, count excludes the SNOCONE_EOF)
 *   into a postfix ScPToken array.  Caller frees with sc_parse_free().
 *
 * sc_parse_free(r)
 *   Free heap memory owned by the result.
 *
 * sc_ptoken_kind_name(kind)
 *   Return a static debug name for a ScPToken kind (wraps sc_kind_name for
 *   base kinds; handles SNOCONE_CALL / SNOCONE_ARRAY_REF).
 * ------------------------------------------------------------------------- */
ScParseResult snocone_parse(const SnoconeToken *toks, int count);
void          sc_parse_free(ScParseResult *r);
const char   *sc_ptoken_kind_name(SnoconeKind kind);

#endif /* SNOCONE_PARSE_H */
