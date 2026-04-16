// Qize.sc — Snocone port of Qize.sno
// Qize(str)    — quote-ize a string into a SNOBOL4 expression
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
