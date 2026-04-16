/*============================================================= raku_re.h ====
 * Raku regex engine — table-driven NFA (Thompson construction)
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 *==========================================================================*/
#ifndef RAKU_RE_H
#define RAKU_RE_H

/*------------------------------------------------------------------------
 * Character class bitmap (256 bits)
 *----------------------------------------------------------------------*/
typedef struct { unsigned char bits[32]; } Raku_cc;

int raku_cc_test(const Raku_cc *cc, unsigned char c);

/*------------------------------------------------------------------------
 * NFA state kinds
 *----------------------------------------------------------------------*/
typedef enum {
    NK_EPS,         /* epsilon — no input consumed, follow out1 */
    NK_SPLIT,       /* fork — follow out1 AND out2 (alternation/quantifier) */
    NK_CHAR,        /* match literal character ch */
    NK_ANY,         /* match any char except newline */
    NK_CLASS,       /* match char class bitmap */
    NK_ANCHOR_BOL,  /* ^ zero-width assertion */
    NK_ANCHOR_EOL,  /* $ zero-width assertion */
    NK_ACCEPT       /* accepting state */
} Nfa_kind;

#define NFA_NULL (-1)

/*------------------------------------------------------------------------
 * NFA state — element of the flat state table
 *
 * BB lifter note (Phase 3): each Nfa_state maps 1-to-1 to a BB box.
 *   NK_SPLIT  → E_ALTERNATE (two successor boxes)
 *   NK_EPS    → epsilon edge (no BB yield, just follow out1)
 *   NK_ACCEPT → BB_PUMP return with match position
 *   NK_CHAR / NK_ANY / NK_CLASS → consume one char, advance pos or FAILDESCR
 *   NK_ANCHOR_* → zero-width test, FAILDESCR if assertion fails
 * The bb_id field is reserved for the lifter to store the registered box id.
 *----------------------------------------------------------------------*/
typedef struct {
    int       id;       /* index in state table (== position in array) */
    Nfa_kind  kind;
    unsigned char ch;   /* NK_CHAR: the literal character */
    Raku_cc   cc;       /* NK_CLASS: character class bitmap */
    int       out1;     /* primary successor state id  (NFA_NULL = none) */
    int       out2;     /* secondary successor (NK_SPLIT only) */
    int       bb_id;    /* reserved: BB box id for Phase-3 lifter */
} Nfa_state;

/*------------------------------------------------------------------------
 * NFA graph (opaque handle)
 *----------------------------------------------------------------------*/
typedef struct Raku_nfa Raku_nfa;

/* Compile a pattern string to an NFA state table.
 * Returns NULL on syntax error (message printed to stderr).
 * Caller owns the result; free with raku_nfa_free(). */
Raku_nfa *raku_nfa_build(const char *pattern);

/* Number of states in the compiled NFA (used by RK-32 gate). */
int raku_nfa_state_count(const Raku_nfa *nfa);

/* Table-driven simulation: returns 1 if pattern matches in subject, 0 if not.
 * Unanchored (tries every start position) unless pattern starts with ^. */
int raku_nfa_match(const Raku_nfa *nfa, const char *subject);

/* Release NFA memory. */
void raku_nfa_free(Raku_nfa *nfa);

/* Expose raw state array for BB lifter (Phase 3). */
Nfa_state *raku_nfa_states(Raku_nfa *nfa);

#endif /* RAKU_RE_H */
