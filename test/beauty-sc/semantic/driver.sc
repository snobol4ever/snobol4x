// driver.sc — test driver for semantic.sc (Snocone)
struct link_counter { next, value }
xTrace = 0;
epsilon = '';

procedure InitCounter() { $'#N' = ''; return; }
procedure PushCounter() { $'#N' = link_counter($'#N', 0); PushCounter = .dummy; nreturn; }
procedure IncCounter()  { value($'#N') = value($'#N') + 1; IncCounter = .dummy; nreturn; }
procedure DecCounter()  { value($'#N') = value($'#N') - 1; DecCounter = .dummy; nreturn; }
procedure PopCounter() {
    if (DIFFER($'#N')) { $'#N' = next($'#N'); PopCounter = .dummy; nreturn; } else { freturn; }
}
procedure TopCounter() {
    if (DIFFER($'#N')) { TopCounter = value($'#N'); return; } else { freturn; }
}

procedure nPush() { nPush = epsilon . *PushCounter(); return; }
procedure nInc()  { nInc  = epsilon . *IncCounter();  return; }
procedure nDec()  { nDec  = epsilon . *DecCounter();  return; }
procedure nTop()  { nTop  = TopCounter(); return; }
procedure nPop()  { nPop  = epsilon . *PopCounter();  return; }

&STLIMIT = 1000000;
InitCounter();

if (IDENT(DATATYPE(nPush()), 'PATTERN')) { OUTPUT = 'PASS: 1 nPush=PATTERN'; } else { OUTPUT = 'FAIL: 1'; }
if (IDENT(DATATYPE(nInc()),  'PATTERN')) { OUTPUT = 'PASS: 2 nInc=PATTERN';  } else { OUTPUT = 'FAIL: 2'; }
if (IDENT(DATATYPE(nPop()),  'PATTERN')) { OUTPUT = 'PASS: 3 nPop=PATTERN';  } else { OUTPUT = 'FAIL: 3'; }

if ('' ? nPush()) { } else { OUTPUT = 'FAIL: 4 nPush match'; }
if (EQ(nTop(), 0)) { OUTPUT = 'PASS: 4 nPush match; nTop=0'; } else { OUTPUT = 'FAIL: 4 nTop=' && nTop(); }

if ('' ? nInc()) { } else { OUTPUT = 'FAIL: 5'; }
if (EQ(nTop(), 1)) { OUTPUT = 'PASS: 5 nInc match; nTop=1'; } else { OUTPUT = 'FAIL: 5'; }

if ('' ? nInc()) { } else { OUTPUT = 'FAIL: 6'; }
if (EQ(nTop(), 2)) { OUTPUT = 'PASS: 6 nInc x2; nTop=2'; } else { OUTPUT = 'FAIL: 6'; }

if ('' ? nPush()) { } else { OUTPUT = 'FAIL: 7'; }
if (EQ(nTop(), 0)) { OUTPUT = 'PASS: 7 nested nPush; nTop=0'; } else { OUTPUT = 'FAIL: 7'; }

v = nTop();
if (IDENT(DATATYPE(v), 'INTEGER')) { OUTPUT = 'PASS: 8 nTop INTEGER'; } else { OUTPUT = 'FAIL: 8 ' && DATATYPE(v); }
