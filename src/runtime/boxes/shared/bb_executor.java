package bb;

import java.util.ArrayList;
import java.util.List;

/**
 * bb_executor.java — 5-phase SNOBOL4 statement executor
 *
 * Direct Java port of stmt_exec.c (the canonical 5-phase model).
 * Drives a compiled bb_box graph through the scan loop.
 *
 * PHASES
 * ------
 *   Phase 1: buildSubject  — subject string already resolved by caller → MatchState
 *   Phase 2: buildPattern  — pattern bb_box graph built by caller → root bb_box
 *   Phase 3: runMatch      — scan loop drives root.α()/beta(), collects captures
 *   Phase 4: buildRepl     — replacement value already as String → caller provides
 *   Phase 5: performRepl   — splice into subject, assign, return :S/:F
 *
 * PUBLIC API
 *   ExecResult exec(String subjVar, String subjVal,
 *                   bb_box root, boolean hasPattern,
 *                   String replVal,   boolean hasRepl,
 *                   boolean anchor)
 *
 * Returns SUCCESS (true → :S branch) or FAILURE (false → :F branch).
 */
public class bb_executor {

    public interface VarStore {
        String get(String name);
        void   set(String name, String value);
    }

    private final VarStore vars;

    /* Pending deferred captures (.var) collected during Phase 3 */
    private final List<bb_capture> pendingCaptures = new ArrayList<>();

    public bb_executor(VarStore vars) { this.vars = vars; }

    /**
     * Register a bb_capture box so its commitPending() is called on :S.
     * Call this for every .var capture box before calling exec().
     */
    public void registerCapture(bb_capture c) { pendingCaptures.add(c); }
    public void clearCaptures()              { pendingCaptures.clear(); }

    /**
     * Execute one SNOBOL4 statement through all 5 phases.
     *
     * @param subjVar   Name of subject variable (for Phase 5 write-back), or null
     * @param subjVal   Current value of subject (Phase 1 resolved by caller)
     * @param root      Pattern root bb_box (Phase 2 built by caller), or null if no pattern
     * @param hasRepl   Whether a replacement exists
     * @param replVal   Replacement string (Phase 4 already evaluated by caller)
     * @param anchor    &ANCHOR — if true, try position 0 only
     * @return true → :S branch taken; false → :F branch taken
     */
    /**
     * Overload that accepts an existing MatchState (shared with PatternBuilder/boxes).
     * This is the canonical entry point — boxes must be built with this same ms.
     */
    public boolean exec(String subjVar, String subjVal,
                        bb_box.MatchState ms,
                        bb_box root,     boolean hasRepl,
                        String replVal, boolean anchor) {

        // Sync MatchState subject in case it changed
        ms.sigma = subjVal;
        ms.omega = subjVal.length();

        if (root == null) {
            if (hasRepl && subjVar != null) vars.set(subjVar, replVal);
            return true;
        }

        int scanLimit = anchor ? 0 : ms.omega;

        for (int scanPos = 0; scanPos <= scanLimit; scanPos++) {
            ms.delta = scanPos;

            try {
                bb_box.Spec result = root.α();

                if (result != null) {
                    // Match succeeded at scanPos
                    int matchStart = scanPos;
                    int matchEnd   = ms.delta;           // cursor after match

                    // Phase 5: commit deferred captures
                    for (bb_capture c : pendingCaptures) c.commitPending();

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
                // (root.β() would extend, but scan loop advances cursor instead)

            } catch (bb_abort.AbortException e) {
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
