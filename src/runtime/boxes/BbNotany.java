/**
 * BbNotany.java — NOTANY: match one char if NOT in set
 * Port of bb_notany.c / bb_notany.s
 *
 *   NOTANY_α:  if (Δ>=Ω || chars.indexOf(Σ[Δ])>=0)  goto NOTANY_ω;
 *              NOTANY=spec(Σ+Δ,1); Δ++;               goto NOTANY_γ;
 *   NOTANY_β:  Δ--;                                   goto NOTANY_ω;
 */
class BbNotany extends BbBox {
    private final String chars;

    public BbNotany(MatchState ms, String chars) { super(ms); this.chars=chars; }

    @Override public Spec alpha() {
        if (ms.delta >= ms.omega) return null;
        if (chars.indexOf(ms.sigma.charAt(ms.delta)) >= 0) return null;
        return new Spec(ms.delta++, 1);
    }

    @Override public Spec beta() { ms.delta--; return null; }
}
