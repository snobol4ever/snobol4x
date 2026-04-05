/*
 * sn4parse.c — Faithful C translation of SNOBOL4 SIL lexer/parser
 *
 * Follows v311.sil exactly:                    (v311.sil = CSNOBOL4 2.3.3 SIL source)
 *   CMPILE → compile one statement             (v311.sil:1608  "Procedure to compile statement")
 *   FORBLK / FORWRD → inter-field scanning     (v311.sil:2241  "Procedure to get to nonblank")
 *   ELEMNT → element analysis                  (v311.sil:1924  "Element analysis procedure")
 *   EXPR / EXPR1 → expression compiler         (v311.sil:2093  "Procedure to compile expression")
 *
 * SIL = SNOBOL4 Implementation Language — the macro-assembly used in CSNOBOL4.
 * Each procedure here is a direct C translation of the corresponding SIL procedure.
 * 256-byte chrs[] arrays lifted verbatim from snobol4-2.3.3/syn.c.
 * stream() lifted verbatim from snobol4-2.3.3/lib/stream.c.
 * SIL names used throughout. Tree nodes carry SIL STYPE token-type codes.
 *
 * Output: s-expression IR tree printed to stdout.
 *
 * Build:  gcc -O0 -g -Wall -o sn4parse sn4parse.c
 * Usage:  ./sn4parse file.sno   or   cat file.sno | ./sn4parse
 *
 * Authors: Lon Jones Cherryholmes · Claude Sonnet 4.6  2026-04-04
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

/* =========================================================================
 * syntab / acts — from snobol4-2.3.3/include/syntab.h
 *
 * A syntab (syntax table) is a finite-state scanner step:
 *   chrs[256]   — maps each input byte to an index into the actions[] array.
 *                 Index 0 means ACT_CONTIN (fast-path: no action, keep scanning).
 *   actions[]   — parallel array; each entry says what to do when chrs[c] fires.
 *   put         — integer token code written to the global STYPE on a trigger.
 *   act         — ACT_CONTIN / ACT_STOP / ACT_STOPSH / ACT_ERROR / ACT_GOTO.
 *   go          — pointer to next syntab when act == ACT_GOTO (table chain).
 *
 * stream(sp1, sp2, tp) scans sp2 byte-by-byte through table tp, fills sp1
 * with the consumed prefix, shrinks sp2 to the remainder, sets STYPE.
 * ========================================================================= */

typedef enum { ACT_CONTIN=0, ACT_STOP, ACT_STOPSH, ACT_ERROR, ACT_GOTO } action_t;
/*            consume+continue  consume+stop  stop(no consume)  error  chain→go */
typedef struct syntab syntab_t;
typedef struct { int put; action_t act; syntab_t *go; } acts_t;
struct syntab { const char *name; unsigned char chrs[256]; acts_t *actions; };

/* STYPE — "Descriptor returned by STREAM" (v311.sil:10842 "STYPE DESCR 0,FNC,0").
 * stream() writes the triggering action's put-code here.
 * Callers read STYPE after stream() returns to learn what token type was found. */
static int STYPE;

/* =========================================================================
 * stream() — verbatim from snobol4-2.3.3/lib/stream.c
 *
 * SIL calling convention (v311.sil "STREAM XSP,TEXTSP,IBLKTB,..."):
 *   sp1   = XSP   — output: spec of the consumed prefix (token text)
 *   sp2   = TEXTSP — input/output: remaining source text (shrinks as consumed)
 *   tp    = table  — the syntab to drive (IBLKTB, FRWDTB, BIOPTB, etc.)
 *
 * A "spec" (specifier) is a (ptr, len) pair — SIL's SPEC type, two words.
 * Returns ST_STOP (ACT_STOP hit), ST_EOS (input exhausted), ST_ERROR (ACT_ERROR).
 * Sets STYPE to the put-code of the triggering action.
 * ========================================================================= */

typedef enum { ST_STOP, ST_EOS, ST_ERROR } stream_ret_t;

typedef struct { const char *ptr; int len; } spec_t; /* SIL SPEC: (pointer, length) pair */

static int g_trace_stream = 0;  /* set to 1 to emit STREAM trace */

static stream_ret_t stream(spec_t *sp1, spec_t *sp2, syntab_t *tp) {
    const char *tab_name  = tp->name;
    const char *input_ptr = sp2->ptr;
    int         input_len = sp2->len;

    unsigned char *cp = (unsigned char *)sp2->ptr;
    int len = sp2->len;
    stream_ret_t ret;
    int put = 0;

    for (; len > 0; cp++, len--) {
        unsigned aindex = tp->chrs[*cp];
        if (aindex == 0) continue;          /* ACT_CONTIN fast path */
        acts_t *ap = tp->actions + (aindex - 1);
        if (ap->put) put = ap->put;
        switch (ap->act) {
        case ACT_CONTIN: break;
        case ACT_STOP:   cp++; len--;        /* accept char */
            /* FALLTHROUGH */
        case ACT_STOPSH: ret = ST_STOP; goto done;
        case ACT_ERROR:  STYPE = 0; return ST_ERROR;
        case ACT_GOTO:   tp = ap->go; break;
        }
    }
    ret = ST_EOS;
done:
    STYPE = put;
    /* sp1 = prefix (what was consumed) */
    sp1->ptr = sp2->ptr;
    sp1->len = sp2->len - len;
    /* sp2 = remainder */
    if (ret != ST_EOS) sp2->ptr += sp1->len;
    sp2->len = len;
    if (g_trace_stream) {
        char ibuf[32]; int ilen = input_len < 20 ? input_len : 20;
        memcpy(ibuf, input_ptr, ilen); ibuf[ilen] = 0;
        /* replace non-printable with '.' */
        for (int i=0;i<ilen;i++) if ((unsigned char)ibuf[i]<32||ibuf[i]==127) ibuf[i]='.';
        const char *retname = ret==ST_STOP?"STOP":ret==ST_EOS?"EOS":"ERROR";
        fprintf(stderr, "STREAM %-10s [%-20s] -> ret=%-5s stype=%d\n",
                tab_name, ibuf, retname, put);
    }
    return ret;
}

/* =========================================================================
 * SIL token type constants — from snobol4-2.3.3/equ.h
 *
 * These integers are "put" codes written to STYPE by stream().
 * The same integer value can mean different things in different table contexts;
 * the comments below give the meaning in each table where the code appears.
 * All values and names are verbatim from equ.h.
 * ========================================================================= */

/* ELEMTB / VARTB / INTGTB / FLITB result codes — element (token) type */
#define QLITYP  1   /* quoted literal: 'text' or "text"         (equ.h) */
#define ILITYP  2   /* integer literal: digits                  (equ.h) */
#define VARTYP  3   /* variable name: [A-Za-z][A-Za-z0-9]*     (equ.h) */
#define NSTTYP  4   /* nested/parenthesized expression: (       (equ.h) */
#define FNCTYP  5   /* function call: IDENT(                    (equ.h) */
#define FLITYP  6   /* floating-point literal: digits.digits    (equ.h) */
#define ARYTYP  7   /* array/table subscript: IDENT<            (equ.h) */
#define SELTYP  50  /* P2B: alternative evaluation (e1,e2,en)   (SPITBOL ext) */

/* FRWDTB / FORBLK result codes — inter-field delimiter type */
#define NBTYP   1   /* non-blank: a real token starts here      (equ.h) — "non-blank" */
#define EQTYP   4   /* '=' equals sign: replacement separator   (equ.h) */
#define CLNTYP  5   /* ':' colon: goto field separator          (equ.h) */
#define EOSTYP  6   /* end of statement (';' or line end)       (equ.h) */

/* ELEMTB/VARTB inner delimiters */
#define RPTYP   3   /* ')' right parenthesis: ends arg list     (equ.h) */
#define CMATYP  2   /* ',' comma: separates function arguments  (equ.h) */
#define RBTYP   7   /* ']' or '>' right bracket: ends subscript (equ.h) */

/* GOTOTB / GOTSTB / GOTFTB result codes — goto field sub-type */
#define SGOTYP  2   /* :S(  success goto with label in parens   (equ.h) */
#define FGOTYP  3   /* :F(  failure goto with label in parens   (equ.h) */
#define UGOTYP  1   /* :(   unconditional goto                  (equ.h) */
#define STOTYP  5   /* :S<  success direct goto                 (equ.h) */
#define FTOTYP  6   /* :F<  failure direct goto                 (equ.h) */
#define UTOTYP  4   /* :<   unconditional direct goto           (equ.h) */

/* CARDTB result codes — source card (line) type */
#define CMTTYP  2   /* comment card: first char is '*'          (equ.h) */
#define CTLTYP  3   /* control card: first char is '-'          (equ.h) */
#define CNTTYP  4   /* continuation card: first char is '+'     (equ.h) */
#define NEWTYP  1   /* new statement card: any other first char  (equ.h) */

/* Binary operator function descriptor codes — put-codes from BIOPTB.
 * Each maps to a SIL DESCR in v311.sil data section (line 11629+).
 * "FN" suffix = "FuNction descriptor" in SIL terminology. */
#define ADDFN   201  /* X + Y  addition           (v311.sil:11629 "ADD,0,2") */
#define SUBFN   202  /* X - Y  subtraction         (v311.sil:11691 "SUB,0,2") */
#define MPYFN   203  /* X * Y  multiplication      (v311.sil:11682 "MPY,0,2") */
#define DIVFN   204  /* X / Y  division             (v311.sil:11673 "DIV,0,2") */
#define EXPFN   205  /* X ** Y exponentiation       (v311.sil:11679 "EXPOP,0,2") */
#define ORFN    206  /* X | Y  alternation (pattern)(v311.sil:11688 "OR,0,2") */
#define NAMFN   207  /* X . Y  naming / cond.assign (v311.sil:11685 "NAM,0,2") */
#define DOLFN   208  /* X $ Y  immediate naming     (v311.sil:11676 "DOL,0,2") */
#define BIATFN  209  /* X @ Y  user-definable       (v311.sil:11635 "UNDF") */
#define BIPDFN  210  /* X # Y  user-definable       (v311.sil:11641 "UNDF") */
#define BIPRFN  211  /* X % Y  user-definable       (v311.sil:11644 "UNDF") */
#define BIAMFN  212  /* X & Y  user-definable       (v311.sil:11632 "UNDF") */
#define BINGFN  213  /* X ~ Y  user-definable       (v311.sil:11638 "UNDF") */
#define BIQSFN  214  /* X ? Y  user-definable       (v311.sil:11659 "UNDF") */
#define BISNFN  215  /* X ? Y  SPITBOL scan-replace (SPITBOL extension) */
#define BIEQFN  216  /* X = Y  SPITBOL assignment   (SPITBOL extension) */

/* Unary prefix operator function descriptor codes — put-codes from UNOPTB.
 * Each maps to a SIL DESCR in v311.sil data section (line 11694+). */
#define PLSFN   301  /* +X  unary plus              (v311.sil:11712 "PLS,0,1") */
#define MNSFN   302  /* -X  unary minus             (v311.sil:11706 "MNS,0,1") */
#define DOTFN   303  /* .X  name-of (lvalue ref)    (v311.sil:11700 "NAME,0,1") */
#define INDFN   304  /* $X  indirect reference      (v311.sil:11702 "IND,0,1") */
#define STRFN   305  /* *X  unevaluated expression  (v311.sil:11720 "STR,0,1") */
#define SLHFN   306  /* /X  user-definable          (v311.sil:11718 "UNDF") */
#define PRFN    307  /* %X  user-definable          (v311.sil:11714 "UNDF") */
#define ATFN    308  /* @X  scanner cursor position (v311.sil:11696 "ATOP,0,1") */
#define PDFN    309  /* #X  user-definable          (v311.sil:11710 "UNDF") */
#define KEYFN   310  /* &X  keyword reference       (v311.sil:11704 "KEYWRD,0,1") */
#define NEGFN   311  /* ~X  not/negation            (v311.sil:11708 "NEG,0,1") */
#define BARFN   312  /* |X  user-definable          (v311.sil:11698 "UNDF") */
#define QUESFN  313  /* ?X  interrogation/test       (v311.sil:11716 "QUES,0,1") */
#define AROWFN  314  /* ^X  user-definable          (v311.sil:11694 "UNDF") */

/* =========================================================================
 * Syntax tables — chrs[] verbatim from snobol4-2.3.3/syn.c
 *
 * Each table implements one step of the SIL STREAM instruction.
 * The 256-byte chrs[] array maps ASCII byte values to 1-based action indices.
 * chrs[c] == 0  → ACT_CONTIN fast path (no entry in actions[], just keep going).
 * chrs[c] == N  → actions[N-1] is the entry to fire.
 *
 * Table naming convention (SIL):
 *   *TB suffix = "Table"
 *   LBL = label field,  CARD = card type,  IBLK = inter-field blank,
 *   FRWD = forward scan, ELEM = element, VAR = variable, INTG = integer,
 *   FLIT = float literal, SQL/DQL = single/double quote literal,
 *   UNOP = unary operator, BIOP = binary operator,
 *   START = disambiguate * vs **, TBLK = trailing blank after operator,
 *   GOTO = goto field, GOTS/GOTF = S-goto / F-goto.
 * ========================================================================= */

/* ---- LBLTB: label field scanner (v311.sil:1613 "STREAM XSP,TEXTSP,LBLTB")
 * Accepts alphanumeric chars (columns 1+) as label text.
 * Stops on blank or ';' (end-of-statement).  Errors on any other char. ---- */
syntab_t LBLXTB;  /* forward — LBLTB_actions[0].go references it */
static acts_t LBLTB_actions[] = {
    {0, ACT_GOTO, &LBLXTB},
    {0, ACT_STOPSH, NULL},
    {0, ACT_ERROR, NULL},
};
/* LBLXTB: continuation of label scan — same as LBLTB but accepts digits too.
 * SIL transitions LBLTB→LBLXTB after the first alphanumeric char. Here merged. */
static acts_t LBLXTB_actions[] = {
    {0, ACT_STOPSH, NULL},
};
syntab_t LBLXTB = { "LBLXTB", {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, LBLXTB_actions };

syntab_t LBLTB = { "LBLTB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  2,  3,  3,  3,  3,
     3,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,
     3,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
}, LBLTB_actions };

/* ---- CARDTB: card (line) type scanner (v311.sil:1033 "STREAM XSP,TEXTSP,CARDTB")
 * First char of each source line determines card type.  All stops are STOPSH
 * (char is NOT consumed — it stays as the start of the next field).
 * "Card" = punch-card terminology from SNOBOL4's batch origins. ---- */
static acts_t CARDTB_actions[] = {
    {CMTTYP, ACT_STOPSH, NULL},
    {CTLTYP, ACT_STOPSH, NULL},
    {CNTTYP, ACT_STOPSH, NULL},
    {NEWTYP, ACT_STOPSH, NULL},
};
syntab_t CARDTB = { "CARDTB", {
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  1,  4,  1,  4,  4,  4,  4,  4,  4,  1,  3,  4,  2,  3,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
}, CARDTB_actions };

/* ---- FRWDTB: forward scan — skips transparent chars (space=0x20, tab=0x09
 * have chrs[]=0 → ACT_CONTIN fast-path) and stops on the next field delimiter.
 * (v311.sil:2215 "STREAM XSP,TEXTSP,FRWDTB — Break for next nonblank") ---- */
/* ---- IBLKTB: inter-field blank scanner — expects a leading space/tab,
 * chains via ACT_GOTO to FRWDTB on the first non-blank, fires EOSTYP on ';'.
 * Returns ST_ERROR when there is NO leading blank (BINOP's no-blank / BINOP1 path).
 * (v311.sil:2242 "STREAM XSP,TEXTSP,IBLKTB — Break out nonblank from blank") ---- */
static acts_t FRWDTB_actions[] = {
    {EQTYP, ACT_STOP, NULL},
    {RPTYP, ACT_STOP, NULL},
    {RBTYP, ACT_STOP, NULL},
    {CMATYP, ACT_STOP, NULL},
    {CLNTYP, ACT_STOP, NULL},
    {EOSTYP, ACT_STOP, NULL},
    {NBTYP, ACT_STOPSH, NULL},
};
syntab_t FRWDTB = { "FRWDTB", {
     7,  7,  7,  7,  7,  7,  7,  7,  7,  0,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     0,  7,  7,  7,  7,  7,  7,  7,  7,  2,  7,  7,  4,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  5,  6,  7,  1,  3,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  3,  7,  1,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
}, FRWDTB_actions };

static acts_t IBLKTB_actions[] = {
    {0, ACT_GOTO, &FRWDTB},
    {EOSTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t IBLKTB = { "IBLKTB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, IBLKTB_actions };

/* ---- ELEMTB: element type dispatch (v311.sil:1926 "STREAM XSP,TEXTSP,ELEMTB")
 * Classifies the first character of a token and chains to the appropriate
 * sub-scanner.  SIL then does "SELBRA STYPE,(ELEILT,ELEVBL,ELENST,ELEFNC,ELEFLT,ELEARY)"
 * to branch on the resulting STYPE. ---- */
static acts_t ELEMTB_actions[] = {
    {ILITYP, ACT_GOTO, NULL},
    {VARTYP, ACT_GOTO, NULL},
    {QLITYP, ACT_GOTO, NULL},
    {QLITYP, ACT_GOTO, NULL},
    {NSTTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
/* Forward declarations for goto targets wired in init_tables() */
syntab_t VARTB, INTGTB, SQLITB, DQLITB;

syntab_t ELEMTB = { "ELEMTB", {
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  4,  6,  6,  6,  6,  3,  5,  6,  6,  6,  6,  6,  6,  6,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  6,  6,  6,  6,  6,  6,
     6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  6,  6,  6,  6,  6,
     6,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  6,  6,  6,  6,  6,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, ELEMTB_actions };

/* ---- VARTB: variable / function-name scanner (v311.sil:ELEVBL branch)
 * Entered after ELEMTB fires ACT_GOTO for a letter.  Accumulates alphanumeric
 * chars (chrs[]=0 fast-path).  Stops on the first non-identifier char and
 * classifies it: plain variable, function call IDENT(, or array ref IDENT<. ---- */
static acts_t VARTB_actions[] = {
    {VARTYP, ACT_STOPSH, NULL},
    {FNCTYP, ACT_STOP, NULL},
    {ARYTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t VARTB = { "VARTB", {
     4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     1,  4,  4,  4,  4,  4,  4,  4,  2,  1,  4,  4,  1,  4,  0,  4,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  1,  3,  4,  1,  4,
     4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  4,  1,  4,  0,
     4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, VARTB_actions };

/* ---- INTGTB: integer digit accumulator (v311.sil:ELEILT branch)
 * Entered after ELEMTB fires ACT_GOTO for a digit.  Continues on digits,
 * stops on terminators, chains to FLITB on '.', chains to EXPTB on 'e'/'E'. ---- */
syntab_t FLITB;  /* forward — INTGTB references FLITB before it is defined */
static acts_t INTGTB_actions[] = {
    {ILITYP, ACT_STOPSH, NULL},
    {FLITYP, ACT_GOTO, NULL},
    {FLITYP, ACT_GOTO, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t EXPTB, EXPBTB; /* forward declarations — used by INTGTB/FLITB ACT_GOTO */
syntab_t INTGTB = { "INTGTB", {
     4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     1,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,  1,  4,  2,  4,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  1,  4,  4,  1,  4,
     4,  4,  4,  4,  4,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  1,  4,  4,
     4,  4,  4,  4,  4,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
}, INTGTB_actions };

/* ---- FLITB: floating-point fraction digit accumulator
 * Entered after INTGTB sees '.'.  Continues on digits, stops on terminators,
 * chains to EXPTB on 'e'/'E' (scientific notation exponent). ---- */
static acts_t FLITB_actions[] = {
    {0, ACT_STOPSH, NULL},
    {0, ACT_GOTO, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t FLITB = { "FLITB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     1,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  1,  3,  3,  3,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  1,  3,  3,  1,  3,
     3,  3,  3,  3,  3,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,
     3,  3,  3,  3,  3,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, FLITB_actions };

/* ---- EXPTB / EXPBTB: exponent sign and digit scanners
 * EXPTB: entered after 'e'/'E' in a float; accepts optional '+'/'-' then chains to EXPBTB.
 * EXPBTB: accumulates exponent digits, stops on any token terminator. ---- */
static acts_t EXPBTB_actions[] = {
    {0, ACT_STOPSH, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t EXPBTB = { "EXPBTB", {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     1,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  1,  2,  2,  2,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  2,  1,  2,  2,  1,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, EXPBTB_actions };
static acts_t EXPTB_actions[] = {
    {0, ACT_GOTO, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t EXPTB = { "EXPTB", {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  1,  2,  2,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, EXPTB_actions };

/* ---- SQLITB / DQLITB: string literal body scanners
 * SQLITB: scans body of a single-quoted string '...'; stops on matching '.
 * DQLITB: scans body of a double-quoted string "..."; stops on matching ".
 * Both accept ALL other bytes including spaces and operators (ACT_CONTIN). ---- */
static acts_t SQLITB_actions[] = {
    {0, ACT_STOP, NULL},
};
syntab_t SQLITB = { "SQLITB", {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, SQLITB_actions };
static acts_t DQLITB_actions[] = {
    {0, ACT_STOP, NULL},
};
syntab_t DQLITB = { "DQLITB", {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
}, DQLITB_actions };

/* ---- UNOPTB: unary prefix operator scanner (v311.sil:2510 "STREAM XSP,TEXTSP,UNOPTB")
 * Entered by ELEMNT's UNOP loop before the element itself.
 * Each operator char fires ACT_STOPSH (does NOT consume the char — leaves it for re-use)
 * and puts its operator code into STYPE.  All are ACT_STOPSH because the operator
 * IS the single-char token; the char must not be consumed into XSP here. ---- */
/* UOP_* aliases match the *FN defines above — same numeric values, kept separate
 * only for readability inside the UNOPTB_actions table. */
#define UOP_PLS  301  /* = PLSFN:  +X unary plus              */
#define UOP_MNS  302  /* = MNSFN:  -X unary minus             */
#define UOP_DOT  303  /* = DOTFN:  .X name-of (lvalue)        */
#define UOP_IND  304  /* = INDFN:  $X indirect reference      */
#define UOP_STR  305  /* = STRFN:  *X unevaluated expression  */
#define UOP_SLH  306  /* = SLHFN:  /X user-definable          */
#define UOP_PCT  307  /* = PRFN:   %X user-definable          */
#define UOP_AT   308  /* = ATFN:   @X scanner cursor position */
#define UOP_PD   309  /* = PDFN:   #X user-definable          */
#define UOP_KEY  310  /* = KEYFN:  &X keyword reference       */
#define UOP_NEG  311  /* = NEGFN:  ~X not/negation            */
#define UOP_BAR  312  /* = BARFN:  |X user-definable          */
#define UOP_QUE  313  /* = QUESFN: ?X interrogation/test      */
#define UOP_ARW  314  /* = AROWFN: ^X user-definable          */

syntab_t NBLKTB;
static acts_t NBLKTB_actions[] = {
    {0, ACT_ERROR, NULL},
    {0, ACT_STOPSH, NULL},
};




syntab_t NBLKTB = { "NBLKTB", {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     1,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  1,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  1,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, NBLKTB_actions };

static acts_t UNOPTB_actions[] = {
    {PLSFN, ACT_GOTO, &NBLKTB},
    {MNSFN, ACT_GOTO, &NBLKTB},
    {DOTFN, ACT_GOTO, &NBLKTB},
    {INDFN, ACT_GOTO, &NBLKTB},
    {STRFN, ACT_GOTO, &NBLKTB},
    {SLHFN, ACT_GOTO, &NBLKTB},
    {PRFN, ACT_GOTO, &NBLKTB},
    {ATFN, ACT_GOTO, &NBLKTB},
    {PDFN, ACT_GOTO, &NBLKTB},
    {KEYFN, ACT_GOTO, &NBLKTB},
    {NEGFN, ACT_GOTO, &NBLKTB},
    {BARFN, ACT_GOTO, &NBLKTB},
    {QUESFN, ACT_GOTO, &NBLKTB},
    {AROWFN, ACT_GOTO, &NBLKTB},
    {0, ACT_ERROR, NULL},
};
syntab_t UNOPTB = { "UNOPTB", {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 12, 15,  9,  4,  7, 10, 15, 15, 15,  5,  1, 15,  2,  3,  6,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13,
     8, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 15, 14, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 12, 15, 11, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
}, UNOPTB_actions };

/* BIOPTB and TBLKTB are mutually referential — forward-declare both. */
extern syntab_t TBLKTB, STARTB;

/* ---- BIOPTB: binary operator scanner — SNOBOL4 mode
 * (v311.sil:1567 "STREAM XSP,TEXTSP,BIOPTB,BINCON")
 * Called by BINOP() AFTER the leading blank has been consumed by IBLKTB.
 * Each entry: set STYPE to the operator function code, then ACT_GOTO to TBLKTB
 * (which must consume the mandatory trailing blank that follows every binary op).
 * Special case: '*' goes to STARTB instead of TBLKTB to handle '**' vs '*'.  ---- */
static acts_t BIOPTB_actions[] = {
    {ADDFN, ACT_GOTO, &TBLKTB},
    {SUBFN, ACT_GOTO, &TBLKTB},
    {NAMFN, ACT_GOTO, &TBLKTB},
    {DOLFN, ACT_GOTO, &TBLKTB},
    {MPYFN, ACT_GOTO, &STARTB},
    {DIVFN, ACT_GOTO, &TBLKTB},
    {BIATFN, ACT_GOTO, &TBLKTB},
    {BIPDFN, ACT_GOTO, &TBLKTB},
    {BIPRFN, ACT_GOTO, &TBLKTB},
    {EXPFN, ACT_GOTO, &TBLKTB},
    {ORFN, ACT_GOTO, &TBLKTB},
    {BIAMFN, ACT_GOTO, &TBLKTB},
    {BINGFN, ACT_GOTO, &TBLKTB},
    {BIQSFN, ACT_GOTO, &TBLKTB},
    {0, ACT_ERROR, NULL},
};

/* STARTB: disambiguate '*' (multiply) from '**' (exponentiation)
 * (v311.sil: BIOPTB action[4] goes here after consuming the first '*')
 * Peeks at the next char: if it is another '*' consume it and set EXPFN;
 * if it is a blank/tab consume it and keep STYPE=MPYFN (already set). ---- */
static acts_t STARTB_actions[] = {
    {0, ACT_STOP, NULL},
    {EXPFN, ACT_GOTO, &TBLKTB},
    {0, ACT_ERROR, NULL},
};
syntab_t STARTB = { "STARTB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, STARTB_actions };

/* TBLKTB: trailing blank consumer — every binary operator must be followed by a blank.
 * (v311.sil: every BIOPTB action goes to TBLKTB after setting STYPE)
 * Consumes exactly one space or tab (the mandatory post-operator blank), then stops.  ---- */
static acts_t TBLKTB_actions[] = {
    {0, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t TBLKTB = { "TBLKTB", {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
}, TBLKTB_actions };

syntab_t BIOPTB = { "BIOPTB", {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 11, 15,  8,  4,  9, 12, 15, 15, 15,  5,  1, 15,  2,  3,  6,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14,
     7, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13, 15, 10, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 15, 13, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
}, BIOPTB_actions };

/* ---- GOTOTB: goto field top-level scanner (v311.sil:1730 "STREAM XSP,TEXTSP,GOTOTB")
 * Entered after ':' is detected by FORBLK.  Classifies the goto type:
 *   :( or :< → unconditional;  :S → success (chains to GOTSTB);  :F → failure (chains to GOTFTB).
 * GOTSTB: sub-scanner after 'S' — expects '(' (label in parens) or '<' (direct address).
 * GOTFTB: sub-scanner after 'F' — same but for failure branch. ---- */
static acts_t GOTSTB_actions[] = {
    {SGOTYP, ACT_STOP, NULL},
    {STOTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t GOTSTB = { "GOTSTB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, GOTSTB_actions };
static acts_t GOTFTB_actions[] = {
    {FGOTYP, ACT_STOP, NULL},
    {FTOTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t GOTFTB = { "GOTFTB", {
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  1,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
}, GOTFTB_actions };
static acts_t GOTOTB_actions[] = {
    {0, ACT_GOTO, &GOTSTB},
    {0, ACT_GOTO, &GOTFTB},
    {UGOTYP, ACT_STOP, NULL},
    {UTOTYP, ACT_STOP, NULL},
    {0, ACT_ERROR, NULL},
};
syntab_t GOTOTB = { "GOTOTB", {
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  3,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  4,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  2,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  1,  5,  5,  5,  5,  5,  5,  5,  4,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  2,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  1,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
}, GOTOTB_actions };

/* Wire up forward-declared goto targets in action tables.
 * These cannot be static initialisers because C does not allow forward references
 * to syntab_t objects in struct literals.  Called once from main() before parsing. */
static void init_tables(void) {
    ELEMTB_actions[0].go  = &INTGTB;
    ELEMTB_actions[1].go  = &VARTB;
    ELEMTB_actions[2].go  = &SQLITB;
    ELEMTB_actions[3].go  = &DQLITB;
    INTGTB_actions[1].go  = &FLITB;
    INTGTB_actions[2].go  = &EXPTB;
    FLITB_actions[1].go   = &EXPTB;
    EXPTB_actions[0].go   = &EXPBTB;
}

/* =========================================================================
 * Tree node — uses SIL names / codes throughout
 *
 * Each NODE corresponds to one SIL "tree node" (NODESZ = 3*DESCR words in SIL).
 * In SIL the tree is built with ADDSON/ADDSIB; here we use a flat children[] array.
 * stype mirrors the SIL STYPE field: the same integer codes from equ.h that
 * stream() writes into STYPE are stored here to label every node kind.
 * ========================================================================= */

typedef struct NODE NODE;
struct NODE {
    int      stype;          /* SIL STYPE code: QLITYP/ILITYP/VARTYP/FNCTYP/ADDFNe tc. */
    char    *text;           /* token text: var name, literal value, operator name */
    NODE   **children;       /* child nodes (args, operands) — grown with realloc */
    int      nchildren, nalloc; /* used / allocated slots in children[] */
    /* numeric literal payloads (only one is valid, selected by stype) */
    long long ival;          /* integer value when stype==ILITYP */
    double    fval;          /* float value when stype==FLITYP */
};

static NODE *node_new(int stype, const char *text, int tlen) {
    NODE *n  = calloc(1, sizeof *n);
    n->stype = stype;
    n->text  = tlen >= 0 ? strndup(text, tlen) : strdup(text ? text : "");
    return n;
}

static void node_add(NODE *parent, NODE *child) {
    if (!child) return;
    if (parent->nchildren >= parent->nalloc) {
        parent->nalloc = parent->nalloc ? parent->nalloc * 2 : 4;
        parent->children = realloc(parent->children,
                                   parent->nalloc * sizeof(NODE*));
    }
    parent->children[parent->nchildren++] = child;
}

/* =========================================================================
 * Compiler state — mirrors SIL globals
 * TEXTSP = remaining source text
 * XSP    = last token prefix extracted by stream()
 * BRTYPE = break type set by FORBLK/FORWRD (mirrors SIL BRTYPE)
 * ========================================================================= */

static spec_t TEXTSP;    /* remaining input */
static spec_t XSP;       /* last prefix from stream() */
static int    BRTYPE;    /* break type from FORWRD/FORBLK */

/* Error reporting */
static int  g_error;
static char g_errmsg[256];
static void sil_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_errmsg, sizeof g_errmsg, fmt, ap);
    va_end(ap);
    g_error = 1;
}

/* -I include search paths (populated from command-line -I flags) */
#define MAX_INCLUDE_PATHS 64
const char *g_include_paths[MAX_INCLUDE_PATHS];
int         g_num_include_paths = 0;

/* =========================================================================
 * IO state for true streaming — mirrors SIL UNIT/TEXTSP/NEXTSP globals.
 *
 * CSNOBOL4 keeps TEXTSP = one physical line at a time.  FORWRD on ST_EOS
 * calls FORRUN which calls IO_READ for the next physical card, runs CARDTB,
 * dispatches NEWCRD:
 *   CMTTYP (comment)      → skip, loop (FORRN0)
 *   CNTTYP (continuation) → strip '+', RTN2 → re-drive FORWRD on remainder
 *   CTLTYP (control -XXX) → handle -INCLUDE, then treat as comment
 *   NEWTYP (new stmt)     → save as g_pending_*, return EOSTYP to FORWRD
 *
 * No pre-joining. No linebuf. This is what CSNOBOL4 does.
 * ========================================================================= */
#define IO_LINEBUF_SZ 4096

static FILE       *g_io_file     = NULL;   /* current source file */
static const char *g_io_path     = NULL;   /* path of current source file */
static int         g_io_lineno   = 0;      /* physical line counter */
static int         g_io_depth    = 0;      /* -INCLUDE nesting depth */

/* Physical line buffer — TEXTSP.ptr points into here during CMPILE */
static char g_io_linebuf[IO_LINEBUF_SZ];

/* Pending line — a NEWTYP card read by FORRUN that belongs to the NEXT stmt */
static char g_pending_buf[IO_LINEBUF_SZ];
static int  g_pending_len  = 0;   /* >0 means a pending line is waiting */
static int  g_pending_lineno = 0;

/* g_stmt_lineno — physical line number of current statement's first card */
static int  g_stmt_lineno  = 0;

/* g_io_eof — set when the file (and all includes) are exhausted */
static int  g_io_eof = 0;

/* Forward declaration — resolve_include_path is defined later in the file */
static char *resolve_include_path(const char *base_path, const char *incname);

/* io_read_raw — read one raw physical line into buf (max bufsz), strip CRLF.
 * Returns length > 0 on success, 0 on EOF/error. */
static int io_read_raw(FILE *f, char *buf, int bufsz) {
    if (!fgets(buf, bufsz, f)) return 0;
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1]=='\n' || buf[len-1]=='\r')) buf[--len] = '\0';
    return len;
}

/* forrun — implements SIL FORRUN: read next physical card and dispatch NEWCRD.
 * Called by FORWRD/FORBLK when STREAM returns ST_EOS.
 * Sets TEXTSP to the continuation content and returns:
 *   2 → continuation card: TEXTSP is ready, caller (FORWRD) should re-drive STREAM
 *   1 → new statement pending (saved in g_pending_*) or EOF: TEXTSP exhausted
 *
 * Mirrors CSNOBOL4 snobol4.c FORRUN/NEWCRD logic exactly.
 */
static int forrun(void);  /* forward decl — defined after CMPILE helpers */

/* =========================================================================
 * FORWRD / FORBLK — inter-field scanning (v311.sil lines 2214, 2241)
 *
 * FORWRD: STREAM XSP,TEXTSP,FRWDTB → sets BRTYPE
 * FORBLK: STREAM XSP,TEXTSP,IBLKTB → sets BRTYPE (skips leading blank first)
 * ========================================================================= */

/* FORWRD — "Procedure to get to next character" (v311.sil:2214)
 *
 * SIL:  STREAM XSP,TEXTSP,FRWDTB,COMP3,FORRUN
 *   ST_ERROR → COMP3  (sil_error)
 *   ST_EOS   → FORRUN (fetch next physical card; if continuation, re-drive)
 *   ST_STOP  → FORJRN (set BRTYPE=STYPE, return)
 */
static void FORWRD(void) {
retry:;
    stream_ret_t r = stream(&XSP, &TEXTSP, &FRWDTB);
    if (r == ST_ERROR) { sil_error("FORWRD: scan error"); return; }
    if (r == ST_EOS) {
        int fr = forrun();
        if (fr == 2) goto retry;   /* continuation: TEXTSP loaded, re-drive */
        BRTYPE = EOSTYP;            /* EOF or new-stmt pending */
        return;
    }
    BRTYPE = STYPE;
}

/* FORBLK — "Procedure to get to nonblank" (v311.sil:2241)
 *
 * SIL:  STREAM XSP,TEXTSP,IBLKTB,RTN1,FORRUN,FORJRN
 *   ST_ERROR → RTN1   (no blank at current pos — caller handles; NOT an error)
 *   ST_EOS   → FORRUN (fetch next physical card; if continuation, re-drive)
 *   ST_STOP  → FORJRN (set BRTYPE=STYPE, return normally)
 *
 * BINOP calls FORBLK and treats ST_ERROR (BRTYPE unchanged / 0) as "no blank → BINOP1".
 */
static void FORBLK(void) {
retry:;
    stream_ret_t r = stream(&XSP, &TEXTSP, &IBLKTB);
    if (r == ST_ERROR) { /* RTN1: no blank here — leave BRTYPE as-is, caller decides */ return; }
    if (r == ST_EOS) {
        int fr = forrun();
        if (fr == 2) goto retry;   /* continuation: re-drive */
        BRTYPE = EOSTYP;
        return;
    }
    BRTYPE = STYPE;  /* FORJRN */
}

/* =========================================================================
 * CATFN — sentinel return value meaning juxtaposition (blank concatenation).
 * SIL uses CONCL descriptor for this; we use a distinct integer.
 * ========================================================================= */
#define CATFN  100  /* juxtaposition operator: blank-separated concatenation.
                    * SIL uses CONCL descriptor (v311.sil:10773 "CONFN,FNC,0 — concatenation").
                    * Returned by BINOP() when a blank was found but no explicit operator char followed.
                    * expr_prec_continue() builds a CAT/SEQ node when it sees CATFN. */

/* =========================================================================
 * BINOP — reads binary operator via BIOPTB (called from EXPR)
 *
 * Mirrors SIL BINOP procedure exactly (v311.sil line 1558):
 *
 *   BINOP:  RCALL ,FORBLK,,BINOP1   — skip leading blank; if no blank → BINOP1
 *           AEQLC BRTYPE,NBTYP,RTN2 — if BRTYPE=NBTYP (non-blank field break) → fail (RTN2)
 *           STREAM XSP,TEXTSP,BIOPTB,BINCON  — read operator; if fail → BINCON
 *           BINCON: return CATFN (juxtaposition)
 *   BINOP1: RCALL ,FORWRD,,COMP3    — if no leading blank, use FORWRD
 *           SELBRA BRTYPE,(...RTN2...) — break chars → fail
 *
 * Returns: operator code (ADDFN, SUBFN, MPYFN, EXPFN, ORFN, ...) on success,
 *          CATFN (100) for juxtaposition (blank with no following operator),
 *          0 if no operator and no juxtaposition (field delimiter or EOS).
 * ========================================================================= */

static int BINOP(void) {
    spec_t saved_text = TEXTSP;

    /* SIL BINOP: RCALL ,FORBLK,,BINOP1 */
    /* Try to skip a leading blank */
    spec_t blank_tok;
    stream_ret_t br = stream(&blank_tok, &TEXTSP, &IBLKTB);

    if (br == ST_EOS) {
        /* End of input — no operator */
        TEXTSP = saved_text;
        return 0;
    }

    if (br == ST_ERROR) {
        /* IBLKTB error = no blank at current position → BINOP1 path */
        TEXTSP = saved_text;
        /* BINOP1: RCALL ,FORWRD,,COMP3 — find next character */
        FORWRD();
        /* SELBRA BRTYPE,(,RTN2,RTN2,,,RTN2,RTN2) — field delimiters fail */
        switch (BRTYPE) {
        case RPTYP: case CMATYP: case EOSTYP: case EQTYP:
        case CLNTYP: case RBTYP:
            TEXTSP = saved_text;
            return 0;
        default:
            TEXTSP = saved_text;
            return 0;
        }
    }

    /* br == ST_STOP: blank was found and consumed.
     * IBLKTB chains to FRWDTB which sets STYPE:
     *   NBTYP  = stopped before a real token (e.g. '+', '*') — proceed to BIOPTB.
     *   EOSTYP/CLNTYP/EQTYP/RPTYP/CMATYP/RBTYP = field delimiter → no operator.
     * SIL AEQLC BRTYPE,NBTYP,RTN2 only fires on the BINOP1 (no-blank) path, NOT here.
     * On the blank-found path, NBTYP is the normal/expected result — the operator char
     * is sitting right there in TEXTSP. We must fall through to BIOPTB. */
    int stype_after_blank = STYPE;  /* what IBLKTB→FRWDTB found after the blank */
    if (stype_after_blank == EOSTYP || stype_after_blank == CLNTYP ||
        stype_after_blank == EQTYP  || stype_after_blank == RPTYP  ||
        stype_after_blank == CMATYP || stype_after_blank == RBTYP) {
        /* Field delimiter after blank → end of expression */
        TEXTSP = saved_text;
        return 0;
    }

    /* Blank was found, TEXTSP now points past it at the operator or next token.
     * Try BIOPTB to consume the operator character(s). */
    spec_t op_tok;
    stream_ret_t or_ = stream(&op_tok, &TEXTSP, &BIOPTB);

    if (or_ == ST_ERROR || or_ == ST_EOS) {
        /* BINCON: no explicit operator → juxtaposition (concatenation) */
        /* Do NOT restore TEXTSP — the blank was real, stay past it */
        return CATFN;
    }

    /* Got an operator. STYPE = ADDFN, SUBFN, MPYFN (→STARTB), EXPFN, etc. */
    int op = STYPE;

    /* STARTB disambiguates * vs **: consume the trailing space or second * */
    /* stream() already chased the BIOPTB GOTO to STARTB/TBLKTB if needed,
     * so STYPE is already resolved: MPYFN for *, EXPFN for **. */

    return op;
}

/* =========================================================================
 * Operator precedence — mirrors SIL's precedence descriptor in function nodes
 * SIL stores prec in CODE+2*DESCR of each operator function descriptor.
 * We table it directly.
 * ========================================================================= */

/* op_prec — operator precedence for Pratt-style precedence climbing.
 * Mirrors the precedence encoded in SIL's operator CODE+2*DESCR fields
 * (v311.sil:2155 "GETDC EXOPCL,EXOPCL,2*DESCR — Get left precedence").
 * Higher number = binds tighter.  BIAMFN(&) is lowest; NAMFN/DOLFN are highest. */
static int op_prec(int fn) {
    switch (fn) {
    case BIAMFN: return 1;   /* '&' user-definable — lowest precedence of all */
    case BISNFN: return 1;   /* '?' SPITBOL scan-replace — same level as & (lowest) */
    case BIQSFN: return 1;   /* '?' via BIOPTB table (BIQSFN=BISNFN alias) */
    case ORFN:   return 2;   /* '|' pattern alternation — second lowest */
    case ADDFN:
    case SUBFN:  return 3;   /* '+' '-' addition/subtraction */
    case MPYFN:
    case DIVFN:
    case BIPRFN: return 4;   /* '*' '/' '%' multiplicative */
    case EXPFN:  return 5;   /* '**' exponentiation — right-associative */
    case BIATFN: return 6;   /* '@' cursor — binds tight */
    case NAMFN:
    case DOLFN:  return 7;   /* '.' '$' naming/capture — tightest, right-associative */
    default:     return 3;   /* other user-definable operators default to additive prec */
    }
}

/* op_right_assoc — true for operators that associate right-to-left.
 * SIL's tree builder (ADDSON/INSERT) achieves right-associativity by inserting
 * nodes at the right spine of the tree.  We replicate this with next_min=prec
 * instead of prec+1 in expr_prec_continue(). */
static int op_right_assoc(int fn) {
    return fn == EXPFN  /* '**' — 2**3**2 = 2**(3**2)=512, not (2**3)**2=64 */
        || fn == NAMFN  /* '.'  — A.B.C → A.(B.C) */
        || fn == DOLFN  /* '$'  — immediate naming, same right-spine rule */
        || fn == BIATFN;/* '@'  — cursor capture, right-associative */
}

static const char *fn_name(int fn) {
    switch(fn) {
    case ADDFN:  return "ADDFN(+)";
    case SUBFN:  return "SUBFN(-)";
    case MPYFN:  return "MPYFN(*)";
    case DIVFN:  return "DIVFN(/)";
    case EXPFN:  return "EXPFN(**)";
    case ORFN:   return "ORFN(|)";
    case NAMFN:  return "NAMFN(.)";
    case DOLFN:  return "DOLFN($)";
    case BIATFN: return "BIATFN(@)";
    case BIPDFN: return "BIPDFN(#)";
    case BIPRFN: return "BIPRFN(%)";
    case BIAMFN: return "BIAMFN(&)";
    case BISNFN: return "BISNFN(?)";
    case BINGFN: return "BINGFN(~)";
    case BIQSFN: return "BIQSFN(?)";
    default: { static char buf[16]; snprintf(buf,16,"FN(%d)",fn); return buf; }
    }
}

/* stype_name — human-readable label for an STYPE integer.
 * STYPE codes are context-dependent: the same integer means different things
 * depending on which table produced it.  The labels here list all meanings
 * for each numeric value so the debug output is self-documenting. */
static const char *stype_name(int st) {
    static const char *names[] = {
        "ST(0)",
        "QLITYP=1/UGOTYP=1/NEWTYP=1/NBTYP=1", /* 1: quoted-lit | uncond-goto | new-card | non-blank */
        "ILITYP=2/SGOTYP=2/CMATYP=2/CMTTYP=2", /* 2: int-lit | :S( goto | comma | comment */
        "VARTYP=3/FGOTYP=3/RPTYP=3/CTLTYP=3",  /* 3: variable | :F( goto | ) | control-card */
        "NSTTYP=4/EQTYP=4/UTOTYP=4/CNTTYP=4",  /* 4: nested-( | = repl | :< direct | continuation */
        "FNCTYP=5/CLNTYP=5/STOTYP=5",           /* 5: func-call | : goto-field | :S< direct */
        "FLITYP=6/EOSTYP=6/FTOTYP=6",           /* 6: float-lit | end-of-stmt | :F< direct */
        "ARYTYP=7/RBTYP=7",                      /* 7: array-ref | ) or > closing bracket */
    };
    if (st >= 0 && st <= 7) return names[st];
    if (st == SELTYP) return "SELECT";
    { static char buf[20]; snprintf(buf,20,"ST(%d)",st); return buf; }
}

/* =========================================================================
 * ELEMNT — element analysis procedure (v311.sil:1924 "Element analysis procedure")
 *
 * Mirrors SIL exactly:
 *   RCALL ELEMND,UNOP       — collect chain of unary prefix operators into ELEMND tree
 *   STREAM XSP,TEXTSP,ELEMTB — classify first char of the next token
 *   SELBRA STYPE,(ELEILT,ELEVBL,ELENST,ELEFNC,ELEFLT,ELEARY)
 *                             — branch to handler: integer / variable / nested /
 *                               function-call / float / array-ref
 *
 * ELEMND  = "ELEMent Node" — SIL temporary holding the built tree
 * ELEXND  = "ELEment eXtra Node" — SIL temporary for the leaf node
 * ELEILT  = integer literal handler
 * ELEVBL  = variable / literal handler
 * ELENST  = nested (parenthesized) expression handler
 * ELEFNC  = function call handler
 * ELEFLT  = float literal handler
 * ELEARY  = array subscript handler
 * ========================================================================= */

static NODE *EXPR(void);  /* forward */
static NODE *EXPR1(void); /* forward */
static NODE *expr_prec_continue(NODE *left, int min_prec); /* forward */

static NODE *ELEMNT(void) {
    if (g_error) return NULL;

    /* --- UNOP: collect unary prefix operators (UNOPTB) --- */
    /* SIL: RCALL ELEMND,UNOP,,RTN2  — builds unary operator tree */
    NODE *unary_chain = NULL;
    NODE *unary_tail  = NULL;
    int   unary_nospace = 0; /* last op had no trailing space (operand follows directly) */
    for (;;) {
        spec_t saved = TEXTSP;
        spec_t tok;
        stream_ret_t r = stream(&tok, &TEXTSP, &UNOPTB);
        if (r == ST_ERROR) {
            /* UNOPTB→NBLKTB returned ERROR.  Two sub-cases:
             *
             * (a) UNOPTB itself rejected the char (not a unary op): tok.len==0,
             *     TEXTSP==saved — truly not a unary op, stop the chain.
             *
             * (b) UNOPTB recognised the op char (ACT_GOTO→NBLKTB), then NBLKTB
             *     rejected the next char because there is no space between the op
             *     and its operand.  In this case UNOPTB.chrs[*TEXTSP.ptr] < 15
             *     (not the error-value) and the next char is a token starter.
             *     SPITBOL allows *VAR, $X, .N with no intervening space.
             *     Manually consume the op char, derive STYPE from the action table,
             *     and continue. */
            if (TEXTSP.len >= 2) {
                unsigned char opc = (unsigned char)TEXTSP.ptr[0];
                unsigned char nxt = (unsigned char)TEXTSP.ptr[1];
                unsigned aidx = UNOPTB.chrs[opc];
                /* aidx==15 means ERROR in UNOPTB itself — not a unary op char */
                /* nxt must be a token-starting char (letter, digit, quote, paren) */
                int nxt_is_token = (nxt >= 'A' && nxt <= 'Z') ||
                                   (nxt >= 'a' && nxt <= 'z') ||
                                   (nxt >= '0' && nxt <= '9') ||
                                   nxt == '\'' || nxt == '"'  || nxt == '(';
                if (aidx != 15 && aidx != 0 && nxt_is_token) {
                    /* Consume the op char manually */
                    acts_t *ap = &UNOPTB.actions[aidx - 1];
                    STYPE = ap->put;   /* e.g. STRFN for '*' */
                    TEXTSP.ptr++;
                    TEXTSP.len--;
                    unary_nospace = 1;
                    /* fall through to build the unary node */
                } else {
                    TEXTSP = saved; break;
                }
            } else {
                TEXTSP = saved; break;
            }
        } else {
            unary_nospace = 0;
        }
        int uop = STYPE;
        static const char *uop_names[] = {
            "?","UOP_PLS","UOP_MNS","UOP_DOT","UOP_IND","UOP_STR",
            "UOP_SLH","UOP_PCT","UOP_AT","UOP_PD","UOP_KEY",
            "UOP_NEG","UOP_BAR","UOP_QUE","UOP_ARW"
        };
        const char *nm = (uop >= 301 && uop <= 314)
                         ? uop_names[uop-300] : "UOP?";
        NODE *unode = node_new(uop, nm, -1);
        if (!unary_chain) { unary_chain = unary_tail = unode; }
        else              { node_add(unary_tail, unode); unary_tail = unode; }
    }

    /* After unary prefix operators, skip any whitespace before the operand.
     * When a space separated op from operand (unary_nospace==0), UNOPTB→NBLKTB
     * left TEXTSP pointing AT the space; FORWRD skips to the token.
     * When no space (unary_nospace==1), TEXTSP already points at the operand. */
    if (unary_chain && !unary_nospace) FORWRD();

    /* --- STREAM XSP,TEXTSP,ELEMTB — classify element --- */
    /* SIL: STREAM XSP,TEXTSP,ELEMTB,ELEICH,ELEILI
     *   ST_ERROR → ELEICH (illegal character)
     *   ST_EOS   → ELEILI: if STYPE==0 → ELEICH (error), else continue
     *   ST_STOP  → fallthrough (normal) */
    stream_ret_t r = stream(&XSP, &TEXTSP, &ELEMTB);
    if (r == ST_ERROR) {
        sil_error("ELEMNT: illegal character");
        return NULL;
    }
    if (r == ST_EOS) {
        /* ELEILI: AEQLC STYPE,0,,ELEICH */
        if (STYPE == 0) { sil_error("ELEMNT: illegal character"); return NULL; }
        /* else STYPE!=0: a table GOTO fired before EOS — continue with what we have */
    }
    int elem_stype = STYPE;

    NODE *atom = NULL;

    /* Classify by first character consumed (XSP.ptr[0]),
       since STYPE reflects the terminal table after all GOTO chains:
       - LETTER  → VARTB final STYPE = VARTYP/FNCTYP/ARYTYP
       - NUMBER  → INTGTB final STYPE = ILITYP or FLITYP
       - QUOTE   → SQLITB/DQLITB final STYPE = 0 (just stopped)
       - LPAREN  → ACT_STOP, elem_stype = NSTTYP
       SIL's SELBRA STYPE,(,ELEILT,ELEVBL,ELENST,ELEFNC,ELEFLT,ELEARY)
       works because ELEMTB put-codes go into STYPE before the GOTO. */
    unsigned char first = XSP.len > 0 ? (unsigned char)XSP.ptr[0] : 0;
    int is_digit  = (first >= '0' && first <= '9');
    int is_letter = ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_');
    int is_quote  = (first == 39 || first == 34);  /* 39=' 34=" */
    int is_lparen = (first == '(');

    /* elem_stype from ELEMTB put before GOTO: ILITYP=NUMBER, VARTYP=LETTER,
       QLITYP=QUOTE, NSTTYP=LPAREN. After chasing GOTOs STYPE changes.
       Use first-char to re-establish the branch. */
    (void)elem_stype; /* replaced by first-char dispatch */
    int dispatch = is_digit ? ILITYP : is_letter ? VARTYP :
                   is_quote ? QLITYP : is_lparen ? NSTTYP : 0;
    switch (dispatch) {

    case ILITYP: {  /* ELEILT: integer — STREAM consumed digits via INTGTB */
        /* XSP holds the digit string; STYPE may be FLITYP if it became real */
        /* INTGTB's ACT_GOTO to FLITB already consumed the fraction */
        int final_type = STYPE; /* may be ILITYP or FLITYP */
        char buf[64]; memcpy(buf, XSP.ptr, XSP.len < 63 ? XSP.len : 63);
        buf[XSP.len < 63 ? XSP.len : 63] = '\0';
        atom = node_new(final_type, buf, -1);
        if (final_type == ILITYP) atom->ival = atoll(buf);
        else                      atom->fval = atof(buf);
        break;
    }

    case FLITYP: {  /* ELEFLT: real literal starting with . */
        char buf[64]; memcpy(buf, XSP.ptr, XSP.len < 63 ? XSP.len : 63);
        buf[XSP.len < 63 ? XSP.len : 63] = '\0';
        atom = node_new(FLITYP, buf, -1);
        atom->fval = atof(buf);
        break;
    }

    case QLITYP: {  /* ELEVBL (quote): STREAM consumed up to closing quote */
        /* XSP includes the closing quote; strip both delimiters */
        const char *p = XSP.ptr + 1;          /* skip open quote */
        int len = XSP.len - 2;                 /* strip both quotes */
        if (len < 0) len = 0;
        atom = node_new(QLITYP, p, len);
        break;
    }

    case VARTYP: {  /* ELEVBL: variable — STREAM via VARTB; STYPE = VARTYP/FNCTYP/ARYTYP */
        int final = STYPE;
        const char *p = XSP.ptr;
        int len = XSP.len;
        /* strip trailing ( or < consumed by VARTB STOP */
        if (final == FNCTYP || final == ARYTYP) len--;
        atom = node_new(final, p, len);

        if (final == FNCTYP) {   /* ELEFNC: function call */
            /* SIL ELEMN2: RCALL EXELND,EXPR; FORWRD to get delimiter.
             * After FNCTYP STOPSH, TEXTSP points at first char after '('.
             * CS trace: FRWDTB[firstarg] ELEMTB FRWDTB[,] FRWDTB[nextarg] ELEMTB ...
             * First FORWRD positions past '(' (or space) to first arg token.
             * After comma (CMATYP), a second FORWRD skips space to next arg. */
            FORWRD();  /* position past '(' to first arg (or to ')' for zero-arg) */
            /* After opening '(', TEXTSP may have a leading space — skip it. */
            while (BRTYPE == NBTYP) { /* NBTYP=1: stopped short before non-blank */; break; }
            while (!g_error) {
                if (BRTYPE == RPTYP) break;  /* ')': end of arg list (zero-arg or after last) */
                if (BRTYPE == CMATYP) {
                    /* empty arg: F(,x) or F(x,,y) — push NULL, advance past comma */
                    node_add(atom, node_new(0, "NULL", -1));
                    FORWRD();  /* skip space to next arg or next comma */
                    continue;
                }
                NODE *arg = EXPR();
                node_add(atom, arg);
                FORWRD();  /* get delimiter after arg: CMATYP or RPTYP */
                if (BRTYPE == RPTYP) break;
                if (BRTYPE == CMATYP) {
                    FORWRD();  /* skip space after comma to position at next arg */
                    continue;
                }
                /* SIL v311 ELEMN2 fall-through: any other BRTYPE (e.g. EQTYP from
                 * 'sno = Pop()' inside arg list) — consume the separator and loop.
                 * FORWRD stopped short (STOPSH) or consumed; call FORWRD again to
                 * advance past the separator to the next token. */
                if (BRTYPE == EOSTYP || BRTYPE == CLNTYP) break; /* true end-of-stmt */
                FORWRD();  /* skip unexpected separator (e.g. '=') and continue */
            }
        } else if (final == ARYTYP) {  /* ELEARY: array subscript */
            /* Mirror ELEFNC: FORWRD past '<', handle space-after-comma */
            FORWRD();  /* position past '<' to first subscript */
            while (!g_error) {
                if (BRTYPE == RBTYP) break;  /* empty subscript: A<> */
                if (BRTYPE == CMATYP) {
                    node_add(atom, node_new(0, "NULL", -1));  /* empty slot */
                    FORWRD();
                    continue;
                }
                NODE *sub = EXPR();
                node_add(atom, sub);
                /* P2D: A[J=J+1] — CSNOBOL4 ELEARG loop re-enters EXPR after EQTYP.
                 * IBLKTB consumed '='; TEXTSP is at ' J+1>...'.
                 * SIL UNOP calls FORWRD before UNOPTB (v311.sil:2507), so ELEMNT always
                 * receives a non-space TEXTSP. Mirror that: FORWRD() here skips the space,
                 * then continue → EXPR() → ELEMNT sees 'J' directly. */
                if (BRTYPE == EQTYP) { FORWRD(); FORWRD(); continue; }
                FORWRD();  /* get delimiter: CMATYP or RBTYP */
                if (BRTYPE == RBTYP) break;
                if (BRTYPE == CMATYP) {
                    FORWRD();  /* skip space after comma */
                    continue;
                }
                sil_error("ELEMNT: expected > or , in subscript, got BRTYPE=%d", BRTYPE);
                break;
            }
        }
        break;
    }

    case NSTTYP: {  /* ELENST: parenthesized expression — SIL v311.sil:2003 */
        /* '(' was ACT_STOP consumed by ELEMTB. TEXTSP points at inner content.
         * Skip any leading whitespace before the expression (e.g. "( SPAN(...) )").
         * P2B: if comma follows first expr, build SELECT node for alt-eval.
         *   (e1, e2, ..., en) — evaluate left to right until one succeeds.
         * SPITBOL manual §AppC p275: "selection or alternative construction". */
        FORWRD();  /* skip space/tab after '(' to position at first token */
        NODE *first = EXPR();
        if (BRTYPE == CMATYP && !g_error) {
            /* Alternative evaluation — collect all alternatives */
            atom = node_new(SELTYP, "SELECT", -1);
            node_add(atom, first);
            while (BRTYPE == CMATYP && !g_error) {
                /* TEXTSP is positioned AT ',' (FRWDTB ACT_STOP does not consume it).
                 * Manually skip the comma, then FORWRD to next token. */
                if (TEXTSP.len > 0 && *TEXTSP.ptr == ',') {
                    TEXTSP.ptr++; TEXTSP.len--;
                }
                FORWRD();  /* position at next expression */
                node_add(atom, EXPR());
            }
        } else {
            atom = first;
        }
        FORWRD();
        BRTYPE = RPTYP;
        break;
    }

    default:
        sil_error("ELEMNT: unknown element STYPE=%d", elem_stype);
        return NULL;
    }

    /* Wrap atom in unary chain (innermost last) */
    if (unary_tail) {
        node_add(unary_tail, atom);
        atom = unary_chain;
    }

    return atom;
}

/* =========================================================================
 * EXPR / EXPR1 — expression compiler (v311.sil:2093 "Procedure to compile expression")
 *
 * Mirrors SIL's operator-precedence tree builder:
 *   EXPR:  RCALL EXELND,ELEMNT  — compile one element into EXELND
 *          then EXPR2 loop: RCALL EXOPCL,BINOP → get operator → insert into tree
 *   EXPR1: same entry point but called from CMPILE's scan-replace path
 *
 * EXELND = "EXpression ELement Node" — SIL temp for the element just parsed
 * EXOPCL = "EXpression OPerator CLass" — SIL temp for the current operator descriptor
 * EXOPND = "EXpression OPerand Node"  — SIL temp for the current operand node
 * EXPRND = "EXPRession Node"          — SIL temp for the accumulated expression tree
 *
 * SIL builds the tree with ADDSON (add child) and INSERT (insert between nodes).
 * We use Pratt-style precedence climbing (expr_prec_continue) — same result,
 * simpler to follow.  BRTYPE is updated by each FORBLK/FORWRD call inside ELEMNT. *
 * CATFN (juxtaposition): when BINOP() finds a blank but no operator char,
 * expr_prec_continue() builds a CAT node, mirroring SIL's BINCON → CONCL path.
 * ========================================================================= */

static NODE *expr_prec(int min_prec);

static NODE *EXPR(void) {
    if (g_error) return NULL;
    /* EXPR: RCALL EXELND,ELEMNT,,(RTN1,EXPNUL) */
    spec_t saved = TEXTSP;
    NODE *left = ELEMNT();
    if (!left || g_error) {
        /* EXPNUL: return null node */
        TEXTSP = saved;
        return node_new(0, "NULL", -1);
    }
    /* EXPR2: RCALL EXOPCL,BINOP loop */
    return expr_prec_continue(left, 0);
}

static NODE *EXPR1(void) {
    /* EXPR1: PUSH EXPRND; RCALL EXELND,ELEMNT; POP EXPRND; → EXPR2 */
    return EXPR();
}

static NODE *expr_prec_continue(NODE *left, int min_prec);

static NODE *expr_prec(int min_prec) {
    NODE *left = ELEMNT();
    if (!left || g_error) return node_new(0, "NULL", -1);
    return expr_prec_continue(left, min_prec);
}

static NODE *expr_prec_continue(NODE *left, int min_prec) {
    /* CATFN juxtaposition precedence = 10 (highest — tighter than any explicit op).
     * SIL BINCON path: blank found, no explicit operator char → CONCL concatenation.
     * We use a flat n-ary CAT node: accumulate all juxtaposed elements into one. */
#define CATFN_PREC 10
    for (;;) {
        /* SPITBOL P2C: postfix [] subscript on any expression — e.g. f(g(x)[i])
         * and chained subscripts T['n']['!'].
         * '[' hits FRWDTB action NBTYP (STOPSH) so BRTYPE=1 after FORWRD.
         * After ELEARY closes on ']', TEXTSP.len may be 0 even though more text follows
         * (the stream exhausted the current segment at the closing ']').
         * In that case, call FORWRD() speculatively; if it stops on '[' (NBTYP),
         * fire the postfix handler. If it stops on a field sep, save/restore. */
        /* After ELEARY closes on ']', TEXTSP.len==0 (stream segment exhausted).
         * Call FORWRD() to advance to the next token.
         * If it fires NBTYP on '[' → chained subscript: fall through to '[' check.
         * Any other result → expression is done (field sep or EOS): break now.
         * Do NOT fall through to BINOP after this FORWRD — that would double-advance. */
        if (TEXTSP.len == 0 && BRTYPE == RBTYP) {
            FORWRD();
            if (!(TEXTSP.len > 0 && *TEXTSP.ptr == '[')) {
                break;  /* field separator or EOS: expression complete */
            }
            /* else: TEXTSP.ptr == '[', fall through to postfix handler below */
        }
        if (TEXTSP.len > 0 && *TEXTSP.ptr == '[') {
            /* consume '[' */
            TEXTSP.ptr++; TEXTSP.len--;
            /* build IDX node: left is base, indices are children[1..] */
            NODE *idx = node_new(ARYTYP, "IDX", -1);
            node_add(idx, left);
            FORWRD();  /* skip space to first subscript (or to ']') */
            while (!g_error) {
                if (BRTYPE == RBTYP) break;   /* ']' or '>' — empty or last index */
                if (BRTYPE == CMATYP) {
                    node_add(idx, node_new(0, "NULL", -1));
                    FORWRD();
                    continue;
                }
                NODE *sub = EXPR();
                node_add(idx, sub);
                /* P2D: IBLKTB consumed '='; FORWRD skips space so ELEMNT sees 'J' not space. */
                if (BRTYPE == EQTYP) { FORWRD(); FORWRD(); continue; }
                FORWRD();  /* get delimiter: CMATYP or RBTYP */
                if (BRTYPE == RBTYP) break;
                if (BRTYPE == CMATYP) {
                    FORWRD();  /* skip space after comma */
                    continue;
                }
                sil_error("ELEMNT: expected ] or , in [] subscript, got BRTYPE=%d", BRTYPE);
                break;
            }
            left = idx;
            continue;
        }

        spec_t saved = TEXTSP;
        int op = BINOP();
        if (!op) { TEXTSP = saved; break; }     /* no operator → done */

        if (op == CATFN) {
            /* Juxtaposition — CATFN_PREC=10, left-associative (collect into flat CAT) */
            if (CATFN_PREC < min_prec) { TEXTSP = saved; break; }
            NODE *right = expr_prec(CATFN_PREC + 1);
            if (!right) { TEXTSP = saved; break; }
            /* Flatten: if left is already a CAT node, append; else create new CAT */
            if (left->stype == CATFN) {
                node_add(left, right);
            } else {
                NODE *cat = node_new(CATFN, "CAT", -1);
                node_add(cat, left);
                node_add(cat, right);
                left = cat;
            }
            continue;
        }

        int prec  = op_prec(op);
        if (prec < min_prec) { TEXTSP = saved; break; }

        int next_min = op_right_assoc(op) ? prec : prec + 1;
        FORWRD();  /* skip whitespace after operator before RHS — mirrors CSNOBOL4 IBLKTB call */
        NODE *right = expr_prec(next_min);

        NODE *binop = node_new(op, fn_name(op), -1);
        node_add(binop, left);
        node_add(binop, right);
        left = binop;
    }

    /* BRTYPE: set it based on what stopped us */
    /* FORWRD already set BRTYPE when FORBLK was called before ELEMNT */
    /* For closing delimiters inside expressions (args, subscripts),
       VARTB / ELEMTB set STYPE which we propagate as BRTYPE */
    BRTYPE = STYPE;
    return left;
}

/* =========================================================================
 * CMPILE — compile one statement (v311.sil:1608 "Procedure to compile statement")
 *
 * Implements the full SNOBOL4 statement grammar:
 *   [label:]  subject  [pattern]  [= replacement]  [:goto]
 *
 * SIL branch labels and their meanings:
 *   CMPIL0 = after label scan ("Break out label")
 *   CMPILA = after FORBLK; start of body ("Get to next character")
 *   CMPSUB = subject field (RCALL SUBJND,ELEMNT)
 *   CMPSB1 = after subject; decide next field
 *   CMPAT2 = pattern field (RCALL PATND,EXPR)
 *   CMPFRM = '=' replacement without pattern (SUBJECT = REPLACEMENT)
 *   CMPASP = '=' replacement after pattern  (SUBJECT PATTERN = REPLACEMENT)
 *   CMPGO  = goto field (STREAM XSP,TEXTSP,GOTOTB)
 *   CMPSGO / CMPFGO / CMPUGO = success / failure / unconditional goto handlers
 *
 * BOSCL  (v311.sil:11049 "Offset of beginning of statement") — we ignore this;
 *         in SIL it tracks object-code offset for the compiler's output buffer.
 * ========================================================================= */

/* STMT — one compiled SNOBOL4 statement; mirrors CSNOBOL4's STMT_t (src/frontend/snobol4/scrip_cc.h)
 * and the SIL object-code layout produced by CMPILE. */
typedef struct STMT STMT;
struct STMT {
    char *label;          /* column-1 label text, or NULL if no label */
    NODE *subject;        /* subject expression (always present per SNOBOL4 grammar) */
    NODE *pattern;        /* pattern expression, or NULL if no pattern field */
    NODE *replacement;    /* replacement expression, or NULL (present when has_eq) */
    int   has_eq;         /* 1 if '=' was present in source (distinguishes assign from match) */
    int   is_scan;        /* 1 if binary '?' scan operator was used (SUBJECT ? PATTERN) */
    char *go_s;           /* :S(label) success-goto target, or NULL */
    char *go_f;           /* :F(label) failure-goto target, or NULL */
    char *go_u;           /* :(label)  unconditional-goto target, or NULL */
    int   is_end;         /* 1 if this is the END statement (terminates program) */
    STMT *next;           /* linked-list chain to next statement in program */
};

/* xsp_dup — copy the last token text from XSP into a malloc'd C string.
 * XSP (eXtra SPecifier) is the output spec filled by stream(); it points
 * into the source line buffer and is valid only until the next stream() call. */
static char *xsp_dup(void) {
    return strndup(XSP.ptr, XSP.len); /* XSP.ptr/XSP.len = (pointer,length) into source line */
}

static STMT *CMPILE(void) {
    if (g_error) return NULL;
    STMT *s = calloc(1, sizeof *s);

    /* CMPIL0: STREAM XSP,TEXTSP,LBLTB,CERR1 — break out label field
     * ST_ERROR → CERR1 (label error); ST_EOS/ST_STOP → fallthrough */
    {
        stream_ret_t lr = stream(&XSP, &TEXTSP, &LBLTB);
        if (lr == ST_ERROR) { sil_error("CMPILE: label error"); return s; }
    }
    if (XSP.len > 0)
        s->label = xsp_dup();

    /* CMPILA: RCALL ,FORBLK — get to next character */
    FORBLK();

    /* BRTYPE==0 means ST_EOS on empty body (bare label line or blank line).
     * Treat same as EOSTYP — no body to parse. */
    if (BRTYPE == 0 || BRTYPE == EOSTYP) {
        return s;
    }

    /* If first char after blank was a colon → goto field only */
    if (BRTYPE == CLNTYP) goto CMPGO;

    /* If '=' immediately (no subject, no leading blank): null-subject assignment.
     * Handles unindented "VAR = value" where LBLTB consumed VAR as label and
     * FORBLK found '=' directly. Route to CMPFRM with no subject. */
    if (BRTYPE == EQTYP) goto CMPFRM;

    /* If non-blank (NBTYP) → subject field */
    /* CMPSUB: RCALL SUBJND,ELEMNT */
    if (BRTYPE != EOSTYP) {
        s->subject = ELEMNT();
        if (g_error) return s;

        /* Chained postfix [] subscript on subject: T['n']['!'] = ...
         * CMPILE calls ELEMNT() (not EXPR()), so expr_prec_continue never runs.
         * After ELEARY closes on ']', TEXTSP.len==0 and BRTYPE==RBTYP.
         * Advance with FORWRD(); if it stops on '[' (NBTYP), consume chained subscripts.
         * Stop as soon as FORWRD gives anything other than NBTYP+'['. */
        while (!g_error) {
            if (TEXTSP.len == 0 && BRTYPE == RBTYP) {
                FORWRD();  /* advance past the spent segment */
            }
            if (!(TEXTSP.len > 0 && *TEXTSP.ptr == '['))
                break;  /* no chained subscript — done */
            /* consume '[' */
            TEXTSP.ptr++; TEXTSP.len--;
            NODE *idx = node_new(ARYTYP, "IDX", -1);
            node_add(idx, s->subject);
            FORWRD();  /* position to first subscript index */
            while (!g_error) {
                if (BRTYPE == RBTYP) break;
                if (BRTYPE == CMATYP) {
                    node_add(idx, node_new(0, "NULL", -1));
                    FORWRD();
                    continue;
                }
                NODE *sub = EXPR();
                node_add(idx, sub);
                /* P2D: IBLKTB consumed '='; FORWRD skips space so ELEMNT sees 'J' not space. */
                if (BRTYPE == EQTYP) { FORWRD(); FORWRD(); continue; }
                FORWRD();
                if (BRTYPE == RBTYP) break;
                if (BRTYPE == CMATYP) { FORWRD(); continue; }
                sil_error("ELEMNT: expected ] in chained subscript, got BRTYPE=%d", BRTYPE);
                break;
            }
            s->subject = idx;
            /* TEXTSP.len==0 after closing ']'; loop continues → FORWRD() peeks next */
        }

        /* Check for END label */
        if (s->subject && s->subject->stype == VARTYP &&
            strcmp(s->subject->text, "END") == 0) {
            s->is_end = 1;
            return s;
        }

        /* FORBLK: get to next field */
        FORBLK();
    
        /* CMPSB1: after subject */
        if (BRTYPE == EQTYP) goto CMPFRM;   /* = replacement */
        if (BRTYPE == CLNTYP) goto CMPGO;   /* : goto */
        if (BRTYPE == EOSTYP) return s;     /* bare invoke */

        /* NBTYP: FORBLK stopped short (STOPSH) before a non-blank.
         * SIL [PLB32]: if char is '?' followed by space/tab/end → binary scan op.
         * Consume the '?', set scan flag, fall through to pattern parse.
         * CS source: v311.sil:1667-1675 */
        if (BRTYPE == NBTYP) {
            if (TEXTSP.len >= 1 && TEXTSP.ptr[0] == '?' &&
                (TEXTSP.len == 1 || TEXTSP.ptr[1] == ' ' || TEXTSP.ptr[1] == '\t')) {
                /* binary ? consumed — TEXTSP advances past it */
                TEXTSP.ptr++; TEXTSP.len--;
                s->is_scan = 1;   /* mark as ? scan statement */
                FORBLK();         /* skip whitespace to pattern */
                if (BRTYPE == EOSTYP || BRTYPE == 0) return s;
                if (BRTYPE == CLNTYP) goto CMPGO;
            } else {
                /* NBTYP without ? — juxtaposition in pattern context, fall through */
            }
        }

        /* Otherwise: pattern field follows */
        /* CMPAT2: RCALL PATND,EXPR */
        s->pattern = EXPR();
        if (g_error) return s;
        FORBLK();

        if (BRTYPE == EQTYP) goto CMPASP;
        if (BRTYPE == CLNTYP) goto CMPGO;
        return s;
    }
    return s;

CMPFRM:  /* SUBJECT = REPLACEMENT */
    s->has_eq = 1;
    FORWRD();  /* '=' already consumed; position to replacement value (no mandatory blank) */
    if (BRTYPE == EOSTYP || BRTYPE == 0) return s;   /* null replacement: X = */
    if (BRTYPE == CLNTYP) goto CMPGO;
    s->replacement = EXPR();
    if (g_error) return s;
    FORBLK();
    if (BRTYPE == CLNTYP) goto CMPGO;
    return s;

CMPASP:  /* SUBJECT PATTERN = REPLACEMENT (= consumed; position to replacement) */
    s->has_eq = 1;
    FORWRD();  /* '=' consumed; use FORWRD not FORBLK — no mandatory blank required */
    if (BRTYPE == EOSTYP || BRTYPE == 0) return s;   /* null replacement: X PAT = */
    if (BRTYPE == CLNTYP) goto CMPGO;
    s->replacement = EXPR();
    if (g_error) return s;
    FORBLK();
    if (BRTYPE == CLNTYP) goto CMPGO;
    return s;

CMPGO: {
    /* STREAM XSP,TEXTSP,GOTOTB,CERR11,CERR12 — classify goto type
     * ST_ERROR → CERR11 (bad goto), ST_EOS → CERR12 (bad goto), ST_STOP → ok */
    stream_ret_t r = stream(&XSP, &TEXTSP, &GOTOTB);
    if (r == ST_ERROR) { sil_error("CMPGO: bad goto"); return s; }
    if (r == ST_EOS)   { sil_error("CMPGO: truncated goto"); return s; }
    int gotype = STYPE;

    /* CMPSGO / CMPFGO / CMPUGO */
    if (gotype == UGOTYP || gotype == UTOTYP) {
        /* Unconditional: :(label) or :<label>
         * For UTOTYP, GOTOTB consumed '<'; TEXTSP may have leading space before label.
         * For UGOTYP, GOTOTB consumed '('; TEXTSP is positioned at label. */
        if (gotype == UTOTYP) FORWRD();  /* skip whitespace after '<' */
        NODE *lbl = EXPR();
        s->go_u = lbl ? strdup(lbl->text ? lbl->text : "") : NULL;
        FORWRD();   /* consume closing ) or > → BRTYPE=RPTYP or RBTYP */
        return s;
    }

    if (gotype == SGOTYP || gotype == STOTYP) {
        /* Success: :S(label) or :S<label> */
        FORWRD();  /* skip whitespace after '(' or '<' to reach label expr */
        NODE *lbl = EXPR();
        s->go_s = lbl ? strdup(lbl->text ? lbl->text : "") : NULL;
        FORWRD();   /* consume closing ) or > → TEXTSP now at F, :F, or EOS */
        /* SNOBOL4 allows :S(x)F(y) or :S(x):F(y) — skip optional ':' */
        if (BRTYPE != EOSTYP && BRTYPE != 0 && TEXTSP.len > 0) {
            spec_t saved2 = TEXTSP;
            if (TEXTSP.ptr[0] == ':') { /* skip optional colon */
                TEXTSP.ptr++; TEXTSP.len--;
            }
            stream_ret_t gr = stream(&XSP, &TEXTSP, &GOTOTB);
            if (gr != ST_ERROR && (STYPE == FGOTYP || STYPE == FTOTYP)) {
                if (STYPE == FTOTYP) FORWRD();  /* skip whitespace after '<' */
                NODE *fl = EXPR();
                s->go_f = fl ? strdup(fl->text ? fl->text : "") : NULL;
                FORWRD();
            } else {
                TEXTSP = saved2;  /* not a goto — restore */
            }
        }
        return s;
    }

    if (gotype == FGOTYP || gotype == FTOTYP) {
        /* Failure: :F(label) or :F<label> */
        FORWRD();  /* skip whitespace after '(' or '<' to reach label expr */
        NODE *lbl = EXPR();
        s->go_f = lbl ? strdup(lbl->text ? lbl->text : "") : NULL;
        FORWRD();   /* consume closing ) or > */
        if (BRTYPE != EOSTYP && BRTYPE != 0 && TEXTSP.len > 0) {
            spec_t saved2 = TEXTSP;
            if (TEXTSP.ptr[0] == ':') {
                TEXTSP.ptr++; TEXTSP.len--;
            }
            stream_ret_t gr = stream(&XSP, &TEXTSP, &GOTOTB);
            if (gr != ST_ERROR && (STYPE == SGOTYP || STYPE == STOTYP)) {
                if (STYPE == STOTYP) FORWRD();  /* skip whitespace after '<' */
                NODE *sl = EXPR();
                s->go_s = sl ? strdup(sl->text ? sl->text : "") : NULL;
                FORWRD();
            } else {
                TEXTSP = saved2;
            }
        }
        return s;
    }

    sil_error("CMPGO: unrecognized goto type %d", gotype);
    return s;
    }
}

/* =========================================================================
 * IR printer — s-expression output using SIL node type names
 * Prints the parse tree to stdout in a Lisp-style nested form for debugging.
 * ========================================================================= */

static void print_node(NODE *n, int depth) {
    if (!n) { printf("%*s(NULL)\n", depth*2, ""); return; }
    printf("%*s(%s", depth*2, "", stype_name(n->stype));
    if (n->text && n->text[0])
        printf(" \"%s\"", n->text);
    if (n->stype == ILITYP)
        printf(" ival=%lld", n->ival);
    if (n->stype == FLITYP)
        printf(" fval=%g", n->fval);
    if (n->nchildren == 0) {
        printf(")\n");
    } else {
        printf("\n");
        for (int i = 0; i < n->nchildren; i++)
            print_node(n->children[i], depth+1);
        printf("%*s)\n", depth*2, "");
    }
}

static void print_stmt(STMT *s, int idx) {
    printf("=== stmt %d ===\n", idx);
    if (s->is_end)      { printf("  END\n"); return; }
    if (s->label)         printf("  label:   %s\n", s->label);
    if (s->subject)     { printf("  subject:\n"); print_node(s->subject, 2); }
    if (s->is_scan)       printf("  op:      ?\n");
    if (s->pattern)     { printf("  pattern:\n"); print_node(s->pattern, 2); }
    if (s->has_eq && s->replacement)
                        { printf("  replace:\n"); print_node(s->replacement, 2); }
    if (s->go_s)          printf("  :S(%s)\n", s->go_s);
    if (s->go_f)          printf("  :F(%s)\n", s->go_f);
    if (s->go_u)          printf("  :(%s)\n",  s->go_u);
}

/* =========================================================================
 * compile_state — shared mutable state threaded through compile_file().
 * No linebuf. No pre-joining. TEXTSP = one physical line. FORWRD calls
 * forrun() on EOS to load the next card, exactly as CSNOBOL4 does. */
typedef struct {
    STMT  *head;
    STMT  *tail;
    int    stmt_idx;
    int    done;   /* set to 1 when END statement is seen */
} compile_state_t;

/* g_cst — pointer to the active compile_state (set by compile_file) */
static compile_state_t *g_cst = NULL;

/* compile_one_stmt — compile TEXTSP as one statement, append to g_cst.
 * Called from compile_file after TEXTSP is set to a NEWTYP physical line. */
static void compile_one_stmt(void) {
    if (!g_cst || g_cst->done) return;
    g_error = 0;
    STYPE = 0; BRTYPE = 0;
    STMT *s = CMPILE();
    if (!s) {
        if (g_error)
            fprintf(stderr, "line %d: %s\n", g_stmt_lineno, g_errmsg);
        return;
    }
    s->next = NULL;
    if (!g_cst->head) g_cst->head = s; else g_cst->tail->next = s;
    g_cst->tail = s;
    if (g_error)
        fprintf(stderr, "line %d: %s\n", g_stmt_lineno, g_errmsg);
    print_stmt(s, ++g_cst->stmt_idx);
    if (s->is_end) g_cst->done = 1;
}

/* =========================================================================
 * forrun() — SIL FORRUN: fetch next physical card, dispatch NEWCRD.
 *
 * Called by FORWRD/FORBLK when STREAM returns ST_EOS on TEXTSP.
 * Returns:
 *   2 → continuation: TEXTSP updated to post-'+' content; caller re-drives STREAM
 *   1 → new stmt saved in g_pending_* (or EOF): caller sets BRTYPE=EOSTYP
 *
 * CSNOBOL4 reference: snobol4.c FORRUN (line 1850) + NEWCRD (line 1927).
 * ========================================================================= */
static int forrun(void) {
retry:
    if (!g_io_file || g_io_eof) return 1;

    char rawline[IO_LINEBUF_SZ];
    int len = io_read_raw(g_io_file, rawline, sizeof rawline);
    if (len == 0) {
        g_io_eof = 1;
        return 1;   /* EOF */
    }
    g_io_lineno++;

    /* CARDTB: determine card type */
    spec_t card = { rawline, len };
    spec_t tok;
    stream(&tok, &card, &CARDTB);
    int ctype = STYPE;

    /* NEWCRD dispatch (v311.sil NEWCRD, snobol4.c NEWCRD) */
    switch (ctype) {
    case CMTTYP:   /* comment — skip, loop (FORRN0) */
        goto retry;

    case CTLTYP: { /* control card (-XXX) — handle -INCLUDE, then skip */
        const char *p = rawline + 1;
        while (*p == '-') p++;
        const char *name_start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        int namelen = (int)(p - name_start);
        if (namelen == 7 && strncasecmp(name_start, "INCLUDE", 7) == 0) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p) {
                char *incpath = resolve_include_path(g_io_path, p);
                FILE *incf = fopen(incpath, "r");
                if (!incf) { char *cr = strchr(incpath,'\r'); if(cr)*cr='\0'; incf=fopen(incpath,"r"); }
                if (incf) {
                    /* push IO state, recurse */
                    FILE *save_file = g_io_file;
                    const char *save_path = g_io_path;
                    int save_lineno = g_io_lineno;
                    int save_eof    = g_io_eof;
                    int save_depth  = g_io_depth;
                    g_io_file = incf; g_io_path = incpath;
                    g_io_lineno = 0; g_io_eof = 0; g_io_depth++;
                    /* run included file: compile_file equivalent via outer loop */
                    /* We can't call compile_file here without restructuring;
                     * instead: drain the included file by calling forrun until EOF,
                     * compiling each NEWTYP statement as we go. */
                    while (!g_io_eof && !(g_cst && g_cst->done)) {
                        int fr2 = forrun();
                        if (fr2 == 2) {
                            /* continuation at top level — compile pending if any */
                            /* (shouldn't happen at include-open boundary) */
                        } else {
                            /* fr2==1: new stmt in g_pending or EOF */
                            if (g_pending_len > 0) {
                                memcpy(g_io_linebuf, g_pending_buf, g_pending_len);
                                g_io_linebuf[g_pending_len] = '\0';
                                TEXTSP.ptr = g_io_linebuf;
                                TEXTSP.len = g_pending_len;
                                g_stmt_lineno = g_pending_lineno;
                                g_pending_len = 0;
                                compile_one_stmt();
                            }
                            if (g_io_eof) break;
                        }
                    }
                    fclose(incf);
                    free(incpath);
                    g_io_file = save_file; g_io_path = save_path;
                    g_io_lineno = save_lineno; g_io_eof = save_eof;
                    g_io_depth = save_depth;
                } else {
                    fprintf(stderr, "sno4parse: cannot open include '%s': %s\n", incpath, strerror(errno));
                    free(incpath);
                }
            }
        }
        goto retry;  /* skip control card, loop */
    }

    case CNTTYP: { /* continuation — strip '+', set TEXTSP, return 2 (re-drive) */
        /* SIL CNTCRD: S_L(TEXTSP)--; S_O(TEXTSP)++;  (skip the '+') */
        const char *p = rawline;
        if (*p == '+') p++;   /* strip leading '+' */
        int clen = len - (int)(p - rawline);
        memcpy(g_io_linebuf, p, clen);
        g_io_linebuf[clen] = '\0';
        TEXTSP.ptr = g_io_linebuf;
        TEXTSP.len = clen;
        return 2;   /* RTN2 → BRANCH(FORWRD): caller re-drives STREAM */
    }

    default:      /* NEWTYP (new statement) — save as pending, return 1 */
        memcpy(g_pending_buf, rawline, len);
        g_pending_buf[len] = '\0';
        g_pending_len = len;
        g_pending_lineno = g_io_lineno;
        return 1;   /* RTN3: FORRUN sets EOSCL, FORWRD sets BRTYPE=EOSTYP */
    }
}

/* resolve_include_path — resolve 'filename' relative to base_path directory.
 * Returns malloc'd absolute path; caller frees.
 * base_path is the path of the including file (e.g. "/home/claude/corpus/foo.sno"). */
static char *resolve_include_path(const char *base_path, const char *incname) {
    /* Strip quotes if present */
    char name[1024];
    int nlen = strlen(incname);
    if (nlen >= 2 && (incname[0]=='\'' || incname[0]=='"') &&
        incname[nlen-1] == incname[0]) {
        nlen -= 2;
        memcpy(name, incname+1, nlen);
        name[nlen] = '\0';
    } else {
        memcpy(name, incname, nlen+1);
    }
    /* Strip trailing \r if present (CRLF files) */
    { int l = strlen(name); while (l > 0 && name[l-1]=='\r') name[--l]='\0'; }

    /* If already absolute, return as-is */
    if (name[0] == '/') return strdup(name);

    /* 1. Try relative to including file's directory */
    char base[4096];
    strncpy(base, base_path ? base_path : ".", sizeof(base)-1);
    base[sizeof(base)-1] = '\0';
    char *slash = strrchr(base, '/');
    if (slash) { *slash = '\0'; }
    else        { strcpy(base, "."); }

    char result[4096];
    snprintf(result, sizeof result, "%s/%s", base, name);
    if (access(result, R_OK) == 0) return strdup(result);

    /* 2. Search -I include paths in order */
    extern const char *g_include_paths[];
    extern int         g_num_include_paths;
    for (int i = 0; i < g_num_include_paths; i++) {
        snprintf(result, sizeof result, "%s/%s", g_include_paths[i], name);
        if (access(result, R_OK) == 0) return strdup(result);
        /* Case-insensitive fallback: scan directory for matching name */
        {
            DIR *d = opendir(g_include_paths[i]);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)) != NULL) {
                    if (strcasecmp(de->d_name, name) == 0) {
                        snprintf(result, sizeof result, "%s/%s",
                                 g_include_paths[i], de->d_name);
                        closedir(d);
                        return strdup(result);
                    }
                }
                closedir(d);
            }
        }
    }

    /* 3. Not found anywhere — return the relative-to-base path so caller
     *    can report a useful error message */
    snprintf(result, sizeof result, "%s/%s", base, name);
    return strdup(result);
}

/* compile_file — process one source file.
 *
 * TRUE STREAMING — mirrors CSNOBOL4 XLATNX (snobol4.c line 113).
 * No pre-joining. No linebuf. TEXTSP = one physical line at a time.
 * FORWRD calls forrun() on EOS to load continuations transparently.
 */
static void compile_file(FILE *f, const char *base_path, compile_state_t *st) {
    g_io_file   = f;
    g_io_path   = base_path;
    g_io_lineno = 0;
    g_io_eof    = 0;
    g_cst       = st;

    /* Drain any pending line left from a prior forrun() call (include re-entry) */
    if (g_pending_len > 0) {
        memcpy(g_io_linebuf, g_pending_buf, g_pending_len);
        g_io_linebuf[g_pending_len] = '\0';
        TEXTSP.ptr = g_io_linebuf;
        TEXTSP.len = g_pending_len;
        g_stmt_lineno = g_pending_lineno;
        g_pending_len = 0;
        compile_one_stmt();
    }

    char rawline[IO_LINEBUF_SZ];

    while (!st->done && !g_io_eof) {
        int len = io_read_raw(f, rawline, sizeof rawline);
        if (len == 0) break;
        g_io_lineno++;

        spec_t card = { rawline, len };
        spec_t tok;
        stream(&tok, &card, &CARDTB);
        int ctype = STYPE;

        if (ctype == CMTTYP) continue;

        if (ctype == CTLTYP) {
            const char *p = rawline + 1;
            while (*p == '-') p++;
            const char *ns = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            int nlen = (int)(p - ns);
            if (nlen == 7 && strncasecmp(ns, "INCLUDE", 7) == 0) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p) {
                    char *incpath = resolve_include_path(base_path, p);
                    FILE *incf = fopen(incpath, "r");
                    if (!incf) { char *cr=strchr(incpath,'\r'); if(cr)*cr='\0'; incf=fopen(incpath,"r"); }
                    if (incf) {
                        FILE *sv_f=g_io_file; const char *sv_p=g_io_path;
                        int sv_ln=g_io_lineno, sv_eof=g_io_eof, sv_dep=g_io_depth;
                        g_io_depth++;
                        compile_file(incf, incpath, st);
                        fclose(incf); free(incpath);
                        g_io_file=sv_f; g_io_path=sv_p;
                        g_io_lineno=sv_ln; g_io_eof=sv_eof; g_io_depth=sv_dep;
                    } else {
                        fprintf(stderr,"sno4parse: cannot open include '%s': %s\n",incpath,strerror(errno));
                        free(incpath);
                    }
                }
            }
            continue;
        }

        if (ctype == CNTTYP) continue;  /* continuation at top level — skip */

        /* NEWTYP: a real statement line */
        memcpy(g_io_linebuf, rawline, len);
        g_io_linebuf[len] = '\0';
        TEXTSP.ptr = g_io_linebuf;
        TEXTSP.len = len;
        g_stmt_lineno = g_io_lineno;
        compile_one_stmt();

        /* P2F: semicolon statement separator — SPITBOL extension.
         * ACT_STOP *consumes* the triggering char (stream.c:94: cp++; len--), so
         * after FORWRD fires EOSTYP on ';', TEXTSP.ptr is already past the ';'.
         * Remaining TEXTSP bytes are the next stmt(s) on the same physical line.
         * Special case: ';*' is an inline comment — skip rest of line.
         * CMPILE always runs LBLTB first; a leading space causes ST_STOP with
         * XSP.len==0 → no label.  Prepend a space into g_io_linebuf so the
         * semicolon-split statement is treated as indented (body-only, no label). */
        while (!st->done && BRTYPE == EOSTYP && TEXTSP.len > 0) {
            /* skip leading whitespace */
            while (TEXTSP.len > 0 && (*TEXTSP.ptr == ' ' || *TEXTSP.ptr == '\t')) {
                TEXTSP.ptr++; TEXTSP.len--;
            }
            if (TEXTSP.len == 0) break;
            if (*TEXTSP.ptr == '*') break;       /* ;* inline comment — rest is comment */
            int rem = (int)TEXTSP.len;
            const char *src = TEXTSP.ptr;
            if (rem + 2 <= (int)sizeof(g_io_linebuf)) {
                g_io_linebuf[0] = ' ';
                memcpy(g_io_linebuf + 1, src, rem);
                g_io_linebuf[rem + 1] = '\0';
                TEXTSP.ptr = g_io_linebuf;
                TEXTSP.len = rem + 1;
                compile_one_stmt();
            } else break;
        }

        /* Drain any pending lines that forrun() buffered during CMPILE */
        while (!st->done && g_pending_len > 0) {
            memcpy(g_io_linebuf, g_pending_buf, g_pending_len);
            g_io_linebuf[g_pending_len] = '\0';
            TEXTSP.ptr = g_io_linebuf;
            TEXTSP.len = g_pending_len;
            g_stmt_lineno = g_pending_lineno;
            g_pending_len = 0;
            compile_one_stmt();
        }
    }

    /* Drain final pending */
    while (!st->done && g_pending_len > 0) {
        memcpy(g_io_linebuf, g_pending_buf, g_pending_len);
        g_io_linebuf[g_pending_len] = '\0';
        TEXTSP.ptr = g_io_linebuf;
        TEXTSP.len = g_pending_len;
        g_stmt_lineno = g_pending_lineno;
        g_pending_len = 0;
        compile_one_stmt();
    }
}


int main(int argc, char **argv) {
    init_tables();
    g_trace_stream = (getenv("SNO_TRACE") != NULL);

    /* Parse flags: -Ipath or -I path adds an include search directory.
     * Remaining non-flag args are input files (processed in order).
     * If no input files given, read from stdin. */
    const char *input_files[256];
    int n_input_files = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-I", 2) == 0) {
            const char *path = argv[i] + 2;
            if (*path == '\0' && i+1 < argc) path = argv[++i]; /* -I path */
            if (g_num_include_paths < MAX_INCLUDE_PATHS)
                g_include_paths[g_num_include_paths++] = path;
        } else if (strcmp(argv[i], "--") == 0) {
            /* everything after -- is an input file */
            while (++i < argc && n_input_files < 256)
                input_files[n_input_files++] = argv[i];
            break;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "sno4parse: unknown flag '%s' (use -Idir to add include path)\n", argv[i]);
        } else {
            if (n_input_files < 256) input_files[n_input_files++] = argv[i];
        }
    }

    compile_state_t st;
    memset(&st, 0, sizeof st);

    if (n_input_files == 0) {
        /* stdin mode */
        compile_file(stdin, NULL, &st);
    } else {
        for (int i = 0; i < n_input_files && !st.done; i++) {
            FILE *f = fopen(input_files[i], "r");
            if (!f) { perror(input_files[i]); continue; }
            compile_file(f, input_files[i], &st);
            fclose(f);
        }
    }

    printf("=== %d statements ===\n", st.stmt_idx);
    return 0;
}
