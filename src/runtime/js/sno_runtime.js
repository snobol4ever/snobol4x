'use strict';
/*
 * sno_runtime.js — SNOBOL4 JavaScript runtime
 *
 * Provides the primitive operations used by emit_js.c output.
 *
 * Design decisions (SJ-1, do not re-debate):
 *   - _vars: Proxy — set trap for OUTPUT writes to process.stdout
 *   - _FAIL: sentinel object (not null, which is valid SNOBOL4 null/empty)
 *   - All values are JS strings, numbers, or null (SNOBOL4 null)
 *   - Arithmetic coerces via _num(); concatenation via _str()
 *
 * Sprint: SJ-2  Milestone: M-SJ-A01
 * Authors: Lon Jones Cherryholmes (arch), Claude Sonnet 4.6 (impl)
 */

/* -----------------------------------------------------------------------
 * Failure sentinel
 * ----------------------------------------------------------------------- */
const _FAIL = Object.freeze({ _sno_fail: true });

function _is_fail(v) { return v === _FAIL; }

/* -----------------------------------------------------------------------
 * _vars — SNOBOL4 variable store with IO trapping
 * ----------------------------------------------------------------------- */
const _store = {};

const _vars = new Proxy(_store, {
    set(o, k, v) {
        o[k] = v;
        if (k === 'OUTPUT') {
            process.stdout.write(String(v === null ? '' : v) + '\n');
        }
        return true;
    },
    get(o, k) {
        if (k in o) return o[k];
        if (k === 'INPUT') {
            /* synchronous readline — only works in Node.js */
            try {
                const fs = require('fs');
                const buf = Buffer.alloc(4096);
                const n = fs.readSync(0, buf, 0, 4095, null);
                if (n <= 0) return _FAIL;
                return buf.slice(0, n).toString().replace(/\r?\n$/, '');
            } catch(e) { return _FAIL; }
        }
        return null; /* unset variable = SNOBOL4 null */
    }
});

/* -----------------------------------------------------------------------
 * Type coercion
 * ----------------------------------------------------------------------- */

/** Coerce to SNOBOL4 string (null → empty string) */
function _str(v) {
    if (v === null || v === undefined) return '';
    if (v === _FAIL) return '';
    return String(v);
}

/** Coerce to number; throws if not numeric */
function _num(v) {
    if (v === null || v === undefined || v === '') return 0;
    const n = Number(v);
    if (!isFinite(n)) throw new Error('SNOBOL4 type error: not a number: ' + String(v));
    return n;
}

/* -----------------------------------------------------------------------
 * Arithmetic — match SNOBOL4 semantics (integer if both integer-valued)
 * ----------------------------------------------------------------------- */

function _int_if_possible(n) {
    return Number.isInteger(n) ? n : n;
}

function _add(a, b) { return _int_if_possible(_num(a) + _num(b)); }
function _sub(a, b) { return _int_if_possible(_num(a) - _num(b)); }
function _mul(a, b) { return _int_if_possible(_num(a) * _num(b)); }
function _div(a, b) {
    const bv = _num(b);
    if (bv === 0) throw new Error('SNOBOL4: division by zero');
    return _int_if_possible(_num(a) / bv);
}
function _pow(a, b) { return _int_if_possible(Math.pow(_num(a), _num(b))); }

/* -----------------------------------------------------------------------
 * String concatenation (n-ary)
 * ----------------------------------------------------------------------- */

function _cat(...args) {
    return args.map(_str).join('');
}

/* -----------------------------------------------------------------------
 * Keyword access (&STCOUNT etc.)
 * ----------------------------------------------------------------------- */

const _kw_store = {
    STCOUNT: 0,
    STLIMIT: -1,
    ALPHABET: (function() { let s=''; for(let i=0;i<256;i++) s+=String.fromCharCode(i); return s; })(),
    MAXLNGTH: 5000,
    TRIM: 0,
    RTNTYPE: '',
    ERRTEXT: '',
    ERRLIMIT: 0,
    FNNAME: '',
    LASTNO: 0,
    UCASE: (function() { let s=''; for(let i=65;i<=90;i++) s+=String.fromCharCode(i); return s; })(),
    LCASE: (function() { let s=''; for(let i=97;i<=122;i++) s+=String.fromCharCode(i); return s; })(),
};

function _kw(name) {
    const k = name.replace(/^&/, '');
    if (k in _kw_store) return _kw_store[k];
    return null;
}

/* -----------------------------------------------------------------------
 * Function application — builtin dispatch
 * ----------------------------------------------------------------------- */

const _builtins = {
    SIZE(args)    { return _str(args[0]).length; },
    TRIM(args)    { return _str(args[0]).trimEnd(); },
    DUPL(args)    { const s=_str(args[0]), n=_num(args[1]); return s.repeat(Math.max(0,n)); },
    SUBSTR(args)  { const s=_str(args[0]),i=_num(args[1])-1,n=args[2]!=null?_num(args[2]):s.length-i; return s.substr(i,n); },
    IDENT(args)   { return _str(args[0])===_str(args[1]) ? _str(args[0]) : _FAIL; },
    DIFFER(args)  { return _str(args[0])!==_str(args[1]) ? _str(args[0]) : _FAIL; },
    LT(args)      { return _num(args[0])<_num(args[1])   ? _str(args[0]) : _FAIL; },
    LE(args)      { return _num(args[0])<=_num(args[1])  ? _str(args[0]) : _FAIL; },
    GT(args)      { return _num(args[0])>_num(args[1])   ? _str(args[0]) : _FAIL; },
    GE(args)      { return _num(args[0])>=_num(args[1])  ? _str(args[0]) : _FAIL; },
    EQ(args)      { return _num(args[0])===_num(args[1]) ? _str(args[0]) : _FAIL; },
    NE(args)      { return _num(args[0])!==_num(args[1]) ? _str(args[0]) : _FAIL; },
    INTEGER(args) { const n=Number(args[0]); return Number.isInteger(n) ? n : _FAIL; },
    REAL(args)    { const n=Number(args[0]); return isFinite(n) ? n : _FAIL; },
    CONVERT(args) { /* basic: return arg[0] */ return args[0]; },
    DATATYPE(args){ return typeof args[0] === 'number' ? (Number.isInteger(args[0])?'INTEGER':'REAL') : 'STRING'; },
    INPUT(args)   { return _vars['INPUT']; },
    OUTPUT(args)  { if(args[0]!==undefined) _vars['OUTPUT']=args[0]; return args[0]; },
    CHAR(args)    { return String.fromCharCode(_num(args[0])); },
    CODE(args)    { const s=_str(args[0]); return s.length ? s.charCodeAt(0) : _FAIL; },
    LPAD(args)    { const s=_str(args[0]),n=_num(args[1]),c=args[2]!=null?_str(args[2]):''; return s.padStart(n,c[0]||' '); },
    RPAD(args)    { const s=_str(args[0]),n=_num(args[1]),c=args[2]!=null?_str(args[2]):''; return s.padEnd(n,c[0]||' '); },
    REPLACE(args) { /* REPLACE(s, from, to) */ const s=_str(args[0]),f=_str(args[1]),t=_str(args[2]); let r=''; for(let i=0;i<s.length;i++){const fi=f.indexOf(s[i]);r+=fi>=0?(t[fi]??''):s[i];}return r; },
    REVERSE(args) { return _str(args[0]).split('').reverse().join(''); },
    UPPER(args)   { return _str(args[0]).toUpperCase(); },
    LOWER(args)   { return _str(args[0]).toLowerCase(); },
    ABORT(args)   { process.exit(1); },
    FENCE(args)   { return args[0] !== undefined ? args[0] : ''; },
    FAIL(args)    { return _FAIL; },
    SUCCEED(args) { return args[0] !== undefined ? args[0] : ''; },
    APPLY(args)   { return _apply(_str(args[0]), args.slice(1)); },
    REMDR(args)   { const a=_num(args[0]),b=_num(args[1]); if(b===0) throw new Error('SNOBOL4: remdr by zero'); return Math.trunc(a)%Math.trunc(b); },
    DEFINE(args)  { /* stub — user-defined functions not yet wired */ return null; },
    ARRAY(args)   { /* stub */ return []; },
    TABLE(args)   { /* stub */ return {}; },
    PROTOTYPE(args){ return null; },
    ARB(args)     { return ''; /* zero-width succeed in value context */ },
    REM(args)     { return ''; },
    ANY(args)     { return _str(args[0])[0] || _FAIL; },
    NOTANY(args)  { return _FAIL; /* stub */ },
    SPAN(args)    { return _FAIL; /* stub */ },
    BREAK(args)   { return _FAIL; /* stub */ },
    LEN(args)     { return ''; /* stub zero-width */ },
    POS(args)     { return ''; /* stub */ },
    RPOS(args)    { return ''; /* stub */ },
    TAB(args)     { return ''; /* stub */ },
    RTAB(args)    { return ''; /* stub */ },
    ARBNO(args)   { return ''; /* stub */ },
};

function _apply(name, args) {
    const uname = name.toUpperCase();
    if (uname in _builtins) return _builtins[uname](args);
    /* user-defined: look up in _user_fns */
    if (name in _user_fns) return _user_fns[name](args);
    throw new Error('SNOBOL4: undefined function: ' + name);
}

const _user_fns = {};

/* -----------------------------------------------------------------------
 * Pattern matching — simple substring match for M-SJ-A01
 * Full Byrd-box dispatch in M-SJ-A02+
 * ----------------------------------------------------------------------- */

/**
 * _match(subject, pattern) → true/false
 * pattern may be a string (literal) or a pattern descriptor object.
 * For M-SJ-A01: string patterns only.
 */
function _match(subject, pattern) {
    if (pattern === null || pattern === undefined || pattern === _FAIL) return false;
    if (typeof pattern === 'string') {
        return subject.includes(pattern);
    }
    /* future: pattern object with .type / Byrd-box dispatch */
    return false;
}

/**
 * _replace(subject, replacement) → new string
 * For M-SJ-A01: replace first occurrence matched by _match.
 * Full implementation deferred to M-SJ-A02.
 */
function _replace(subject, replacement) {
    /* stub — returns subject with replacement appended */
    return _str(replacement);
}

/* -----------------------------------------------------------------------
 * Exports
 * ----------------------------------------------------------------------- */

module.exports = {
    _vars,
    _FAIL,
    _is_fail,
    _str,
    _num,
    _cat,
    _add,
    _sub,
    _mul,
    _div,
    _pow,
    _apply,
    _kw,
    _match,
    _replace,
    _user_fns,
};
