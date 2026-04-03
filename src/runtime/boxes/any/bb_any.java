package bb;

/**
 * bb_any.java — ANY: match one char if in set
 * Port of bb_any.c / bb_any.s
 *
 *   ANY_α:  if (Δ>=Ω || chars.indexOf(Σ[Δ])<0)  goto ANY_ω;
 *           ANY=spec(Σ+Δ,1); Δ++;                goto ANY_γ;
 *   ANY_β:  Δ--;                                 goto ANY_ω;
 */
public class bb_any extends bb_box {
    private final String chars;

    public bb_any(MatchState ms, String chars) { super(ms); this.chars=chars; }

    @Override public Spec α() {
        if (ms.delta >= ms.omega) return null;
        if (chars.indexOf(ms.sigma.charAt(ms.delta)) < 0) return null;
        return new Spec(ms.delta++, 1);
    }

    @Override public Spec β() { ms.delta--; return null; }
}
