/**
 * BbAtp.java — ATP: @var — write cursor position Δ as integer into varname
 * Port of bb_atp.c / bb_atp.s
 *
 *   ATP_α:  if (varname) NV_SET(varname, Δ);
 *           ATP=spec(Σ+Δ,0);                    goto ATP_γ;
 *   ATP_β:                                      goto ATP_ω;
 */
class BbAtp extends BbBox {

    public interface IntSetter { void set(String varname, int value); }

    private final String    varname;
    private final IntSetter setter;

    public BbAtp(MatchState ms, String varname, IntSetter setter) {
        super(ms);
        this.varname = varname;
        this.setter  = setter;
    }

    @Override public Spec alpha() {
        if (varname != null && !varname.isEmpty())
            setter.set(varname, ms.delta);
        return new Spec(ms.delta, 0);                                          // ATP_γ
    }

    @Override public Spec beta() { return null; }                              // ATP_ω
}
