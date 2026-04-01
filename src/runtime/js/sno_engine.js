'use strict';
/*
 * sno_engine.js — SNOBOL4 pattern match engine for JavaScript
 *
 * Architecture: iterative state machine, Clojure match.clj model.
 *
 * Greek variable glossary:
 *   ζ     — frame (7-element array)
 *   Σ/Δ   — subject string + cursor on entry to this frame
 *   σ/δ   — subject string + current cursor
 *   Π     — current pattern node
 *   φ     — child index (ALT/SEQ) or retry state
 *   Ψ     — parent frame stack (inside ζ)
 *   Ω     — backtrack stack
 *   λ     — current node type tag
 *   (action variable is named 'action' — not α, to avoid confusion with α port label)
 *
 * Action signal — α holds one of these runtime values:
 *   'proceed' — enter node fresh, cursor at current position
 *   'succeed' — match succeeded, propagate forward
 *   'recede'  — resume after backtrack from child
 *   'concede' — match failed, restore cursor, propagate back
 *
 * These are INTERPRETER concepts — runtime values passed through the
 * dispatch loop.  They are NOT port addresses.
 *
 * Byrd-box ports (α/β/γ/ω) are COMPILE-TIME labels in the emitted
 * code (emit_js.c, emit_byrd_c.c, x64, JVM, .NET).  They name static
 * entry points wired together at compile time — they never exist as
 * runtime values.  The interpreter simulates the same state machine
 * by making wiring explicit as Ψ/Ω stacks and signals explicit as
 * the 'proceed'/'succeed'/'recede'/'concede' action strings above.
 *
 * Frame is a plain 7-element JS array. Transitions create NEW arrays — the old
 * frame is never mutated. GC reclaims unreachable frames. Ψ inside each frame
 * is shared by reference — correct because frames are immutable after creation.
 *
 * Pattern nodes:
 *   null/undefined  — success sentinel (end of pattern)
 *   string          — literal match
 *   {t:'ALT',  ap:[P1,P2,...]} — alternation (try each child left to right)
 *   {t:'SEQ',  ap:[P1,P2,...]} — sequence
 *   {t:'ANY',  cs:string}      — any char in cs
 *   {t:'NOTANY',cs:string}     — any char not in cs
 *   {t:'SPAN', cs:string}      — 1+ chars in cs (backtrackable)
 *   {t:'BREAK',cs:string}      — 0+ chars not in cs
 *   {t:'ARB'}                  — 0..n chars (shortest first)
 *   {t:'REM'}                  — rest of subject
 *   {t:'LEN', n:int}           — exactly n chars
 *   {t:'POS', n:int}           — assert cursor == n
 *   {t:'RPOS',n:int}           — assert cursor == len-n
 *   {t:'TAB', n:int}           — advance cursor to n
 *   {t:'RTAB',n:int}           — advance cursor to len-n
 *   {t:'FENCE'}                — match empty, cut backtrack
 *   {t:'SUCCEED'}              — always succeed (infinite backtrack)
 *   {t:'FAIL'}                 — always fail
 *   {t:'ABORT'}                — terminate match immediately
 *   {t:'BAL'}                  — balanced parens
 *   {t:'ARBNO',p:node}         — 0+ repetitions of p
 *   {t:'CAPT_IMM', p:node, v:string}  — immediate capture ($ operator)
 *   {t:'CAPT_COND',p:node, v:string}  — conditional capture (. operator)
 *
 * Authors: Lon Jones Cherryholmes (arch), Claude Sonnet 4.6 (impl)
 * Sprint: SJ-5  Milestone: M-SJ-B01
 */

/* ── Frame constructors ──────────────────────────────────────────────────── */
// ζ_make: build a fresh root frame
function ζ_make(S, Π) {
    // [Σ,  Δ, σ,  δ,  Π,  φ, Ψ]
    return [S, 0,  S,  0,  Π,  1, []];
}

// ζ_down: descend into child φ of current node (pushes self onto Ψ)
function ζ_down(ζ) {
    const [Σ,Δ,σ,δ,Π,φ,Ψ] = ζ;
    const child = Array.isArray(Π.ap) ? Π.ap[φ-1] : Π.p;
    const newΨ = Ψ.concat([ζ]);         // structural share: new array, same frames
    return [Σ,Δ,Σ,Δ, child, 1, newΨ];
}

// ζ_down_to: descend into an explicit pattern node (no Ψ push variant)
function ζ_down_to(ζ, node) {
    const [Σ,Δ,σ,δ,Π,φ,Ψ] = ζ;
    const newΨ = Ψ.concat([ζ]);
    return [Σ,Δ,Σ,Δ, node, 1, newΨ];
}

// ζ_up: ascend to parent (pop Ψ, inherit parent's Π/φ, keep current σ/δ)
function ζ_up(ζ, newδ) {
    const [Σ,Δ,σ,δ,Π,φ,Ψ] = ζ;
    const d = (newδ !== undefined) ? newδ : δ;
    if (Ψ.length === 0) return [Σ,Δ,Σ,d, null, 0, []];
    const parent = Ψ[Ψ.length-1];
    const newΨ = Ψ.slice(0,-1);
    return [parent[0], parent[1], Σ, d, parent[4], parent[5], newΨ];
}

// ζ_next: advance φ (move to next child in ALT/SEQ), reset σ/δ to Σ/Δ
function ζ_next_commit(ζ, newδ) {
    // Used by SEQ on child success: commit new position, advance to next child
    const [Σ,Δ,σ,δ,Π,φ,Ψ] = ζ;
    const d = (newδ !== undefined) ? newδ : δ;
    return [Σ,d, Σ,d, Π, φ+1, Ψ];
}

function ζ_next_retry(ζ) {
    // Used by ALT on child fail: keep Σ/Δ, advance to next child
    const [Σ,Δ,σ,δ,Π,φ,Ψ] = ζ;
    return [Σ,Δ, Σ,Δ, Π, φ+1, Ψ];
}

/* ── Pattern type tag ─────────────────────────────────────────────────────── */
function ζλ(ζ) {
    if (!ζ) return null;
    const Π = ζ[4];
    if (Π === null || Π === undefined) return null;
    if (typeof Π === 'string') return 'LIT';
    return Π.t;
}

/* ── Pending conditional captures ────────────────────────────────────────── */
// Array of {v, text} — committed on overall match success
let _pending_cond = [];

/* ── The engine ──────────────────────────────────────────────────────────── */
/*
 * engine(S, Π, anchorStart) → {matched:bool, start:int, end:int, caps:{}}
 *   S           — subject string
 *   Π           — root pattern node
 *   anchorStart — if true, only try position 0 (MATCH); else slide (SEARCH)
 */
function engine(S, Π, anchorStart) {
    const n = S.length;
    const startPositions = anchorStart ? [0] : Array.from({length: n+1}, (_,i)=>i);

    for (const startPos of startPositions) {
        _pending_cond = [];
        const result = engine_ζ(S, n, Π, startPos);
        if (result !== null) {
            // commit conditional captures
            for (const {v, text} of _pending_cond)
                _vars_set(v, text);
            return result;
        }
    }
    return null;
}

/* engine_at: try matching Π against S starting at position startPos */
function engine_ζ(S, n, Π, startPos) {
    let action = 'proceed';
    let ζ = ζ_make(S, Π);
    ζ[1] = startPos; ζ[2] = S; ζ[3] = startPos;  // set Δ=δ=startPos
    let Ω = [];   // backtrack stack: array of saved frames

    while (true) {
        const λ = ζλ(ζ);

        /* ── Terminal: Π is null ─────────────────────────────────────────── */
        if (λ === null) {
            if (action === 'proceed' || action === 'succeed')
                return {matched:true, start:startPos, end:ζ[3]};
            else
                return null;
        }

        switch (λ + '/' + action) {

        /* ── ALT ────────────────────────────────────────────────────────── */
        case 'ALT/proceed':
            if (ζ[5] <= ζ[4].ap.length) {
                Ω.push(ζ);
                ζ = ζ_down(ζ);
                action = 'proceed';
            } else {
                ζ = Ω.length ? Ω.pop() : null;
                action = 'recede';
            }
            break;
        case 'ALT/succeed':
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;
        case 'ALT/concede':
            ζ = ζ_next_retry(ζ);
            action = 'proceed';
            break;
        case 'ALT/recede':
            ζ = ζ_next_retry(ζ);
            action = 'proceed';
            break;

        /* ── SEQ ────────────────────────────────────────────────────────── */
        case 'SEQ/proceed':
            if (ζ[5] <= ζ[4].ap.length) {
                ζ = ζ_down(ζ);
                action = 'proceed';
            } else {
                ζ = ζ_up(ζ);
                action = 'succeed';
            }
            break;
        case 'SEQ/succeed': {
            // child succeeded — commit its cursor, advance to next child
            const childδ = ζ[3];
            ζ = ζ_next_commit(ζ, childδ);
            action = 'proceed';
            break;
        }
        case 'SEQ/concede':
        case 'SEQ/recede':
            if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
            else return null;
            break;

        /* ── LIT ────────────────────────────────────────────────────────── */
        case 'LIT/proceed': {
            const pat = ζ[4];
            const δ   = ζ[3];
            if (δ + pat.length <= n && S.startsWith(pat, δ)) {
                ζ = ζ_up(ζ, δ + pat.length);
                action = 'succeed';
            } else {
                ζ = ζ_up(ζ);
                action = 'concede';
            }
            break;
        }

        /* ── ANY ────────────────────────────────────────────────────────── */
        case 'ANY/proceed': {
            const δ = ζ[3];
            if (δ < n && ζ[4].cs.includes(S[δ])) {
                ζ = ζ_up(ζ, δ+1); action = 'succeed';
            } else {
                ζ = ζ_up(ζ);      action = 'concede';
            }
            break;
        }

        /* ── NOTANY ─────────────────────────────────────────────────────── */
        case 'NOTANY/proceed': {
            const δ = ζ[3];
            if (δ < n && !ζ[4].cs.includes(S[δ])) {
                ζ = ζ_up(ζ, δ+1); action = 'succeed';
            } else {
                ζ = ζ_up(ζ);      action = 'concede';
            }
            break;
        }

        /* ── SPAN ───────────────────────────────────────────────────────── */
        case 'SPAN/proceed': {
            const cs = ζ[4].cs;
            let δ = ζ[3];
            const start = δ;
            while (δ < n && cs.includes(S[δ])) δ++;
            if (δ > start) {
                ζ = ζ_up(ζ, δ); action = 'succeed';
            } else {
                ζ = ζ_up(ζ);    action = 'concede';
            }
            break;
        }

        /* ── BREAK ──────────────────────────────────────────────────────── */
        case 'BREAK/proceed': {
            const cs = ζ[4].cs;
            let δ = ζ[3];
            while (δ < n && !cs.includes(S[δ])) δ++;
            // BREAK succeeds even if 0 chars consumed (stopped at break char or eol)
            // but requires the break char to actually be there (not eol)
            if (δ < n) {
                ζ = ζ_up(ζ, δ); action = 'succeed';
            } else {
                ζ = ζ_up(ζ);    action = 'concede';
            }
            break;
        }

        /* ── ARB ────────────────────────────────────────────────────────── */
        case 'ARB/proceed': {
            // match 0 chars first; push retry frame with φ=1
            const saved = Object.assign([], ζ);
            saved[5] = 1;
            Ω.push(saved);
            ζ = ζ_up(ζ, ζ[3]);   // succeed with 0 chars
            action = 'succeed';
            break;
        }
        case 'ARB/recede': {
            const len = ζ[5];
            const δ   = ζ[1];     // Δ is entry position
            if (δ + len <= n) {
                const saved = Object.assign([], ζ);
                saved[5] = len + 1;
                Ω.push(saved);
                ζ = ζ_up(ζ, δ + len);
                action = 'succeed';
            } else {
                if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
                else return null;
            }
            break;
        }

        /* ── REM ────────────────────────────────────────────────────────── */
        case 'REM/proceed':
            ζ = ζ_up(ζ, n);
            action = 'succeed';
            break;

        /* ── LEN ────────────────────────────────────────────────────────── */
        case 'LEN/proceed': {
            const δ = ζ[3], len = ζ[4].n;
            if (δ + len <= n) { ζ = ζ_up(ζ, δ+len); action = 'succeed'; }
            else              { ζ = ζ_up(ζ);         action = 'concede'; }
            break;
        }

        /* ── POS ────────────────────────────────────────────────────────── */
        case 'POS/proceed': {
            const δ = ζ[3];
            if (δ === ζ[4].n) { ζ = ζ_up(ζ); action = 'succeed'; }
            else               { ζ = ζ_up(ζ); action = 'concede'; }
            break;
        }

        /* ── RPOS ───────────────────────────────────────────────────────── */
        case 'RPOS/proceed': {
            const δ = ζ[3];
            if (n - δ === ζ[4].n) { ζ = ζ_up(ζ); action = 'succeed'; }
            else                   { ζ = ζ_up(ζ); action = 'concede'; }
            break;
        }

        /* ── TAB ────────────────────────────────────────────────────────── */
        case 'TAB/proceed': {
            const δ = ζ[3], target = ζ[4].n;
            if (target >= δ && target <= n) { ζ = ζ_up(ζ, target); action = 'succeed'; }
            else                            { ζ = ζ_up(ζ);          action = 'concede'; }
            break;
        }

        /* ── RTAB ───────────────────────────────────────────────────────── */
        case 'RTAB/proceed': {
            const δ = ζ[3], target = n - ζ[4].n;
            if (target >= δ) { ζ = ζ_up(ζ, target); action = 'succeed'; }
            else              { ζ = ζ_up(ζ);          action = 'concede'; }
            break;
        }

        /* ── FENCE ──────────────────────────────────────────────────────── */
        case 'FENCE/proceed':
            Ω.push('ABORT');    // sentinel: if we backtrack here, abort
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;
        case 'FENCE/recede':
        case 'FENCE/concede':
            return null;        // FENCE cuts — no retry

        /* ── SUCCEED ────────────────────────────────────────────────────── */
        case 'SUCCEED/proceed': {
            const saved = Object.assign([], ζ);
            Ω.push(saved);
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;
        }
        case 'SUCCEED/recede':
            // retry: re-succeed from same position
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;

        /* ── FAIL ───────────────────────────────────────────────────────── */
        case 'FAIL/proceed':
            ζ = ζ_up(ζ);
            action = 'concede';
            break;

        /* ── ABORT ──────────────────────────────────────────────────────── */
        case 'ABORT/proceed':
            return null;

        /* ── BAL ────────────────────────────────────────────────────────── */
        case 'BAL/proceed': {
            const δ = ζ[3];
            let pos = δ, nest = 0;
            let found = -1;
            while (pos < n) {
                const ch = S[pos];
                if (ch === '(') nest++;
                else if (ch === ')') { nest--; if (nest < 0) break; }
                pos++;
                if (nest === 0) { found = pos; break; }
            }
            if (found >= 0) {
                const saved = Object.assign([], ζ);
                saved[5] = found; // retry from here
                Ω.push(saved);
                ζ = ζ_up(ζ, found);
                action = 'succeed';
            } else {
                ζ = ζ_up(ζ); action = 'concede';
            }
            break;
        }
        case 'BAL/recede': {
            // continue scan from saved position
            let pos = ζ[5], nest = 0;
            let found = -1;
            while (pos < n) {
                const ch = S[pos];
                if (ch === '(') nest++;
                else if (ch === ')') { nest--; if (nest < 0) break; }
                pos++;
                if (nest === 0) { found = pos; break; }
            }
            if (found >= 0) {
                const saved = Object.assign([], ζ);
                saved[5] = found;
                Ω.push(saved);
                ζ = ζ_up(ζ, found);
                action = 'succeed';
            } else {
                if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
                else return null;
            }
            break;
        }

        /* ── ARBNO ──────────────────────────────────────────────────────── */
        case 'ARBNO/proceed': {
            // Expand lazily: ALT[ε, SEQ[P, ARBNO(P)]]
            const P = ζ[4].p;
            const expanded = {t:'ALT', ap: [
                '',                                      // ε — match zero reps
                {t:'SEQ', ap: [P, ζ[4]]}                // P then recurse
            ]};
            ζ = ζ_down_to(ζ, expanded);
            action = 'proceed';
            break;
        }
        case 'ARBNO/succeed':
            ζ = ζ_up(ζ); action = 'succeed';
            break;
        case 'ARBNO/concede':
        case 'ARBNO/recede':
            if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
            else return null;
            break;

        /* ── CAPT_IMM ($) ───────────────────────────────────────────────── */
        case 'CAPT_IMM/proceed':
            Ω.push(ζ);
            ζ = ζ_down_to(ζ, ζ[4].p);
            action = 'proceed';
            break;
        case 'CAPT_IMM/succeed': {
            const v    = ζ[4].v;
            const text = S.slice(ζ[1], ζ[3]);   // Δ to δ
            _vars_set(v, text);                   // immediate, unconditional
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;
        }
        case 'CAPT_IMM/concede':
        case 'CAPT_IMM/recede':
            if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
            else return null;
            break;

        /* ── CAPT_COND (.) ──────────────────────────────────────────────── */
        case 'CAPT_COND/proceed':
            Ω.push(ζ);
            ζ = ζ_down_to(ζ, ζ[4].p);
            action = 'proceed';
            break;
        case 'CAPT_COND/succeed': {
            const v    = ζ[4].v;
            const text = S.slice(ζ[1], ζ[3]);
            _pending_cond.push({v, text});         // deferred until overall success
            ζ = ζ_up(ζ);
            action = 'succeed';
            break;
        }
        case 'CAPT_COND/concede':
        case 'CAPT_COND/recede':
            if (Ω.length) { ζ = Ω.pop(); action = 'recede'; }
            else return null;
            break;

        /* ── fallback: generic fail/recede ──────────────────────────────── */
        default:
            if (action === 'concede' || action === 'recede') {
                if (Ω.length) {
                    const top = Ω.pop();
                    if (top === 'ABORT') return null;
                    ζ = top; action = 'recede';
                } else return null;
            } else {
                // unknown node type — treat as fail
                ζ = ζ_up(ζ); action = 'concede';
            }
            break;
        }
    }
}

/* ── Variable set hook (injected by sno_runtime.js) ─────────────────────── */
let _vars_set = (v, text) => { /* stub — overridden by runtime */ };

/* ── Pattern builder helpers (used by emitted code) ─────────────────────── */
function PAT_lit(s)       { return s; }
function PAT_alt(...args) { return {t:'ALT',  ap:args}; }
function PAT_seq(...args) { return {t:'SEQ',  ap:args}; }
function PAT_any(cs)      { return {t:'ANY',  cs}; }
function PAT_notany(cs)   { return {t:'NOTANY',cs}; }
function PAT_span(cs)     { return {t:'SPAN', cs}; }
function PAT_break(cs)    { return {t:'BREAK',cs}; }
function PAT_arb()        { return {t:'ARB'}; }
function PAT_rem()        { return {t:'REM'}; }
function PAT_len(n)       { return {t:'LEN', n}; }
function PAT_pos(n)       { return {t:'POS', n}; }
function PAT_rpos(n)      { return {t:'RPOS',n}; }
function PAT_tab(n)       { return {t:'TAB', n}; }
function PAT_rtab(n)      { return {t:'RTAB',n}; }
function PAT_fence()      { return {t:'FENCE'}; }
function PAT_succeed()    { return {t:'SUCCEED'}; }
function PAT_fail()       { return {t:'FAIL'}; }
function PAT_abort()      { return {t:'ABORT'}; }
function PAT_bal()        { return {t:'BAL'}; }
function PAT_arbno(p)     { return {t:'ARBNO',p}; }
function PAT_capt_imm(p,v){ return {t:'CAPT_IMM', p, v}; }
function PAT_capt_cond(p,v){ return {t:'CAPT_COND',p, v}; }

/* ── Public search/match API ─────────────────────────────────────────────── */
function sno_search(S, Π) { return engine(S, Π, false); }
function sno_match(S, Π)  { return engine(S, Π, true);  }

module.exports = {
    sno_search, sno_match,
    PAT_lit, PAT_alt, PAT_seq, PAT_any, PAT_notany,
    PAT_span, PAT_break, PAT_arb, PAT_rem,
    PAT_len, PAT_pos, PAT_rpos, PAT_tab, PAT_rtab,
    PAT_fence, PAT_succeed, PAT_fail, PAT_abort, PAT_bal,
    PAT_arbno, PAT_capt_imm, PAT_capt_cond,
    _set_vars_hook: (fn) => { _vars_set = fn; },
};
