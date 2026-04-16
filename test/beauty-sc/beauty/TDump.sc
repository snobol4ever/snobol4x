// TDump.sc — Snocone port of TDump.sno
// TValue(x)        — render leaf/node value as string
// TDump(x, outNm)  — dump tree to output in lisp-like form
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
