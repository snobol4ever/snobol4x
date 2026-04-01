// driver.sc — test driver for FENCE (Snocone)
// FENCE is builtin — no include needed.
&STLIMIT = 1000000;

// 1: FENCE in alternation — gamma path (LEN(1)) taken, FENCE never tried
if ('ab' ? (LEN(1) . X | FENCE)) {
    OUTPUT = 'PASS: FENCE alt gamma';
} else {
    OUTPUT = 'FAIL: FENCE alt failed';
}

// 2: FENCE alone as match — SPITBOL semantics: seals, match reports success then fails backtrack
// Our runtime (SPITBOL-compatible): 'x' ? FENCE succeeds on first pass
if ('x' ? FENCE) {
    OUTPUT = 'FAIL: FENCE should not succeed as subject match';
} else {
    OUTPUT = 'PASS: FENCE alone fails match';
}
