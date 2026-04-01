#!/usr/bin/env node
/**
 * bench_engine.js — one4all sno_engine vs spipatjs head-to-head benchmark
 *
 * Usage:
 *   node --experimental-vm-modules test/js/bench_engine.js
 *
 * Both engines run the same 8 patterns against the same subjects in the
 * same Node.js process (equal JIT warmup). Reports ops/sec per engine.
 *
 * Sprint: SJ-6  Milestone: MILESTONE-JS-BENCH
 */
'use strict';

const { performance } = require('perf_hooks');
const path = require('path');

const RUNTIME_DIR = path.join(__dirname, '../../src/runtime/js');
const SPIPAT_PATH = path.join(__dirname, '../../../spipatjs/spipat.mjs');

const WARMUP  = 2000;
const MEASURE = 20000;

/* ── Load one4all engine (CJS) ─────────────────────────────────────────── */
const E = require(path.join(RUNTIME_DIR, 'sno_engine.js'));

/* ── Benchmark cases ────────────────────────────────────────────────────── */
// Each case: { id, desc, subject, our_pat_fn, spipat_fn }
// our_pat_fn()  → one4all pattern node (built fresh once)
// spipat_fn(sp) → spipatjs pattern object (built once after import)

const CASES = [
  {
    id: 'B01', desc: 'Literal match',
    subject: 'HELLO WORLD',
    our:   () => 'WORLD',
    spip:  (sp) => sp.pat('WORLD'),
  },
  {
    id: 'B02', desc: 'BREAK+SPAN word scan',
    subject: 'the quick brown fox jumps over the lazy dog',
    our:   () => E.PAT_seq(E.PAT_break(' '), E.PAT_span('abcdefghijklmnopqrstuvwxyz')),
    spip:  (sp) => sp.breakp(' ').and(sp.span('abcdefghijklmnopqrstuvwxyz')),
  },
  {
    id: 'B03', desc: 'ARB backtrack depth 12',
    subject: 'aaaaaaaaaaax',
    our:   () => E.PAT_seq(E.PAT_arb(), 'x'),
    spip:  (sp) => sp.arb.and(sp.pat('x')),
  },
  {
    id: 'B04', desc: 'ARBNO multi-rep',
    subject: 'hello world foo bar baz',
    our:   () => E.PAT_arbno(E.PAT_span('abcdefghijklmnopqrstuvwxyz')),
    spip:  (sp) => sp.arbno(sp.span('abcdefghijklmnopqrstuvwxyz')),
  },
  {
    id: 'B05', desc: 'BAL balanced parens',
    subject: '(a(b(c)d)e)rest',
    our:   () => E.PAT_bal(),
    spip:  (sp) => sp.bal,
  },
  {
    id: 'B06', desc: 'Wide ALT (4 alternatives)',
    subject: 'the quick baz runs fast',
    our:   () => E.PAT_seq(E.PAT_alt('foo','bar','baz','qux'),
                           E.PAT_span('abcdefghijklmnopqrstuvwxyz ')),
    spip:  (sp) => sp.pat('foo').or(sp.pat('bar')).or(sp.pat('baz')).or(sp.pat('qux'))
                     .and(sp.span('abcdefghijklmnopqrstuvwxyz ')),
  },
  {
    id: 'B07', desc: 'Deep SEQ (10 literals)',
    subject: 'abcdefghij_tail',
    our:   () => E.PAT_seq('a','b','c','d','e','f','g','h','i','j'),
    spip:  (sp) => sp.pat('a').and(sp.pat('b')).and(sp.pat('c')).and(sp.pat('d'))
                     .and(sp.pat('e')).and(sp.pat('f')).and(sp.pat('g')).and(sp.pat('h'))
                     .and(sp.pat('i')).and(sp.pat('j')),
  },
  {
    id: 'B08', desc: 'CAPT_IMM capture overhead',
    subject: 'hello world',
    our:   () => E.PAT_capt_imm(E.PAT_span('abcdefghijklmnopqrstuvwxyz'), 'W'),
    spip:  (sp) => { const v = new sp.Var(); return sp.span('abcdefghijklmnopqrstuvwxyz').imm(v); },
  },
];

/* ── Timer ──────────────────────────────────────────────────────────────── */
function bench(label, n, fn) {
    // warmup
    for (let i = 0; i < WARMUP; i++) fn();
    const t0 = performance.now();
    for (let i = 0; i < n; i++) fn();
    const elapsed = performance.now() - t0;
    const ops = Math.round(n / (elapsed / 1000));
    console.log(`${label.padEnd(28)} ${String(ops).padStart(12)} ops/sec   (${elapsed.toFixed(1)}ms)`);
    return ops;
}

/* ── Main ───────────────────────────────────────────────────────────────── */
async function main() {
    // Load spipatjs as ESM
    let sp;
    try {
        sp = await import(SPIPAT_PATH);
    } catch (err) {
        console.error('Could not load spipatjs:', err.message);
        console.error('Expected at:', SPIPAT_PATH);
        process.exit(1);
    }

    // Silence one4all capture writes during bench
    E._set_vars_hook(() => {});

    console.log(`\none4all sno_engine vs spipatjs — ${MEASURE} iterations each`);
    console.log('='.repeat(65));
    console.log(`${'Benchmark'.padEnd(28)} ${'ops/sec'.padStart(12)}   elapsed`);
    console.log('-'.repeat(65));

    const results = [];

    for (const c of CASES) {
        const ourPat  = c.our();
        const spipPat = c.spip(sp);

        const ourOps  = bench(`${c.id} one4all  ${c.desc}`.slice(0,27), MEASURE,
                              () => E.sno_search(c.subject, ourPat));
        const spipOps = bench(`${c.id} spipatjs ${c.desc}`.slice(0,27), MEASURE,
                              () => spipPat.umatch(c.subject));
        const ratio = (ourOps / spipOps).toFixed(2);
        console.log(`    → one4all/spipatjs ratio: ${ratio}x  (>1 = one4all faster)`);
        console.log();
        results.push({ id: c.id, desc: c.desc, ourOps, spipOps, ratio });
    }

    console.log('='.repeat(65));
    console.log('SUMMARY');
    console.log(`${'ID'.padEnd(5)} ${'one4all'.padStart(12)} ${'spipatjs'.padStart(12)} ${'ratio'.padStart(7)}  desc`);
    for (const r of results) {
        const flag = parseFloat(r.ratio) >= 1 ? '✓' : ' ';
        console.log(`${r.id.padEnd(5)} ${String(r.ourOps).padStart(12)} ${String(r.spipOps).padStart(12)} ${String(r.ratio).padStart(7)}x ${flag} ${r.desc}`);
    }

    // Write results file
    const fs = require('fs');
    const outPath = path.join(__dirname, 'bench_engine_results.txt');
    const lines = [
        `bench_engine results — ${new Date().toISOString()}`,
        `node ${process.version}  WARMUP=${WARMUP}  MEASURE=${MEASURE}`,
        '',
        'ID     one4all    spipatjs   ratio  desc',
        ...results.map(r =>
            `${r.id.padEnd(6)} ${String(r.ourOps).padStart(10)} ${String(r.spipOps).padStart(10)}  ${String(r.ratio).padStart(6)}x  ${r.desc}`
        ),
    ];
    fs.writeFileSync(outPath, lines.join('\n') + '\n');
    console.log(`\nResults written to ${outPath}`);
}

main().catch(err => { console.error(err); process.exit(1); });
