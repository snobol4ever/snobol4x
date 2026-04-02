'use strict';
/*
 * bb_boxes.js — Dynamic Byrd Box Runtime for JavaScript
 *
 * Direct port of src/runtime/boxes/bb_*.c
 * One factory function per box: bb_lit(), bb_any(), ... etc.
 * Each factory returns a {α, β} port-function pair.
 *
 * Global match state (shared across all boxes during one match):
 *   _Σ  — subject string
 *   _Δ  — cursor (mutated as match proceeds)
 *   _Ω  — subject length
 *
 * These are module-level variables set by exec_stmt() Phase 1.
 * All box functions close over them via the module-level let bindings below.
 *
 * Port protocol (mirrors C):
 *   box.α()  — fresh entry (try to match at current _Δ)
 *   box.β()  — backtrack re-entry (undo last match, try next alternative)
 *   return value: matched string slice, or null (= spec_empty = ω fired)
 *
 * Capture protocol:
 *   Conditional captures (.) accumulate in _pending[]; committed by exec_stmt Phase 5.
 *   Immediate captures ($) write directly to _vars[].
 *
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 * Oracle: src/runtime/boxes/bb_*.c + bb_box.h
 */

/* ── module-level match state ───────────────────────────────────────────── */
let _Σ = '';   /* subject string                                              */
let _Δ = 0;    /* cursor                                                      */
let _Ω = 0;    /* subject length                                              */

/* Set by exec_stmt Phase 1; called before build_pattern() */
function bb_set_subject(subj) {
    _Σ = subj;
    _Δ = 0;
    _Ω = subj.length;
}

/* ── capture lists (Phase 3 → Phase 5) ─────────────────────────────────── */
let _pending = [];          /* [{varname, value}] — conditional captures      */

function bb_reset_captures() { _pending = []; }
function bb_get_pending()    { return _pending; }

/* ── helpers ────────────────────────────────────────────────────────────── */
/* spec(start, len) → matched string slice, or null on failure               */
const _FAIL = null;
function _spec(start, len) { return (len === 0) ? '' : _Σ.slice(start, start + len); }
function _is_fail(v)        { return v === null; }


/* ══════════════════════════════════════════════════════════════════════════
 *  bb_lit — literal string match
 *  Oracle: bb_lit.c
 *  α: if Σ[Δ..Δ+len] == lit → advance Δ, γ; else ω
 *  β: Δ -= len; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_lit(lit) {
    const len = lit.length;
    return {
        α() {
            if (_Δ + len > _Ω)                       return _FAIL;
            if (_Σ.slice(_Δ, _Δ + len) !== lit)      return _FAIL;
            const r = _spec(_Δ, len); _Δ += len;     return r;
        },
        β() { _Δ -= len;                             return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_any — match one char if in charset
 *  Oracle: bb_any.c
 *  α: if Σ[Δ] in chars → advance, γ; else ω
 *  β: Δ--; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_any(chars) {
    return {
        α() {
            if (_Δ >= _Ω || chars.indexOf(_Σ[_Δ]) < 0) return _FAIL;
            const r = _spec(_Δ, 1); _Δ++;              return r;
        },
        β() { _Δ--;                                    return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_notany — match one char NOT in charset
 *  Oracle: bb_notany.c
 *  α: if Σ[Δ] NOT in chars → advance, γ; else ω
 *  β: Δ--; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_notany(chars) {
    return {
        α() {
            if (_Δ >= _Ω || chars.indexOf(_Σ[_Δ]) >= 0) return _FAIL;
            const r = _spec(_Δ, 1); _Δ++;               return r;
        },
        β() { _Δ--;                                     return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_seq — concatenation: left then right; β retries right then left
 *  Oracle: bb_seq.c
 *  α: try left.α; if γ → try right.α; if right γ → SEQ_γ
 *  β: try right.β; if ω → try left.β; loop
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_seq(left, right) {
    return {
        α() {
            const lr = left.α();
            if (_is_fail(lr)) return _FAIL;
            /* left γ — try right */
            let rr = right.α();
            while (_is_fail(rr)) {
                /* right ω — retry left */
                const lr2 = left.β();
                if (_is_fail(lr2)) return _FAIL;
                rr = right.α();
            }
            return rr;   /* right γ — seq γ (right result is the full span end) */
        },
        β() {
            /* retry right first */
            let rr = right.β();
            while (_is_fail(rr)) {
                const lr = left.β();
                if (_is_fail(lr)) return _FAIL;
                rr = right.α();
            }
            return rr;
        }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_alt — alternation: try each child on α; β retries same child only
 *  Oracle: bb_alt.c
 *  α: try children[0].α … children[n-1].α in order
 *  β: retry children[current].β; if ω → ω (don't try next child)
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_alt(children) {
    /* children: array of {α,β} boxes */
    let current = 0;
    let saved_Δ = 0;
    return {
        α() {
            saved_Δ = _Δ;
            current = 0;
            while (current < children.length) {
                _Δ = saved_Δ;
                const r = children[current].α();
                if (!_is_fail(r)) return r;
                current++;
            }
            return _FAIL;
        },
        β() {
            /* retry the branch that last succeeded */
            if (current >= children.length) return _FAIL;
            const r = children[current].β();
            if (!_is_fail(r)) return r;
            /* that branch exhausted — try subsequent children */
            current++;
            while (current < children.length) {
                _Δ = saved_Δ;
                const r2 = children[current].α();
                if (!_is_fail(r2)) return r2;
                current++;
            }
            return _FAIL;
        }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_arb — match 0..n chars lazily; β extends by 1
 *  Oracle: bb_arb.c
 *  α: match 0 chars (ARB_γ immediately)
 *  β: count++; if start+count > Ω → ω; else advance, γ
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_arb() {
    let count = 0, start = 0;
    return {
        α() {
            count = 0; start = _Δ;
            return _spec(_Δ, 0);   /* zero-width γ */
        },
        β() {
            count++;
            if (start + count > _Ω) return _FAIL;
            _Δ = start;
            const r = _spec(_Δ, count); _Δ += count;
            return r;
        }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_arbno — zero-or-more of body; zero-advance guard; β unwinds stack
 *  Oracle: bb_arbno.c
 *  α: push frame(cursor=0, start=Δ); try body.α
 *     body_γ: if Δ==frame.start → γ_now (zero-advance, stop); else push+retry
 *     body_ω: γ with accumulated match
 *  β: pop frame; restore Δ; γ (with shorter match)
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_arbno(body) {
    const stack = [];  /* array of {start} */
    return {
        α() {
            stack.length = 0;
            stack.push({ start: _Δ });
            /* try to extend greedily */
            while (true) {
                const frame = stack[stack.length - 1];
                const br = body.α();
                if (_is_fail(br)) {
                    /* body ω — stop here, succeed with what we have */
                    return _spec(stack[0].start, _Δ - stack[0].start);
                }
                if (_Δ === frame.start) {
                    /* zero advance guard — stop to avoid infinite loop */
                    return _spec(stack[0].start, _Δ - stack[0].start);
                }
                stack.push({ start: _Δ });
            }
        },
        β() {
            if (stack.length <= 1) return _FAIL;
            stack.pop();
            const frame = stack[stack.length - 1];
            _Δ = frame.start;
            return _spec(stack[0].start, _Δ - stack[0].start);
        }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_span — longest prefix ≥1 char in charset; β ω (no retry)
 *  Oracle: bb_span.c
 *  α: scan forward while in chars; if δ==0 → ω; advance, γ
 *  β: Δ -= δ; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_span(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) >= 0) δ++;
            if (δ <= 0) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ;
            return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_brk (BREAK) — scan to first char IN set; may be zero-width
 *  Oracle: bb_brk.c
 *  α: scan while NOT in chars; if Δ+δ>=Ω → ω; advance, γ
 *  β: Δ -= δ; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_brk(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) < 0) δ++;
            if (_Δ + δ >= _Ω) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ;
            return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_breakx (BREAKX) — like BRK but fails on zero advance
 *  Oracle: bb_breakx.c
 *  α: scan while NOT in chars; if δ==0 or Δ+δ>=Ω → ω
 *  β: Δ -= δ; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_breakx(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) < 0) δ++;
            if (δ === 0 || _Δ + δ >= _Ω) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ;
            return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_len — match exactly n characters
 *  Oracle: bb_len.c
 *  α: if Δ+n > Ω → ω; advance n, γ
 *  β: Δ -= n; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_len(n) {
    return {
        α() {
            if (_Δ + n > _Ω) return _FAIL;
            const r = _spec(_Δ, n); _Δ += n;
            return r;
        },
        β() { _Δ -= n; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_pos — assert cursor == n (zero-width)
 *  Oracle: bb_pos.c
 *  α: if Δ != n → ω; γ zero-width
 *  β: ω (no retry)
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_pos(n) {
    return {
        α() { return (_Δ !== n) ? _FAIL : _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_rpos — assert cursor == Ω-n (zero-width)
 *  Oracle: bb_rpos.c
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_rpos(n) {
    return {
        α() { return (_Δ !== _Ω - n) ? _FAIL : _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_tab — advance cursor TO absolute position n
 *  Oracle: bb_tab.c
 *  α: if Δ > n → ω; advance = n-Δ; Δ = n; γ
 *  β: Δ -= advance; ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_tab(n) {
    let advance = 0;
    return {
        α() {
            if (_Δ > n) return _FAIL;
            advance = n - _Δ;
            const r = _spec(_Δ, advance); _Δ = n;
            return r;
        },
        β() { _Δ -= advance; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_rtab — advance cursor TO position Ω-n
 *  Oracle: bb_rtab.c
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_rtab(n) {
    let advance = 0;
    return {
        α() {
            const target = _Ω - n;
            if (_Δ > target) return _FAIL;
            advance = target - _Δ;
            const r = _spec(_Δ, advance); _Δ = target;
            return r;
        },
        β() { _Δ -= advance; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_rem — match entire remainder; no backtrack
 *  Oracle: bb_rem.c
 *  α: γ with Σ[Δ..Ω]; Δ = Ω
 *  β: ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_rem() {
    return {
        α() { const r = _spec(_Δ, _Ω - _Δ); _Δ = _Ω; return r; },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_fence — succeed once; β cuts (no retry)
 *  Oracle: bb_fence.c
 *  α: γ zero-width (always succeeds fresh)
 *  β: ω (cut — prevents backtrack through)
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_fence() {
    return {
        α() { return _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_fail — always ω; force backtrack
 *  Oracle: bb_fail.c
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_fail() {
    return {
        α() { return _FAIL; },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_succeed — always γ zero-width; outer scan loop retries position
 *  Oracle: bb_succeed.c
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_succeed() {
    return {
        α() { return _spec(_Δ, 0); },
        β() { return _spec(_Δ, 0); }   /* succeed on backtrack too — outer loop advances */
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_abort — always ω; force total match failure (not just backtrack)
 *  Oracle: bb_abort.c
 *  Note: In JS engine, ABORT sets a flag that exec_stmt checks after Phase 3.
 * ══════════════════════════════════════════════════════════════════════════ */
let _abort_flag = false;
function bb_reset_abort() { _abort_flag = false; }
function bb_aborted()     { return _abort_flag; }

function bb_abort() {
    return {
        α() { _abort_flag = true; return _FAIL; },
        β() { _abort_flag = true; return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_eps — zero-width success once; done flag prevents double-γ
 *  Oracle: bb_eps.c
 *  α: if done → ω; done=1; γ zero-width
 *  β: ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_eps() {
    let done = false;
    return {
        α() {
            if (done) return _FAIL;
            done = true;
            return _spec(_Δ, 0);
        },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_capture — $ (immediate) or . (conditional) capture wrapper
 *  Oracle: bb_capture.c
 *  Wraps a child box; on child γ, either writes to _vars[] immediately ($)
 *  or pushes to _pending[] for Phase 5 commit (.).
 *  On child ω → CAP_ω (clear pending, propagate ω).
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_capture(child, varname, immediate, vars) {
    return {
        α() {
            const cr = child.α();
            if (_is_fail(cr)) { return _FAIL; }
            _do_capture(cr, varname, immediate, vars);
            return cr;
        },
        β() {
            const cr = child.β();
            if (_is_fail(cr)) { return _FAIL; }
            _do_capture(cr, varname, immediate, vars);
            return cr;
        }
    };
}

function _do_capture(cr, varname, immediate, vars) {
    if (!varname) return;
    if (immediate) {
        vars[varname] = cr;   /* $ — write now */
    } else {
        _pending.push({ varname, value: cr });   /* . — defer to Phase 5 */
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_atp — @var: write cursor position as integer; no backtrack
 *  Oracle: bb_atp.c
 *  α: write Δ to vars[varname]; γ zero-width
 *  β: ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_atp(varname, vars) {
    return {
        α() {
            if (varname) vars[varname] = _Δ;
            return _spec(_Δ, 0);
        },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_dvar — *VAR/VAR: re-resolve live pattern value on every α
 *  Oracle: bb_dvar.c
 *  On α: look up vars[name]; if DT_P → build_pattern; if string → bb_lit;
 *  delegate α/β to rebuilt child.
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_dvar(name, vars, build_pattern_fn) {
    let child = null;
    let last_val = undefined;
    return {
        α() {
            const val = vars[name];
            /* rebuild child if value changed */
            if (val !== last_val) {
                last_val = val;
                if (val !== null && val !== undefined && typeof val === 'object' && val._pat) {
                    child = build_pattern_fn(val);   /* pattern value */
                } else if (typeof val === 'string') {
                    child = bb_lit(val);             /* string → literal match */
                } else {
                    child = bb_fail();               /* unknown → fail */
                }
            } else if (child && child._reset) {
                child._reset();
            }
            return child ? child.α() : _FAIL;
        },
        β() { return child ? child.β() : _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_not — \X (NOT): succeed iff X fails; β always ω
 *  Oracle: bb_not.c
 *  α: save Δ; run child.α; if child γ → restore Δ, ω (child matched → we fail)
 *                            if child ω → restore Δ, γ zero-width (child failed → we succeed)
 *  β: ω (negation succeeds at most once per position)
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_not(child) {
    return {
        α() {
            const saved = _Δ;
            const cr = child.α();
            _Δ = saved;   /* always restore — NOT never advances cursor */
            return _is_fail(cr) ? _spec(_Δ, 0) : _FAIL;
        },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_bal — balanced parens (STUB — M-DYN-BAL pending)
 *  Oracle: bb_bal.c
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_bal() {
    console.error('bb_bal: unimplemented — ω');
    return {
        α() { return _FAIL; },
        β() { return _FAIL; }
    };
}

/* ══════════════════════════════════════════════════════════════════════════
 *  bb_interr — ?X (interrogation): null result if X succeeds; ω if X fails
 *  Oracle: bb_interr.c
 *  α: save Δ; run child.α; if child γ → restore Δ, γ zero-width (null string)
 *                            if child ω → ω
 *  β: ω
 * ══════════════════════════════════════════════════════════════════════════ */
function bb_interr(child) {
    return {
        α() {
            const saved = _Δ;
            const cr = child.α();
            _Δ = saved;
            return _is_fail(cr) ? _FAIL : _spec(_Δ, 0);
        },
        β() { return _FAIL; }
    };
}

/* ── exports ────────────────────────────────────────────────────────────── */
module.exports = {
    /* state management */
    bb_set_subject, bb_reset_captures, bb_get_pending,
    bb_reset_abort, bb_aborted,
    /* box factories */
    bb_lit, bb_any, bb_notany,
    bb_seq, bb_alt,
    bb_arb, bb_arbno,
    bb_span, bb_brk, bb_breakx,
    bb_len, bb_pos, bb_rpos, bb_tab, bb_rtab, bb_rem,
    bb_fence, bb_fail, bb_succeed, bb_abort,
    bb_eps, bb_capture, bb_atp, bb_dvar,
    bb_not, bb_bal, bb_interr,
};
