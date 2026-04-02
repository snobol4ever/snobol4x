/**
 * BbRtab.java — RTAB: advance cursor TO position Ω-n
 * Port of bb_rtab.c / bb_rtab.s
 *
 *   RTAB_α:  if (Δ > Ω-n)                               goto RTAB_ω;
 *            advance=(Ω-n)-Δ; RTAB=spec(Σ+Δ,advance); Δ=Ω-n;  goto RTAB_γ;
 *   RTAB_β:  Δ -= advance;                              goto RTAB_ω;
 */
class BbRtab extends BbBox {
    private final int n;
    private int       advance;

    public BbRtab(MatchState ms, int n) { super(ms); this.n=n; }

    @Override public Spec alpha() {
        int target = ms.omega - n;
        if (ms.delta > target) return null;
        advance = target - ms.delta;
        Spec r = new Spec(ms.delta, advance);
        ms.delta = target;
        return r;
    }

    @Override public Spec beta() { ms.delta -= advance; return null; }
}
