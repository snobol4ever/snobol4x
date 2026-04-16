// io.sc — Snocone port of io.sno
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
