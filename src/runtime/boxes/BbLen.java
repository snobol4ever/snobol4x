/**
 * BbLen.java — LEN: match exactly n characters
 * Port of bb_len.c / bb_len.s
 *
 *   LEN_α:  if (Δ+n > Ω)            goto LEN_ω;
 *           LEN=spec(Σ+Δ,n); Δ+=n;  goto LEN_γ;
 *   LEN_β:  Δ-=n;                   goto LEN_ω;
 */
class BbLen extends BbBox {
    private final int n;

    public BbLen(MatchState ms, int n) { super(ms); this.n=n; }

    @Override public Spec alpha() {
        if (ms.delta + n > ms.omega) return null;
        Spec r = new Spec(ms.delta, n);
        ms.delta += n;
        return r;
    }

    @Override public Spec beta() { ms.delta -= n; return null; }
}
