// driver.sc — combined beauty subsystem driver
// Assembled by util_assemble_beauty_driver.sh — do not edit by hand.
// Source order: global case io assign match counter stack tree
//               ShiftReduce Gen Qize ReadWrite TDump XDump semantic omega trace
//               beauty (main body)

// === shared struct declarations ===
struct link { next, value }
struct tree { t, v, n, c }

// === required globals ===
xTrace      = 0;
t8Max       = 0;
txOfs       = 0;
tz          = '';
tx          = '';
dummy       = '';
doParseTree = 0;
epsilon     = '';

// === global ===

nul       = CHAR(0);
bs        = CHAR(8);
ht        = CHAR(9);
tab       = CHAR(9);
nl        = CHAR(10);
lf        = CHAR(10);
vt        = CHAR(11);
ff        = CHAR(12);
cr        = CHAR(13);
fSlash    = CHAR(47);
semicolon = CHAR(59);
bSlash    = CHAR(92);
TRUE   = 1;
FALSE  = 0;
digits = '0123456789';
UTF = TABLE();
UTF[CHAR(194) && CHAR(169)] = 'COPYRIGHT_SIGN';
UTF[CHAR(194) && CHAR(174)] = 'REGISTERED_SIGN';
UTF[CHAR(226) && CHAR(128) && CHAR(148)] = 'EM_DASH';
UTF_Array = SORT(UTF);
_utf_n = SIZE(UTF_Array);
i = 0;
while (1) {
    i = i + 1;
    if (GT(i, _utf_n)) { break; }
    _nm = UTF_Array[i, 2];
    $_nm = UTF_Array[i, 1];
}
UTF_Array = '';
_utf_n = '';
i = '';
_nm = '';


// === case ===

procedure lwr(s) { lwr = REPLACE(s, &UCASE, &LCASE); return; }
procedure upr(s) { upr = REPLACE(s, &LCASE, &UCASE); return; }
procedure cap(s) {
    cap = REPLACE(SUBSTR(s,1,1), &LCASE, &UCASE) && REPLACE(SUBSTR(s,2), &UCASE, &LCASE);
    return;
}
procedure icase(str,   letter, character) {
    if (~DIFFER(str)) { return; }
    if (str ? (POS(0) && ANY(&UCASE && &LCASE) . letter && '')) {
        icase = icase(upr(letter) | lwr(letter));
        goto icase_loop;
    }
    str ? (POS(0) && LEN(1) . character && '');
    icase = icase && character;
    icase_loop:
    icase = icase(str);
    return;
}

// === io ===
// Under scrip/one4all FENCE is a function (not a built-in pattern), so the
// OPSYN remapping guard fires and we skip the remap — INPUT/OUTPUT are
// already built-in functions in scrip.  This file is a no-op stub that
// defines input_/output_ for completeness but does not remap INPUT/OUTPUT.

procedure input_(name, channel, fileName) {
    input_ = INPUT(name, channel, fileName);
    return;
}

procedure output_(name, channel, fileName) {
    output_(name, channel, fileName);
    return;
}

// === assign ===

procedure assign(name, expression) {
    assign = .dummy;
    if (IDENT(DATATYPE(expression), 'EXPRESSION')) {
        $name = EVAL(expression);
        nreturn;
    }
    $name = expression;
    nreturn;
}


// === match ===

procedure match(subject, pattern) {
    match = .dummy;
    if (subject ? pattern) { nreturn; } else { freturn; }
}

procedure notmatch(subject, pattern) {
    notmatch = .dummy;
    if (subject ? pattern) { freturn; } else { nreturn; }
}


// === counter ===

struct link_counter { next, value }
xTrace = 0;

procedure InitCounter() { $'#N' = ''; return; }
procedure PushCounter() { $'#N' = link_counter($'#N', 0); PushCounter = .dummy; nreturn; }
procedure IncCounter()  { value($'#N') = value($'#N') + 1; IncCounter = .dummy; nreturn; }
procedure DecCounter()  { value($'#N') = value($'#N') - 1; DecCounter = .dummy; nreturn; }
procedure PopCounter() {
    if (DIFFER($'#N')) { $'#N' = next($'#N'); PopCounter = .dummy; nreturn; }
    else { freturn; }
}
procedure TopCounter() {
    if (DIFFER($'#N')) { TopCounter = value($'#N'); return; }
    else { freturn; }
}


// === stack ===
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
    Top = .value($'@S');
    nreturn;
}


// === tree ===

procedure MakeLeaf(type, val) { MakeLeaf = tree(type, val, 0, ''); return; }
procedure MakeNode(type, val, nc, kids) { MakeNode = tree(type, val, nc, kids); return; }


// === ShiftReduce ===
xTrace = 0;


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


// === Gen ===
_indent = DUPL(' ', 120);
$'#L' = 0;
$'$B' = '';
$'$C' = '';
$'$X' = '';

procedure IncLevel(delta) {
    IncLevel = .dummy;
    if (~DIFFER(delta)) { delta = 2; }
    $'#L' = $'#L' + delta;
    nreturn;
}
procedure DecLevel(delta) {
    DecLevel = .dummy;
    if (~DIFFER(delta)) { delta = 2; }
    $'#L' = $'#L' - delta;
    nreturn;
}
procedure SetLevel(level) { SetLevel = .dummy; $'#L' = level; nreturn; }
procedure GetLevel()      { GetLevel = $'#L'; return; }

procedure Gen(str, outNm,   ind, outline) {
    Gen = .dummy;
    if (~DIFFER(outNm)) { outNm = .OUTPUT; }
    ind = '';
    if (GT($'#L', 0)) {
        _indent ? (POS(0) && LEN($'#L' - SIZE($'$X')) . ind);
    }
    if (DIFFER($'$B')) {
        $'$B' = $'$B' && str;
    } else {
        $'$B' = $'$X' && ind && str;
    }
    gen_flush:
    if (~($'$B' ? (POS(0) && BREAK(nl) . outline && nl && ''))) { nreturn; }
    $'$X' = $'$C';
    $outNm = outline;
    gen_more:
    if (~($'$B' ? (POS(0) && BREAK(nl) . outline && nl && ''))) { nreturn; }
    $outNm = $'$C' && ind && outline;
    goto gen_more;
}
procedure GenTab(pos,   p) {
    GenTab = .dummy;
    if (~DIFFER(pos)) { pos = $'#L'; }
    if (IDENT($'$B')) {
        $'$B' = $'$X' && ' ' && DUPL(' ', pos - SIZE($'$X') - 1);
        nreturn;
    }
    if (LE(SIZE($'$B'), pos - 1)) {
        $'$B' = $'$B' && ' ' && DUPL(' ', pos - SIZE($'$B') - 1);
    } else {
        $'$B' = $'$B' && ' ';
    }
    nreturn;
}
procedure GenSetCont(cont) {
    GenSetCont = .dummy;
    $'$X' = '';
    $'$C' = cont;
    nreturn;
}

// === Qize ===
// SQize(str)   — single-quote-ize
// DQize(str)   — double-quote-ize
// SqlSQize(str)— SQL single-quote escape
// Intize(str)  — parse JSON-style quoted string into raw string
// Extize(str)  — stub (no-op)
// LEQ(a,b)     — lexicographic <=: returns a if a<=b, else freturn

procedure LEQ(a, b) {
    if (~DIFFER(a, b)) { LEQ = a; return; }   // equal => success
    if (LLT(a, b))     { LEQ = a; return; }   // a < b  => success
    freturn;
}

procedure Ucvt(hex2) {
    // Convert 2-hex-digit string to single character via CODE/CHAR
    Ucvt = CHAR(INTEGER('0X' && hex2));
    return;
}

procedure Qize(str,   part) {
    if (~DIFFER(str)) { Qize = "''"; return; }
    Qize_loop:
    if (~DIFFER(str)) { return; }
    if (DIFFER(Qize)) { Qize = Qize && ' '; }
    // try special char
    if (str ? (POS(0) && (bSlash | bs | ff | nl | cr | tab) . part && '')) {
        if      (IDENT(part, bSlash)) { Qize = Qize && 'bSlash'; }
        else if (IDENT(part, bs))     { Qize = Qize && 'bs'; }
        else if (IDENT(part, ff))     { Qize = Qize && 'ff'; }
        else if (IDENT(part, nl))     { Qize = Qize && 'nl'; }
        else if (IDENT(part, cr))     { Qize = Qize && 'cr'; }
        else if (IDENT(part, tab))    { Qize = Qize && 'tab'; }
        goto Qize_loop;
    }
    // try single-quote segment
    if (str ? (POS(0) && (BREAK('"' && "'" && bSlash && bs && ff && nl && cr && tab)
               && '"' && ARBNO(NOTANY("'" && bSlash && bs && ff && nl && cr && tab))) . part
               && RTAB(0) . str)) {
        Qize = Qize && "'" && part && "'";
        goto Qize_loop;
    }
    // try double-quote segment
    if (str ? (POS(0) && (BREAK("'" && '"' && bSlash && bs && ff && nl && cr && tab)
               && "'" && ARBNO(NOTANY('"' && bSlash && bs && ff && nl && cr && tab))) . part
               && RTAB(0) . str)) {
        Qize = Qize && '"' && part && '"';
        goto Qize_loop;
    }
    // plain break segment
    if (str ? (POS(0) && BREAK(bSlash && bs && ff && nl && cr && tab) . part && '')) {
        Qize = Qize && "'" && part && "'";
        goto Qize_loop;
    }
    // remainder
    str ? (POS(0) && REM . part && '');
    Qize = Qize && "'" && part && "'";
    str = '';
    goto Qize_loop;
}

procedure SQize(str,   part) {
    if (~DIFFER(str)) { return; }
    SQize_loop:
    if (~DIFFER(str)) { return; }
    if (DIFFER(SQize)) { SQize = SQize && ' '; }
    if (str ? (POS(0) && BREAK("'") . part && "'" && '')) {
        SQize = SQize && "'" && part && "'" && ' "' && "'" && '"';
        goto SQize_loop;
    }
    str ? (POS(0) && REM . part && '');
    SQize = SQize && "'" && part && "'";
    str = '';
    goto SQize_loop;
}

procedure DQize(str,   part) {
    if (~DIFFER(str)) { return; }
    DQize_loop:
    if (~DIFFER(str)) { return; }
    if (DIFFER(DQize)) { DQize = DQize && ' '; }
    if (str ? (POS(0) && BREAK('"') . part && '"' && '')) {
        DQize = DQize && '"' && part && '"' && " '" && '"' && "'";
        goto DQize_loop;
    }
    str ? (POS(0) && REM . part && '');
    DQize = DQize && '"' && part && '"';
    str = '';
    goto DQize_loop;
}

procedure SqlSQize(str,   part) {
    if (~DIFFER(str)) { return; }
    SqlSQize_loop:
    if (str ? (POS(0) && BREAK("'") . part && "'" && '')) {
        SqlSQize = SqlSQize && part && "''";
        goto SqlSQize_loop;
    }
    str ? (POS(0) && REM . part && '');
    SqlSQize = SqlSQize && part;
    str = '';
}

procedure Intize(qqstr,   iq, qqdlm) {
    // Parse JSON/SNOBOL4 quoted string literal → raw string value
    if (~(qqstr ? (POS(0) && ("'" | '"') . qqdlm))) { freturn; }
    Intize_loop:
    if (qqstr ? (POS(0) && bSlash)) {
        if      (qqstr ? (POS(0) && bSlash && ''))  { Intize = Intize && bSlash; goto Intize_loop; }
        else if (qqstr ? (POS(0) && '"'    && ''))   { Intize = Intize && '"';    goto Intize_loop; }
        else if (qqstr ? (POS(0) && "'"    && ''))   { Intize = Intize && "'";    goto Intize_loop; }
        else if (qqstr ? (POS(0) && 'b'    && ''))   { Intize = Intize && bs;     goto Intize_loop; }
        else if (qqstr ? (POS(0) && 'f'    && ''))   { Intize = Intize && ff;     goto Intize_loop; }
        else if (qqstr ? (POS(0) && 'n'    && ''))   { Intize = Intize && lf;     goto Intize_loop; }
        else if (qqstr ? (POS(0) && 'r'    && ''))   { Intize = Intize && cr;     goto Intize_loop; }
        else if (qqstr ? (POS(0) && 't'    && ''))   { Intize = Intize && tab;    goto Intize_loop; }
        else if (qqstr ? (POS(0) && 'u' && '00' && LEN(2) . iq && '')) {
            Intize = Intize && Ucvt(iq); goto Intize_loop;
        }
        else if (qqstr ? (POS(0) && 'u' && LEN(4) . iq && '')) {
            Intize = Intize && bSlash && 'u' && iq; goto Intize_loop;
        }
        goto Intize_loop;
    }
    if (qqstr ? (POS(0) && BREAK(*qqdlm && bSlash) . iq && '')) {
        Intize = Intize && iq; goto Intize_loop;
    }
    if (~(qqstr ? (POS(0) && *qqdlm && RPOS(0)))) { freturn; }
    return;
}

procedure Extize(str) {
    // stub — no-op
    return;
}

// === ReadWrite ===
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


// === TDump ===
// TValue(x)        — render leaf/node value as string
// TLump(x, len)    — build string rep of tree (freturn if too long)

procedure TValue(x,   i) {
    if (~DIFFER(v(x))) { TValue = '.'; return; }
    if (IDENT(t(x), 'Name'))      { TValue = v(x); return; }
    if (IDENT(t(x), 'float'))     { TValue = v(x); return; }
    if (IDENT(t(x), 'integer'))   { TValue = v(x); return; }
    if (IDENT(t(x), 'bool'))      { TValue = v(x); return; }
    if (IDENT(t(x), 'datetime'))  { TValue = "'" && SqlSQize(v(x)) && "'"; return; }
    if (IDENT(t(x), 'character')) { TValue = "'" && SqlSQize(v(x)) && "'"; return; }
    if (IDENT(t(x), 'string'))    { TValue = "'" && SqlSQize(v(x)) && "'"; return; }
    if (IDENT(t(x), 'identifier')){ TValue = v(x); return; }
    if (DIFFER(t(x)))             { TValue = t(x); return; }
    // composite: join child values
    i = 0;
    TValue_loop:
    i = LT(i, n(x)) i + 1;
    if (~DIFFER(i)) { return; }
    TValue = TValue && (DIFFER(TValue) '.', '') && v(c(x)[i]);
    goto TValue_loop;
}

procedure TDump(x, outNm,   i, _t) {
    TDump = .dummy;
    if (~DIFFER(outNm)) { outNm = .OUTPUT; }
    if (IDENT(REPLACE(DATATYPE(x), &LCASE, &UCASE), 'NAME')) { x = $x; }
    Gen(TLump(x, 140 - GetLevel()) && nl, outNm);
    if (~DIFFER(TLump)) { return; }  // TLump succeeded — single line done
    // TLump failed (too long) — multi-line
    if (~IDENT(REPLACE(DATATYPE(x), &LCASE, &UCASE), 'TREE')) {
        Gen(TValue(x) && nl, outNm); return;
    }
    // check if type name is plain identifier
    if (t(x) ? (POS(0) && ANY(&UCASE && &LCASE)
                && (SPAN(digits && &UCASE && '_' && &LCASE) | epsilon) && RPOS(0))) {
        _t = t(x);
    } else {
        _t = '"' && t(x) && '"';
    }
    Gen('(' && _t && nl, outNm);
    IncLevel();
    i = 0;
    TDump_loop:
    i = LT(i, n(x)) i + 1;
    if (~DIFFER(i)) { goto TDump_done; }
    TDump(c(x)[i], outNm);
    goto TDump_loop;
    TDump_done:
    DecLevel();
    Gen(')' && nl, outNm);
    return;
}

procedure TLump(x, len,   i, _t) {
    if (~GT(len, 0)) { freturn; }
    if (~DIFFER(x)) { TLump = '()'; return; }
    if (~IDENT(REPLACE(DATATYPE(x), &LCASE, &UCASE), 'TREE')) {
        TLump = TValue(x);
        if (~LE(SIZE(TLump), len)) { freturn; }
        return;
    }
    TLump = '(';
    if (t(x) ? (POS(0) && ANY(&UCASE && &LCASE)
                && (SPAN(digits && &UCASE && '_' && &LCASE) | epsilon) && RPOS(0))) {
        _t = t(x);
    } else {
        _t = '"' && t(x) && '"';
    }
    TLump = TLump && _t;
    i = 0;
    TLump_loop:
    i = LT(i, n(x)) i + 1;
    if (~DIFFER(i)) { goto TLump_done; }
    _child = TLump(c(x)[i], len - SIZE(TLump) - 2);
    if (~DIFFER(_child)) { TLump = TLump && ' ' && _child; goto TLump_loop; }
    freturn;
    TLump_done:
    TLump = TLump && ')';
    return;
}

// === XDump ===

procedure XDump(object, nm,   i, iMax, iMin, objArr, objField, objKey, objKeyNm,
                               objType, objVal) {
    objType = REPLACE(DATATYPE(object), &LCASE, &UCASE);
    if (IDENT(objType, 'CODE'))       { OUTPUT = nm && ' = ' && objType; return; }
    if (IDENT(objType, 'EXPRESSION')) { OUTPUT = nm && ' = ' && objType; return; }
    if (IDENT(objType, 'INTEGER'))    { OUTPUT = nm && ' = ' && object;  return; }
    if (IDENT(objType, 'NAME'))       { OUTPUT = nm && ' = ' && objType; return; }
    if (IDENT(objType, 'PATTERN'))    { OUTPUT = nm && ' = ' && objType; return; }
    if (IDENT(objType, 'REAL'))       { OUTPUT = nm && ' = ' && object;  return; }
    if (IDENT(objType, 'STRING'))     { OUTPUT = nm && ' = ' && Qize(object); return; }
    if (IDENT(objType, 'ARRAY')) {
        objProto = PROTOTYPE(object);
        objProto ? (POS(0) && (('+' | '-' | epsilon) && SPAN(digits)) . iMin && ':'
                            && (('+' | '-' | epsilon) && SPAN(digits)) . iMax && RPOS(0));
        OUTPUT = nm && " = ARRAY['" && objProto && "']";
        i = iMin - 1;
        XDump_array:
        i = LT(i, iMax) i + 1;
        if (~DIFFER(i)) { return; }
        XDump(object[i], nm && '[' && i && ']');
        goto XDump_array;
    }
    if (IDENT(objType, 'TABLE')) {
        OUTPUT = nm && ' = TABLE';
        objArr = SORT(object);
        if (~DIFFER(objArr)) { return; }
        i = 0;
        XDump_table:
        i = i + 1;
        objKey = objArr[i, 1];
        if (~DIFFER(objKey)) { return; }
        objVal = objArr[i, 2];
        if (IDENT(REPLACE(DATATYPE(objKey), &LCASE, &UCASE), 'INTEGER')) {
            objKeyNm = objKey;
        } else if (IDENT(REPLACE(DATATYPE(objKey), &LCASE, &UCASE), 'STRING')) {
            objKeyNm = Qize(objKey);
        } else {
            objKeyNm = DATATYPE(objKey);
        }
        XDump(objVal, nm && '[' && objKeyNm && ']');
        goto XDump_table;
    }
    // user-defined DATA type
    OUTPUT = nm && ' = ' && objType && '()';
    i = 0;
    XDump_data:
    i = i + 1;
    objField = FIELD(objType, i);
    if (~DIFFER(objField)) { return; }
    XDump(APPLY(objField, object), objField && '(' && nm && ')');
    goto XDump_data;
}

// === semantic ===
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


// === omega ===
// TV/TW/TX: build pattern with case-folded string capture + LEQ guard
// TY/TZ:    build pattern with T8Trace instrumentation (xTrace>0 path)

procedure TV(lvl, pat, name,   omega) {
    if (~DIFFER(doParseTree)) {
        omega = pat;
    } else {
        omega = '(' && pat && " ~ 'identifier')";
    }
    omega = omega && " $ tx *LEQ(lwr(tx), '" && lwr(name) && "')";
    TV = TZ(lvl, name, EVAL(omega));
    if (DIFFER(TV)) { return; } else { freturn; }
}

procedure TW(lvl, pat, name,   omega) {
    if (~DIFFER(doParseTree)) {
        omega = pat;
    } else {
        omega = '(' && pat && " ~ 'identifier')";
    }
    omega = omega && " $ tx *LEQ(upr(tx), '" && upr(name) && "')";
    TW = TZ(lvl, name, EVAL(omega));
    if (DIFFER(TW)) { return; } else { freturn; }
}

procedure TX(lvl, pat, name,   omega) {
    if (~DIFFER(doParseTree)) {
        omega = pat;
    } else {
        omega = '(' && pat && " ~ 'identifier')";
    }
    omega = omega && " $ tx *LEQ(tx, '" && name && "')";
    TX = TZ(lvl, name, EVAL(omega));
    if (DIFFER(TX)) { return; } else { freturn; }
}

procedure TY(lvl, name, pat,   omega) {
    if (LE(xTrace, 0)) {
        TY = pat && @txOfs . *assign(.t8Max, *(GT(txOfs, t8Max) txOfs));
        return;
    }
    omega = pat && " $ tz @txOfs $ *T8Trace(" && lvl && ", " && Qize(name && ': ') && " tz, txOfs)";
    TY = EVAL(omega);
    if (DIFFER(TY)) { return; } else { freturn; }
}

procedure TZ(lvl, name, pat,   omega) {
    if (LE(xTrace, 0)) {
        TZ = pat && @txOfs . *assign(.t8Max, *(GT(txOfs, t8Max) txOfs));
        return;
    }
    omega = "@txOfs $ *T8Trace(" && lvl && ", '?' " && Qize(name) && ", txOfs)"
         && " " && pat && " $ tz @txOfs $ *T8Trace(" && lvl && ", " && Qize(name && ': ') && " tz, txOfs)";
    TZ = EVAL(omega);
    if (DIFFER(TZ)) { return; } else { freturn; }
}

// === trace ===
strOfs = 0; t8Max = 0; t8MaxLine = 0; t8MaxLast = 0; doDebug = 0; t8Map = '';

procedure T8Pos(t8Ofs, _map, i) {
    if (IDENT(_map, '')) { T8Pos = LPAD(t8Ofs, 8); return; }
    i = t8Ofs;
    if (GT(t8Ofs, t8Max)) { t8Max = t8Ofs; }
    while (1) {
        if (~IDENT(_map[i], '')) { break; }
        i = i - 1;
        if (LT(i, 0)) { T8Pos = LPAD(t8Ofs, 8); return; }
    }
    t8Line = _map[i];
    t8Pos  = t8Ofs - i + 1;
    i = t8Max;
    while (1) {
        if (~IDENT(_map[i], '')) { break; }
        i = i - 1;
        if (LT(i, 0)) { T8Pos = LPAD(t8Ofs, 8); return; }
    }
    t8MaxLine = _map[i];
    t8MaxPos  = t8Max - i + 1;
    T8Pos = '(' && LPAD(t8MaxLine, 5) && ', ' && LPAD(t8MaxPos, 3) &&
            ', ' && LPAD(t8Line, 5)   && ', ' && LPAD(t8Pos, 3) && ')';
    return;
}

procedure T8Trace(lvl, str, ofs) {
    T8Trace = .dummy;
    if (~GT(doDebug, 0)) { nreturn; }
    if (~LE(lvl, doDebug)) { nreturn; }
    if (~GT(doDebug, 1)) {
        if (str ? (POS(0) && '?')) { nreturn; }
        nreturn;
    }
    if (str ? (POS(0) && '?')) {
        str = '? ' && SUBSTR(str, 2);
    } else {
        str = '  ' && str;
    }
    _t8p = T8Pos(strOfs + ofs, t8Map);
    if (~GE(t8MaxLine, 621)) { nreturn; }
    if (GE(t8Max, t8MaxLast)) { t8MaxLast = t8Max; }
    OUTPUT = _t8p && str;
    nreturn;
}

dSTRING = DATATYPE('');

r1 = T8Pos(5, '');
if (IDENT(r1, '       5')) { OUTPUT = 'PASS: 1 T8Pos nil map=LPAD'; } else { OUTPUT = 'FAIL: 1 [' && r1 && ']'; }

t8Map2 = TABLE(); t8Map2[0] = 1; t8Map2[5] = 2; t8Max = 0;
r2 = T8Pos(7, t8Map2);
if (IDENT(r2, '(    2,   3,     2,   3)')) { OUTPUT = 'PASS: 2 T8Pos map line/col'; } else { OUTPUT = 'FAIL: 2 [' && r2 && ']'; }

t8Map3 = TABLE(); t8Map3[0] = 1; t8Max = 0;
T8Pos(12, t8Map3);
if (EQ(t8Max, 12)) { OUTPUT = 'PASS: 3 T8Pos updates t8Max'; } else { OUTPUT = 'FAIL: 3 t8Max=' && t8Max; }

doDebug = 0;
r4 = T8Trace(1, 'hello', 0);
if (IDENT(DATATYPE(r4), dSTRING)) { OUTPUT = 'PASS: 4 T8Trace doDebug=0 returns STRING'; } else { OUTPUT = 'FAIL: 4'; }

doDebug = 1; t8Max = 0; t8MaxLine = 0; strOfs = 0; t8Map = '';
r5 = T8Trace(2, 'skip', 0);
if (IDENT(DATATYPE(r5), dSTRING)) { OUTPUT = 'PASS: 5 T8Trace lvl>doDebug NRETURN'; } else { OUTPUT = 'FAIL: 5'; }

doDebug = 1; t8Max = 0; t8MaxLine = 621; t8MaxLast = 0; strOfs = 0; t8Map = '';
r6 = T8Trace(1, '?x', 0);
if (IDENT(DATATYPE(r6), dSTRING)) { OUTPUT = 'PASS: 6 T8Trace ?-prefix doDebug=1 NRETURN'; } else { OUTPUT = 'FAIL: 6'; }

doDebug = 2; t8Max = 0; t8MaxLine = 0; t8MaxLast = 0; strOfs = 0; t8Map = '';
r7 = T8Trace(1, 'blocked', 0);
if (IDENT(DATATYPE(r7), dSTRING)) { OUTPUT = 'PASS: 7 T8Trace t8MaxLine<621 NRETURN'; } else { OUTPUT = 'FAIL: 7'; }

doDebug = 2; t8Max = 0; t8MaxLine = 621; t8MaxLast = 0; strOfs = 0; t8Map = '';
OUTPUT = '--- test 8 output follows ---';
T8Trace(1, '?node', 0);
OUTPUT = 'PASS: 8 T8Trace doDebug=2 ?-expand output';

doDebug = 2; t8Max = 10; t8MaxLine = 621; t8MaxLast = 5; strOfs = 0; t8Map = '';
T8Trace(1, 'upd', 0);
if (EQ(t8MaxLast, 10)) { OUTPUT = 'PASS: 9 t8MaxLast updated to t8Max'; } else { OUTPUT = 'FAIL: 9 t8MaxLast=' && t8MaxLast; }

// === beauty main body ===
// ShiftReduce/TDump/Gen/Qize/ReadWrite/XDump/semantic/omega/trace) supplied via

&FULLSCAN  = 1;
&MAXLNGTH  = 524288;

ppStop   = ARRAY('1:4');
ppStop[1] = 15;  ppStop[2] = 30;  ppStop[3] = 33;  ppStop[4] = 80;
ppSmBump  = 5;   ppLgBump  = 42;

//  Argument parsing
ppArgs        = HOST(0);
ppTokBreakEq  = BREAK('= ');
ppTokBreakSp  = BREAK(' ');
ppTokNamePat  = *ppTokBreakEq | REM;
ppTokValPat   = *ppTokBreakSp | REM;

while (1) {
    ppArgs ? (POS(0) && SPAN(' ') && '') = ;
    if (~DIFFER(ppArgs)) { break; }
    // Positive match form avoids if(~(s ? *deref . cap)) emitter underflow
    ppTokName = '';
    ppArgs ? ('--' && *ppTokNamePat . ppTokName) = ;
    if (IDENT(ppTokName)) { break; }   // no --name found
    ppTokVal = '';
    if (ppArgs ? (POS(0) && '=')) { ppArgs ? ('=' && *ppTokValPat . ppTokVal) = ; }
    if      (IDENT(ppTokName, 'micro'))  { ppStop[1]=11; ppStop[2]=26; ppStop[3]=29; ppStop[4]=55; ppSmBump=3; ppLgBump=21;
    } else if (IDENT(ppTokName, 'small'))  { ppStop[1]=13; ppStop[2]=28; ppStop[3]=31; ppStop[4]=60; ppSmBump=4; ppLgBump=24;
    } else if (IDENT(ppTokName, 'medium')) { ppStop[1]=15; ppStop[2]=30; ppStop[3]=33; ppStop[4]=80; ppSmBump=5; ppLgBump=42;
    } else if (IDENT(ppTokName, 'large'))  { ppStop[1]=17; ppStop[2]=32; ppStop[3]=35; ppStop[4]=96; ppSmBump=6; ppLgBump=54;
    } else if (IDENT(ppTokName, 'wide'))   { ppStop[1]=19; ppStop[2]=34; ppStop[3]=37; ppStop[4]=110; ppSmBump=7; ppLgBump=68;
    } else if (IDENT(ppTokName, 's1'))     { ppStop[1]  = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 's2'))     { ppStop[2]  = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 's3'))     { ppStop[3]  = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 's4'))     { ppStop[4]  = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 'smbump')) { ppSmBump   = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 'lgbump')) { ppLgBump   = INTEGER(ppTokVal);
    } else if (IDENT(ppTokName, 'auto'))   { ppAutoMode = 1;
    } else { OUTPUT = '* Warning: unknown switch --' && ppTokName; }
}

//  --auto two-pass mode
if (DIFFER(ppAutoMode)) {
    ppTab     = CHAR(9);
    ppSpOrEps = SPAN(' ' && ppTab) | epsilon;
    ppSFOrEps = ANY('SF') | epsilon;
    ppBrOrEps = ANY('(<') | epsilon;
    ppGSfx    = *ppSpOrEps && ':' && *ppSFOrEps && *ppBrOrEps && REM;
    ppGPat    = BREAK(':') . ppGCon && *ppGSfx;
    ppTrimPat = SPAN(' ' && ppTab) && RPOS(0) . ppDrop;
    ppNg      = 0;
    ppWArr    = ARRAY(2000);
    ppTmpFile = '/tmp/beauty_auto_' && HOST(1) && '.sno';
    output__(.ppTmp, 3, '', ppTmpFile);
    ppStmt = '';
    while (DIFFER(ppLn = INPUT)) {
        ppTmp = ppLn;
        if (ppLn ? (POS(0) && ANY('*-'))) { goto ppAutoNext; }
        if (ppLn ? (POS(0) && ANY('.+'))) { ppStmt = ppStmt && ' ' && ppLn; goto ppAutoNext; }
        if (DIFFER(ppStmt) && (ppStmt ? ppGPat)) {
            ppGConT = ppGCon;
            ppGConT ? *ppTrimPat = ;
            ppW = SIZE(ppGConT);
            if (DIFFER(ppW, 0)) { ppNg = ppNg + 1; ppWArr[ppNg] = ppW; }
        }
        ppStmt = ppLn;
        ppAutoNext:
    }
    ENDFILE(3);
    for (ppI = 1; LT(ppI, ppNg); ppI = ppI + 1) {
        ppJ = ppI; ppKey = ppWArr[ppJ];
        while (GT(ppJ, 1) && LT(ppKey, ppWArr[ppJ - 1])) {
            ppWArr[ppJ] = ppWArr[ppJ - 1]; ppJ = ppJ - 1;
        }
        ppWArr[ppJ] = ppKey;
    }
    ppP90i = (ppNg * 9) / 10;
    if (LT(ppP90i, 1)) { ppP90i = 1; }
    ppP90 = ppWArr[ppP90i];
    if (LT(ppP90, 20)) { ppP90 = 20; }
    ppStop[4] = ppP90 + 6;
    if (LT(ppStop[4], 40)) { ppStop[4] = 40; }
    ppStop[1] = 11 + (ppStop[4] - 40) * 8 / 70;
    ppStop[2] = ppStop[1] + 15;
    ppStop[3] = ppStop[2] + 3;
    ppSmBump  = 3 + (ppStop[4] - 40) * 4 / 70;
    ppLgBump  = ppStop[4] - ppStop[3] - 5;
    if (LT(ppLgBump, 10)) { ppLgBump = 10; }
    OUTPUT = '* --auto: n=' && ppNg
          && ' p90=' && ppP90
          && ' s1='  && ppStop[1] && ' s2=' && ppStop[2]
          && ' s3='  && ppStop[3] && ' s4=' && ppStop[4]
          && ' smbump=' && ppSmBump && ' lgbump=' && ppLgBump;
    input__(.INPUT, 1, '', ppTmpFile);
}

//  Parser patterns
Integer    = SPAN(digits);
DQ         = '"' && BREAK('"' && nl) && '"';
SQ         = "'" && BREAK("'" && nl) && "'";
String     = *SQ | *DQ;
Real       = ( SPAN(digits)
            && ('.' && FENCE(SPAN(digits) | epsilon) | epsilon)
            && ('E' | 'e') && ('+' | '-' | epsilon) && SPAN(digits)
           | SPAN(digits) && '.' && FENCE(SPAN(digits) | epsilon)
           );
Id         = ANY(&UCASE && &LCASE) && FENCE(SPAN('.' && digits && &UCASE && '_' && &LCASE) | epsilon);
Function   = SPAN('.' && digits && &UCASE && '_' && &LCASE) . tx . *match(Functions, TxInList);
BuiltinVar = SPAN('.' && digits && &UCASE && '_' && &LCASE) . tx . *match(BuiltinVars, TxInList);
SpecialNm  = SPAN('.' && digits && &UCASE && '_' && &LCASE) . tx . *match(SpecialNms, TxInList);
ProtKwd    = '&' && SPAN(&UCASE && &LCASE) . tx . *match(ProtKwds, TxInList);
UnprotKwd  = '&' && SPAN(&UCASE && &LCASE) . tx . *match(UnprotKwds, TxInList);
Gray  = *White | epsilon;
White = SPAN(' ' && tab) && FENCE(nl && ('+' | '.') && FENCE(SPAN(' ' && tab) | epsilon) | epsilon)
      | nl && ('+' | '.') && FENCE(SPAN(' ' && tab) | epsilon);
TxInList    = (POS(0) | ' ') && EVAL('upr(tx)') && (' ' | RPOS(0));
SpecialNms  = 'ABORT CONTINUE END FRETURN NRETURN RETURN SCONTINUE START';
BuiltinVars = 'ABORT ARB BAL FAIL FENCE INPUT OUTPUT REM TERMINAL';
ProtKwds    = 'ABORT ALPHABET ARB BAL FAIL FENCE FILE FNCLEVEL '
           && 'LASTFILE LASTLINE LASTNO LCASE LINE REM RTNTYPE '
           && 'STCOUNT STNO SUCCEED UCASE';
UnprotKwds  = 'ABEND ANCHOR CASE CODE COMPARE DUMP ERRLIMIT '
           && 'ERRTEXT ERRTYPE FTRACE INPUT MAXLNGTH OUTPUT '
           && 'PROFILE STLIMIT TRACE TRIM FULLSCAN';
Functions   = 'ANY APPLY ARBNO ARG ARRAY ATAN BACKSPACE BREAK BREAKX '
           && 'CHAR CHOP CLEAR CODE COLLECT CONVERT COPY COS DATA '
           && 'DATATYPE DATE DEFINE DETACH DIFFER DUMP DUPL EJECT '
           && 'ENDFILE EQ EVAL EXIT EXP FENCE FIELD GE GT HOST '
           && 'IDENT INPUT INTEGER ITEM LE LEN LEQ LGE LGT LLE '
           && 'LLT LN LNE LOAD LOCAL LPAD LT NE NOTANY OPSYN OUTPUT '
           && 'POS PROTOTYPE REMDR REPLACE REVERSE REWIND RPAD RPOS '
           && 'RSORT RTAB SET SETEXIT SIN SIZE SORT SPAN SQRT STOPTR '
           && 'SUBSTR TAB TABLE TAN TIME TRACE TRIM UNLOAD';
$'='  = *White && '=' && *White;   $'?'  = *White && '?' && *White;
$'|'  = *White && '|' && *White;   $'+'  = *White && '+' && *White;
$'-'  = *White && '-' && *White;   $'/'  = *White && '/' && *White;
$'*'  = *White && '*' && *White;   $'^'  = *White && '^' && *White;
$'!'  = *White && '!' && *White;   $'**' = *White && '**' && *White;
$'$'  = *White && '$' && *White;   $'.'  = *White && '.' && *White;
$'&'  = *White && '&' && *White;   $'@'  = *White && '@' && *White;
$'#'  = *White && '#' && *White;   $'%'  = *White && '%' && *White;
$'~'  = *White && '~' && *White;   $','  = *Gray  && ',' && *Gray;
$'('  = '(' && *Gray;  $'['  = '[' && *Gray;  $'<'  = '<' && *Gray;
$')'  = *Gray && ')';  $']'  = *Gray && ']';  $'>'  = *Gray && '>';
ExprList = nPush() && *XList && ("'ExprList'" & '*(GT(nTop(), 1) nTop())') && nPop();
XList    = nInc() && (*Expr | epsilon . '') && FENCE($',' && *XList | epsilon);
Expr     = *Expr0;
Expr0    = *Expr1  && FENCE($'='  && *Expr0  && ("'='"  & 2) | epsilon);
Expr1    = *Expr2  && FENCE($'?'  && *Expr1  && ("'?'"  & 2) | epsilon);
Expr2    = *Expr3  && FENCE($'&'  && *Expr2  && ("'&'"  & 2) | epsilon);
Expr3    = nPush() && *X3  && ("'|'"  & '*(GT(nTop(), 1) nTop())') && nPop();
X3       = nInc()  && *Expr4 && FENCE($'|' && *X3 | epsilon);
Expr4    = nPush() && *X4  && ("'..'" & '*(GT(nTop(), 1) nTop())') && nPop();
X4       = nInc()  && *Expr5 && FENCE(*White && *X4 | epsilon);
Expr5    = *Expr6  && FENCE($'@'  && *Expr5  && ("'@'"  & 2) | epsilon);
Expr6    = *Expr7  && FENCE($'+'  && *Expr6  && ("'+'"  & 2) | $'-' && *Expr6 && ("'-'" & 2) | epsilon);
Expr7    = *Expr8  && FENCE($'#'  && *Expr7  && ("'#'"  & 2) | epsilon);
Expr8    = *Expr9  && FENCE($'/'  && *Expr8  && ("'/'"  & 2) | epsilon);
Expr9    = *Expr10 && FENCE($'*'  && *Expr9  && ("'*'"  & 2) | epsilon);
Expr10   = *Expr11 && FENCE($'%'  && *Expr10 && ("'%'"  & 2) | epsilon);
Expr11   = *Expr12 && FENCE(($'^' | $'!' | $'**') && *Expr11 && ("'^'" & 2) | epsilon);
Expr12   = *Expr13 && FENCE($'$'  && *Expr12 && ("'$'"  & 2) | $'.' && *Expr12 && ("'.'" & 2) | epsilon);
Expr13   = *Expr14 && FENCE($'~'  && *Expr13 && ("'~'"  & 2) | epsilon);
Expr14   = '@' && *Expr14 && ("'@'" & 1) | '~' && *Expr14 && ("'~'" & 1)
         | '?' && *Expr14 && ("'?'" & 1) | *ProtKwd   . '' && 'ProtKwd'
         | *UnprotKwd . '' && 'UnprotKwd' | '&' && *Expr14 && ("'&'" & 1)
         | '+' && *Expr14 && ("'+'" & 1)  | '-' && *Expr14 && ("'-'" & 1)
         | '*' && *Expr14 && ("'*'" & 1)  | '$' && *Expr14 && ("'$'" & 1)
         | '.' && *Expr14 && ("'.'" & 1)  | '!' && *Expr14 && ("'!'" & 1)
         | '%' && *Expr14 && ("'%'" & 1)  | '/' && *Expr14 && ("'/'" & 1)
         | '#' && *Expr14 && ("'#'" & 1)  | '=' && *Expr14 && ("'='" & 1)
         | '|' && *Expr14 && ("'|'" & 1)  | *Expr15;
Expr15   = *Expr17 && FENCE(nPush() && *Expr16 && ("'[]'" & 'nTop() + 1') && nPop() | epsilon);
Expr16   = nInc() && ($'[' && *ExprList && $']' | $'<' && *ExprList && $'>') && FENCE(*Expr16 | epsilon);
Expr17   = FENCE(
             nPush() && $'(' && *Expr
               && ($',' && *XList && ("','" & 'nTop() + 1') | epsilon . '' && ("'()'" & 1))
               && $')' && nPop()
           | *Function  . '' && 'Function' && $'(' && *ExprList && $')' && ("'Call'" & 2)
           | *Id        . '' && 'Id'       && $'(' && *ExprList && $')' && ("'Call'" & 2)
           | *BuiltinVar . '' && 'BuiltinVar' | *SpecialNm  . '' && 'SpecialNm'
           | *Id         . '' && 'Id'         | *String     . '' && 'String'
           | *Real       . '' && 'Real'        | *Integer    . '' && 'Integer'
           );
SGoto    = ('S' | 's') . *assign(.SorF, *'S');
FGoto    = ('F' | 'f') . *assign(.SorF, *'F');
SorF     = *SGoto | *FGoto;
Target   = $'(' . *assign(.Brackets, *'()') && *Expr && $')'
         | $'<' . *assign(.Brackets, *'<>') && *Expr && $'>';
Goto     = *Gray && ':' && *Gray
        && FENCE(
             *Target && ("*(':' Brackets)" & 1) && epsilon . ''
           | (*SGoto | *FGoto) && *Target && ("*(':' SorF Brackets)" & 1)
             && FENCE(*Gray && (*SGoto | *FGoto) && *Target && ("*(':' SorF Brackets)" & 1) | epsilon . '')
           );
Control   = '-' && BREAK(nl && ';');
Comment   = '*' && BREAK(nl);
Label     = BREAK(' ' && tab && nl && ';') . '' && 'Label';
Stmt      = *Label
         && ( *White && *Expr14
              && FENCE(
                   epsilon . '' && *White && ('=' . '' && *White && *Expr | '=' . '' && epsilon . '')
                 | ($'?' | *White) && *Expr1
                   && FENCE( *White && ('=' . '' && *White && *Expr | '=' . '' && epsilon . '')
                            | epsilon . '' && epsilon . '')
                 | epsilon . '' && epsilon . '' && epsilon . '')
            | epsilon . '' && epsilon . '' && epsilon . '' && epsilon . '')
         && FENCE(*Goto | epsilon . '' && epsilon . '') && *Gray;
Commands  = *Command && FENCE(*Commands | epsilon);
Command   = nInc()
         && FENCE(
              *Comment  . '' && 'Comment' && ("'Comment'" & 1) && nl
            | *Control  . '' && 'Control' && ("'Control'" & 1) && (nl | ';')
            | *Stmt     && ("'Stmt'" & 7) && (nl | ';')
            );
Parse     = nPush() && ARBNO(*Command) && ("'Parse'" & 'nTop()') && nPop();
Compiland = nPush() && ARBNO(*Command) && ("'Parse'" & 'nTop()')
         && (icase('END') && (' ' && BREAK(nl) && nl | nl) && ARBNO(BREAK(nl) && nl) | epsilon)
         && nPop();

//=============================================================================
//  ppLeaf(x, t) — emit a leaf node; return 1 on success
//=============================================================================
procedure ppLeaf(x, t) {
    if (Gen(ss(x))) { return; }
    error();
}

//=============================================================================
//  ppUnOp(x, t, c) — emit a unary operator node
//=============================================================================
procedure ppUnOp(x, t, c) {
    if (Gen(ss(x, ppWidth - GetLevel()))) { return; }
    Gen(t);  pp(c[1]);
    return;
}

//=============================================================================
//  ppBinOp(x, t, c) — emit a binary operator node
//=============================================================================
procedure ppBinOp(x, t, c) {
    if (Gen(ss(x, ppWidth - GetLevel()))) { return; }
    pp(c[1]);
    Gen(nl);  DecLevel();  Gen(t);  IncLevel();  GenTab();
    pp(c[2]);
    return;
}

//=============================================================================
//  ppStmt(x) — columnar layout for Stmt nodes
//=============================================================================
procedure ppStmt(x, c, ppLbl, ppSubj, ppPatrn, ppAsgn, ppRepl, ppGo1, ppGo2) {
    SetLevel(0);  GenSetCont('+');
    ppWidth = ppStop[4];
    c       = c(x);
    ppLbl   = ss(c[1]);   ppSubj  = c[2];  ppPatrn = c[3];
    ppAsgn  = v(c[4]);    ppRepl  = c[5];  ppGo1   = c[6];  ppGo2 = c[7];
    Gen(ppLbl);
    if (DIFFER(t(ppSubj))) {
        Gen(' ');  GenTab(ppStop[1]);  SetLevel(ppStop[1]);
        ppWidth = IDENT(t(ppPatrn)) && IDENT(ppAsgn) && IDENT(t(ppGo1)) && ppStop[4] + ppLgBump;
        pp(ppSubj);
        if (DIFFER(t(ppPatrn))) {
            Gen(' ');  GenTab(ppStop[2]);  SetLevel(ppStop[2]);
            ppWidth = IDENT(ppAsgn) && IDENT(t(ppGo1)) && ppStop[4] + ppLgBump;
            pp(ppPatrn);
            if (DIFFER(ppAsgn)) {
                Gen(' =');
                if (DIFFER(t(ppRepl))) { Gen(' ');  pp(ppRepl); }
            }
        } else if (DIFFER(ppAsgn)) {
            Gen(' ');  GenTab(ppStop[2]);  SetLevel(ppStop[2]);
            Gen('=');
            if (DIFFER(t(ppRepl))) {
                Gen(' ');  GenTab(ppStop[3]);  SetLevel(ppStop[3]);
                ppWidth = IDENT(t(ppGo1)) && ppStop[4] + ppLgBump;
                pp(ppRepl);
            }
        }
    }
    if (DIFFER(t(ppGo1))) {
        ppWidth = 256;
        Gen(' ');  GenTab(ppStop[4]);  SetLevel(ppStop[4]);
        Gen(':');  pp(ppGo1);
        if (DIFFER(t(ppGo2))) { pp(ppGo2); }
    }
    Gen(nl);
    return;
}

//=============================================================================
//  ppList(x, sep, open, close) — emit children with separator
//  sep: ',' '|' '..' '[]' — controls layout
//=============================================================================
procedure ppList(x, sep, open, close, c, i, n) {
    c = c(x);  n = n(x);
    if (Gen(ss(x, ppWidth - GetLevel()))) { return; }
    if (DIFFER(open)) { Gen(open);  IncLevel();  GenTab(); }
    pp(c[1]);
    for (i = 2; LE(i, n); i = i + 1) {
        Gen(nl);  DecLevel();  Gen(sep);  IncLevel();  GenTab();
        pp(c[i]);
    }
    if (DIFFER(close)) { Gen(nl);  DecLevel();  Gen(close); }
    return;
}

//=============================================================================
//  pp(x) — pretty-print a tree node to OUTPUT
//=============================================================================
procedure pp(x, c, i, n, t, v) {
    if (~DIFFER(x)) { return; }
    t = t(x);  v = v(x);  n = n(x);  c = c(x);
    if (~DIFFER(t)) { return; }

    if (IDENT(t, 'BuiltinVar') || IDENT(t, 'Function')  || IDENT(t, 'Id')      ||
        IDENT(t, 'Integer')    || IDENT(t, 'Label')      || IDENT(t, 'ProtKwd') ||
        IDENT(t, 'Real')       || IDENT(t, 'SpecialNm')  || IDENT(t, 'String')  ||
        IDENT(t, 'UnprotKwd')  || IDENT(t, ':()') || IDENT(t, ':<>') ||
        IDENT(t, ':S()') || IDENT(t, ':S<>') || IDENT(t, ':F()') || IDENT(t, ':F<>')) {
        ppLeaf(x, t); return;
    }
    if (IDENT(t, 'Parse') || IDENT(t, '0')) {
        ppWidth = ppStop[4];
        for (i = 1; LE(i, n); i = i + 1) { pp(c[i]); }
        return;
    }
    if (IDENT(t, 'Comment') || IDENT(t, 'Control')) {
        SetLevel(0);  GenSetCont();  Gen(v(c[1]) && nl); return;
    }
    if (IDENT(t, 'Stmt'))     { ppStmt(x); return; }
    if (IDENT(t, 'ExprList')) { ppList(x, ',',  '',  ''); return; }
    if (IDENT(t, ','))        { ppList(x, ',',  '(', ')'); return; }
    if (IDENT(t, '..'))       { ppList(x, '',   '',  ''); return; }
    if (IDENT(t, '[]'))       { ppList(x, ']',  '[', ']'); return; }
    if (IDENT(t, '()')) {
        if (Gen(ss(x, ppWidth - GetLevel()))) { return; }
        Gen('(');  IncLevel();  GenTab();  pp(c[1]);
        Gen(nl);   DecLevel();  Gen(')');  return;
    }
    if (IDENT(t, 'Call')) {
        if (Gen(ss(x, ppWidth - GetLevel()))) { return; }
        pp(c[1]);  Gen('(' && nl);
        IncLevel();  GenTab();  pp(c[2]);
        Gen(nl);   DecLevel();  Gen(')');  return;
    }
    if (IDENT(t, '|')) {
        if (EQ(n, 1)) { ppUnOp(x, t, c); return; }
        ppList(x, '|', '', ''); return;
    }
    if (EQ(n, 1)) { ppUnOp(x, t, c); return; }
    if (EQ(n, 2)) { ppBinOp(x, t, c); return; }
    error();
}

//=============================================================================
//  ss helpers
//=============================================================================
//=============================================================================
//  ss_leaf(t, v, c, len) — stringify leaf/goto nodes; freturn if too long
//  Returns the string in ss_leaf (caller assigns to ss).
//=============================================================================
procedure ss_leaf(t, v, c, len) {
    if      (IDENT(t,'BuiltinVar')||IDENT(t,'Function')||IDENT(t,'ProtKwd')||
             IDENT(t,'SpecialNm') ||IDENT(t,'UnprotKwd')) { ss_leaf = upr(v); }
    else if (IDENT(t,'Id')||IDENT(t,'Integer')||IDENT(t,'Real')||IDENT(t,'String')) {
        ss_leaf = v;
    }
    else if (IDENT(t, 'Label')) {
        if (v ? (POS(0) && *SpecialNm && RPOS(0))) { ss_leaf = upr(v); } else { ss_leaf = v; }
    }
    else if (IDENT(t,':()'))  { ss_leaf='('  && ss(c[1],len-2) && ')'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else if (IDENT(t,':<>'))  { ss_leaf='<'  && ss(c[1],len-2) && '>'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else if (IDENT(t,':S()')) { ss_leaf='S(' && ss(c[1],len-3) && ')'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else if (IDENT(t,':S<>')) { ss_leaf='S<' && ss(c[1],len-3) && '>'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else if (IDENT(t,':F()')) { ss_leaf='F(' && ss(c[1],len-3) && ')'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else if (IDENT(t,':F<>')) { ss_leaf='F<' && ss(c[1],len-3) && '>'; if(DIFFER(ss_leaf)){return;}else{freturn;} }
    else { freturn; }   // not a leaf — caller handles compound
    if (LE(SIZE(ss_leaf), len)) { return; } else { freturn; }
}

//=============================================================================
//  ss(x, len) — stringify a tree node; freturn if result exceeds len chars
//=============================================================================
procedure ss(x, len, c, i, n, t, v) {
    if (~DIFFER(x)) { return; }
    if (IDENT(len)) { len = 1024; }
    if (~GT(len, 0)) { freturn; }
    t = t(x);  v = v(x);  n = n(x);  c = c(x);
    if (~DIFFER(t)) { return; }
    // Try leaf first
    ss = ss_leaf(t, v, c, len);
    if (DIFFER(ss)) { return; }
    // Compound nodes
    if (IDENT(t, '|') && EQ(n, 1)) { goto ss_unop; }
    if (IDENT(t, '|')) {
        ss = ss(c[1], len);  if (~DIFFER(ss)) { freturn; }
        for (i = 2; LE(i, n); i = i + 1) {
            ss = ss && ' | ' && ss(c[i], len - SIZE(ss) - 3);
            if (~DIFFER(ss)) { freturn; }
        }
        return;
    }
    if (IDENT(t, '..')) {
        ss = ss(c[1], len);  if (~DIFFER(ss)) { freturn; }
        for (i = 2; LE(i, n); i = i + 1) {
            ss = ss && ' ' && ss(c[i], len - SIZE(ss) - 1);
            if (~DIFFER(ss)) { freturn; }
        }
        return;
    }
    if (IDENT(t, 'ExprList')) {
        ss = ss(c[1], len);  if (~DIFFER(ss)) { freturn; }
        for (i = 2; LE(i, n); i = i + 1) {
            ss = ss && ', ' && ss(c[i], len - SIZE(ss) - 2);
            if (~DIFFER(ss)) { freturn; }
        }
        return;
    }
    if (IDENT(t, ',')) {
        ss = '(' && ss(c[1], len - 4);  if (~DIFFER(ss)) { freturn; }
        for (i = 2; LE(i, n); i = i + 1) {
            ss = ss && ', ' && ss(c[i], len - SIZE(ss) - 3);
            if (~DIFFER(ss)) { freturn; }
        }
        ss = ss && ')'; return;
    }
    if (IDENT(t, '[]')) {
        ss = ss(c[1], len);  if (~DIFFER(ss)) { freturn; }
        for (i = 2; LE(i, n); i = i + 1) {
            ss = ss && '[' && ss(c[i], len - SIZE(ss) - 2) && ']';
            if (~DIFFER(ss)) { freturn; }
        }
        return;
    }
    if (IDENT(t, '()')) {
        ss = '(' && ss(c[1], len - 2) && ')';
        if (DIFFER(ss)) { return; } else { freturn; }
    }
    if (IDENT(t, 'Call')) {
        ss = ss(c[1]) && '(' && ss(c[2], len - SIZE(v) - 2) && ')';
        if (DIFFER(ss)) { return; } else { freturn; }
    }
    if (EQ(n, 1)) { goto ss_unop; }
    if (EQ(n, 2)) { goto ss_binop; }
    error();

    ss_unop:
    ss = t && ss(c[1], len - SIZE(t));
    if (DIFFER(ss)) { return; } else { freturn; }

    ss_binop:
    ss = ss(c[1], len);
    if (~DIFFER(ss)) { freturn; }
    ss = ss && ' ' && t && ' ' && ss(c[2], len - SIZE(ss) - SIZE(t) - 2);
    if (DIFFER(ss)) { return; } else { freturn; }
}

procedure bVisit(x, fnc, i) {
    if (~APPLY(fnc, x)) { return; }
    for (i = 1; LE(i, n(x)); i = i + 1) { bVisit(c(x)[i], fnc); }
    return;
}

Refs = '';
procedure findRefs(x, n, v) {
    if (~DIFFER(x)) { return; }
    if (IDENT(t(x), 'Call')) {
        for (n = 2; LE(n, n(x)); n = n + 1) { bVisit(c(x)[n], .findRefs); }
        freturn;
    }
    if      (IDENT(t(x), '&') && EQ(n(x), 1)) { v = ss(x); }
    else if (IDENT(t(x), 'Id'))                { v = v(x);  }
    else                                        { return;    }
    if (~(v ? (POS(0) && SPAN('0123456789' && &UCASE && '_') && RPOS(0)))) { freturn; }
    if (DIFFER(Refs)) { Refs = Refs && ' ' && v; } else { Refs = v; }
    freturn;
}

procedure refs(p, c, n, s, subj) {
    c = c(p);
    for (n = 1; LE(n, n(p)); n = n + 1) {
        if (~IDENT(t(c[n]), 'Stmt'))               { goto refs_next; }
        s = s + 1;
        if (~IDENT(t(c(c[n])[3])))                 { goto refs_next; }
        if (~IDENT(t(c(c[n])[4]), '='))             { goto refs_next; }
        if (~(IDENT(t(c(c[n])[2]), 'Id') ||
              IDENT(t(c(c[n])[2]), '$'))) { goto refs_next; }
        subj = ss(c(c[n])[2]);
        Refs = '';
        bVisit(c(c[n])[5], .findRefs);
        OUTPUT = LPAD(s, 3, 0) && ': ' && RPAD(subj, 38) && ' ' && Refs;
        refs_next:
    }
    return;
}

//=============================================================================
//  Main loop
//=============================================================================
doDebug = 0;
Space   = SPAN(' ' && tab) | epsilon;

while (DIFFER(Line = INPUT)) {
    Src = '';
    while (Line ? (POS(0) && ANY('*-'))) {
        OUTPUT = Line;
        Line = INPUT;
        if (~DIFFER(Line)) { goto END; }
    }
    while (1) {
        Src  = Src && Line && nl;
        Line = INPUT;
        if (~DIFFER(Line)) {
            if (~(Src ? (POS(0) && *Parse && *Space && RPOS(0)))) { goto mainErr1; }
            sno = Pop();
            if (~DIFFER(sno)) { goto mainErr2; }
            pp(sno);
            goto END;
        }
        if (~(Line ? (POS(0) && ANY('.+')))) { break; }
    }
    if (~(Src ? (POS(0) && *Parse && *Space && RPOS(0)))) { goto mainErr1; }
    sno = Pop();
    if (~DIFFER(sno)) { goto mainErr2; }
    pp(sno);
}
goto END;

mainErr1:  OUTPUT = 'Parse Error';    OUTPUT = Src;  goto END;
mainErr2:  OUTPUT = 'Internal Error'; OUTPUT = Src;

END:
