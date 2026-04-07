'use strict';
/*
 * sno-interp.js — SNOBOL4 tree-walk interpreter (pure JS, no IPC)
 *
 * Architecture:
 *   Lexer   — single-pass, character-stream, line-aware, emits field-boundary tokens
 *   Parser  — recursive-descent, pure token-driven, builds EXPR_t/STMT_t IR
 *   IR      — identical shape to scrip-cc: { kind, sval, ival, dval, children }
 *   Exec    — tree-walk over Program linked list, sno_engine.js for patterns
 *
 * Token additions vs lex.h (for single-pass line handling):
 *   T_NEWLINE   — logical line end (after continuation folding)
 *   T_GOTO_SEP  — ':' separating body from goto field (depth-0)
 *   T_STMT_SEP  — ';' multi-statement separator (depth-0)
 *
 * No textual preprocessing — all line-structure logic is in the Lexer.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 * SPRINT:  SJ-6
 */

const path = require('path');
const fs   = require('fs');

/* ── Load runtime and engine ─────────────────────────────────────────────── */
const RUNTIME_DIR = __dirname;
const sno_rt  = require(path.join(RUNTIME_DIR, 'sno_runtime.js'));
const sno_eng = require(path.join(RUNTIME_DIR, 'sno_engine.js'));

const { sno_search, sno_match,
        PAT_lit, PAT_alt, PAT_seq, PAT_any, PAT_notany,
        PAT_span, PAT_break, PAT_arb, PAT_rem,
        PAT_len, PAT_pos, PAT_rpos, PAT_tab, PAT_rtab,
        PAT_fence, PAT_succeed, PAT_fail, PAT_pred, PAT_abort, PAT_bal,
        PAT_arbno, PAT_capt_imm, PAT_capt_cond, PAT_capt_cursor,
        PAT_deferred,
        _set_vars_hook, _set_dcall_hook } = sno_eng;

const { _FAIL, _is_fail, _str, _num, _add, _sub, _mul, _div, _pow,
        _vars, _kw, _is_int, _is_real, _real_result } = sno_rt;

_set_vars_hook((v, text) => { _vars[v] = text; });
_set_dcall_hook((fname, exprs, text) => {
    /* Deferred capture target *f(text) — synthesise a literal arg and call */
    const litExpr = {kind: E_QLIT, sval: text, children: []};
    _call(fname, exprs.length ? exprs : [litExpr]);
});

/* Initialise SNOBOL4 keyword variables so &ALPHABET etc. resolve correctly */
{
  const KW = {
    '&ALPHABET': (()=>{ let s=''; for(let i=0;i<256;i++) s+=String.fromCharCode(i); return s; })(),
    '&ANCHOR':0, '&TRIM':0, '&MAXLNGTH':5000, '&STCOUNT':0, '&STLIMIT':-1,
    '&RTNTYPE':'', '&ERRTEXT':'', '&ERRLIMIT':0, '&FNNAME':'', '&LASTNO':0,
    '&UCASE':(()=>{ let s=''; for(let i=65;i<=90;i++) s+=String.fromCharCode(i); return s; })(),
    '&LCASE':(()=>{ let s=''; for(let i=97;i<=122;i++) s+=String.fromCharCode(i); return s; })(),
  };
  for(const [k,v] of Object.entries(KW)) _vars[k]=v;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EKind string constants — identical to C enum names
 * ═══════════════════════════════════════════════════════════════════════════ */
const E_QLIT='E_QLIT', E_ILIT='E_ILIT', E_FLIT='E_FLIT', E_NUL='E_NUL';
const E_VAR='E_VAR', E_KEYWORD='E_KEYWORD', E_INDIRECT='E_INDIRECT', E_DEFER='E_DEFER';
const E_INTERROGATE='E_INTERROGATE', E_NAME='E_NAME';
const E_MNS='E_MNS', E_PLS='E_PLS';
const E_ADD='E_ADD', E_SUB='E_SUB', E_MUL='E_MUL', E_DIV='E_DIV', E_MOD='E_MOD', E_POW='E_POW';
const E_SEQ='E_SEQ', E_CAT='E_CAT', E_ALT='E_ALT', E_OPSYN='E_OPSYN';
const E_ARB='E_ARB', E_ARBNO='E_ARBNO';
const E_POS='E_POS', E_RPOS='E_RPOS';
const E_ANY='E_ANY', E_NOTANY='E_NOTANY';
const E_SPAN='E_SPAN', E_BREAK='E_BREAK', E_BREAKX='E_BREAKX';
const E_LEN='E_LEN', E_TAB='E_TAB', E_RTAB='E_RTAB', E_REM='E_REM';
const E_FAIL='E_FAIL', E_SUCCEED='E_SUCCEED', E_FENCE='E_FENCE', E_ABORT='E_ABORT', E_BAL='E_BAL';
const E_CAPT_COND_ASGN='E_CAPT_COND_ASGN', E_CAPT_IMMED_ASGN='E_CAPT_IMMED_ASGN';
const E_CAPT_CURSOR='E_CAPT_CURSOR';
const E_FNC='E_FNC', E_IDX='E_IDX', E_ASSIGN='E_ASSIGN', E_SCAN='E_SCAN';

/* ── PAT_KINDS: E_* kinds that are pattern-only (never pure string values) ─ */
/* Must be declared before Parser class and _expr_is_pat — both reference it.  */
const PAT_KINDS = new Set([
  E_ARB, E_ARBNO, E_CAPT_COND_ASGN, E_CAPT_IMMED_ASGN, E_CAPT_CURSOR, E_DEFER,
  E_LEN, E_TAB, E_RTAB, E_POS, E_RPOS,
  E_ANY, E_NOTANY, E_SPAN, E_BREAK, E_BREAKX, E_REM,
  E_FAIL, E_SUCCEED, E_FENCE, E_ABORT, E_BAL,
]);
/* E_FNC names that always return a pattern — used by _expr_is_pat/_is_pat     */
const PAT_FNC_NAMES = new Set([
  'ANY','NOTANY','SPAN','BREAK','BREAKX','LEN','POS','RPOS','TAB','RTAB',
  'ARB','ARBNO','REM','FAIL','SUCCEED','FENCE','ABORT','BAL',
]);
/* E_FNC names that are predicates (return '' on success, FAIL on failure).
 * In "X = guard(args) repl" form, these trigger guard_assign splitting. */
const GUARD_FNC_NAMES = new Set([
  'EQ','NE','GT','LT','GE','LE','LGT','LLT','LGE','LLE','LEQ','LNE',
  'IDENT','DIFFER','APPLY',
]);

/* ═══════════════════════════════════════════════════════════════════════════
 * IR node constructors — same fields as C EXPR_t / STMT_t
 * ═══════════════════════════════════════════════════════════════════════════ */
function expr_new(kind)          { return { kind, sval:null, ival:0, dval:0.0, children:[] }; }
function expr_leaf(kind, sval)   { const e=expr_new(kind); e.sval=sval; return e; }
function expr_ival(kind, ival)   { const e=expr_new(kind); e.ival=ival; return e; }
function expr_dval(kind, dval)   { const e=expr_new(kind); e.dval=dval; return e; }
function expr_unary(kind, child) { const e=expr_new(kind); e.children=[child]; return e; }
function expr_binary(kind, l, r) { const e=expr_new(kind); e.children=[l,r]; return e; }
function stmt_new()              { return { label:null, subject:null, pattern:null,
                                            replacement:null, has_eq:false, guard_assign:false,
                                            go:null, is_end:false, lineno:0, next:null }; }

/* ═══════════════════════════════════════════════════════════════════════════
 * Token kind constants
 * ═══════════════════════════════════════════════════════════════════════════ */
const T_IDENT=0,T_INT=1,T_REAL=2,T_STR=3,T_KEYWORD=4,T_WS=5;
const T_PLUS=6,T_MINUS=7,T_STAR=8,T_SLASH=9,T_PCT=10;
const T_CARET=11,T_BANG=12,T_STARSTAR=13;
const T_AMP=14,T_AT=15,T_TILDE=16,T_DOLLAR=17,T_DOT=18;
const T_HASH=19,T_PIPE=20,T_EQ=21,T_QMARK=22;
const T_COMMA=23,T_LPAREN=24,T_RPAREN=25;
const T_LBRACKET=26,T_RBRACKET=27,T_LANGLE=28,T_RANGLE=29;
const T_COLON=30,T_END=31,T_EOF=32,T_ERR=33;
const T_NEWLINE=34;   /* logical line boundary — emitted by single-pass lexer */
const T_GOTO_SEP=35;  /* ':' at depth-0 — body/goto separator                */
const T_STMT_SEP=36;  /* ';' at depth-0 — multi-statement separator           */
/* Binary operator tokens (WS op WS) — mirroring snobol4.l W-op-W rules */
const T_BIN_PLUS=37, T_BIN_MINUS=38, T_BIN_STAR=39, T_BIN_SLASH=40;
const T_BIN_PCT=41,  T_BIN_CARET=42, T_BIN_BANG=43, T_BIN_STARSTAR=44;
const T_BIN_AT=45,   T_BIN_TILDE=46, T_BIN_DOLLAR=47, T_BIN_DOT=48;
const T_BIN_HASH=49, T_BIN_PIPE=50,  T_BIN_EQ=51,  T_BIN_QMARK=52;
const T_BIN_AMP=53;
/* T_CONCAT: whitespace between atoms (juxtaposition) */
const T_CONCAT=54;

function tok(kind, sval, ival, dval, lineno) {
  return { kind, sval:(sval!=null?sval:null), ival:ival||0, dval:dval||0.0, lineno };
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LEXER — single-pass, character-stream
 *
 * All source-structure logic (continuation lines, comment lines, control
 * lines, semicolons, goto colon) is handled here — no preprocessing pass.
 *
 * Line types (col-0 character):
 *   '*' '!' '#' '|'  → comment: skip entire physical line, no tokens
 *   '-'              → control line: handle -INCLUDE inline, skip others
 *   '+' '.'          → continuation: fold next line into current logical line
 *   any other        → ordinary line: emit label (if non-space col-0), then body
 *
 * Special tokens emitted:
 *   T_NEWLINE   after each logical line (including after continuation folding)
 *   T_GOTO_SEP  for ':' at paren depth 0, outside string
 *   T_STMT_SEP  for ';' at paren depth 0, outside string (non-comment)
 * ═══════════════════════════════════════════════════════════════════════════ */
class Lexer {
  constructor(src, filename) {
    this.src      = src;
    this.filename = filename || '<stdin>';
    this.pos      = 0;
    this.lineno   = 1;
    this.errors   = 0;
    /* Line-position tracking */
    this._bol     = true;   /* beginning of (physical) line */
    /* Paren depth for colon/semicolon discrimination */
    this._depth   = 0;
    /* One-token lookahead */
    this._peeked  = null;
    /* Track whether last emitted token was WS — binary vs unary discrimination */
    this._prev_ws = true;   /* true at body start = unary context */
  }

  _c()  { return this.pos < this.src.length ? this.src[this.pos]   : '\0'; }
  _c1() { return this.pos+1 < this.src.length ? this.src[this.pos+1] : '\0'; }

  _adv() {
    if (this.pos < this.src.length) {
      if (this.src[this.pos] === '\n') { this.lineno++; this._bol = true; }
      else this._bol = false;
      this.pos++;
    }
  }

  /* Skip to end of physical line (comment / unsupported control line) */
  _skip_to_eol() {
    while (this.pos < this.src.length && this.src[this.pos] !== '\n') this.pos++;
    /* leave '\n' for _raw_next to handle as T_NEWLINE */
    this._prev_ws = true;  /* comment line ends → unary context at BOL */
  }

  /* Read rest of physical line as string, consuming the newline */
  _read_line() {
    let s = '';
    while (this.pos < this.src.length && this.src[this.pos] !== '\n')
      { s += this.src[this.pos]; this.pos++; }
    if (this.pos < this.src.length) { this.pos++; this.lineno++; this._bol = true; }
    return s.trim();
  }

  /* Inject included file text at current position */
  _inject(fname) {
    const extra = (process.env.SNO_LIB || '').split(':').filter(Boolean);
    /* Also add parent of each SNO_LIB dir so relative paths like 'lib/x.sno' resolve */
    const extraParents = extra.map(d => path.dirname(d));
    const dirs = [path.dirname(path.resolve(this.filename)), '.', ...extra, ...extraParents];
    for (const d of dirs) {
      const full = path.join(d, fname);
      if (fs.existsSync(full)) {
        const text = fs.readFileSync(full, 'utf8');
        if (text && !text.endsWith('\n')) text += '\n';
        this.src = this.src.slice(0, this.pos) + text + this.src.slice(this.pos);
        this._bol = true;
        return;
      }
    }
    process.stderr.write(`${this.filename}: cannot open include '${fname}'\n`);
    this.errors++;
  }

  /* Lex a quoted string; opening quote already consumed */
  _lex_str(q, ln) {
    let s = '';
    while (this.pos < this.src.length && this.src[this.pos] !== q && this.src[this.pos] !== '\n')
      { s += this.src[this.pos]; this._adv(); }
    if (this.pos < this.src.length && this.src[this.pos] === q) this._adv();
    return tok(T_STR, s, 0, 0, ln);
  }

  _raw_next() {
    const src = this.src;
    const len = src.length;

    top:
    while (this.pos < len) {
      const ln = this.lineno;
      const c  = src[this.pos];

      /* ── BOL (beginning-of-line) dispatch ──────────────────────────── */
      if (this._bol) {
        this._bol = false;

        /* Comment lines — skip, no tokens */
        if (c === '*' || c === '!' || c === '#' || c === '|') {
          this._skip_to_eol();
          continue;
        }

        /* Control line */
        if (c === '-') {
          this.pos++; /* consume '-' */
          const rest = this._read_line();
          if (rest.toUpperCase().startsWith('INCLUDE')) {
            let fname = rest.slice(7).trim();
            if (fname[0] === "'" || fname[0] === '"') fname = fname.slice(1,-1);
            this._inject(fname);
            /* _inject replaces this.src — the local 'src' cache is stale.
             * Restart _raw_next so the new src string is captured fresh. */
            return this._raw_next();
          }
          /* other control lines silently dropped */
          continue;
        }

        /* Continuation line — fold: consume '+'/'.', skip leading ws, continue */
        if (c === '+' || c === '.') {
          this.pos++; /* consume +/. */
          while (this.pos < len && (src[this.pos] === ' ' || src[this.pos] === '\t')) this.pos++;
          continue; /* resume tokenizing same logical line */
        }

        /* Ordinary line — fall through to normal tokenization below */
        /* (BOL flag already cleared above) */
      }

      /* ── Newline: end of logical line ──────────────────────────────── */
      if (c === '\n') {
        this._adv(); /* sets _bol=true, increments lineno */
        this._depth = 0;
        /* Peek: if the next non-empty physical line starts with '+' or '.' (continuation),
         * suppress this T_NEWLINE so the continuation folds into the current logical line. */
        let peek = this.pos;
        while (peek < len && src[peek] === '\n') peek++; /* skip blank lines */
        if (peek < len && (src[peek] === '+' || src[peek] === '.')) {
          /* Consume the continuation marker and its leading whitespace now.
           * Leave one virtual space by not advancing past the first space — the
           * whitespace token handler will emit T_WS, connecting the two lines
           * so _e4 juxtaposition can continue collecting items across the fold. */
          this.pos = peek + 1; /* skip '+' or '.' */
          /* skip all-but-one space so T_WS is emitted for juxtaposition */
          let wsCount = 0;
          while (this.pos < len && (src[this.pos] === ' ' || src[this.pos] === '\t')) { this.pos++; wsCount++; }
          /* back up one char to leave a space token — only if whitespace was present */
          if (wsCount > 0) this.pos--;
          this._bol = false;
          continue; /* fold: resume tokenizing same logical line */
        }
        return tok(T_NEWLINE, null, 0, 0, ln);
      }
      /* reset unary context at start of each logical line */
      if (this._bol) this._prev_ws = true;

      /* ── Whitespace ──────────────────────────────────────────────── */
      /* Mirrors beauty.sno: $op = *White op *White.
       * After consuming all whitespace, if the next char is a binary-eligible op
       * AND followed by whitespace/EOL, emit T_BIN_* directly (White consumed into op).
       * Otherwise emit T_WS for concatenation context (_e4). */
      if (c === ' ' || c === '\t') {
        while (this.pos < len && (src[this.pos] === ' ' || src[this.pos] === '\t'))
          this.pos++;
        if (this.pos < len) {
          const op = src[this.pos];
          /* ** check first */
          if (op === '*' && this.pos+1 < len && src[this.pos+1] === '*') {
            const a2 = this.pos+2 < len ? src[this.pos+2] : '\0';
            if (a2 === ' ' || a2 === '\t' || a2 === '\n' || a2 === '\0') {
              this.pos += 2; /* consume ** */
              while (this.pos < len && (src[this.pos] === ' ' || src[this.pos] === '\t'))
                this.pos++;
              this._prev_ws = false;
              return tok(T_BIN_STARSTAR, null, 0, 0, ln);
            }
          }
          /* Single-char binary ops: must be followed by WS or EOL */
          const BIN_MAP = {
            '+':T_BIN_PLUS, '-':T_BIN_MINUS, '*':T_BIN_STAR, '/':T_BIN_SLASH,
            '%':T_BIN_PCT,  '^':T_BIN_CARET, '!':T_BIN_BANG, '@':T_BIN_AT,
            '~':T_BIN_TILDE,'$':T_BIN_DOLLAR,'.':T_BIN_DOT,  '#':T_BIN_HASH,
            '|':T_BIN_PIPE, '=':T_BIN_EQ,    '?':T_BIN_QMARK,'&':T_BIN_AMP
          };
          const bk = BIN_MAP[op];
          if (bk !== undefined) {
            const after = this.pos+1 < len ? src[this.pos+1] : '\0';
            if (after === ' ' || after === '\t' || after === '\n' || after === '\0') {
              this.pos++; /* consume op char */
              /* consume trailing White — mirrors *White op *White */
              while (this.pos < len && (src[this.pos] === ' ' || src[this.pos] === '\t'))
                this.pos++;
              this._prev_ws = false;
              return tok(bk, null, 0, 0, ln);
            }
          }
        }
        this._prev_ws = true;
        return tok(T_WS, null, 0, 0, ln);
      }

      /* ── String literals ─────────────────────────────────────────── */
      if (c === "'") { this._adv(); this._prev_ws=false; return this._lex_str("'", ln); }
      if (c === '"') { this._adv(); this._prev_ws=false; return this._lex_str('"', ln); }

      /* ── Number ──────────────────────────────────────────────────── */
      if (c >= '0' && c <= '9') {
        let s = '';
        while (this.pos < len && src[this.pos] >= '0' && src[this.pos] <= '9')
          { s += src[this.pos]; this._adv(); }
        /* real: digit . digit */
        if (this.pos < len && src[this.pos] === '.' &&
            this.pos+1 < len && src[this.pos+1] >= '0' && src[this.pos+1] <= '9') {
          s += '.'; this._adv();
          while (this.pos < len && src[this.pos] >= '0' && src[this.pos] <= '9')
            { s += src[this.pos]; this._adv(); }
          if (this.pos < len && (src[this.pos]==='e'||src[this.pos]==='E')) {
            s += src[this.pos]; this._adv();
            if (this.pos < len && (src[this.pos]==='+'||src[this.pos]==='-'))
              { s += src[this.pos]; this._adv(); }
            while (this.pos < len && src[this.pos] >= '0' && src[this.pos] <= '9')
              { s += src[this.pos]; this._adv(); }
          }
          return tok(T_REAL, s, 0, parseFloat(s), ln);  /* sval=raw string preserves float marker */
        }
        this._prev_ws = false;
        return tok(T_INT, null, parseInt(s,10), 0, ln);
      }

      /* ── &keyword ────────────────────────────────────────────────── */
      if (c === '&') {
        this._adv();
        if (this.pos < len && /\w/.test(src[this.pos])) {
          let s = '';
          while (this.pos < len && /\w/.test(src[this.pos])) { s += src[this.pos]; this._adv(); }
          this._prev_ws = false;
          return tok(T_KEYWORD, s, 0, 0, ln);
        }
        /* bare & — check binary/unary */
        const pw3=this._prev_ws; this._prev_ws=false;
        const nc4=this.pos<len?src[this.pos]:'\0';
        const nw3=(nc4===' '||nc4==='\t'||nc4==='\n'||nc4==='\0');
        return tok((pw3&&nw3)?T_BIN_AMP:T_AMP, null, 0, 0, ln);
      }

      /* ── Identifier ──────────────────────────────────────────────── */
      if (/[a-zA-Z_]/.test(c) || c.charCodeAt(0) >= 0x80) {
        let s = '';
        while (this.pos < len) {
          const cc = src[this.pos];
          if (/[\w.]/.test(cc) || cc.charCodeAt(0) >= 0x80) { s += cc; this._adv(); }
          else break;
        }
        if (s.toUpperCase() === 'END') { this._prev_ws=false; return tok(T_END, s, 0, 0, ln); }
        this._prev_ws = false;
        return tok(T_IDENT, s, 0, 0, ln);
      }

      /* ── ** before * ─────────────────────────────────────────────── */
      if (c === '*' && this._c1() === '*') {
        this._adv(); this._adv();
        const pw2=this._prev_ws; this._prev_ws=false;
        const nc3=this.pos<len?src[this.pos]:'\0';
        const nw2=(nc3===' '||nc3==='\t'||nc3==='\n'||nc3==='\0');
        return tok((pw2&&nw2)?T_BIN_STARSTAR:T_STARSTAR,null,0,0,ln);
      }

      /* ── Semicolon — stmt sep or inline comment ──────────────────── */
      if (c === ';') {
        this._adv();
        const nc = this.pos < len ? src[this.pos] : '\0';
        /* ';*' or end-of-line → inline comment → treat as logical line end */
        if (nc === '*' || nc === '\n' || nc === '\0') {
          this._skip_to_eol();
          /* The '\n' will be consumed next iteration → T_NEWLINE */
          continue;
        }
        return tok(T_STMT_SEP, null, 0, 0, ln);
      }

      /* ── Colon at depth-0: body/goto separator ───────────────────── */
      if (c === ':' && this._depth === 0) {
        this._adv();
        return tok(T_GOTO_SEP, null, 0, 0, ln);
      }

      /* ── Single-char operators ───────────────────────────────────── */
      /* Mirrors snobol4.l: WS op WS → binary token; bare op → unary token */
      this._adv();
      const pw = this._prev_ws; /* was previous token whitespace? */
      this._prev_ws = false;    /* operators are not whitespace */
      /* peek ahead: is next char whitespace or end-of-body? */
      const nc2 = this.pos < len ? src[this.pos] : '\0';
      const nw = (nc2 === ' ' || nc2 === '\t' || nc2 === '\n' || nc2 === '\0');
      const bin = pw && nw; /* binary: preceded AND followed by whitespace */
      switch (c) {
        case '+': return tok(bin?T_BIN_PLUS   :T_PLUS,    null,0,0,ln);
        case '-': return tok(bin?T_BIN_MINUS  :T_MINUS,   null,0,0,ln);
        case '*': return tok(bin?T_BIN_STAR   :T_STAR,    null,0,0,ln);
        case '/': return tok(bin?T_BIN_SLASH  :T_SLASH,   null,0,0,ln);
        case '%': return tok(bin?T_BIN_PCT    :T_PCT,     null,0,0,ln);
        case '^': return tok(bin?T_BIN_CARET  :T_CARET,   null,0,0,ln);
        case '!': return tok(bin?T_BIN_BANG   :T_BANG,    null,0,0,ln);
        case '@': return tok(bin?T_BIN_AT     :T_AT,      null,0,0,ln);
        case '~': return tok(bin?T_BIN_TILDE  :T_TILDE,   null,0,0,ln);
        case '$': return tok(bin?T_BIN_DOLLAR :T_DOLLAR,  null,0,0,ln);
        case '.': return tok(bin?T_BIN_DOT    :T_DOT,     null,0,0,ln);
        case '#': return tok(bin?T_BIN_HASH   :T_HASH,    null,0,0,ln);
        case '|': return tok(bin?T_BIN_PIPE   :T_PIPE,    null,0,0,ln);
        case '=': return tok(bin?T_BIN_EQ     :T_EQ,      null,0,0,ln);
        case '?': return tok(bin?T_BIN_QMARK  :T_QMARK,   null,0,0,ln);
        case ',': this._prev_ws=true; return tok(T_COMMA,   null,0,0,ln);
        case '(': this._depth++; this._prev_ws=true; return tok(T_LPAREN,  null,0,0,ln);
        case ')': if(this._depth>0)this._depth--; return tok(T_RPAREN,  null,0,0,ln);
        case '[': this._prev_ws=true; return tok(T_LBRACKET,null,0,0,ln);
        case ']': return tok(T_RBRACKET,null,0,0,ln);
        case '<': this._prev_ws=true; return tok(T_LANGLE,  null,0,0,ln);
        case '>': return tok(T_RANGLE,  null,0,0,ln);
        case ':': return tok(T_COLON,   null,0,0,ln); /* depth>0 colon */
        default:
          this.errors++;
          process.stderr.write(`${this.filename}:${ln}: unexpected char '${c}'\n`);
          return tok(T_ERR, null, 0, 0, ln);
      }
    }
    return tok(T_EOF, null, 0, 0, this.lineno);
  }

  next()    { if(this._peeked!==null){const t=this._peeked;this._peeked=null;return t;} return this._raw_next(); }
  peek()    { if(this._peeked===null) this._peeked=this._raw_next(); return this._peeked; }
  at_end()  { return this.peek().kind===T_EOF; }

  /* Speculative lookahead checkpoint */
  mark()    { return {pos:this.pos,peeked:this._peeked,lineno:this.lineno,
                      _bol:this._bol,_depth:this._depth,_prev_ws:this._prev_ws}; }
  restore(m){ this.pos=m.pos;this._peeked=m.peeked;this.lineno=m.lineno;
              this._bol=m._bol;this._depth=m._depth;this._prev_ws=m._prev_ws; }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PARSER — recursive-descent, token-driven
 * Direct port of parse.c grammar levels, adapted for streaming token API.
 * ═══════════════════════════════════════════════════════════════════════════ */
class Parser {
  constructor(lx) { this.lx = lx; }

  _err(msg) {
    const t = this.lx.peek();
    process.stderr.write(`${this.lx.filename}:${t.lineno}: error: ${msg}\n`);
    this.lx.errors++;
  }

  skip_ws() { while(this.lx.peek().kind===T_WS) this.lx.next(); }
  eat_ws()  { if(this.lx.peek().kind===T_WS){this.lx.next();this.skip_ws();return true;} return false; }

  _at_end() { const k=this.lx.peek().kind; return k===T_NEWLINE||k===T_STMT_SEP||k===T_GOTO_SEP||k===T_EOF; }

  /* ── expr17 — atom ──────────────────────────────────────────────────── */
  _e17() {
    const t = this.lx.peek();
    if (t.kind===T_LPAREN) {
      this.lx.next(); this.skip_ws();
      const inner = this._e0();
      this.skip_ws();
      if (this.lx.peek().kind===T_COMMA) {
        const alt=expr_new(E_ALT); if(inner) alt.children.push(inner);
        while(this.lx.peek().kind===T_COMMA) {
          this.lx.next(); this.skip_ws();
          alt.children.push(this._e0()||expr_new(E_NUL)); this.skip_ws();
        }
        if(this.lx.peek().kind===T_RPAREN) this.lx.next();
        return alt.children.length===1 ? alt.children[0] : alt;
      }
      this.skip_ws(); if(this.lx.peek().kind===T_RPAREN) this.lx.next();
      return inner;
    }
    if (t.kind===T_STR)     { this.lx.next(); return expr_leaf(E_QLIT, t.sval); }
    if (t.kind===T_REAL)    { this.lx.next(); return expr_leaf(E_FLIT, t.sval); } /* sval="1.0" preserves real */
    if (t.kind===T_INT)     { this.lx.next(); return expr_ival(E_ILIT, t.ival); }
    if (t.kind===T_KEYWORD) { this.lx.next(); return expr_leaf(E_KEYWORD, t.sval); }
    if (t.kind===T_END)     { this.lx.next(); return expr_leaf(E_VAR, t.sval); }
    if (t.kind===T_IDENT) {
      this.lx.next();
      if (this.lx.peek().kind===T_LPAREN) {
        this.lx.next();
        const e=expr_leaf(E_FNC, t.sval);
        this._arglist(e);
        if(this.lx.peek().kind===T_RPAREN) this.lx.next();
        return e;
      }
      return expr_leaf(E_VAR, t.sval);
    }
    return null;
  }

  _arglist(node) {
    this.skip_ws();
    const k=this.lx.peek().kind;
    if(k===T_RPAREN||k===T_RBRACKET||k===T_RANGLE||k===T_EOF) return;
    node.children.push(this._e0()||expr_new(E_NUL));
    while(this.lx.peek().kind===T_COMMA) {
      this.lx.next(); this.skip_ws();
      const k2=this.lx.peek().kind;
      if(k2===T_RPAREN||k2===T_RBRACKET||k2===T_RANGLE||k2===T_EOF){node.children.push(expr_new(E_NUL));break;}
      node.children.push(this._e0()||expr_new(E_NUL));
    }
    this.skip_ws();
  }

  /* ── expr15 — postfix subscript ─────────────────────────────────────── */
  _e15() {
    let e=this._e17(); if(!e) return null;
    for(;;) {
      const open=this.lx.peek().kind;
      const close=open===T_LBRACKET?T_RBRACKET:open===T_LANGLE?T_RANGLE:-1;
      if(close<0) break;
      this.lx.next();
      const tmp=expr_new(E_NUL); this._arglist(tmp);
      if(this.lx.peek().kind===close) this.lx.next();
      const idx=expr_new(E_IDX); idx.children=[e,...tmp.children]; e=idx;
    }
    return e;
  }

  /* ── expr14 — unary prefix ──────────────────────────────────────────── */
  _e14() {
    const t=this.lx.peek();
    /* Bare (unary) tokens only — binary tokens (T_BIN_*) never appear here */
    const MAP={[T_AT]:E_CAPT_CURSOR,[T_TILDE]:E_INDIRECT,[T_QMARK]:E_INTERROGATE,
               [T_AMP]:E_OPSYN,[T_PLUS]:E_PLS,[T_MINUS]:E_MNS,[T_STAR]:E_DEFER,
               [T_DOLLAR]:E_INDIRECT,[T_DOT]:E_NAME,[T_BANG]:E_POW,
               [T_PCT]:E_MOD,[T_SLASH]:E_DIV,[T_HASH]:E_MUL,[T_EQ]:E_ASSIGN,[T_PIPE]:E_ALT};
    const uk=MAP[t.kind];
    if(!uk) return this._e15();
    this.lx.next();
    const op=this._e14();
    if(!op){this._err('expected operand after unary operator');return expr_new(E_NUL);}
    let e=expr_unary(uk, op);
    /* For $ and .: if inner parse consumed subscripts (e.g. $.a<2> parsed inner as
     * E_IDX(E_NAME(a),2) giving E_INDIRECT(E_IDX(NAME(a),2)) = $(a<2>)), hoist them:
     * E_INDIRECT(E_IDX(base,idx...)) -> E_IDX(E_INDIRECT(base), idx...) so $.a<2>=($a)<2>. */
    if((uk===E_INDIRECT||uk===E_NAME) && e.children[0]&&e.children[0].kind===E_IDX) {
      const inner=e.children[0];                    /* E_IDX(base, idxs...) */
      const hoisted=expr_unary(uk, inner.children[0]); /* E_INDIRECT(base) */
      const rehoisted=expr_new(E_IDX);
      rehoisted.children=[hoisted,...inner.children.slice(1)];
      e=rehoisted;
    }
    return e;
  }

  /* ── expr13 — ~ binary (T_BIN_TILDE) ───────────────────────────────── */
  _e13() {
    let l=this._e14(); if(!l) return null;
    for(;;) {
      if(this.lx.peek().kind!==T_BIN_TILDE) break;
      this.lx.next();
      l=expr_binary(E_CAPT_COND_ASGN, l, this._e13());
    }
    return l;
  }

  /* ── expr12 — $ . binary (T_BIN_DOLLAR / T_BIN_DOT) ────────────────── */
  _e12() {
    let l=this._e13(); if(!l) return null;
    for(;;) {
      const k=this.lx.peek().kind;
      if(k!==T_BIN_DOLLAR && k!==T_BIN_DOT) break;
      this.lx.next();
      l=expr_binary(k===T_BIN_DOLLAR?E_CAPT_IMMED_ASGN:E_CAPT_COND_ASGN, l, this._e13());
    }
    return l;
  }

  /* ── Generic left-associative binary helper ────────────────────────── */
  /* White is swallowed into T_BIN_* token by lexer — match directly, no WS peek */
  _lbin(nxt, ops) {
    let l=nxt.call(this); if(!l) return null;
    for(;;) {
      const k=this.lx.peek().kind, ek=ops[k];
      if(ek===undefined) break;
      this.lx.next();
      l=expr_binary(ek, l, nxt.call(this));
    }
    return l;
  }

  /* ── Right-associative binary helper ────────────────────────────────── */
  _rbin(nxt, ops) {
    const l=nxt.call(this); if(!l) return null;
    const k=this.lx.peek().kind, ek=ops[k];
    if(ek===undefined) return l;
    this.lx.next();
    return expr_binary(ek, l, this._rbin(nxt, ops));
  }

  _e11() { return this._rbin(this._e12, {[T_BIN_CARET]:E_POW,[T_BIN_BANG]:E_POW,[T_BIN_STARSTAR]:E_POW}); }
  _e10() { return this._lbin(this._e11, {[T_BIN_PCT]:E_MOD}); }
  _e9()  { return this._lbin(this._e10, {[T_BIN_STAR]:E_MUL}); }
  _e8()  { return this._lbin(this._e9,  {[T_BIN_SLASH]:E_DIV}); }
  _e7()  { return this._lbin(this._e8,  {[T_BIN_HASH]:E_MUL}); }
  _e6()  { return this._lbin(this._e7,  {[T_BIN_PLUS]:E_ADD,[T_BIN_MINUS]:E_SUB}); }
  _e5()  { return this._lbin(this._e6,  {[T_BIN_AT]:E_CAPT_CURSOR}); }

  /* ── expr4 — whitespace concatenation ──────────────────────────────── */
  _is_cat_start(k) {
    /* Binary tokens (T_BIN_*) and boundaries are NOT concatenation starters */
    switch(k) {
      /* Unary ops that ARE valid cat-starters (begin next subexpr) */
      case T_AT:case T_PLUS:case T_MINUS:case T_HASH:case T_SLASH:case T_PCT:
      case T_CARET:case T_BANG:case T_STAR:case T_DOT:case T_TILDE:
      case T_EQ:case T_QMARK:case T_AMP:case T_PIPE:case T_DOLLAR:
      case T_STARSTAR:
        return true;
      /* Binary ops and boundaries are NOT cat-starters */
      case T_BIN_PLUS:case T_BIN_MINUS:case T_BIN_STAR:case T_BIN_SLASH:
      case T_BIN_PCT:case T_BIN_CARET:case T_BIN_BANG:case T_BIN_STARSTAR:
      case T_BIN_AT:case T_BIN_TILDE:case T_BIN_DOLLAR:case T_BIN_DOT:
      case T_BIN_HASH:case T_BIN_PIPE:case T_BIN_EQ:case T_BIN_QMARK:
      case T_BIN_AMP:
      case T_COMMA:case T_RPAREN:case T_RBRACKET:case T_RANGLE:
      case T_COLON:case T_GOTO_SEP:case T_STMT_SEP:case T_NEWLINE:
      case T_WS:case T_EOF:case T_ERR: return false;
      default: return true;
    }
  }

  _e4() {
    const first=this._e5(); if(!first) return null;
    const items=[first];
    for(;;) {
      const m=this.lx.mark();
      if(this.lx.peek().kind!==T_WS) break;
      this.lx.next();
      if(!this._is_cat_start(this.lx.peek().kind)){this.lx.restore(m);break;}
      const nxt=this._e5(); if(!nxt){this.lx.restore(m);break;}
      items.push(nxt);
    }
    if(items.length===1) return first;
    const e=expr_new(E_SEQ); e.children=items; return e;
  }

  /* ── expr3 — | alternation (T_BIN_PIPE) ────────────────────────────── */
  _e3() {
    const first=this._e4(); if(!first) return null;
    if(this.lx.peek().kind!==T_BIN_PIPE) return first;
    const e=expr_new(E_ALT); e.children=[first];
    for(;;) {
      if(this.lx.peek().kind!==T_BIN_PIPE) break;
      this.lx.next();
      e.children.push(this._e4()||expr_new(E_NUL));
    }
    return e;
  }

  /* ── expr2 — & ──────────────────────────────────────────────────────── */
  _e2() { return this._lbin(this._e3, {[T_BIN_AMP]:E_OPSYN}); }

  /* ── expr0 — = assignment, ? scan (right-assoc) ────────────────────── */
  _e0() {
    const l=this._e2(); if(!l) return null;
    const k=this.lx.peek().kind;
    if(k===T_BIN_EQ)    {this.lx.next();return expr_binary(E_ASSIGN,l,this._e0());}
    if(k===T_BIN_QMARK) {this.lx.next();return expr_binary(E_SCAN,  l,this._e0());}
    return l;
  }

  _expr() { this.skip_ws(); return this._e0(); }

  /* ── M-G4: value-context E_SEQ → E_CAT fixup ────────────────────────── */
  _fixup_val(e) {
    if(!e) return;
    if(e.kind===E_SEQ) e.kind=E_CAT;
    e.children.forEach(c=>this._fixup_val(c));
  }
  _is_pat(e) {
    if(!e) return false;
    if([E_ARB,E_ARBNO,E_CAPT_COND_ASGN,E_CAPT_IMMED_ASGN,E_CAPT_CURSOR,E_DEFER].includes(e.kind)) return true;
    /* E_FNC whose name is a pattern primitive (LEN, POS, TAB, ANY, etc.) */
    if(e.kind===E_FNC && PAT_FNC_NAMES.has(e.sval.toUpperCase())) return true;
    return (e.children||[]).some(c=>this._is_pat(c));
  }

  /* ── Goto label parser ───────────────────────────────────────────────── */
  _goto_label() {
    const open=this.lx.peek().kind;
    const close=open===T_LPAREN?T_RPAREN:open===T_LANGLE?T_RANGLE:-1;
    if(close<0) return null;
    this.lx.next(); this.skip_ws();
    const t=this.lx.peek(); let label=null;
    if(t.kind===T_IDENT||t.kind===T_KEYWORD||t.kind===T_END) { this.lx.next(); label=t.sval; }
    else if(t.kind===T_DOLLAR) {
      this.lx.next();
      if(this.lx.peek().kind===T_LPAREN) {
        this.lx.next(); let depth=1, parts=[];
        while(!this.lx.at_end()&&depth>0) {
          const tt=this.lx.next();
          if(tt.kind===T_LPAREN) depth++;
          else if(tt.kind===T_RPAREN){if(--depth===0)break;}
          else parts.push(tt.sval||String(tt.ival)||'');
        }
        label='$COMPUTED:'+parts.join('');
      } else if(this.lx.peek().kind===T_STR) {
        const n2=this.lx.next(); label=`$COMPUTED:'${n2.sval}'`;
      } else { const n2=this.lx.next(); label='$'+(n2.sval||'?'); }
    }
    this.skip_ws(); if(this.lx.peek().kind===close) this.lx.next();
    return label;
  }

  _goto_field() {
    /* Called after T_GOTO_SEP consumed */
    this.skip_ws(); if(this._at_end()) return null;
    const g={uncond:null,onsuccess:null,onfailure:null};
    while(!this._at_end()) {
      const t=this.lx.peek();
      if(t.kind===T_IDENT&&t.sval) {
        if(t.sval.toUpperCase()==='S'){this.lx.next();g.onsuccess=this._goto_label();this.skip_ws();continue;}
        if(t.sval.toUpperCase()==='F'){this.lx.next();g.onfailure=this._goto_label();this.skip_ws();continue;}
      }
      if(t.kind===T_LPAREN||t.kind===T_LANGLE){g.uncond=this._goto_label();this.skip_ws();continue;}
      this.lx.next(); /* unexpected — skip */
    }
    return(!g.uncond&&!g.onsuccess&&!g.onfailure)?null:g;
  }

  /* ── consume tokens until logical line boundary ──────────────────────── */
  _to_newline() {
    while(true) {
      const k=this.lx.peek().kind;
      if(k===T_NEWLINE||k===T_STMT_SEP||k===T_EOF) { if(k!==T_EOF) this.lx.next(); return; }
      this.lx.next();
    }
  }

  /* ── parse one statement from token stream ───────────────────────────── */
  parse_stmt() {
    /* Skip blank / comment lines */
    while(this.lx.peek().kind===T_NEWLINE) this.lx.next();
    if(this.lx.at_end()) return null;

    const ln=this.lx.peek().lineno;
    const s=stmt_new(); s.lineno=ln;

    /* Label: non-ws, non-boundary token at start of logical line.
     * The lexer guarantees that after T_NEWLINE (or at file start),
     * the next token is the first token of a new logical line.
     * If it's an IDENT/END at what would have been col-0, it's a label. */
    const t0=this.lx.peek();
    if((t0.kind===T_IDENT||t0.kind===T_END) && t0.kind!==T_WS) {
      /* Peek one more: if the token after is WS or a boundary, it's a label */
      const m=this.lx.mark(); this.lx.next();
      const t1=this.lx.peek().kind;
      if(t1===T_WS||t1===T_NEWLINE||t1===T_STMT_SEP||t1===T_GOTO_SEP||t1===T_EOF) {
        s.label=t0.sval;
        if(t0.kind===T_END){s.is_end=true;this._to_newline();return s;}
        /* consume the WS after label */
        this.skip_ws();
      } else {
        /* not a label — restore and parse as subject */
        this.lx.restore(m);
      }
    }

    /* Skip WS / handle label-only or goto-only lines */
    this.skip_ws();
    if(this.lx.peek().kind===T_GOTO_SEP){this.lx.next();s.go=this._goto_field();this._to_newline();return s;}
    if(this._at_end()){if(this.lx.peek().kind!==T_EOF)this.lx.next();return s;}

    /* Subject */
    s.subject=this._e14();
    this._fixup_val(s.subject);

    /* Pattern / assignment / goto
     * With binary-vs-unary lexer: T_BIN_EQ = "subj = repl" assignment;
     * T_BIN_QMARK = "subj ? pat" match; WS after subject = pattern follows.
     * No WS-peeking needed — binary tokens are unambiguous. */
    const k_after=this.lx.peek().kind;

    if(k_after===T_BIN_EQ) {
      /* Simple assignment: SUBJ = REPL */
      this.lx.next(); s.has_eq=true;
      if(!this._at_end()) {
        const rhs=this._expr();
        /* S = P R form: split only when leading fn is a known guard/predicate.
         * Oracle (snobol4.y:216) handles the no-equals case via E_VAR first-child.
         * For the T_BIN_EQ case: "X = guard(args) repl" where guard is a predicate fn. */
        if(s.subject && rhs && (rhs.kind===E_SEQ||rhs.kind===E_CAT) && rhs.children.length>=2
           && rhs.children[0].kind===E_FNC
           && GUARD_FNC_NAMES.has((rhs.children[0].sval||'').toUpperCase())) {
          const kids=rhs.children;
          s.pattern=kids[0];
          let replKid;
          if(kids.length===2){ replKid=kids[1]; }
          else { const r=expr_new(E_SEQ); r.children=kids.slice(1); replKid=r; }
          s.replacement=replKid;
          s.guard_assign=true;
          if(!this._is_pat(s.replacement)) this._fixup_val(s.replacement);
        } else {
          s.replacement=rhs;
          if(s.replacement&&!this._is_pat(s.replacement)) this._fixup_val(s.replacement);
        }
      }
    } else if(k_after===T_BIN_QMARK) {
      /* Explicit match: SUBJ ? PAT = REPL */
      this.lx.next();
      if(!this._at_end()) s.pattern=this._e3();
      if(this.lx.peek().kind===T_BIN_EQ) {
        this.lx.next(); s.has_eq=true;
        if(!this._at_end()) s.replacement=this._expr();
        if(s.replacement&&!this._is_pat(s.replacement)) this._fixup_val(s.replacement);
      }
    } else if(k_after===T_WS) {
      /* WS after subject: pattern (and optional = repl) follows */
      this.lx.next(); this.skip_ws();
      if(!this._at_end()) {
        s.pattern=this._e3();
        if(this.lx.peek().kind===T_BIN_EQ) {
          this.lx.next(); s.has_eq=true;
          if(!this._at_end()) s.replacement=this._expr();
          if(s.replacement&&!this._is_pat(s.replacement)) this._fixup_val(s.replacement);
        }
      }
    }

    this.skip_ws();  /* consume any WS between body and :(goto) */
    if(this.lx.peek().kind===T_GOTO_SEP){this.lx.next();s.go=this._goto_field();}
    this._to_newline();
    return s;
  }

  /* ── parse_program: full program → linked list ──────────────────────── */
  parse_program() {
    const prog={head:null,tail:null,nstmts:0};
    while(!this.lx.at_end()) {
      while(this.lx.peek().kind===T_NEWLINE) this.lx.next();
      if(this.lx.at_end()) break;
      const s=this.parse_stmt(); if(!s) break;
      if(!prog.head) prog.head=prog.tail=s;
      else{prog.tail.next=s;prog.tail=s;}
      prog.nstmts++;
      if(s.is_end) break;
    }
    return prog;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * INTERPRETER — tree-walk executor
 * Mirrors scrip-interp.c: label_table, call_stack, interp_eval, execute_program
 * ═══════════════════════════════════════════════════════════════════════════ */

const label_table = Object.create(null);
function build_label_table(prog) {
  for(let s=prog.head;s;s=s.next) if(s.label) label_table[s.label.toUpperCase()]=s;
}
function label_lookup(name) { return name?label_table[name.toUpperCase()]||null:null; }

/* Function registry */
const func_table = Object.create(null);
/* Operator override table: maps op-char → {fn:string, arity:1|2} */
const opsyn_table = Object.create(null);
function define_fn(spec, entry) {
  const m=spec.match(/^(\w+)\(([^)]*)\)(.*)?$/); if(!m) return;
  const fname=m[1].toUpperCase();
  const params=m[2].split(',').map(s=>s.trim()).filter(Boolean).map(s=>s.toUpperCase());
  const locals=(m[3]||'').split(',').map(s=>s.trim()).filter(Boolean).map(s=>s.toUpperCase());
  func_table[fname]={fname, params, locals, entry: entry||fname};
}

/* Control-flow signals (thrown as exceptions) */
class SnoReturn  { constructor(v){this.v=v;} }
class SnoFReturn {}
class SnoNReturn { constructor(v){this.v=v;} }
class EndSignal  {}

/* Call stack */
const call_stack=[];
const CALL_MAX=256;

/* ── interp_eval ─────────────────────────────────────────────────────────── */
function interp_eval(e) {
  if(!e) return null;
  if(e._val !== undefined) return e._val;  /* pre-evaluated (APPLY args) */
  switch(e.kind) {
    case E_QLIT:    return e.sval;
    case E_ILIT:    return e.ival;
    case E_FLIT:    return _real_result(parseFloat(e.sval)); /* tagged real */
    case E_NUL:     return null;
    case E_VAR:     { const v=_vars[e.sval]; return v===undefined?null:v; }
    case E_KEYWORD: { const v=_vars['&'+e.sval.toUpperCase()]; return v===undefined?null:v; }
    case E_ADD: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL; return _add(a,b); }
    case E_SUB: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL; return _sub(a,b); }
    case E_MUL: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL; return _mul(a,b); }
    case E_DIV: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL; return _div(a,b); }
    case E_MOD: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL;
                  const an=_num(a),bn=_num(b); return bn===0?_FAIL:an%bn; }
    case E_POW: { const a=interp_eval(e.children[0]),b=interp_eval(e.children[1]);
                  if(_is_fail(a)||_is_fail(b)) return _FAIL; return _pow(a,b); }
    case E_MNS: { const a=interp_eval(e.children[0]); if(_is_fail(a)) return _FAIL;
                  const r=-_num(a); return _is_int(a)?r:_real_result(r); }
    case E_PLS: { const a=interp_eval(e.children[0]); if(_is_fail(a)) return _FAIL;
                  const r=_num(a); return _is_int(a)?r:_real_result(r); }
    case E_SEQ:  /* M-SJ-B07: E_SEQ with pattern children → pattern object */
      if (_expr_is_pat(e)) return _build_pat(e);
      /* fall through — pure E_SEQ (no pattern nodes) = string concat */
    case E_CAT: {
      /* Evaluate children eagerly; if any is a PAT object, switch to PAT_seq */
      const parts=[];
      for(const c of e.children){const v=interp_eval(c);if(_is_fail(v))return _FAIL;parts.push(v);}
      if(parts.some(v=>v!==null&&typeof v==='object'&&v.__pat)) {
        /* Dynamic PAT concat: coerce strings to PAT_lit, pass through PAT objects */
        return PAT_seq(...parts.map(v=>(v!==null&&typeof v==='object'&&v.__pat)?v:PAT_lit(_str(v??''))));
      }
      return parts.map(v=>_str(v??'')).join('');
    }
    case E_ASSIGN: {
      const rhs=interp_eval(e.children[1]);
      if(_is_fail(rhs)) return _FAIL;   /* fail propagates, no assignment */
      _assign(e.children[0], rhs); return rhs;
    }
    case E_INDIRECT: {
      const v=interp_eval(e.children[0]); if(_is_fail(v)) return _FAIL;
      const name=_str(v); const r=_vars[name]; return r===undefined?null:r;
    }
    case E_DEFER: { const v=interp_eval(e.children[0]); return _is_fail(v)?_FAIL:v; }
    case E_FNC:   { const _r=_call(e.sval, e.children); return (_r&&_r.__nameref)?(_vars[_r.__nameref]??null):_r; }
    case E_IDX: {
      const base=interp_eval(e.children[0]); if(_is_fail(base)) return _FAIL;
      const idxs=e.children.slice(1).map(c=>{const v=interp_eval(c);return v;});
      if(idxs.some(_is_fail)) return _FAIL;
      if(base && base.__sno_array) {
        if(idxs.length===1) {
          const n=_num(idxs[0]); const d=base.__dims[0];
          if(n<d.lo||n>d.hi) return _FAIL;
          return base[n]??base.__defval;
        }
        /* multi-dim: compute flat key string with bounds check */
        const nums=idxs.map(v=>_num(v));
        for(let i=0;i<nums.length;i++){const d=base.__dims[i]||base.__dims[0];if(nums[i]<d.lo||nums[i]>d.hi)return _FAIL;}
        const key=nums.join(',');
        return base[key]??base.__defval;
      }
      if(base instanceof Map) return base.get(_str(idxs[0]))??null;
      if(Array.isArray(base)) return base[_num(idxs[0])-1]??null;
      return null;
    }
    case E_SCAN: {
      const s=interp_eval(e.children[0]); if(_is_fail(s)) return _FAIL;
      const pat=_build_pat(e.children[1]); if(_is_fail(pat)) return _FAIL;
      const res=sno_search(_str(s),pat);
      return res?_str(s).slice(res.start,res.end):_FAIL;
    }
    case E_CAPT_COND_ASGN:
    case E_CAPT_IMMED_ASGN: return interp_eval(e.children[0]);
    case E_CAPT_CURSOR: {
      /* binary @ → check if OPSYN'd */
      if(e.children[1]!==undefined && opsyn_table['@'] && opsyn_table['@'].arity===2) {
        const a=interp_eval(e.children[0]), b=interp_eval(e.children[1]);
        if(_is_fail(a)||_is_fail(b)) return _FAIL;
        const fn=opsyn_table['@'].fn;
        const ea=expr_new(E_NUL); ea._val=a;
        const eb=expr_new(E_NUL); eb._val=b;
        return _call(fn,[ea,eb]);
      }
      return interp_eval(e.children[0]);  /* @V in expr ctx: eval V */
    }
    case E_NAME:            return e.children[0]?.sval||null;
    case E_INTERROGATE:     { const v=interp_eval(e.children[0]); return _is_fail(v)?_FAIL:null; }
    case E_ALT: {
      /* unary | → check if OPSYN'd as unary op */
      if(e.children.length===1 && opsyn_table['|'] && opsyn_table['|'].arity===1) {
        const a=interp_eval(e.children[0]); if(_is_fail(a)) return _FAIL;
        const fn=opsyn_table['|'].fn;
        const ea=expr_new(E_NUL); ea._val=a;
        return _call(fn,[ea]);
      }
      /* Build ALT pattern: evaluate each child as a value first.
       * If the child is already a PAT object (dynamic accumulation) use it directly.
       * If it's a PAT_KINDS node, route through _build_pat.
       * Otherwise (value function result, string literal), wrap with PAT_lit. */
      const arms = e.children.map(c => {
        if (PAT_KINDS.has(c.kind)) return _build_pat(c);
        const v = interp_eval(c);
        if (_is_fail(v)) return PAT_lit('');
        if (v !== null && typeof v === 'object' && v.__pat) return v;
        return PAT_lit(_str(v??''));
      });
      return PAT_alt(...arms);
    }
    default:
      process.stderr.write(`sno-interp: unhandled expr ${e.kind}\n`);
      return _FAIL;
  }
}

/* ── _assign: write val into lvalue expression ──────────────────────────── */
function _assign(lhs, val) {
  if(!lhs) return;
  if(lhs.kind===E_VAR)     { _vars[lhs.sval]=val; return; }
  if(lhs.kind===E_KEYWORD) { _vars['&'+lhs.sval.toUpperCase()]=val; return; }
  if(lhs.kind===E_INDIRECT){ const n=_str(interp_eval(lhs.children[0])); _vars[n]=val; return; }
  if(lhs.kind===E_IDX) {
    const base=interp_eval(lhs.children[0]);
    const idxs=lhs.children.slice(1).map(c=>interp_eval(c));
    if(base && base.__sno_array) {
      if(idxs.length===1) { const n=_num(idxs[0]); const d=base.__dims[0]; if(n>=d.lo&&n<=d.hi) base[n]=val; return; }
      const key=idxs.map(v=>_num(v)).join(','); base[key]=val; return;
    }
    if(base instanceof Map)  { base.set(_str(idxs[0]),val); return; }
    if(Array.isArray(base))  { base[_num(idxs[0])-1]=val; return; }
  }
  if(lhs.kind===E_FNC) {
    const fn=lhs.sval.toUpperCase();
    /* ITEM(arr, i1, ...) = val */
    if(fn==='ITEM') {
      const base=interp_eval(lhs.children[0]);
      const idxs=lhs.children.slice(1).map(c=>interp_eval(c));
      if(base && base.__sno_array) {
        if(idxs.length===1) { const n=_num(idxs[0]); const d=base.__dims[0]; if(n>=d.lo&&n<=d.hi) base[n]=val; return; }
        const key=idxs.map(v=>_num(v)).join(','); base[key]=val; return;
      }
      if(base instanceof Map) { base.set(_str(idxs[0]),val); return; }
      return;
    }
    /* First check if this is a user fn returning a nameref (NRETURN) */
    const _r=_call(lhs.sval, lhs.children);
    if(_r && _r.__nameref) { _vars[_r.__nameref]=val; return; }
    /* DATA field setter: field(obj) = val */
    const fd=func_table[fn];
    if(fd&&fd.__data_field&&lhs.children.length>0) {
      const obj=interp_eval(lhs.children[0]); if(obj&&typeof obj==='object') obj[fd.field]=val;
    }
  }
}

/* ── _expr_is_pat: true if expr tree contains pattern-only nodes ─────────── */
/* M-SJ-B07: used by interp_eval to route E_SEQ containing pattern nodes     */
/* to _build_pat instead of string concat.  PAT_KINDS declared at top.      */
function _expr_is_pat(e) {
  if (!e) return false;
  if (PAT_KINDS.has(e.kind)) return true;
  /* E_FNC whose name is a pattern primitive */
  if (e.kind === E_FNC && PAT_FNC_NAMES.has(e.sval.toUpperCase())) return true;
  return (e.children||[]).some(c => _expr_is_pat(c));
}

/* ── _build_pat: EXPR_t → PAT_* node for sno_engine.js ─────────────────── */
function _build_pat(e) {
  if(!e) return _FAIL;
  switch(e.kind) {
    case E_QLIT:   return PAT_lit(e.sval);
    case E_ILIT:   return PAT_lit(String(e.ival));
    case E_FLIT:   return PAT_lit(_str(e.sval));
    case E_VAR:    {
      /* Bare pattern keywords (no parens) parse as E_VAR — dispatch to constructors */
      switch(e.sval.toUpperCase()) {
        case 'FAIL':    return PAT_fail();
        case 'SUCCEED': return PAT_succeed();
        case 'FENCE':   return PAT_fence();
        case 'ABORT':   return PAT_abort();
        case 'ARB':     return PAT_arb();
        case 'REM':     return PAT_rem();
        case 'BAL':     return PAT_bal();
      }
      const v=_vars[e.sval];
      if (_is_fail(v)) return _FAIL;
      /* pattern-value variable: if stored value is a PAT object, return directly */
      if (v !== null && v !== undefined && (typeof v === 'object' && v.__pat)) return v;
      return PAT_lit(_str(v??''));
    }
    case E_ALT:    return PAT_alt(...e.children.map(_build_pat));
    case E_SEQ:    return PAT_seq(...e.children.map(_build_pat));
    case E_CAT:    return PAT_seq(...e.children.map(_build_pat));
    case E_ARB:    return PAT_arb();
    case E_ARBNO:  return PAT_arbno(_build_pat(e.children[0]));
    case E_REM:    return PAT_rem();
    case E_FAIL:   return PAT_fail();
    case E_SUCCEED:return PAT_succeed();
    case E_FENCE:  return PAT_fence();
    case E_ABORT:  return PAT_abort();
    case E_BAL:    return PAT_bal();
    case E_LEN:    return PAT_len(_num(interp_eval(e.children[0])));
    case E_POS:    return PAT_pos(_num(interp_eval(e.children[0])));
    case E_RPOS:   return PAT_rpos(_num(interp_eval(e.children[0])));
    case E_TAB:    return PAT_tab(_num(interp_eval(e.children[0])));
    case E_RTAB:   return PAT_rtab(_num(interp_eval(e.children[0])));
    case E_ANY:    return PAT_any(_str(interp_eval(e.children[0])));
    case E_NOTANY: return PAT_notany(_str(interp_eval(e.children[0])));
    case E_SPAN:   return PAT_span(_str(interp_eval(e.children[0])));
    case E_BREAK:
    case E_BREAKX: return PAT_break(_str(interp_eval(e.children[0])));
    case E_CAPT_COND_ASGN: {
      const p = _build_pat(e.children[0]);
      const tgt = e.children[1];
      /* If target is E_DEFER(E_FNC(f)), store callable descriptor */
      if (tgt && tgt.kind === E_DEFER && tgt.children[0]?.kind === E_FNC)
        return PAT_capt_cond(p, {__dcall: tgt.children[0].sval, __exprs: tgt.children[0].children});
      return PAT_capt_cond(p, tgt?.sval || '');
    }
    case E_CAPT_IMMED_ASGN: {
      const p = _build_pat(e.children[0]);
      const tgt = e.children[1];
      if (tgt && tgt.kind === E_DEFER && tgt.children[0]?.kind === E_FNC)
        return PAT_capt_imm(p, {__dcall: tgt.children[0].sval, __exprs: tgt.children[0].children});
      return PAT_capt_imm(p, tgt?.sval || '');
    }
    case E_CAPT_CURSOR: {
      /* Binary form: P @ V — match P then capture cursor into V.
       * Unary form (prefix @V from _e14): children[0] is the variable E_VAR/E_NAME.
       * Distinguish: if children[1] exists, it's binary (P @ V). */
      if (e.children[1] !== undefined) {
        const varname = e.children[1]?.sval || '';
        return PAT_seq(_build_pat(e.children[0]), PAT_capt_cursor(varname));
      }
      /* Unary prefix @V */
      const varname = e.children[0]?.sval || e.children[0]?.children?.[0]?.sval || '';
      return PAT_capt_cursor(varname);
    }
    case E_FNC: {
      const fn=e.sval.toUpperCase();
      const a0=()=>_str(interp_eval(e.children[0]));
      const n0=()=>_num(interp_eval(e.children[0]));
      switch(fn) {
        case 'ANY':    return PAT_any(a0());
        case 'NOTANY': return PAT_notany(a0());
        case 'SPAN':   return PAT_span(a0());
        case 'BREAK':  return PAT_break(a0());
        case 'BREAKX': return PAT_break(a0());
        case 'LEN':    return PAT_len(n0());
        case 'POS':    return PAT_pos(n0());
        case 'RPOS':   return PAT_rpos(n0());
        case 'TAB':    return PAT_tab(n0());
        case 'RTAB':   return PAT_rtab(n0());
        case 'ARB':    return PAT_arb();
        case 'ARBNO':  return PAT_arbno(_build_pat(e.children[0]));
        case 'REM':    return PAT_rem();
        case 'FAIL':   return PAT_fail();
        case 'SUCCEED':return PAT_succeed();
        case 'FENCE':  return PAT_fence();
        case 'ABORT':  return PAT_abort();
        case 'BAL':    return PAT_bal();
        default: {
          /* Dispatch based on whether function is user-defined or builtin.
           * User-defined functions in pattern position may return a PAT object
           * (e.g. icase(), mkpat()) — use PAT_deferred so the result is descended into.
           * Builtins are predicates (differ, eq, etc.) — use PAT_pred (zero-width). */
          const sval=e.sval, ch=e.children;
          if (func_table[fn]) {
            /* User-defined: PAT_deferred — call at match time, descend into PAT result */
            return PAT_deferred(() => {
              const r = _call(sval, ch);
              if (_is_fail(r)) return null;
              if (r !== null && typeof r === 'object' && r.__pat) return r;
              return PAT_lit(_str(r??''));
            });
          }
          /* Builtin predicate: PAT_pred — zero-width, lazy */
          return PAT_pred(() => _call(sval, ch));
        }
      }
    }
    case E_DEFER: {
      /* *expr in pattern position.
       * *f()  → dynamic sub-pattern: call f() at match time, descend into result.
       * *VAR  → evaluate immediately (value fixed at build time). */
      const inner = e.children[0];
      if (inner.kind === E_FNC) {
        const sval = inner.sval, ch = inner.children;
        return PAT_deferred(() => {
          const result = _call(sval, ch);
          if (_is_fail(result)) return null;
          if (result && result.__pat) return result;
          return PAT_lit(_str(result ?? ''));
        });
      }
      /* General case: evaluate inner expression now */
      const v = interp_eval(inner);
      if (_is_fail(v)) return _FAIL;
      if (v && v.__pat) return v;
      return PAT_lit(_str(v ?? ''));
    }
    default: { const v=interp_eval(e); return _is_fail(v)?_FAIL:PAT_lit(_str(v)); }
  }
}

/* ── _call: builtin and user-defined function dispatch ──────────────────── */
function _call(fname, arg_exprs) {
  const fn=fname.toUpperCase();
  const args=arg_exprs.map(a=>interp_eval(a));

  /* DATA field accessors and constructors take priority over same-named builtins */
  { const _fd=func_table[fn];
    if(_fd&&(_fd.__data_field||_fd.__data_ctor)) {
      if(_fd.__data_ctor) {
        const obj=Object.create(null); obj.__datatype=fn;
        for(let i=0;i<_fd.fields.length;i++) obj[_fd.fields[i]]=args[i]??null;
        return obj;
      }
      /* field accessor */
      const obj=args[0]; if(!obj||typeof obj!=='object') return _FAIL;
      if(args.length>1){obj[_fd.field]=args[1];return args[1];}
      return obj[_fd.field]??null;
    }
  }

  switch(fn) {
    case 'SIZE':    return _str(args[0]??'').length;
    case 'DUPL':    { const s=_str(args[0]??''),n=Math.max(0,_num(args[1]??0)); return s.repeat(n); }
    case 'REVERSE': return _str(args[0]??'').split('').reverse().join('');
    case 'TRIM':    return _str(args[0]??'').trimEnd();
    case 'REPLACE': {
      const s=_str(args[0]??''),f=_str(args[1]??''),r=_str(args[2]??'');
      let res=''; for(const c of s){const i=f.indexOf(c);res+=i>=0?(r[i]??''):c;} return res;
    }
    case 'SUBSTR':  { const s=_str(args[0]??''),st=_num(args[1]??1)-1,ln=args[2]!==undefined?_num(args[2]):s.length-st; return s.substr(st,ln); }
    case 'LPAD':    { const s=_str(args[0]??''),n=_num(args[1]??0),p=args[2]!==undefined?_str(args[2]):''; return s.padStart(n,p||' '); }
    case 'RPAD':    { const s=_str(args[0]??''),n=_num(args[1]??0),p=args[2]!==undefined?_str(args[2]):''; return s.padEnd(n,p||' '); }
    case 'INTEGER': { const n=Number(_str(args[0]??'')); return isFinite(n)?Math.trunc(n):_FAIL; }
    case 'REAL':    { const n=parseFloat(_str(args[0]??'')); return isFinite(n)?_real_result(n):_FAIL; }
    case 'STRING':  return _str(args[0]??'');
    case 'CONVERT': { const v=args[0],t=_str(args[1]??'').toUpperCase();
                      if(t==='INTEGER') { const n=Math.trunc(_num(v)); return isFinite(n)?n:_FAIL; }
                      if(t==='REAL')    { const n=parseFloat(_str(v??'')); return isFinite(n)?_real_result(n):_FAIL; }
                      if(t==='STRING')  return _str(v);
                      if(t==='ARRAY' && v instanceof Map) {
                        /* TABLE → ARRAY: 2-column array [key, value] rows */
                        const entries=[...v.entries()];
                        const arr=Object.create(null);
                        arr.__sno_array=true; arr.__dims=[{lo:1,hi:entries.length},{lo:1,hi:2}];
                        arr.__defval=null; arr.__proto_str=`${entries.length},2`;
                        for(let i=0;i<entries.length;i++){
                          arr[`${i+1},1`]=entries[i][0]; arr[`${i+1},2`]=entries[i][1];
                        }
                        return arr;
                      }
                      if(t==='TABLE' && v&&v.__sno_array) {
                        /* ARRAY → TABLE: expects 2-column array, col1=key col2=value */
                        const tbl=new Map(); tbl.__sno_table=true;
                        const d=v.__dims; if(!d||d.length<2) return _FAIL;
                        for(let i=d[0].lo;i<=d[0].hi;i++){
                          const k=v[`${i},1`]??null, val=v[`${i},2`]??null;
                          if(k!==null) tbl.set(_str(k),val);
                        }
                        return tbl;
                      }
                      return _FAIL; }
    case 'IDENT': {
                    if (args.length < 2 || args[1] === undefined) {
                      const a = args[0]; if (_is_fail(a)) return _FAIL;
                      const is_null = (a === null || a === '' || a === 0);
                      return is_null ? '' : _FAIL;
                    }
                    if(_is_fail(args[0])||_is_fail(args[1])) return _FAIL;
                    /* DATA objects (not real numbers): identity comparison */
                    if (typeof args[0]==='object'&&args[0]!==null&&args[0].__datatype &&
                        typeof args[1]==='object'&&args[1]!==null&&args[1].__datatype)
                      return args[0]===args[1]?'':_FAIL;
                    return _str(args[0]??'')===_str(args[1]??'')?'':_FAIL; }
    case 'DIFFER': {
                    if (args.length < 2 || args[1] === undefined) {
                      const a = args[0]; if (_is_fail(a)) return _FAIL;
                      const is_null = (a === null || a === '' || a === 0);
                      return is_null ? _FAIL : '';
                    }
                    if(_is_fail(args[0])||_is_fail(args[1])) return _FAIL;
                    /* DATA objects (not real numbers): identity comparison */
                    if (typeof args[0]==='object'&&args[0]!==null&&args[0].__datatype &&
                        typeof args[1]==='object'&&args[1]!==null&&args[1].__datatype)
                      return args[0]!==args[1]?'':_FAIL;
                    return _str(args[0]??'')!==_str(args[1]??'')?'':_FAIL; }
    case 'LT': { const a=_num(args[0]??0),b=_num(args[1]??0); return a< b?args[0]:_FAIL; }
    case 'LE': { const a=_num(args[0]??0),b=_num(args[1]??0); return a<=b?args[0]:_FAIL; }
    case 'GT': { const a=_num(args[0]??0),b=_num(args[1]??0); return a> b?args[0]:_FAIL; }
    case 'GE': { const a=_num(args[0]??0),b=_num(args[1]??0); return a>=b?args[0]:_FAIL; }
    case 'EQ': { const a=_num(args[0]??0),b=_num(args[1]??0); return a===b?args[0]:_FAIL; }
    case 'NE': { const a=_num(args[0]??0),b=_num(args[1]??0); return a!==b?args[0]:_FAIL; }
    case 'DEFINE':  { const spec=_str(args[0]??''),entry=args[1]?_str(args[1]):null; define_fn(spec,entry); return null; }
    case 'ITEM': {
      /* ITEM(arr, i1, i2, ...) — programmatic subscript, equivalent to arr<i1,i2,...> */
      const base=args[0]; const idxs=args.slice(1);
      if(base && base.__sno_array) {
        if(idxs.length===1) { const n=_num(idxs[0]); const d=base.__dims[0]; if(n<d.lo||n>d.hi) return _FAIL; return base[n]??base.__defval; }
        const key=idxs.map(v=>_num(v)).join(','); return base[key]??base.__defval;
      }
      if(base instanceof Map) return base.get(_str(idxs[0]))??null;
      return _FAIL;
    }
    case 'ARRAY': {
      /* Parse spec: '5' → 1-based [1..5]; '2:8' → [2..8]; '3,4' → 3×4 multi-dim */
      const spec = _str(args[0]??'1');
      const dims = spec.split(',').map(s=>{
        const parts = s.trim().split(':');
        const lo = parts.length>1 ? parseInt(parts[0],10) : 1;
        const hi = parts.length>1 ? parseInt(parts[1],10) : parseInt(parts[0],10);
        return {lo, hi};
      });
      const defval = args[1]??null;
      /* Use a plain object with metadata so E_IDX can do bounds check */
      const arr = Object.create(null);
      arr.__sno_array = true;
      arr.__dims = dims;
      arr.__defval = defval;
      arr.__proto_str = dims.map(d=> d.lo===1 ? String(d.hi) : `${d.lo}:${d.hi}`).join(',');
      return arr;
    }
    case 'TABLE':   { const tbl = new Map(); tbl.__sno_table=true; return tbl; }
    case 'PROTOTYPE': {
      const v = args[0];
      if (v && v.__sno_array) return v.__proto_str;
      if (v instanceof Map)   return '';
      return _FAIL;
    }
    case 'VALUE': {
      const v = args[0];
      if (typeof v === 'string') { const r=_vars[v]; return r===undefined||r===null?null:r; }
      return v??null;
    }
    case 'APPLY': {
      const fn = _str(args[0]??'');
      if (!fn) return _FAIL;
      const fnargs = args.slice(1);
      return _call(fn, fnargs.map(a=>{ const e=expr_new(E_NUL); e._val=a; return e; }));
    }
    case 'DATA': {
      const spec = _str(args[0]??'');
      const m = spec.match(/^(\w+)\(([^)]*)\)/);
      if (!m) return null;
      const tname  = m[1].toUpperCase();
      const fields = m[2].split(',').map(s=>s.trim()).filter(Boolean).map(s=>s.toUpperCase());
      /* Register the constructor: tname(f1,f2,...) → object with __datatype */
      func_table[tname] = { __data_ctor: true, fields };
      /* Register field accessor functions */
      for (const f of fields) {
        func_table[f] = { __data_field: true, field: f };
      }
      return null;
    }
    case 'CHAR':    return String.fromCharCode(_num(args[0]??0));
    case 'CODE':    { const s=_str(args[0]??''); return s.length?s.charCodeAt(0):_FAIL; }
    case 'REMDR':   { const a=_num(args[0]??0),b=_num(args[1]??1); return b===0?_FAIL:a%b; }
    case 'ABS':     return Math.abs(_num(args[0]??0));
    case 'MAX':     return Math.max(...args.map(a=>_num(a??0)));
    case 'MIN':     return Math.min(...args.map(a=>_num(a??0)));
    case 'SQRT':    return Math.sqrt(_num(args[0]??0));
    case 'EXP':     return Math.exp(_num(args[0]??0));
    case 'LOG':     return Math.log(_num(args[0]??0));
    case 'DATE':    return new Date().toDateString();
    case 'TIME':    return Date.now();
    case 'DATATYPE': {
      const v = args[0];
      if (v === null || v === undefined) return 'STRING';
      if (v && v.__sno_array) return 'ARRAY';
      if (v instanceof Map)   return 'TABLE';
      if (_is_real(v))        return 'REAL';
      if (typeof v === 'object' && v.__datatype) return v.__datatype;
      if (typeof v === 'number') return 'INTEGER';
      return 'STRING';
    }
    case 'LGT': return _str(args[0]??'') >  _str(args[1]??'') ? args[0] : _FAIL;
    case 'LLT': return _str(args[0]??'') <  _str(args[1]??'') ? args[0] : _FAIL;
    case 'LGE': return _str(args[0]??'') >= _str(args[1]??'') ? args[0] : _FAIL;
    case 'LLE': return _str(args[0]??'') <= _str(args[1]??'') ? args[0] : _FAIL;
    case 'LEQ': return _str(args[0]??'') === _str(args[1]??'') ? args[0] : _FAIL;
    case 'LNE': return _str(args[0]??'') !== _str(args[1]??'') ? args[0] : _FAIL;
    case 'OPSYN': {
      /* opsyn('sym'|.sym, .func, arity) — bind operator or function to another */
      const dest=(_str(args[0]??'')); const src=(_str(args[1]??'')).toUpperCase();
      const arity=args[2]!==undefined?_num(args[2]):2;
      if(dest && /^\W/.test(dest)) {
        /* operator override — dest starts with non-word char */
        opsyn_table[dest]={fn:src, arity};
      } else {
        /* function alias */
        const dk=dest.toUpperCase();
        if(src && func_table[src]) func_table[dk]=func_table[src];
        else func_table[dk]={__alias:src};
      }
      return null;
    }
    case 'SETEXIT': case 'STOPTR': case 'TRACE': case 'SPITBOL': return null;
    case 'INPUT':   {
      try {
        const buf=Buffer.alloc(4096);
        const n=require('fs').readSync(0,buf,0,4095,null);
        if(n<=0) return _FAIL;
        return buf.slice(0,n).toString().replace(/\r?\n$/,'');
      } catch(e) { return _FAIL; }
    }
  }

  /* User-defined function */
  let fd=func_table[fn];
  if (!fd) {
    process.stderr.write(`sno-interp: undefined function ${fn}\n`);
    return _FAIL;
  }
  /* DATA constructor */
  /* Follow alias chain (OPSYN function alias) */
  if (fd.__alias) { fd=func_table[fd.__alias]; if(!fd){process.stderr.write(`sno-interp: undefined function ${fn}\n`);return _FAIL;} }
  if (fd.__data_ctor) {
    const obj = Object.create(null);
    obj.__datatype = fn;  /* already uppercased */
    for (let i=0; i<fd.fields.length; i++) obj[fd.fields[i]] = args[i]??null;
    return obj;
  }
  /* DATA field accessor */
  if (fd.__data_field) {
    const obj = args[0];
    if (!obj || typeof obj !== 'object') return _FAIL;
    if (args.length > 1) { obj[fd.field] = args[1]; return args[1]; }  /* setter */
    return obj[fd.field]??null;
  }
  return _call_user(fn, fd, args);
}

function _call_user(fname, fd, args) {
  if(call_stack.length>=CALL_MAX){process.stderr.write('sno-interp: stack overflow\n');return _FAIL;}

  /* The return-value variable is the DEFINITION name (fd.fname), not the call-site name.
   * For OPSYN aliases, fd.fname='DOUBLE' while fname='TRIPLE'. The body assigns _vars['DOUBLE']. */
  const retvar = (fd.fname||fname);

  /* Save and clear: definition name var + call-site name var (if different) + params + locals */
  const saved={};
  const all=[retvar,...(fname!==retvar?[fname]:[]),...(fd.params||[]),...(fd.locals||[])];
  for(const n of all) { saved[n]=_vars[n]??null; _vars[n]=null; }
  /* Bind args to params */
  for(let i=0;i<(fd.params||[]).length;i++) _vars[fd.params[i]]=args[i]??null;

  call_stack.push({fname: retvar});
  let ret=_FAIL;
  try {
    const body=label_lookup(fd.entry)||label_lookup(retvar);
    if(body) _exec_from(body);
    ret=_vars[retvar]??null;
  } catch(ex) {
    if(ex instanceof SnoReturn)  { ret=ex.v; if(retvar==='ICASE') process.stderr.write('ICASE ret='+JSON.stringify(ret).slice(0,150)+'\n'); }
    else if(ex instanceof SnoFReturn) ret=_FAIL;
    else if(ex instanceof SnoNReturn) { call_stack.pop(); for(const [n,v] of Object.entries(saved)) _vars[n]=v; return {__nameref:ex.v}; }
    else throw ex;
  } finally {
    call_stack.pop();
    for(const [n,v] of Object.entries(saved)) _vars[n]=v;
  }
  return ret;
}

/* ── _exec_from: run statements starting at s ───────────────────────────── */
function _exec_from(start) {
  let s=start, steps=0;
  const LIMIT=10_000_000;

  while(s&&steps++<LIMIT) {
    if(s.is_end) throw new EndSignal();

    /* Update statement counters */
    _vars['&STCOUNT'] = (_vars['&STCOUNT']||0) + 1;
    _vars['&STNO']    = s.lineno || steps;

    let ok=true;
    let subj_name=null, subj_val=null;

    /* Evaluate subject */
    if(s.subject) {
      if(s.subject.kind===E_VAR) {
        subj_name=s.subject.sval;
        subj_val=_vars[subj_name]??null;
      } else {
        subj_val=interp_eval(s.subject);
        if(_is_fail(subj_val)) ok=false;
      }
    }

    /* Pattern match */
    if(ok&&s.pattern) {
      if(s.guard_assign) {
        /* S=PR guard-assignment: "subj = guard(args) repl-expr"
         * Guard is a predicate (ne/differ/eq/…): evaluate it; if fails → stmt fails.
         * On success: assign replacement to subject (never splice into subject string). */
        const pat=_build_pat(s.pattern);
        if(_is_fail(pat)){ok=false;}
        else {
          const subj_str=_str(_vars[subj_name]??subj_val??'');
          const gres=sno_match('',pat)||sno_search(subj_str,pat);
          if(!gres){ok=false;}
          else if(s.has_eq) {
            let repl=s.replacement?(_expr_is_pat(s.replacement)?_build_pat(s.replacement):interp_eval(s.replacement)):null;
            if(_is_fail(repl)){ok=false;}
            else {
              if(subj_name) _vars[subj_name]=repl;
              else if(s.subject) _assign(s.subject,repl);
            }
          }
        }
      } else {
        const pat=_build_pat(s.pattern);
        if(_is_fail(pat)){ok=false;}
        else {
          const subj_str=subj_name?_str(_vars[subj_name]??''):_str(subj_val);
          const anchor=!!_vars['&ANCHOR'];
          const res=anchor?sno_match(subj_str,pat):sno_search(subj_str,pat);
          if(!res){ok=false;}
          else if(s.has_eq) {
            let repl=null;
            if(s.replacement){repl=interp_eval(s.replacement);if(_is_fail(repl))ok=false;}
            if(ok) {
              const ns=subj_str.slice(0,res.start)+_str(repl??'')+subj_str.slice(res.end);
              if(subj_name) _vars[subj_name]=ns;
            }
          }
        }
      }
    } else if(ok&&s.has_eq) {
      /* Assignment (no pattern) */
      /* If RHS contains pattern nodes (LEN, TAB, ARB, captures etc.) build a pattern value */
      let repl=s.replacement?(_expr_is_pat(s.replacement)?_build_pat(s.replacement):interp_eval(s.replacement)):null;
      if(_is_fail(repl)){ok=false;}
      else {
        if(subj_name) _vars[subj_name]=repl;
        else if(s.subject) _assign(s.subject, repl);
      }
    }

    /* Goto */
    const go=s.go; let next_lbl=null;
    if(go) {
      if(go.uncond)              next_lbl=go.uncond;
      else if(ok&&go.onsuccess)  next_lbl=go.onsuccess;
      else if(!ok&&go.onfailure) next_lbl=go.onfailure;
    }

    if(next_lbl) {
      const u=next_lbl.toUpperCase();
      if(u==='RETURN')  throw new SnoReturn(_vars[call_stack[call_stack.length-1]?.fname??'']??null);
      if(u==='FRETURN') throw new SnoFReturn();
      if(u==='NRETURN') throw new SnoNReturn(_vars[call_stack[call_stack.length-1]?.fname??'']??null);
      if(u==='END')     throw new EndSignal();
      const tgt=label_lookup(next_lbl);
      if(!tgt){process.stderr.write(`sno-interp: undefined label '${next_lbl}'\n`);throw new EndSignal();}
      s=tgt;
    } else {
      s=s.next;
    }
  }
  if(steps>=LIMIT) process.stderr.write('sno-interp: step limit exceeded\n');
}

/* ── execute_program ─────────────────────────────────────────────────────── */
function execute_program(prog) {
  /* Pre-scan DEFINE calls */
  for(let s=prog.head;s;s=s.next) {
    if(s.subject?.kind===E_FNC && s.subject.sval?.toUpperCase()==='DEFINE' && s.subject.children.length>=1) {
      const spec_e=s.subject.children[0];
      if(spec_e.kind===E_QLIT) define_fn(spec_e.sval, s.subject.children[1]?.sval||null);
    }
  }
  try { _exec_from(prog.head); } catch(ex) {
    if(ex instanceof EndSignal) return 0;
    throw ex;
  }
  return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */
const [,,src_file]=process.argv;
if(!src_file){process.stderr.write('usage: node sno-interp.js <file.sno>\n');process.exit(1);}

const src=fs.readFileSync(src_file,'utf8');
const lx=new Lexer(src, src_file);
const prog=new Parser(lx).parse_program();

if(lx.errors>0){process.stderr.write(`sno-interp: ${lx.errors} error(s)\n`);process.exit(1);}

build_label_table(prog);
process.exit(execute_program(prog));
