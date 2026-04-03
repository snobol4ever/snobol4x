package bb;

/**
 * bb_brk.java — BRK (BREAK): scan to first char in set (zero-width possible)
 * Port of bb_brk.c / bb_brk.s
 *
 *   BRK_α:  δ=0; while (Δ+δ<Ω && !chars.has(Σ[Δ+δ])) δ++;
 *           if (Δ+δ >= Ω)               goto BRK_ω;   (never found char in set)
 *           BRK=spec(Σ+Δ,δ); Δ+=δ;     goto BRK_γ;
 *   BRK_β:  Δ-=δ;                       goto BRK_ω;
 */
public class bb_brk extends bb_box {
    private final String chars;
    private int          delta;

    public bb_brk(MatchState ms, String chars) { super(ms); this.chars=chars; }

    @Override public Spec α() {
        delta = 0;
        while (ms.delta + delta < ms.omega &&
               chars.indexOf(ms.sigma.charAt(ms.delta + delta)) < 0) delta++;
        if (ms.delta + delta >= ms.omega) return null;
        Spec r = new Spec(ms.delta, delta);
        ms.delta += delta;
        return r;
    }

    @Override public Spec β() { ms.delta -= delta; return null; }
}
