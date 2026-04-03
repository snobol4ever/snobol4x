package bb;

/**
 * bb_dvar.java — DVAR: *VAR — re-resolve live variable value on every α
 * Port of bb_dvar.c / bb_dvar.s
 *
 * On each α entry, look up the variable's current value:
 *   - If DT_P (pattern-valued): rebuild child box from live pattern tree
 *   - If DT_S (string-valued):  rebuild child as bb_lit with that string
 * Then call child(α) / child(β) as normal.
 *
 *   DVAR_α:  resolve name → rebuild child if changed;
 *            DVAR = child(α);  if empty → DVAR_ω;  else → DVAR_γ;
 *   DVAR_β:  DVAR = child(β);  if empty → DVAR_ω;  else → DVAR_γ;
 */
public class bb_dvar extends bb_box {

    /** Callback to resolve variable → current bb_box (handles DT_P and DT_S) */
    public interface BoxResolver {
        /** Return a fresh bb_box for the current value of varname, or null if unset */
        bb_box resolve(String varname, MatchState ms);
    }

    private final String      varname;
    private final BoxResolver resolver;
    private       bb_box       child = null;

    public bb_dvar(MatchState ms, String varname, BoxResolver resolver) {
        super(ms);
        this.varname  = varname;
        this.resolver = resolver;
    }

    @Override public Spec α() {
        // DVAR_α: re-resolve on every fresh entry
        child = resolver.resolve(varname, ms);
        if (child == null) return null;                                        // DVAR_ω
        Spec r = child.α();
        return r;                                                              // DVAR_γ or DVAR_ω
    }

    @Override public Spec β() {
        // DVAR_β: delegate to same child (no re-resolve on backtrack)
        if (child == null) return null;                                        // DVAR_ω
        return child.β();
    }
}
