// test_pattern.sc — SC-9 pattern match gate
// Tests: subject ? pattern, ARB, SPAN, BREAK, ANY, LEN, alternation, capture
// .ref generated from equivalent SNOBOL4 under SPITBOL oracle

// 1. Literal string match
x = 'hello world';
if (x ? 'hello') { OUTPUT = 'PASS: 1 literal match'; } else { OUTPUT = 'FAIL: 1'; }

// 2. Literal non-match
if (x ? 'xyz') { OUTPUT = 'FAIL: 2'; } else { OUTPUT = 'PASS: 2 non-match'; }

// 3. ANY
if (x ? ANY('hxz')) { OUTPUT = 'PASS: 3 ANY'; } else { OUTPUT = 'FAIL: 3'; }

// 4. LEN
if (x ? LEN(5)) { OUTPUT = 'PASS: 4 LEN'; } else { OUTPUT = 'FAIL: 4'; }

// 5. SPAN
if (x ? SPAN('abcdefghijklmnopqrstuvwxyz')) { OUTPUT = 'PASS: 5 SPAN'; } else { OUTPUT = 'FAIL: 5'; }

// 6. BREAK
if (x ? BREAK(' ')) { OUTPUT = 'PASS: 6 BREAK'; } else { OUTPUT = 'FAIL: 6'; }

// 7. ARB
if (x ? ARB) { OUTPUT = 'PASS: 7 ARB'; } else { OUTPUT = 'FAIL: 7'; }

// 8. Pattern alternation (|)
p = 'foo' | 'hello';
if (x ? p) { OUTPUT = 'PASS: 8 alternation'; } else { OUTPUT = 'FAIL: 8'; }

// 9. Conditional capture (.)
x = 'hello world';
if (x ? (SPAN('abcdefghijklmnopqrstuvwxyz') . word)) { OUTPUT = 'PASS: 9 capture word=' && word; } else { OUTPUT = 'FAIL: 9'; }
