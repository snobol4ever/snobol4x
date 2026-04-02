/**
 * BbPos.java — POS: assert cursor == n (zero-width)
 * Port of bb_pos.c / bb_pos.s
 *
 *   POS_α:  if (Δ != n)      goto POS_ω;
 *           POS=spec(Σ+Δ,0); goto POS_γ;
 *   POS_β:                   goto POS_ω;
 */
class BbPos extends BbBox {
    private final int n;

    public BbPos(MatchState ms, int n) { super(ms); this.n=n; }

    @Override public Spec alpha() {
        if (ms.delta != n) return null;
        return new Spec(ms.delta, 0);
    }

    @Override public Spec beta() { return null; }
}
