import java.util.ArrayList;
import java.util.List;

/**
 * BbExecutor.java — 5-phase SNOBOL4 statement executor
 *
 * Direct Java port of stmt_exec.c (the canonical 5-phase model).
 * Drives a compiled BbBox graph through the scan loop.
 *
 * PHASES
 * ------
 *   Phase 1: buildSubject  — subject string already resolved by caller → MatchState
 *   Phase 2: buildPattern  — pattern BbBox graph built by caller → root BbBox
 *   Phase 3: runMatch      — scan loop drives root.alpha()/beta(), collects captures
 *   Phase 4: buildRepl     — replacement value already as String → caller provides
 *   Phase 5: performRepl   — splice into subject, assign, return :S/:F
 *
 * PUBLIC API
 *   ExecResult exec(String subjVar, String subjVal,
 *                   BbBox root, boolean hasPattern,
 *                   String replVal,   boolean hasRepl,
 *                   boolean anchor)
 *
 * Returns SUCCESS (true → :S branch) or FAILURE (false → :F branch).
 */
class BbExecutor {

    public interface VarStore {
        String get(String name);
        void   set(String name, String value);
    }

    private final VarStore vars;

    /* Pending deferred captures (.var) collected during Phase 3 */
    private final List<BbCapture> pendingCaptures = new ArrayList<>();

    public BbExecutor(VarStore vars) { this.vars = vars; }

    /**
     * Register a BbCapture box so its commitPending() is called on :S.
     * Call this for every .var capture box before calling exec().
     */
    public void registerCapture(BbCapture c) { pendingCaptures.add(c); }
    public void clearCaptures()              { pendingCaptures.clear(); }

    /**
     * Execute one SNOBOL4 statement through all 5 phases.
     *
     * @param subjVar   Name of subject variable (for Phase 5 write-back), or null
     * @param subjVal   Current value of subject (Phase 1 resolved by caller)
     * @param root      Pattern root BbBox (Phase 2 built by caller), or null if no pattern
     * @param hasRepl   Whether a replacement exists
     * @param replVal   Replacement string (Phase 4 already evaluated by caller)
     * @param anchor    &ANCHOR — if true, try position 0 only
     * @return true → :S branch taken; false → :F branch taken
     */
    public boolean exec(String subjVar, String subjVal,
                        BbBox root,     boolean hasRepl,
                        String replVal, boolean anchor) {

        // Phase 1: subject is already in subjVal — build MatchState
        BbBox.MatchState ms = new BbBox.MatchState(subjVal);

        if (root == null) {
            // No pattern — Phase 5 (assignment only, always :S if no pattern)
            if (hasRepl && subjVar != null) vars.set(subjVar, replVal);
            return true;
        }

        // Phase 3: scan loop
        int scanLimit = anchor ? 0 : ms.omega;

        for (int scanPos = 0; scanPos <= scanLimit; scanPos++) {
            ms.delta = scanPos;

            try {
                BbBox.Spec result = root.alpha();

                if (result != null) {
                    // Match succeeded at scanPos
                    int matchStart = scanPos;
                    int matchEnd   = ms.delta;           // cursor after match

                    // Phase 5: commit deferred captures
                    for (BbCapture c : pendingCaptures) c.commitPending();

                    // Phase 5: perform replacement
                    if (hasRepl && subjVar != null) {
                        String pre    = subjVal.substring(0, matchStart);
                        String post   = subjVal.substring(matchEnd);
                        vars.set(subjVar, pre + replVal + post);
                    }

                    clearCaptures();
                    return true;                         // :S
                }

                // No match at this position — backtrack fully before advancing
                // (root.beta() would extend, but scan loop advances cursor instead)

            } catch (BbAbort.AbortException e) {
                // ABORT: unconditional match failure
                clearCaptures();
                return false;                            // :F
            }

            if (anchor) break;                          // ANCHOR=1: one attempt only
        }

        // Phase 3 exhausted — no match
        clearCaptures();
        return false;                                    // :F
    }
}
