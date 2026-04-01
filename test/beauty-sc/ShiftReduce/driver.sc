// driver.sc — test driver for ShiftReduce.sc (Snocone)
struct tree { t, v, n, c }
struct link { next, value }
xTrace = 0;

procedure InitStack() { $'@S' = ''; return; }
procedure Push(x) {
    $'@S' = link($'@S', x);
    if (IDENT(x, '')) { Push = .value($'@S'); nreturn; }
    else { Push = .dummy; nreturn; }
}
procedure Pop(var) {
    if (~DIFFER($'@S')) { freturn; }
    if (IDENT(var, '')) { Pop = value($'@S'); $'@S' = next($'@S'); return; }
    else { $var = value($'@S'); $'@S' = next($'@S'); Pop = .dummy; nreturn; }
}
procedure Top() {
    if (~DIFFER($'@S')) { freturn; }
    Top = .value($'@S'); nreturn;
}

procedure Shift(t, v) {
    _s = tree(t, v, 0, '');
    Push(_s);
    if (IDENT(v, '')) { Shift = .value(_s); nreturn; }
    else { Shift = .dummy; nreturn; }
}
procedure Reduce(t, n, c, i, r) {
    Reduce = .dummy;
    if (GE(n, 1)) { c = ARRAY('1:' && n); } else { c = ''; }
    i = n + 1;
    while (GT(i, 1)) { i = i - 1; c[i] = Pop(''); }
    r = tree(t, '', n, c);
    Push(r);
    nreturn;
}

&STLIMIT = 1000000;

// 1: Shift leaf
InitStack();
Shift('Id', 'foo');
nd = Top();
if (IDENT(t(nd),'Id') && IDENT(v(nd),'foo')) { OUTPUT = 'PASS: 1 Shift leaf'; } else { OUTPUT = 'FAIL: 1'; }

// 2: Shift two + Reduce(2)
InitStack();
Shift('Id', 'x'); Shift('Int', '42');
Reduce('BinOp', 2);
nd = Top();
if (IDENT(t(nd),'BinOp') && IDENT(n(nd),2) && IDENT(t(c(nd)[1]),'Id') && IDENT(t(c(nd)[2]),'Int')) {
    OUTPUT = 'PASS: 2 Reduce 2 children';
} else { OUTPUT = 'FAIL: 2'; }

// 3: Reduce(0)
InitStack();
Reduce('Epsilon', 0);
nd = Top();
if (IDENT(t(nd),'Epsilon') && IDENT(n(nd),0)) { OUTPUT = 'PASS: 3 Reduce 0 children'; } else { OUTPUT = 'FAIL: 3'; }

// 4: Shift empty value
InitStack();
Shift('Keyword', '');
nd = Top();
if (IDENT(t(nd),'Keyword') && IDENT(v(nd),'')) { OUTPUT = 'PASS: 4 Shift empty value'; } else { OUTPUT = 'FAIL: 4'; }

// 5: Shift 3, Reduce(3) — correct child order
InitStack();
Shift('A','a'); Shift('B','b'); Shift('C','c');
Reduce('List', 3);
nd = Top();
if (IDENT(n(nd),3) && IDENT(t(c(nd)[1]),'A') && IDENT(t(c(nd)[2]),'B') && IDENT(t(c(nd)[3]),'C')) {
    OUTPUT = 'PASS: 5 Reduce 3 children order';
} else { OUTPUT = 'FAIL: 5'; }
