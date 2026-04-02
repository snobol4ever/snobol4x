package snobol4.runtime.boxes;

/**
 * BbTab.java — TAB: advance cursor TO absolute position n
 * Port of bb_tab.c / bb_tab.s
 *
 *   TAB_α:  if (Δ > n)                      goto TAB_ω;
 *           advance=n-Δ; TAB=spec(Σ+Δ,advance); Δ=n;  goto TAB_γ;
 *   TAB_β:  Δ -= advance;                   goto TAB_ω;
 */
public class BbTab extends BbBox {
    private final int n;
    private int       advance;

    public BbTab(MatchState ms, int n) { super(ms); this.n=n; }

    @Override public Spec alpha() {
        if (ms.delta > n) return null;
        advance = n - ms.delta;
        Spec r = new Spec(ms.delta, advance);
        ms.delta = n;
        return r;
    }

    @Override public Spec beta() { ms.delta -= advance; return null; }
}
