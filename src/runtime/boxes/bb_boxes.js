'use strict';
/*
 * bb_boxes.js — All Byrd box JavaScript implementations, consolidated
 * AUTHORS: Lon Jones Cherryholmes · Jeffrey Cooper M.D. · Claude Sonnet 4.6
 */

let _Σ = '', _Δ = 0, _Ω = 0;
function bb_set_state(s) { _Σ = s.Σ; _Δ = s.Δ; _Ω = s.Ω; }
const _FAIL = null;
function _spec(start, len) { return (len === 0) ? '' : _Σ.slice(start, start + len); }
function _is_fail(v) { return v === null; }

// ───── lit ─────
/*
 * bb_lit.js — Dynamic Byrd Box: LIT
 * Direct port of bb_lit.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_lit(lit) {
    const len = lit.length;
    return {
        α() {
            if (_Δ + len > _Ω)                    return _FAIL;
            if (_Σ.slice(_Δ, _Δ + len) !== lit)   return _FAIL;
            const r = _spec(_Δ, len); _Δ += len;  return r;
        },
        β() { _Δ -= len; return _FAIL; }
    };
}
module.exports = { bb_lit, bb_set_state };

// ───── seq ─────
/*
 * bb_seq.js — Dynamic Byrd Box: SEQ
 * Direct port of bb_seq.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_seq(left, right) {
    return {
        α() {
            const lr = left.α();
            if (_is_fail(lr)) return _FAIL;
            let rr = right.α();
            while (_is_fail(rr)) {
                const lr2 = left.β();
                if (_is_fail(lr2)) return _FAIL;
                rr = right.α();
            }
            return rr;
        },
        β() {
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
module.exports = { bb_seq, bb_set_state };

// ───── alt ─────
/*
 * bb_alt.js — Dynamic Byrd Box: ALT
 * Direct port of bb_alt.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_alt(children) {
    let current = 0, saved_Δ = 0;
    return {
        α() {
            saved_Δ = _Δ; current = 0;
            while (current < children.length) {
                _Δ = saved_Δ;
                const r = children[current].α();
                if (!_is_fail(r)) return r;
                current++;
            }
            return _FAIL;
        },
        β() {
            if (current >= children.length) return _FAIL;
            const r = children[current].β();
            if (!_is_fail(r)) return r;
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
module.exports = { bb_alt, bb_set_state };

// ───── arb ─────
/*
 * bb_arb.js — Dynamic Byrd Box: ARB
 * Direct port of bb_arb.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_arb() {
    let count = 0, start = 0;
    return {
        α() { count = 0; start = _Δ; return _spec(_Δ, 0); },
        β() {
            count++;
            if (start + count > _Ω) return _FAIL;
            _Δ = start;
            const r = _spec(_Δ, count); _Δ += count;
            return r;
        }
    };
}
module.exports = { bb_arb, bb_set_state };

// ───── arbno ─────
/*
 * bb_arbno.js — Dynamic Byrd Box: ARBNO
 * Direct port of bb_arbno.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_arbno(body) {
    const stack = [];
    return {
        α() {
            stack.length = 0;
            stack.push({ start: _Δ });
            while (true) {
                const frame = stack[stack.length - 1];
                const br = body.α();
                if (_is_fail(br)) return _spec(stack[0].start, _Δ - stack[0].start);
                if (_Δ === frame.start) return _spec(stack[0].start, _Δ - stack[0].start);
                stack.push({ start: _Δ });
            }
        },
        β() {
            if (stack.length <= 1) return _FAIL;
            stack.pop();
            _Δ = stack[stack.length - 1].start;
            return _spec(stack[0].start, _Δ - stack[0].start);
        }
    };
}
module.exports = { bb_arbno, bb_set_state };

// ───── any ─────
/*
 * bb_any.js — Dynamic Byrd Box: ANY
 * Direct port of bb_any.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_any(chars) {
    return {
        α() {
            if (_Δ >= _Ω || chars.indexOf(_Σ[_Δ]) < 0) return _FAIL;
            const r = _spec(_Δ, 1); _Δ++;              return r;
        },
        β() { _Δ--; return _FAIL; }
    };
}
module.exports = { bb_any, bb_set_state };

// ───── notany ─────
/*
 * bb_notany.js — Dynamic Byrd Box: NOTANY
 * Direct port of bb_notany.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_notany(chars) {
    return {
        α() {
            if (_Δ >= _Ω || chars.indexOf(_Σ[_Δ]) >= 0) return _FAIL;
            const r = _spec(_Δ, 1); _Δ++;               return r;
        },
        β() { _Δ--; return _FAIL; }
    };
}
module.exports = { bb_notany, bb_set_state };

// ───── span ─────
/*
 * bb_span.js — Dynamic Byrd Box: SPAN
 * Direct port of bb_span.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_span(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) >= 0) δ++;
            if (δ <= 0) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ; return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}
module.exports = { bb_span, bb_set_state };

// ───── brk ─────
/*
 * bb_brk.js — Dynamic Byrd Box: BRK
 * Direct port of bb_brk.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_brk(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) < 0) δ++;
            if (_Δ + δ >= _Ω) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ; return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}
module.exports = { bb_brk, bb_set_state };

// ───── breakx ─────
/*
 * bb_breakx.js — Dynamic Byrd Box: BREAKX
 * Direct port of bb_breakx.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_breakx(chars) {
    let δ = 0;
    return {
        α() {
            δ = 0;
            while (_Δ + δ < _Ω && chars.indexOf(_Σ[_Δ + δ]) < 0) δ++;
            if (δ === 0 || _Δ + δ >= _Ω) return _FAIL;
            const r = _spec(_Δ, δ); _Δ += δ; return r;
        },
        β() { _Δ -= δ; return _FAIL; }
    };
}
module.exports = { bb_breakx, bb_set_state };

// ───── len ─────
/*
 * bb_len.js — Dynamic Byrd Box: LEN
 * Direct port of bb_len.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_len(n) {
    return {
        α() {
            if (_Δ + n > _Ω) return _FAIL;
            const r = _spec(_Δ, n); _Δ += n; return r;
        },
        β() { _Δ -= n; return _FAIL; }
    };
}
module.exports = { bb_len, bb_set_state };

// ───── pos ─────
/*
 * bb_pos.js — Dynamic Byrd Box: POS
 * Direct port of bb_pos.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_pos(n) {
    return {
        α() { return (_Δ !== n) ? _FAIL : _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}
module.exports = { bb_pos, bb_set_state };

// ───── tab ─────
/*
 * bb_tab.js — Dynamic Byrd Box: TAB
 * Direct port of bb_tab.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_tab(n) {
    let advance = 0;
    return {
        α() {
            if (_Δ > n) return _FAIL;
            advance = n - _Δ;
            const r = _spec(_Δ, advance); _Δ = n; return r;
        },
        β() { _Δ -= advance; return _FAIL; }
    };
}
module.exports = { bb_tab, bb_set_state };

// ───── rem ─────
/*
 * bb_rem.js — Dynamic Byrd Box: REM
 * Direct port of bb_rem.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_rem() {
    return {
        α() { const r = _spec(_Δ, _Ω - _Δ); _Δ = _Ω; return r; },
        β() { return _FAIL; }
    };
}
module.exports = { bb_rem, bb_set_state };

// ───── eps ─────
/*
 * bb_eps.js — Dynamic Byrd Box: EPS
 * Direct port of bb_eps.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_eps() {
    let done = false;
    return {
        α() { if (done) return _FAIL; done = true; return _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}
module.exports = { bb_eps, bb_set_state };

// ───── bal ─────
/*
 * bb_bal.js — Dynamic Byrd Box: BAL
 * Direct port of bb_bal.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


/* bb_bal — STUB: M-DYN-BAL pending (matches bb_bal.c) */
function bb_bal() {
    return {
        α() { console.error('bb_bal: unimplemented — ω'); return _FAIL; },
        β() { return _FAIL; }
    };
}
module.exports = { bb_bal, bb_set_state };

// ───── abort ─────
/*
 * bb_abort.js — Dynamic Byrd Box: ABORT
 * Direct port of bb_abort.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


let _abort_flag = false;
function bb_reset_abort() { _abort_flag = false; }
function bb_aborted()     { return _abort_flag; }
function bb_abort() {
    return {
        α() { _abort_flag = true; return _FAIL; },
        β() { _abort_flag = true; return _FAIL; }
    };
}
module.exports = { bb_abort, bb_reset_abort, bb_aborted, bb_set_state };

// ───── not ─────
/*
 * bb_not.js — Dynamic Byrd Box: NOT
 * Direct port of bb_not.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_not(child) {
    return {
        α() {
            const saved = _Δ;
            const cr = child.α();
            _Δ = saved;
            return _is_fail(cr) ? _spec(_Δ, 0) : _FAIL;
        },
        β() { return _FAIL; }
    };
}
module.exports = { bb_not, bb_set_state };

// ───── interr ─────
/*
 * bb_interr.js — Dynamic Byrd Box: INTERR
 * Direct port of bb_interr.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


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
module.exports = { bb_interr, bb_set_state };

// ───── capture ─────
/*
 * bb_capture.js — Dynamic Byrd Box: CAPTURE
 * Direct port of bb_capture.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


let _pending = [];
function bb_reset_captures() { _pending = []; }
function bb_get_pending()    { return _pending; }
function bb_capture(child, varname, immediate, vars) {
    return {
        α() {
            const cr = child.α();
            if (_is_fail(cr)) return _FAIL;
            _do_capture(cr, varname, immediate, vars); return cr;
        },
        β() {
            const cr = child.β();
            if (_is_fail(cr)) return _FAIL;
            _do_capture(cr, varname, immediate, vars); return cr;
        }
    };
}
function _do_capture(cr, varname, immediate, vars) {
    if (!varname) return;
    if (immediate) vars[varname] = cr;
    else _pending.push({ varname, value: cr });
}
module.exports = { bb_capture, bb_reset_captures, bb_get_pending, bb_set_state };

// ───── atp ─────
/*
 * bb_atp.js — Dynamic Byrd Box: ATP
 * Direct port of bb_atp.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_atp(varname, vars) {
    return {
        α() { if (varname) vars[varname] = _Δ; return _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}
module.exports = { bb_atp, bb_set_state };

// ───── dvar ─────
/*
 * bb_dvar.js — Dynamic Byrd Box: DVAR
 * Direct port of bb_dvar.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_dvar(name, vars, build_pattern_fn) {
    let child = null, last_val = undefined;
    const { bb_lit } = require('./bb_lit.js');
    const { bb_fail } = require('./bb_fail.js');
    return {
        α() {
            const val = vars[name];
            if (val !== last_val) {
                last_val = val;
                if (val && typeof val === 'object' && val._pat)
                    child = build_pattern_fn(val);
                else if (typeof val === 'string')
                    child = bb_lit(val);
                else
                    child = bb_fail();
            }
            return child ? child.α() : _FAIL;
        },
        β() { return child ? child.β() : _FAIL; }
    };
}
module.exports = { bb_dvar, bb_set_state };

// ───── fence ─────
/*
 * bb_fence.js — Dynamic Byrd Box: FENCE
 * Direct port of bb_fence.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_fence() {
    return {
        α() { return _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}
module.exports = { bb_fence, bb_set_state };

// ───── fail ─────
/*
 * bb_fail.js — Dynamic Byrd Box: FAIL
 * Direct port of bb_fail.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_fail() {
    return {
        α() { return _FAIL; },
        β() { return _FAIL; }
    };
}
module.exports = { bb_fail, bb_set_state };

// ───── rpos ─────
/*
 * bb_rpos.js — Dynamic Byrd Box: RPOS
 * Direct port of bb_rpos.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_rpos(n) {
    return {
        α() { return (_Δ !== _Ω - n) ? _FAIL : _spec(_Δ, 0); },
        β() { return _FAIL; }
    };
}
module.exports = { bb_rpos, bb_set_state };

// ───── rtab ─────
/*
 * bb_rtab.js — Dynamic Byrd Box: RTAB
 * Direct port of bb_rtab.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_rtab(n) {
    let advance = 0;
    return {
        α() {
            const target = _Ω - n;
            if (_Δ > target) return _FAIL;
            advance = target - _Δ;
            const r = _spec(_Δ, advance); _Δ = target; return r;
        },
        β() { _Δ -= advance; return _FAIL; }
    };
}
module.exports = { bb_rtab, bb_set_state };

// ───── succeed ─────
/*
 * bb_succeed.js — Dynamic Byrd Box: SUCCEED
 * Direct port of bb_succeed.c  |  part of src/runtime/boxes/
 * Author: Claude Sonnet 4.6 — SJ-5, 2026-04-02
 */


function bb_succeed() {
    return {
        α() { return _spec(_Δ, 0); },
        β() { return _spec(_Δ, 0); }
    };
}
module.exports = { bb_succeed, bb_set_state };

