/*
 * emit_js.c — SNOBOL4 → JavaScript emitter for scrip-cc
 *
 * Peer to emit_byrd_c.c / emit_jvm.c / emit_net.c / emit_wasm.c.
 *
 * Architecture:
 *   - Every SNOBOL4 program compiles to a single .js file.
 *   - Simple statements (assign, expr) emit straight-line JS.
 *   - Pattern-match statements emit a Byrd-box dispatch loop:
 *       for(;;) switch(_pc) { case (uid<<2|SIGNAL): ... }
 *     using integer _pc = (node_uid << 2) | signal, matching engine.c.
 *   - OUTPUT via _vars Proxy (set trap writes to stdout).
 *   - Labels map to JS labeled-continue blocks.
 *
 * Dispatch encoding (SJ-1 — do not re-debate):
 *   const PROCEED=0, SUCCEED=1, CONCEDE=2, RECEDE=3;
 *   _pc = (uid << 2) | signal;
 *
 * Entry point: js_emit(Program*, FILE*)
 *
 * Sprint: SJ-2  Milestone: M-SJ-A02
 * Authors: Lon Jones Cherryholmes (arch), Claude Sonnet 4.6 (impl)
 */

#include "../frontend/snobol4/scrip_cc.h"
#include "../ir/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

/* -----------------------------------------------------------------------
 * Output
 * ----------------------------------------------------------------------- */

static FILE *js_out;

static void J(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(js_out, fmt, ap);
    va_end(ap);
}

/* -----------------------------------------------------------------------
 * UID counter — single counter, shared across pattern and stmt emit
 * ----------------------------------------------------------------------- */

static int uid_ctr = 0;
static int js_next_uid(void) { return ++uid_ctr; }

/* -----------------------------------------------------------------------
 * JS-safe variable name mangling
 * Prepends 'v_', replaces non-alnum/_ with '_'.
 * ----------------------------------------------------------------------- */

/* Uppercase a SNOBOL4 variable name for _vars["NAME"] emission.
 * SNOBOL4 identifiers are case-insensitive; we normalize to uppercase. */
static const char *js_upper_var(const char *s) {
    static char buf[512];
    int i;
    for (i = 0; s[i] && i < 510; i++)
        buf[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    buf[i] = '\0';
    return buf;
}

static const char *jv(const char *s) {
    static char buf[520];
    int i = 0, j = 0;
    buf[j++] = 'v'; buf[j++] = '_';
    for (; s[i] && j < 510; i++) {
        unsigned char c = (unsigned char)s[i];
        /* Normalize to uppercase — SNOBOL4 labels are case-insensitive */
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        buf[j++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    buf[j] = '\0';
    return buf;
}

/* -----------------------------------------------------------------------
 * Escape a string for a JS string literal
 * ----------------------------------------------------------------------- */

static void js_escape_string(const char *s) {
    J("\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if      (c == '"')  J("\\\"");
        else if (c == '\\') J("\\\\");
        else if (c == '\n') J("\\n");
        else if (c == '\r') J("\\r");
        else if (c == '\t') J("\\t");
        else if (c < 0x20 || c > 0x7E) J("\\x%02x", c);
        else J("%c", c);
    }
    J("\"");
}

/* -----------------------------------------------------------------------
 * IO name check (OUTPUT, INPUT, PUNCH)
 * ----------------------------------------------------------------------- */

static int js_is_io(const char *n) {
    return n &&
        (strcmp(n,"OUTPUT")==0 || strcmp(n,"INPUT")==0 || strcmp(n,"PUNCH")==0);
}

/* -----------------------------------------------------------------------
 * Emit a JS expression from an EXPR_t node
 * ----------------------------------------------------------------------- */

static void js_emit_expr(EXPR_t *e) {
    if (!e) { J("null"); return; }
    switch (e->kind) {
    case E_NUL:
        J("null");
        break;
    case E_QLIT:
        js_escape_string(e->sval);
        break;
    case E_ILIT:
        J("%ld", e->ival);
        break;
    case E_FLIT: {
        /* Emit real literals as canonical SNOBOL4 string form.
         * "3." for whole-number reals (SPITBOL convention), "%g" for others.
         * Arithmetic functions (_add etc.) call _num() which parses these. */
        double dv = e->dval;
        if (dv == (long)dv && !isinf(dv))
            J("\"%ld.\"", (long)dv);
        else
            J("\"%g\"", dv);
        break;
    }
    case E_VAR:
        J("_vars[\"%s\"]", js_upper_var(e->sval));
        break;
    case E_KEYWORD:
        J("_kw(\"%s\")", e->sval);
        break;
    case E_INDIRECT: {
        EXPR_t *operand = (e->nchildren > 1 && e->children[1]) ? e->children[1] : e->children[0];
        /* $.var: E_CAPT_COND_ASGN(E_VAR) — indirect by name, not by value */
        if (operand->kind == E_CAPT_COND_ASGN && operand->nchildren == 1
                && operand->children[0]->kind == E_VAR) {
            J("_vars[\"%s\"]", js_upper_var(operand->children[0]->sval));
        } else {
            /* $'str' or $(expr): indirect by value */
            J("_vars[_str("); js_emit_expr(operand); J(").toUpperCase()]");
        }
        break;
    }
    case E_MNS:
        J("(-_num("); js_emit_expr(e->children[0]); J("))");
        break;
    case E_PLS:
        J("_num("); js_emit_expr(e->children[0]); J(")");
        break;
    case E_CAT:
        J("_cat(");
        for (int i = 0; i < e->nchildren; i++) {
            if (i) J(", ");
            js_emit_expr(e->children[i]);
        }
        J(")");
        break;
    case E_ADD:
        J("_add("); js_emit_expr(e->children[0]); J(", "); js_emit_expr(e->children[1]); J(")");
        break;
    case E_SUB:
        J("_sub("); js_emit_expr(e->children[0]); J(", "); js_emit_expr(e->children[1]); J(")");
        break;
    case E_MUL:
        J("_mul("); js_emit_expr(e->children[0]); J(", "); js_emit_expr(e->children[1]); J(")");
        break;
    case E_DIV:
        J("_div("); js_emit_expr(e->children[0]); J(", "); js_emit_expr(e->children[1]); J(")");
        break;
    case E_POW:
        J("_pow("); js_emit_expr(e->children[0]); J(", "); js_emit_expr(e->children[1]); J(")");
        break;
    case E_FNC:
        J("_apply(\"%s\", [", e->sval);
        for (int i = 0; i < e->nchildren; i++) {
            if (i) J(", ");
            js_emit_expr(e->children[i]);
        }
        J("])");
        break;
    case E_CAPT_COND_ASGN:
    case E_CAPT_IMMED_ASGN:
        js_emit_expr(e->children[0]);
        break;
    case E_ASSIGN: {
        const char *vname = e->children[0]->sval;
        J("(_vars[\"%s\"] = ", js_upper_var(vname));
        js_emit_expr(e->children[1]);
        J(")");
        break;
    }
    default:
        J("/* unimpl E_%d */null", (int)e->kind);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Pattern dispatch — Byrd-box (uid<<2)|signal switch
 *
 * Signals (matching engine.c):
 *   PROCEED=0  α — enter node forward
 *   SUCCEED=1  γ — node succeeded (unused as _pc target; we jump directly)
 *   CONCEDE=2  β — ask node to try again / backtrack
 *   RECEDE=3   ω — node permanently failed
 *
 * Each pattern node gets a uid.  Cases emitted:
 *   case (uid<<2|0):  α path
 *   case (uid<<2|2):  β path (backtrack)
 * γ and ω are not cases; they are _pc assignments + continue.
 * ----------------------------------------------------------------------- */

#define PROCEED 0
#define CONCEDE 2

/* Emit:  _pc = (uid<<2|sig); continue dispatch; */
static void JP(int uid, int sig) {
    J("_pc = %d; continue dispatch;\n", (uid << 2) | sig);
}

/* Emit a case label */
static void JCASE(int uid, int sig) {
    J("    case %d: /* uid%d %s */\n", (uid << 2) | sig, uid,
      sig == PROCEED ? "PROCEED" : "CONCEDE");
}

/* -----------------------------------------------------------------------
 * Forward declaration
 * ----------------------------------------------------------------------- */
static void js_emit_pat(EXPR_t *pat, int uid_γ, int uid_ω,
                        const char *subj, int uid_stmt);

/* -----------------------------------------------------------------------
 * Leaf pattern nodes — emit α and β cases for uid
 *
 * Convention:
 *   α case: try to match; on success _pc→γ, on fail _pc→ω
 *   β case: restore state, _pc→ω  (most leaves are deterministic)
 * ----------------------------------------------------------------------- */

/* LIT: match literal string s */
static void js_emit_pat_lit(const char *s, int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    int n = (int)strlen(s);
    char safe[2048]; int si = 0;
    safe[si++] = '"';
    for (int i = 0; i < n && si < 2040; i++) {
        unsigned char c = (unsigned char)s[i];
        if      (c == '"')  { safe[si++]='"'; safe[si++]='"'; } /* handled below */
        else if (c == '\\') { safe[si++]='\\'; safe[si++]='\\'; }
        else if (c == '\n') { safe[si++]='\\'; safe[si++]='n'; }
        else if (c < 0x20 || c > 0x7E) si += snprintf(safe+si, 8, "\\x%02x", c);
        else safe[si++] = (char)c;
    }
    safe[si++]='"'; safe[si]='\0';
    /* fix double-quote escaping */
    for (int i = 0; i < si; i++) if (safe[i]=='"' && i>0 && safe[i-1]!='"') { /* ok */ }

    /* α: bounds + match */
    JCASE(uid, PROCEED);
    J("        if (_cur%d + %d > _slen%d) { ", uid_stmt, n, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    if (n == 1) {
        J("        if (_subj%d[_cur%d] !== ", uid_stmt, uid_stmt);
        /* emit single char */
        J("\""); { unsigned char c=(unsigned char)s[0];
            if (c=='"') J("\\\""); else if(c=='\\') J("\\\\"); else J("%c",c); }
        J("\"");
        J(") { "); JP(uid_ω, PROCEED); J(" }\n");
    } else {
        J("        if (_subj%d.slice(_cur%d, _cur%d+%d) !== ", uid_stmt, uid_stmt, uid_stmt, n);
        /* emit JS string literal */
        J("\"");
        for (int i=0;i<n;i++){unsigned char c=(unsigned char)s[i];
            if(c=='"') J("\\\""); else if(c=='\\') J("\\\\");
            else if(c=='\n') J("\\n"); else if(c<0x20||c>0x7E) J("\\x%02x",c);
            else J("%c",c);}
        J("\"");
        J(") { "); JP(uid_ω, PROCEED); J(" }\n");
    }
    J("        _saved[%d] = _cur%d; _cur%d += %d;\n", uid, uid_stmt, uid_stmt, n);
    JP(uid_γ, PROCEED);

    /* β: restore */
    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* ARB: match 0..n chars (backtrackable) */
static void js_emit_pat_arb(int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    /* α: save, succeed with zero chars */
    JCASE(uid, PROCEED);
    J("        _saved[%d] = _cur%d;\n", uid, uid_stmt);
    JP(uid_γ, PROCEED);

    /* β: advance one char and retry */
    JCASE(uid, CONCEDE);
    J("        if (_saved[%d] >= _slen%d) { ", uid, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved%d++; _cur%d = _saved%d;\n", uid, uid_stmt, uid);
    JP(uid_γ, PROCEED);
}

/* REM: match rest of subject */
static void js_emit_pat_rem(int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        _saved[%d] = _cur%d; _cur%d = _slen%d;\n", uid, uid_stmt, uid_stmt, uid_stmt);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* LEN(n): match exactly n chars */
static void js_emit_pat_len(long n, int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d + %ld > _slen%d) { ", uid_stmt, n, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved[%d] = _cur%d; _cur%d += %ld;\n", uid, uid_stmt, uid_stmt, n);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* POS(n): assert cursor == n */
static void js_emit_pat_pos(long n, int uid, int uid_γ, int uid_ω,
                            int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d !== %ld) { ", uid_stmt, n); JP(uid_ω, PROCEED); J(" }\n");
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    JP(uid_ω, PROCEED);
}

/* RPOS(n): assert cursor == len - n */
static void js_emit_pat_rpos(long n, int uid, int uid_γ, int uid_ω,
                             int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d !== _slen%d - %ld) { ", uid_stmt, uid_stmt, n); JP(uid_ω, PROCEED); J(" }\n");
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    JP(uid_ω, PROCEED);
}

/* TAB(n): advance TO position n */
static void js_emit_pat_tab(long n, int uid, int uid_γ, int uid_ω,
                            int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d > %ld) { ", uid_stmt, n); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved[%d] = _cur%d; _cur%d = %ld;\n", uid, uid_stmt, uid_stmt, n);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* RTAB(n): advance TO len - n */
static void js_emit_pat_rtab(long n, int uid, int uid_γ, int uid_ω,
                             int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d > _slen%d - %ld) { ", uid_stmt, uid_stmt, n); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved[%d] = _cur%d; _cur%d = _slen%d - %ld;\n", uid, uid_stmt, uid_stmt, uid_stmt, n);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* ANY(cs): match one char in charset string */
static void js_emit_pat_any(const char *cs, int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d >= _slen%d) { ", uid_stmt, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        if ("); js_escape_string(cs);
    J(".indexOf(_subj%d[_cur%d]) < 0) { ", uid_stmt, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved%d = _cur%d; _cur%d++;\n", uid, uid_stmt, uid_stmt);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* NOTANY(cs): match one char NOT in charset */
static void js_emit_pat_notany(const char *cs, int uid, int uid_γ, int uid_ω,
                               const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        if (_cur%d >= _slen%d) { ", uid_stmt, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        if ("); js_escape_string(cs);
    J(".indexOf(_subj%d[_cur%d]) >= 0) { ", uid_stmt, uid_stmt); JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved%d = _cur%d; _cur%d++;\n", uid, uid_stmt, uid_stmt);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* SPAN(cs): match 1+ chars in charset (backtrackable) */
static void js_emit_pat_span(const char *cs, int uid, int uid_γ, int uid_ω,
                             const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        { var _start%d = _cur%d;\n", uid, uid_stmt);
    J("          while (_cur%d < _slen%d && ", uid_stmt, uid_stmt);
    js_escape_string(cs); J(".indexOf(_subj%d[_cur%d]) >= 0) _cur%d++;\n", uid_stmt, uid_stmt, uid_stmt);
    J("          _saved[%d] = _cur%d - _start%d;\n", uid, uid_stmt, uid);
    J("          if (_saved[%d] === 0) { ", uid); JP(uid_ω, PROCEED); J(" } }\n");
    JP(uid_γ, PROCEED);

    /* β: shrink by one */
    JCASE(uid, CONCEDE);
    J("        if (_saved[%d] <= 1) { _cur%d -= _saved[%d]; _saved[%d] = 0; ", uid, uid_stmt, uid, uid);
    JP(uid_ω, PROCEED); J(" }\n");
    J("        _saved[%d]--; _cur%d--;\n", uid, uid_stmt);
    JP(uid_γ, PROCEED);
}

/* BREAK(cs): match 0+ chars NOT in cs, stop before cs-char */
static void js_emit_pat_break(const char *cs, int uid, int uid_γ, int uid_ω,
                              const char *subj, int uid_stmt) {
    JCASE(uid, PROCEED);
    J("        _saved[%d] = _cur%d;\n", uid, uid_stmt);
    J("        while (_cur%d < _slen%d && ", uid_stmt, uid_stmt);
    js_escape_string(cs); J(".indexOf(_subj%d[_cur%d]) < 0) _cur%d++;\n", uid_stmt, uid_stmt, uid_stmt);
    J("        if (_cur%d >= _slen%d) { _cur%d = _saved%d; ", uid_stmt, uid_stmt, uid_stmt, uid);
    JP(uid_ω, PROCEED); J(" }\n");
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
    JP(uid_ω, PROCEED);
}

/* FENCE: α always succeeds, β always fails */
static void js_emit_pat_fence(int uid, int uid_γ, int uid_ω, int uid_stmt) {
    JCASE(uid, PROCEED);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    JP(uid_ω, PROCEED);
}

/* SUCCEED: always succeeds on both α and β */
static void js_emit_pat_succeed(int uid, int uid_γ, int uid_ω, int uid_stmt) {
    JCASE(uid, PROCEED);
    JP(uid_γ, PROCEED);

    JCASE(uid, CONCEDE);
    JP(uid_γ, PROCEED);
}

/* FAIL: always fails */
static void js_emit_pat_fail(int uid, int uid_γ, int uid_ω, int uid_stmt) {
    JCASE(uid, PROCEED);
    JP(uid_ω, PROCEED);

    JCASE(uid, CONCEDE);
    JP(uid_ω, PROCEED);
}

/* -----------------------------------------------------------------------
 * SEQ (concatenation): α→left_α; β→right_β; left_γ→right_α; right_ω→left_β
 * ----------------------------------------------------------------------- */
static void js_emit_pat_seq(EXPR_t *left, EXPR_t *right,
                            int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    int left_uid  = js_next_uid();
    int right_uid = js_next_uid();
    /* left_β_redirect: a uid whose PROCEED case jumps to left_uid CONCEDE.
     * Passed as right's uid_ω so right's failure backtracks left. */
    int left_β_uid = js_next_uid();

    /* α: → left_α */
    JCASE(uid, PROCEED);
    JP(left_uid, PROCEED);

    /* β: → right_β (resume right first) */
    JCASE(uid, CONCEDE);
    JP(right_uid, CONCEDE);

    /* left_β redirect: right's ω fires this → backtrack left */
    JCASE(left_β_uid, PROCEED);
    JP(left_uid, CONCEDE);
    JCASE(left_β_uid, CONCEDE);
    JP(uid_ω, PROCEED);

    /* emit left:  γ→right_α,    ω→uid_ω (left failing = whole SEQ fails) */
    js_emit_pat(left,  right_uid, uid_ω, subj, uid_stmt);

    /* emit right: γ→uid_γ, ω→left_β (right failing = backtrack left) */
    js_emit_pat(right, uid_γ, left_β_uid, subj, uid_stmt);
}

/* ALT: α→left_α; β→right_β; left_ω→right_α; right_ω→uid_ω */
static void js_emit_pat_alt(EXPR_t *left, EXPR_t *right,
                            int uid, int uid_γ, int uid_ω,
                            const char *subj, int uid_stmt) {
    int left_uid  = js_next_uid();
    int right_uid = js_next_uid();

    JCASE(uid, PROCEED);
    JP(left_uid, PROCEED);

    JCASE(uid, CONCEDE);
    JP(right_uid, CONCEDE);

    js_emit_pat(left,  uid_γ,  right_uid, subj, uid_stmt);
    js_emit_pat(right, uid_γ,  uid_ω,     subj, uid_stmt);
}

/* -----------------------------------------------------------------------
 * Conditional/immediate capture (. and $ operators)
 * ----------------------------------------------------------------------- */
static void js_emit_pat_capt(EXPR_t *pat, const char *varname, int immediate,
                             int uid, int uid_γ, int uid_ω,
                             const char *subj, int uid_stmt) {
    int child_uid = js_next_uid();
    int snap_uid  = js_next_uid();

    /* α: record cursor start, enter child */
    JCASE(uid, PROCEED);
    J("        _saved[%d] = _cur%d;\n", snap_uid, uid_stmt);
    JP(child_uid, PROCEED);

    /* β: re-enter child β */
    JCASE(uid, CONCEDE);
    JP(child_uid, CONCEDE);

    /* child: γ → capture + uid_γ; ω → uid_ω */
    /* We need child's γ to be a capture case */
    int capt_uid = js_next_uid();

    js_emit_pat(pat->children[0], capt_uid, uid_ω, subj, uid_stmt);

    /* capt case: capture and proceed */
    JCASE(capt_uid, PROCEED);
    J("        _vars[\"%s\"] = _subj%d.slice(_saved[%d], _cur%d);\n",
      js_upper_var(varname), uid_stmt, snap_uid, uid_stmt);
    JP(uid_γ, PROCEED);

    JCASE(capt_uid, CONCEDE);
    JP(uid_ω, PROCEED);

    (void)child_uid; (void)snap_uid;
}

/* -----------------------------------------------------------------------
 * Main pattern node dispatcher
 * uid_γ, uid_ω: integer node-UIDs whose PROCEED case is the γ/ω target
 * Special sentinel: uid_γ = -1 means "match succeeded" (break dispatch)
 *                  uid_ω = -2 means "match failed"   (break dispatch)
 * ----------------------------------------------------------------------- */

#define UID_MATCH_OK   (-1)
#define UID_MATCH_FAIL (-2)

static void js_emit_pat(EXPR_t *pat, int uid_γ, int uid_ω,
                        const char *subj, int uid_stmt) {
    if (!pat) {
        /* empty pattern — zero-width succeed */
        int uid = js_next_uid();
        JCASE(uid, PROCEED);
        JP(uid_γ, PROCEED);
        JCASE(uid, CONCEDE);
        JP(uid_ω, PROCEED);
        return;
    }

    int uid = js_next_uid();

    /* Check for named builtins in E_FNC */
    if (pat->kind == E_FNC && pat->sval) {
        const char *fn = pat->sval;
#define FNMATCH(x) (strcasecmp(fn,(x))==0)
        if (FNMATCH("ARB")) {
            js_emit_pat_arb(uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("REM")) {
            js_emit_pat_rem(uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("FENCE")) {
            js_emit_pat_fence(uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("SUCCEED")) {
            js_emit_pat_succeed(uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("FAIL")) {
            js_emit_pat_fail(uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("LEN") && pat->nchildren == 1 && pat->children[0]->kind == E_ILIT) {
            js_emit_pat_len(pat->children[0]->ival, uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("POS") && pat->nchildren == 1 && pat->children[0]->kind == E_ILIT) {
            js_emit_pat_pos(pat->children[0]->ival, uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("RPOS") && pat->nchildren == 1 && pat->children[0]->kind == E_ILIT) {
            js_emit_pat_rpos(pat->children[0]->ival, uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("TAB") && pat->nchildren == 1 && pat->children[0]->kind == E_ILIT) {
            js_emit_pat_tab(pat->children[0]->ival, uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("RTAB") && pat->nchildren == 1 && pat->children[0]->kind == E_ILIT) {
            js_emit_pat_rtab(pat->children[0]->ival, uid, uid_γ, uid_ω, uid_stmt);
            return;
        }
        if (FNMATCH("ANY") && pat->nchildren == 1 && pat->children[0]->kind == E_QLIT) {
            js_emit_pat_any(pat->children[0]->sval, uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("NOTANY") && pat->nchildren == 1 && pat->children[0]->kind == E_QLIT) {
            js_emit_pat_notany(pat->children[0]->sval, uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("SPAN") && pat->nchildren == 1 && pat->children[0]->kind == E_QLIT) {
            js_emit_pat_span(pat->children[0]->sval, uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        if (FNMATCH("BREAK") && pat->nchildren == 1 && pat->children[0]->kind == E_QLIT) {
            js_emit_pat_break(pat->children[0]->sval, uid, uid_γ, uid_ω, subj, uid_stmt);
            return;
        }
        /* Unknown FNC — treat as zero-width succeed stub */
        JCASE(uid, PROCEED);
        J("        /* stub FNC %s */\n", fn);
        JP(uid_γ, PROCEED);
        JCASE(uid, CONCEDE);
        JP(uid_ω, PROCEED);
        return;
    }

    switch (pat->kind) {
    case E_QLIT:
        js_emit_pat_lit(pat->sval ? pat->sval : "", uid, uid_γ, uid_ω, subj, uid_stmt);
        break;

    case E_ILIT:
    case E_VAR:
    case E_NUL: {
        /* variable or integer: evaluate to string, match as literal */
        /* emit a runtime dispatch case */
        JCASE(uid, PROCEED);
        J("        { var _pat_val%d = ", uid);
        js_emit_expr(pat);
        J(";\n");
        J("          var _pat_s%d = (_pat_val%d === null || _pat_val%d === undefined) ? '' : String(_pat_val%d);\n",
          uid, uid, uid, uid);
        J("          var _pat_n%d = _pat_s%d.length;\n", uid, uid);
        J("          if (_cur%d + _pat_n%d > _slen%d) { ", uid_stmt, uid, uid_stmt);
        JP(uid_ω, PROCEED); J(" }\n");
        J("          if (_subj%d.slice(_cur%d, _cur%d+_pat_n%d) !== _pat_s%d) { ",
          uid_stmt, uid_stmt, uid_stmt, uid, uid);
        JP(uid_ω, PROCEED); J(" }\n");
        J("          _saved[%d] = _cur%d; _cur%d += _pat_n%d; }\n", uid, uid_stmt, uid_stmt, uid);
        JP(uid_γ, PROCEED);

        JCASE(uid, CONCEDE);
        J("        _cur%d = _saved[%d];\n", uid_stmt, uid);
        JP(uid_ω, PROCEED);
        break;
    }

    case E_PAT_SEQ:
        if (pat->nchildren == 2) {
            js_emit_pat_seq(pat->children[0], pat->children[1],
                            uid, uid_γ, uid_ω, subj, uid_stmt);
        } else if (pat->nchildren == 0) {
            JCASE(uid, PROCEED); JP(uid_γ, PROCEED);
            JCASE(uid, CONCEDE); JP(uid_ω, PROCEED);
        } else {
            /* n-ary: right-fold */
            js_emit_pat(pat->children[0], uid_γ, uid_ω, subj, uid_stmt);
        }
        break;

    case E_PAT_ALT:
        if (pat->nchildren == 2) {
            js_emit_pat_alt(pat->children[0], pat->children[1],
                            uid, uid_γ, uid_ω, subj, uid_stmt);
        } else {
            JCASE(uid, PROCEED); JP(uid_γ, PROCEED);
            JCASE(uid, CONCEDE); JP(uid_ω, PROCEED);
        }
        break;

    case E_CAPT_IMMED_ASGN:
    case E_CAPT_COND_ASGN: {
        const char *vname = "OUTPUT";
        if (pat->nchildren > 1 && pat->children[1] && pat->children[1]->sval)
            vname = pat->children[1]->sval;
        js_emit_pat_capt(pat, vname, pat->kind == E_CAPT_IMMED_ASGN,
                         uid, uid_γ, uid_ω, subj, uid_stmt);
        break;
    }

    default:
        /* stub */
        JCASE(uid, PROCEED);
        J("        /* stub pat kind %d */\n", (int)pat->kind);
        JP(uid_γ, PROCEED);
        JCASE(uid, CONCEDE);
        JP(uid_ω, PROCEED);
        break;
    }
}

/* -----------------------------------------------------------------------
 * Emit goto logic for a statement's SnoGoto
 * ----------------------------------------------------------------------- */

static void js_emit_goto(SnoGoto *go, int ok_uid) {
    if (!go) return;
    if (go->uncond && go->uncond[0]) {
        J("    return goto_%s;\n", jv(go->uncond));
        return;
    }
    if (ok_uid < 0) {
        if (go->onsuccess && go->onsuccess[0])
            J("    return goto_%s;\n", jv(go->onsuccess));
        return;
    }
    /* Separate guards — mirrors emit_byrd_c.c; never combine with &&
     * (onfailure can be a bad/stale pointer even when structurally non-NULL) */
    if (go->onsuccess && go->onsuccess[0])
        J("    if (_ok%d) return goto_%s;\n", ok_uid, jv(go->onsuccess));
    if (go->onfailure && go->onfailure[0])
        J("    if (!_ok%d) return goto_%s;\n", ok_uid, jv(go->onfailure));
}

/* -----------------------------------------------------------------------
 * Collect all labels
 * ----------------------------------------------------------------------- */

static char **label_list = NULL;
static int    label_count = 0;
static int    label_cap   = 0;

static void label_register(const char *lbl) {
    if (!lbl) return;
    for (int i = 0; i < label_count; i++)
        if (strcmp(label_list[i], lbl) == 0) return;
    if (label_count >= label_cap) {
        label_cap = label_cap ? label_cap * 2 : 64;
        label_list = realloc(label_list, (size_t)label_cap * sizeof(char*));
    }
    label_list[label_count++] = strdup(lbl);
}

static void collect_labels(Program *prog) {
    for (STMT_t *s = prog->head; s; s = s->next) {
        if (s->label) label_register(s->label);
        if (s->go) {
            label_register(s->go->onsuccess);
            label_register(s->go->onfailure);
            label_register(s->go->uncond);
        }
    }
}

/* -----------------------------------------------------------------------
 * Emit one statement
 * ----------------------------------------------------------------------- */

/* Emit the pattern-match body as Byrd-box dispatch */
static void js_emit_pat_stmt(STMT_t *s) {
    int u = js_next_uid();
    /* Declare all local pattern vars — JS var is function-scoped */
    /* (JS engine hoists anyway; just emit the boilerplate) */
    J("    var _subj%d = _str(", u);
    js_emit_expr(s->subject);
    J(");\n");
    J("    var _slen%d = _subj%d.length;\n", u, u);
    J("    var _cur%d  = 0;\n", u);
    J("    var _mstart%d = 0;\n", u);
    J("    var _ok%d   = false;\n", u);
    /* _saved variables will be declared inline with var (JS hoists) */

    /* Allocate all control UIDs up front so we know arb_uid before
     * emitting any output — this lets us write var _pc = (arb_uid<<2)
     * before the dispatch loop. */
    int ok_uid    = js_next_uid();
    int fail_uid  = js_next_uid();
    int arb_uid   = js_next_uid();
    /* relay_uid: a stable entry point between arb and the pattern tree.
     * Arb jumps to relay; relay jumps to the pattern's first uid.
     * This breaks the fragile uid_ctr+1 prediction — relay is always
     * the uid immediately before the pattern allocation. */
    int relay_uid = js_next_uid();

    /* _pc must be initialised BEFORE the dispatch loop */
    J("    var _pc = %d;\n", (arb_uid << 2) | PROCEED);
    J("    var _saved = new Array(1024).fill(0); /* cursor save slots */\n");
    J("    /* Byrd-box dispatch for stmt u%d */\n", u);
    J("    dispatch: for(;;) switch(_pc) {\n");

    /* ARB scanner: α saves cursor, jumps to relay (stable), relay → pattern. */
    JCASE(arb_uid, PROCEED);
    J("        _saved[%d] = _cur%d;\n", arb_uid, u);
    JP(relay_uid, PROCEED);

    JCASE(arb_uid, CONCEDE);
    J("        if (_saved[%d] >= _slen%d) { ", arb_uid, u); JP(fail_uid, PROCEED); J(" }\n");
    J("        _saved[%d]++; _cur%d = _saved[%d]; _mstart%d = _saved[%d];\n",
      arb_uid, u, arb_uid, u, arb_uid);
    JP(relay_uid, PROCEED);

    /* arb_β_uid: a redirect that maps PROCEED → arb CONCEDE.
     * Pattern failures flow here so arb advances the scan position. */
    int arb_β_uid = js_next_uid();

    /* relay: passthrough to the pattern's first uid (uid_ctr+1 at this exact moment) */
    {
        int pat_entry = uid_ctr + 1;   /* next uid js_emit_pat allocates */
        JCASE(relay_uid, PROCEED);
        JP(pat_entry, PROCEED);
        JCASE(relay_uid, CONCEDE);
        JP(arb_β_uid, PROCEED);
    }

    /* arb_β redirect: pattern ω fires this → arb CONCEDE (advance scan) */
    JCASE(arb_β_uid, PROCEED);
    JP(arb_uid, CONCEDE);
    JCASE(arb_β_uid, CONCEDE);
    JP(fail_uid, PROCEED);

    /* Emit the actual pattern tree: γ→ok_uid, ω→arb_β_uid (→ arb CONCEDE) */
    js_emit_pat(s->pattern, ok_uid, arb_β_uid, "subj", u);

    /* ok case: match succeeded */
    JCASE(ok_uid, PROCEED);
    J("        _ok%d = true;\n", u);
    J("        break dispatch;\n");
    JCASE(ok_uid, CONCEDE);
    J("        break dispatch;\n");

    /* fail case: match failed */
    JCASE(fail_uid, PROCEED);
    J("        _ok%d = false;\n", u);
    J("        break dispatch;\n");
    JCASE(fail_uid, CONCEDE);
    J("        break dispatch;\n");

    J("    default:\n");
    J("        /* unreachable — pattern dispatch hole uid=\" + _pc + \" */\n");
    J("        break dispatch;\n");
    J("    } /* end dispatch */\n");

    /* Apply replacement if match succeeded */
    if (s->replacement && s->subject && s->subject->kind == E_VAR) {
        J("    if (_ok%d) {\n", u);
        J("        var _repl%d = _str(", u); js_emit_expr(s->replacement); J(");\n");
        J("        var _head%d = _subj%d.slice(0, _mstart%d);\n", u, u, u);
        J("        var _tail%d = _subj%d.slice(_cur%d);\n", u, u, u);
        J("        _vars[\"%s\"] = _head%d + _repl%d + _tail%d;\n",
          js_upper_var(s->subject->sval), u, u, u);
        J("    }\n");
    }

    js_emit_goto(s->go, u);
}

/* js_emit_stmt_body — emit the body of one statement (no open/close brace).
 * Called from within an open block function. Returns 1 if an explicit
 * transfer (return) was emitted, 0 if fall-through. */
static int js_emit_stmt_body(STMT_t *s);

static void js_emit_stmt(STMT_t *s) {
    J("/* line %d */\n", s->lineno);
    js_emit_stmt_body(s);
}

static int js_emit_stmt_body(STMT_t *s) {
    J("/* line %d */\n", s->lineno);

    if (!s->subject) {
        if (s->go) { js_emit_goto(s->go, -1); return 1; }
        return 0;
    }

    /* ---- pure assignment ---- */
    if (!s->pattern && s->replacement) {
        int u = js_next_uid();
        J("    var _v%d = ", u); js_emit_expr(s->replacement); J(";\n");
        J("    var _ok%d = (_v%d !== _FAIL);\n", u, u);
        J("    if (_ok%d) {\n", u);
        if (s->subject && s->subject->kind == E_VAR)
            J("        _vars[\"%s\"] = _v%d;\n", js_upper_var(s->subject->sval), u);
        else if (s->subject && s->subject->kind == E_INDIRECT) {
            EXPR_t *op = (s->subject->nchildren > 1 && s->subject->children[1])
                         ? s->subject->children[1] : s->subject->children[0];
            if (op->kind == E_CAPT_COND_ASGN && op->nchildren == 1
                    && op->children[0]->kind == E_VAR)
                J("        _vars[\"%s\"] = _v%d;\n", js_upper_var(op->children[0]->sval), u);
            else {
                J("        _vars[_str("); js_emit_expr(op); J(").toUpperCase()] = _v%d;\n", u);
            }
        }
        J("    }\n");
        if (s->go) { js_emit_goto(s->go, u); return 1; }
        return 0;
    }

    /* ---- null assign ---- */
    if (!s->pattern && !s->replacement && s->has_eq) {
        if (s->subject && s->subject->kind == E_VAR)
            J("    _vars[\"%s\"] = null;\n", js_upper_var(s->subject->sval));
        if (s->go) { js_emit_goto(s->go, -1); return 1; }
        return 0;
    }

    /* ---- pattern match ---- */
    if (s->pattern) {
        js_emit_pat_stmt(s);
        /* pat_stmt emits its own goto internally; check if one was emitted */
        return (s->go != NULL) ? 1 : 0;
    }

    /* ---- expression evaluation only ---- */
    {
        int u = js_next_uid();
        J("    var _v%d = ", u); js_emit_expr(s->subject); J(";\n");
        J("    var _ok%d = (_v%d !== _FAIL);\n", u, u);
        if (s->go) { js_emit_goto(s->go, u); return 1; }
        return 0;
    }
}

/* -----------------------------------------------------------------------
 * Fix up _pc initialization for pattern stmts
 * We need to set _pc BEFORE the dispatch loop starts.
 * js_emit_pat_stmt emits a comment with the needed initial _pc value,
 * then emits the dispatch loop.  We do a post-pass to fix up.
 *
 * Simpler approach: collect the first arb_uid and set it before dispatch.
 * We implement this by tracking the "first uid emitted in the stmt" —
 * which is arb_uid — and setting _pc there.
 *
 * Actually: the _pc = 0 initializer is wrong; we need _pc = (arb_uid<<2).
 * Since arb_uid = uid_ctr at the time we start, we can compute it.
 * We use a two-pass approach: save uid_ctr before, then write the real init.
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * Main entry point
 * ----------------------------------------------------------------------- */

void js_emit(Program *prog, FILE *f) {
    js_out   = f;
    uid_ctr  = 0;
    label_count = 0;

    collect_labels(prog);
    /* Ensure START and END are always in the forward-decl set */
    label_register("START");
    label_register("END");

    /* preamble */
    J("'use strict';\n");
    J("const _rt = require(process.env.SNO_RUNTIME || __dirname + '/sno_runtime.js');\n");
    J("const { _vars, _FAIL, _str, _num, _cat, _add, _sub, _mul, _div, _pow,\n");
    J("        _apply, _kw } = _rt;\n");
    J("\n");

    /* Forward-declare all block function vars using jv() naming throughout.
     * Block-grouping model (mirrors emit_byrd_c.c):
     *   goto_vX = function() { <stmts>; return goto_vY; }
     * Trampoline: var _pc = goto_vSTART; while(_pc) _pc = _pc();
     * All label refs go through jv() — START -> v_START, END -> v_END, etc.
     */
    for (int i = 0; i < label_count; i++)
        J("var goto_%s;\n", jv(label_list[i]));
    /* START and END always registered above — loop covers them */
    J("\n");

    /* Emit block functions.
     * A labeled stmt closes the current block and opens a new one.
     * Unlabeled stmts accumulate in the current block.
     * Explicit transfer (return goto_X) terminates that block body. */
    J("/* --- block functions --- */\n");

    int block_open = 0;
    int end_emitted = 0;  /* set when is_end block is written */
    /* syn_label holds a malloc'd synthetic label name when we generate one,
     * or NULL when the current block was opened by a real SNOBOL4 label.
     * Freed when a new block opens. */
    char *syn_label = NULL;
    int   at_start  = 1;   /* first block gets goto_v_START */

    for (STMT_t *s = prog->head; s; s = s->next) {
        /* Labeled stmt: close current block with fall-through to this label */
        if (s->label && block_open) {
            J("    return goto_%s;\n}\n\n", jv(s->label));
            block_open = 0;
            free(syn_label); syn_label = NULL;
        }

        /* Open new block */
        if (!block_open) {
            if (s->label) {
                free(syn_label); syn_label = NULL;
                J("goto_%s = function() {\n", jv(s->label));
            } else if (at_start) {
                J("goto_%s = function() {\n", jv("START"));
            } else if (syn_label) {
                /* Use the pre-generated synthetic label from the previous
                 * transferred block's look-ahead. */
                J("var goto_%s;\n", jv(syn_label));
                J("goto_%s = function() {\n", jv(syn_label));
                /* keep syn_label alive; will be freed on next label-open or block-open */
            } else {
                /* Unlabeled continuation without a pre-generated label —
                 * shouldn't normally happen, but generate one defensively. */
                int cid = js_next_uid();
                syn_label = malloc(32);
                snprintf(syn_label, 32, "_c%d", cid);
                J("var goto_%s;\n", jv(syn_label));
                J("goto_%s = function() {\n", jv(syn_label));
            }
            at_start   = 0;
            block_open = 1;
        }

        if (s->is_end) {
            J("    return null;\n}\n\n");
            block_open = 0;
            end_emitted = 1;
            break;
        }

        /* Emit stmt body; returns 1 if an explicit transfer was emitted */
        int transferred = js_emit_stmt_body(s);
        if (transferred) {
            /* If the next stmt is an unlabeled non-END continuation, the
             * success path of this block falls through to it.  Pre-generate
             * a synthetic label now and emit a return before closing, so the
             * trampoline can find the next block. */
            STMT_t *nx = s->next;
            if (nx && !nx->label && !nx->is_end) {
                int cid = js_next_uid();
                free(syn_label);
                syn_label = malloc(32);
                snprintf(syn_label, 32, "_c%d", cid);
                J("    return goto_%s;\n}\n\n", jv(syn_label));
            } else if (nx && nx->label && !nx->is_end && s->go &&
                       !s->go->uncond &&
                       !(s->go->onsuccess && s->go->onsuccess[0] &&
                         s->go->onfailure && s->go->onfailure[0])) {
                /* Conditional-only goto (:S or :F but not both) — the
                 * untaken branch must fall through to the next label. */
                J("    return goto_%s;\n}\n\n", jv(nx->label));
            } else if (nx && nx->is_end && s->go &&
                       !s->go->uncond &&
                       !(s->go->onsuccess && s->go->onsuccess[0] &&
                         s->go->onfailure && s->go->onfailure[0])) {
                J("    return goto_%s;\n}\n\n", jv("END"));
            } else {
                J("}\n\n");
            }
            block_open = 0;
        }
    }

    if (block_open) {
        J("    return null;\n}\n\n");
    }

    /* END sentinel — only needed if program had no explicit END stmt */
    if (!end_emitted)
        J("goto_%s = function() { return null; };\n\n", jv("END"));

    /* Stubs for undefined goto targets */
    J("/* --- undefined label stubs --- */\n");
    for (int i = 0; i < label_count; i++) {
        const char *lbl = label_list[i];
        int defined = 0;
        for (STMT_t *s2 = prog->head; s2; s2 = s2->next)
            if (s2->label && strcasecmp(s2->label, lbl)==0) { defined=1; break; }
        if (!defined)
            J("if (!goto_%s) goto_%s = function() { return null; };\n",
              jv(lbl), jv(lbl));
    }
    J("\n");

    /* Trampoline kickoff */
    J("/* --- run --- */\n");
    /* Plain loop — no IIFE needed; CJS module wrapper already provides scope.
     * IIFE form crashes in Node v22 strict mode: (intermediate value)(...) is not a function */
    J("{ var _pc = goto_%s; while(_pc) _pc = _pc(); }\n",
      jv("START"));

    /* free */
    free(syn_label);
    for (int i = 0; i < label_count; i++) free(label_list[i]);
    free(label_list);
    label_list = NULL; label_count = 0; label_cap = 0;
}
