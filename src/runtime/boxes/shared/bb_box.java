package bb;

/**
 * bb_box.java — JVM Byrd Box Runtime
 *
 * Direct port of bb_box.h / bb_*.c to Java.
 * Every box is a bb_box instance with two entry points: α() and β().
 * Return null  → ω fired (failure).
 * Return Spec  → γ fired (success, value = matched substring).
 *
 * Global match state (shared across all boxes during one match):
 *   MatchState.sigma  — subject string   (Σ in C)
 *   MatchState.delta  — cursor int[]     (Δ — mutable, shared by ref)
 *   MatchState.omega  — subject length   (Ω in C)
 *
 * Canonical three-column layout preserved in comments for each box.
 */
public abstract class bb_box {

    /* ── Entry port constants (mirrors bb_box.h α=0, β=1) ─────────────── */
    public static final int Α = 0;
    public static final int Β = 1;

    /* ── Spec — substring of subject (mirrors spec_t) ───────────────────── */
    public static final class Spec {
        public final int start;   /* offset into MatchState.sigma */
        public final int len;     /* number of chars matched      */
        public Spec(int start, int len) { this.start = start; this.len = len; }
    }

    /* ── MatchState — global Σ/Δ/Ω (one per active match) ──────────────── */
    public static final class MatchState {
        public String sigma;      /* Σ — subject string         */
        public int    delta;      /* Δ — cursor                 */
        public int    omega;      /* Ω — subject length         */
        public MatchState(String s) { sigma=s; delta=0; omega=s.length(); }
    }

    /* Every box holds a reference to the shared match state */
    protected MatchState ms;

    protected bb_box(MatchState ms) { this.ms = ms; }

    /* The two entry ports every subclass must implement */
    public abstract Spec α();   /* α — fresh entry      */
    public abstract Spec β();   /* β — backtrack re-entry */

    /** Convenience: dispatch by entry constant */
    public final Spec call(int entry) {
        return entry == Α ? α() : β();
    }
}
