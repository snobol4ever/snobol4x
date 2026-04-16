// omega.sc — Snocone port of omega.sno
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
