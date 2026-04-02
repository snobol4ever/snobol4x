/**
 * BbArb.java — ARB: match 0..n chars lazily; β extends by 1
 * Port of bb_arb.c / bb_arb.s
 *
 *   ARB_α:  count=0; start=Δ; ARB=spec(Σ+Δ,0);         goto ARB_γ;
 *   ARB_β:  count++;
 *           if (start+count > Ω)                        goto ARB_ω;
 *           Δ=start; ARB=spec(Σ+Δ,count); Δ+=count;    goto ARB_γ;
 *   ARB_γ:                                              return ARB;
 *   ARB_ω:                                              return spec_empty;
 */
class BbArb extends BbBox {
    private int count;
    private int start;

    public BbArb(MatchState ms) { super(ms); }

    @Override public Spec alpha() {
        // ARB_α: start lazy (zero-width)
        count = 0;
        start = ms.delta;
        return new Spec(ms.delta, 0);                                          // ARB_γ
    }

    @Override public Spec beta() {
        // ARB_β: extend by one char
        count++;
        if (start + count > ms.omega) return null;                             // ARB_ω
        ms.delta = start;
        Spec r = new Spec(ms.delta, count);
        ms.delta += count;
        return r;                                                               // ARB_γ
    }
}
