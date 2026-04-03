package bb;

/**
 * bb_lit.java — LIT: literal string match
 * Port of bb_lit.c / bb_lit.s
 *
 * C original:
 *   LIT_α:  if (Δ + len > Ω)                    goto LIT_ω;
 *           if (memcmp(Σ+Δ, lit, len) != 0)     goto LIT_ω;
 *           LIT = spec(Σ+Δ, len); Δ += len;     goto LIT_γ;
 *   LIT_β:  Δ -= len;                            goto LIT_ω;
 *   LIT_γ:                                       return LIT;
 *   LIT_ω:                                       return spec_empty;
 */
public class bb_lit extends bb_box {
    private final String lit;
    private final int    len;

    public bb_lit(MatchState ms, String lit) {
        super(ms);
        this.lit = lit;
        this.len = lit.length();
    }

    @Override public Spec α() {
        // LIT_α
        if (ms.delta + len > ms.omega)                              return null; // LIT_ω
        if (!ms.sigma.regionMatches(ms.delta, lit, 0, len))        return null; // LIT_ω
        Spec r = new Spec(ms.delta, len);
        ms.delta += len;
        return r;                                                               // LIT_γ
    }

    @Override public Spec β() {
        // LIT_β
        ms.delta -= len;
        return null;                                                            // LIT_ω
    }
}
