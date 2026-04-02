package snobol4.runtime.boxes;

/**
 * BbBreakx.java — BREAKX: like BRK but fails on zero advance
 * Port of bb_breakx.c / bb_breakx.s
 *
 *   BREAKX_α:  δ=0; while (Δ+δ<Ω && !chars.has(Σ[Δ+δ])) δ++;
 *              if (δ==0 || Δ+δ>=Ω)             goto BREAKX_ω;
 *              BREAKX=spec(Σ+Δ,δ); Δ+=δ;       goto BREAKX_γ;
 *   BREAKX_β:  Δ-=δ;                            goto BREAKX_ω;
 */
public class BbBreakx extends BbBox {
    private final String chars;
    private int          delta;

    public BbBreakx(MatchState ms, String chars) { super(ms); this.chars=chars; }

    @Override public Spec alpha() {
        delta = 0;
        while (ms.delta + delta < ms.omega &&
               chars.indexOf(ms.sigma.charAt(ms.delta + delta)) < 0) delta++;
        if (delta == 0 || ms.delta + delta >= ms.omega) return null;
        Spec r = new Spec(ms.delta, delta);
        ms.delta += delta;
        return r;
    }

    @Override public Spec beta() { ms.delta -= delta; return null; }
}
