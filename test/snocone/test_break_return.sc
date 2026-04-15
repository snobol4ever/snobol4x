/* test_break_return.sc — break / return / freturn / nreturn test (SC-6)
 *
 * Each construct is exercised inside a while-loop body, working around a
 * known pre-existing limitation where consecutive top-level OUTPUT statements
 * only emit the last value under --ir-run.
 *
 * Ref: 1 2 3 14 12 "nreturn ok"
 */

/* Test 1: break stops loop at i=4 (prints 1, 2, 3) */
i = 1;
while (LE(i, 10)) {
    if (EQ(i, 4)) { break; }
    OUTPUT = i;
    i = i + 1;
}

/* Test 2: return with value — Double(7) = 14 */
procedure Double(n) { Double = n + n; return; }
j = 1;
while (EQ(j, 1)) {
    r = Double(7);
    OUTPUT = r;
    j = j + 1;
}

/* Test 3: freturn — MayFail(4) = 12, freturn path not triggered */
procedure MayFail(n) {
    if (EQ(n, 0)) { freturn; }
    MayFail = n * 3;
    return;
}
k = 1;
while (EQ(k, 1)) {
    r3 = MayFail(4);
    OUTPUT = r3;
    k = k + 1;
}

/* Test 4: nreturn — NullFn returns null (empty string) */
procedure NullFn(n) { nreturn; }
m = 1;
while (EQ(m, 1)) {
    r4 = NullFn(5);
    if (IDENT(r4, "")) { OUTPUT = "nreturn ok"; }
    m = m + 1;
}
