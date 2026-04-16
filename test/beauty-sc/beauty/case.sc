// case.sc — Snocone port of case.sno

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
