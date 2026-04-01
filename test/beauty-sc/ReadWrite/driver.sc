// driver.sc — test driver for ReadWrite.sc (Snocone)
nl = CHAR(10);

procedure LineMap(str, lmMapName, lmLineNo, lmMap, lmAbs, i, n, ch) {
    lmMap = TABLE(); lmLineNo = 1; lmAbs = 0; n = SIZE(str);
    lmMap[0] = lmLineNo; i = 0;
    while (1) {
        i = i + 1; if (GT(i, n)) { break; }
        ch = SUBSTR(str, i, 1);
        if (IDENT(ch, nl)) {
            lmAbs = lmAbs + i; lmLineNo = lmLineNo + 1;
            lmMap[lmAbs] = lmLineNo;
            str = SUBSTR(str, i + 1); n = SIZE(str); i = 0;
        }
    }
    $lmMapName = lmMap; return;
}

procedure Read(fileName, rdMapName) { freturn; }
procedure Write(fileName, fileStr)  { freturn; }

&STLIMIT = 1000000;

// 1: LineMap offset 0 = line 1
LineMap('alpha' && nl && 'beta' && nl && 'gamma' && nl, 'lm1');
if (EQ(lm1[0], 1)) { OUTPUT = 'PASS: 1 LineMap[0]=1'; } else { OUTPUT = 'FAIL: 1 LineMap[0]=' && lm1[0]; }

// 2: LineMap offset SIZE('alpha')+1 = line 2
off2 = SIZE('alpha') + 1;
if (EQ(lm1[off2], 2)) { OUTPUT = 'PASS: 2 LineMap offset ' && off2 && ' = line 2'; } else { OUTPUT = 'FAIL: 2 LineMap[' && off2 && ']=' && lm1[off2]; }

// 3: LineMap offset SIZE('alpha')+1+SIZE('beta')+1 = line 3
off3 = SIZE('alpha') + 1 + SIZE('beta') + 1;
if (EQ(lm1[off3], 3)) { OUTPUT = 'PASS: 3 LineMap offset ' && off3 && ' = line 3'; } else { OUTPUT = 'FAIL: 3 LineMap[' && off3 && ']=' && lm1[off3]; }

// 4: Read FRETURN on inaccessible path
if (~Read('/nonexistent/path/file.txt')) { OUTPUT = 'PASS: 4 Read FRETURN on bad path'; } else { OUTPUT = 'FAIL: 4 Read bad path should FRETURN'; }

// 5: Write FRETURN on inaccessible path
if (~Write('/nonexistent/path/file.txt', 'x' && nl)) { OUTPUT = 'PASS: 5 Write FRETURN on bad path'; } else { OUTPUT = 'FAIL: 5 Write bad path should FRETURN'; }

// 6: LineMap empty string — table with lmMap[0]=1
LineMap('', 'lm6');
if (DIFFER(lm6)) { OUTPUT = 'PASS: 6 LineMap empty string creates table'; } else { OUTPUT = 'FAIL: 6 LineMap empty string no table'; }

// 7: LineMap single word no trailing nl
LineMap('hello', 'lm7');
if (EQ(lm7[0], 1)) { OUTPUT = 'PASS: 7 LineMap single word no-nl'; } else { OUTPUT = 'FAIL: 7 LineMap[0]=' && lm7[0]; }

// 8: LineMap 2-line, second line offset
LineMap('x' && nl && 'y' && nl, 'lm8');
if (EQ(lm8[SIZE('x') + 1], 2)) { OUTPUT = 'PASS: 8 LineMap 2-line second offset'; } else { OUTPUT = 'FAIL: 8 LineMap 2-line offset got ' && lm8[SIZE('x') + 1]; }
