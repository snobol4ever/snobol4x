package snobol4.runtime.boxes;

/**
 * BbCapture.java — CAPTURE: wrap child; $ writes on every γ; . buffers for Phase-5 commit
 * Port of bb_capture.c / bb_capture.s
 *
 *   CAP_α:       cr = child(α);  if empty → CAP_ω;  else → CAP_γ_core;
 *   CAP_β:       cr = child(β);  if empty → CAP_ω;  else → CAP_γ_core;
 *   CAP_γ_core:  if (immediate) NV_SET(varname, cr);
 *                else           pending=cr; has_pending=true;
 *                               return cr;
 *   CAP_ω:       has_pending=false; return spec_empty;
 *
 * The executor calls commitPending() after overall match success to flush
 * deferred (.) captures — mirroring match.clj commit-pending! and stmt_exec.c.
 */
public class BbCapture extends BbBox {

    /** Callback interface for variable assignment (NV_SET_fn equivalent) */
    public interface VarSetter {
        void set(String varname, String value);
    }

    private final BbBox     child;
    private final String    varname;
    private final boolean   immediate;   /* true = $var; false = .var */
    private final VarSetter setter;

    /* deferred capture state (.var path) */
    private Spec    pending    = null;
    private boolean hasPending = false;

    public BbCapture(MatchState ms, BbBox child, String varname,
                     boolean immediate, VarSetter setter) {
        super(ms);
        this.child     = child;
        this.varname   = varname;
        this.immediate = immediate;
        this.setter    = setter;
    }

    @Override public Spec alpha() { return runChild(child.alpha()); }
    @Override public Spec beta()  { return runChild(child.beta());  }

    private Spec runChild(Spec cr) {
        if (cr == null) {
            // CAP_ω
            hasPending = false;
            return null;
        }
        // CAP_γ_core
        if (varname != null && !varname.isEmpty()) {
            String matched = ms.sigma.substring(cr.start, cr.start + cr.len);
            if (immediate) {
                setter.set(varname, matched);                                  // $ — write now
            } else {
                pending    = cr;                                               // . — buffer
                hasPending = true;
            }
        }
        return cr;
    }

    /**
     * Called by executor after overall match succeeds to commit deferred captures.
     * Mirrors match.clj commit-pending! and stmt_exec.c Phase-5 flush.
     */
    public void commitPending() {
        if (hasPending && pending != null && varname != null && !varname.isEmpty()) {
            setter.set(varname, ms.sigma.substring(pending.start, pending.start + pending.len));
            hasPending = false;
            pending    = null;
        }
    }
}
