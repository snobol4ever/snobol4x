/* test_for.sc — for loop lowering test
 * Ref generated from equivalent SNOBOL4 under SPITBOL oracle.
 */

/* Test 1: count 1..5 */
for (i = 1; LE(i, 5); i = i + 1) {
    OUTPUT = i;
}

/* Test 2: count down 10..1 */
for (i = 10; GE(i, 1); i = i - 1) {
    OUTPUT = i;
}

/* Test 3: step by 2, sum 0+2+4+6+8 = 20 */
s = 0;
for (i = 0; LE(i, 8); i = i + 2) {
    s = s + i;
}
OUTPUT = s;
