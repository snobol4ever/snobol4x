package bb;

/**
 * bb_span.java — SPAN: longest prefix of chars in set (≥1 char required)
 * Port of bb_span.c / bb_span.s
 *
 *   SPAN_α:  δ=0; while (Δ+δ<Ω && chars.has(Σ[Δ+δ])) δ++;
 *            if (δ<=0)                    goto SPAN_ω;
 *            SPAN=spec(Σ+Δ,δ); Δ+=δ;     goto SPAN_γ;
 *   SPAN_β:  Δ-=δ;                        goto SPAN_ω;
 */
public class bb_span extends bb_box {
    private final String chars;
    private int          delta;   /* saved match length */

    public bb_span(MatchState ms, String chars) { super(ms); this.chars=chars; }

    @Override public Spec α() {
        delta = 0;
        while (ms.delta + delta < ms.omega &&
               chars.indexOf(ms.sigma.charAt(ms.delta + delta)) >= 0) delta++;
        if (delta <= 0) return null;
        Spec r = new Spec(ms.delta, delta);
        ms.delta += delta;
        return r;
    }

    @Override public Spec β() { ms.delta -= delta; return null; }
}
