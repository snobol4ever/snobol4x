/*
 * sc_parse.h -- Snocone expression parser public API  (Sprint SC1)
 *
 * Step 2 of the Snocone frontend pipeline:
 *
 *   sc_lex()     -> ScTokenArray      (sc_lex.c)
 *   sc_parse()   -> ScParseResult     (this file)
 *   sc_lower()   -> EXPR_t/STMT_t IR  (sc_lower.c, Sprint SC2)
 *
 * SnoconeParser implements the shunting-yard algorithm from snocone.sc
 * (binop / endexp / begexp) and returns a postfix (RPN) token list.
 *
 * Precedence table (lp / rp from bconv in snocone.sc):
 *   SC_ASSIGN    lp=1  rp=2   right-assoc  (x = y = z  ->  x y z = =)
 *   SC_QUESTION  lp=2  rp=2   left
 *   SC_PIPE      lp=3  rp=3   left
 *   SC_OR        lp=4  rp=4   left
 *   SC_CONCAT    lp=5  rp=5   left
 *   comparisons  lp=6  rp=6   left
 *   SC_PLUS/-    lp=7  rp=7   left
 *   SC_SLASH/STAR/PERCENT  lp=8  rp=8  left
 *   SC_CARET     lp=9  rp=10  right-assoc  (a^b^c -> a b c ^ ^)
 *   SC_PERIOD/.  lp=10 rp=10  left
 *   SC_DOLLAR/$  lp=10 rp=10  left
 *
 * Reduce condition (from binop() in snocone.sc):
 *   while existing_op.lp >= incoming_op.rp  -> reduce
 *
 * Unary operators: ANY("+-*&@~?.$")  -- applied right-to-left, tightest binding.
 * Each ScToken in the output carries the sc_is_unary flag when used as unary.
 *
 * Synthetic token kinds added to ScKind (sc_lex.h):
 *   SC_CALL       -- function call:  f x y... CALL(n)
 *   SC_ARRAY_REF  -- array index:    a i... ARRAY_REF(n)
 * These are emitted at the call/index site and carry arg_count.
 */

#ifndef SC_PARSE_H
#define SC_PARSE_H

#include "sc_lex.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Extended token -- wraps ScToken with parser-level metadata
 * ------------------------------------------------------------------------- */
typedef struct {
    ScKind  kind;
    char   *text;        /* same pointer as lexer (not duplicated)         */
    int     line;        /* 1-based source line                            */
    int     is_unary;    /* 1 if this op was used as a unary prefix        */
    int     arg_count;   /* for SC_CALL / SC_ARRAY_REF: number of args     */
} ScPToken;

/* Synthetic token kinds emitted by the parser.
 * These values must not collide with ScKind.  They are added above SC_UNKNOWN. */
#define SC_CALL       ((ScKind)(SC_UNKNOWN + 1))
#define SC_ARRAY_REF  ((ScKind)(SC_UNKNOWN + 2))

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
 * sc_parse(toks, count)
 *   Parse an infix ScToken array (from sc_lex, count excludes the SC_EOF)
 *   into a postfix ScPToken array.  Caller frees with sc_parse_free().
 *
 * sc_parse_free(r)
 *   Free heap memory owned by the result.
 *
 * sc_ptoken_kind_name(kind)
 *   Return a static debug name for a ScPToken kind (wraps sc_kind_name for
 *   base kinds; handles SC_CALL / SC_ARRAY_REF).
 * ------------------------------------------------------------------------- */
ScParseResult sc_parse(const ScToken *toks, int count);
void          sc_parse_free(ScParseResult *r);
const char   *sc_ptoken_kind_name(ScKind kind);

#endif /* SC_PARSE_H */
