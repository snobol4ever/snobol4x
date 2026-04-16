// Gen.sc — Snocone port of Gen.sno
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
