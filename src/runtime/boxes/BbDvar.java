/**
 * BbDvar.java — DVAR: *VAR — re-resolve live variable value on every α
 * Port of bb_dvar.c / bb_dvar.s
 *
 * On each α entry, look up the variable's current value:
 *   - If DT_P (pattern-valued): rebuild child box from live pattern tree
 *   - If DT_S (string-valued):  rebuild child as BbLit with that string
 * Then call child(α) / child(β) as normal.
 *
 *   DVAR_α:  resolve name → rebuild child if changed;
 *            DVAR = child(α);  if empty → DVAR_ω;  else → DVAR_γ;
 *   DVAR_β:  DVAR = child(β);  if empty → DVAR_ω;  else → DVAR_γ;
 */
class BbDvar extends BbBox {

    /** Callback to resolve variable → current BbBox (handles DT_P and DT_S) */
    public interface BoxResolver {
        /** Return a fresh BbBox for the current value of varname, or null if unset */
        BbBox resolve(String varname, MatchState ms);
    }

    private final String      varname;
    private final BoxResolver resolver;
    private       BbBox       child = null;

    public BbDvar(MatchState ms, String varname, BoxResolver resolver) {
        super(ms);
        this.varname  = varname;
        this.resolver = resolver;
    }

    @Override public Spec alpha() {
        // DVAR_α: re-resolve on every fresh entry
        child = resolver.resolve(varname, ms);
        if (child == null) return null;                                        // DVAR_ω
        Spec r = child.alpha();
        return r;                                                              // DVAR_γ or DVAR_ω
    }

    @Override public Spec beta() {
        // DVAR_β: delegate to same child (no re-resolve on backtrack)
        if (child == null) return null;                                        // DVAR_ω
        return child.beta();
    }
}
