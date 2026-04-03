package bb;

/**
 * bb_abort.java — ABORT: always ω on both ports — force match failure entirely
 * Port of bb_abort.c / bb_abort.s
 *
 * Unlike FAIL (which merely backtracks), ABORT signals the executor to
 * abandon the entire match and take the :F branch immediately.
 * The executor checks for this via the AbortException.
 */
public class bb_abort extends bb_box {

    /** Thrown by bb_abort to signal immediate match termination */
    public static final class AbortException extends RuntimeException {
        public AbortException() { super(null, null, true, false); }
    }

    public bb_abort(MatchState ms) { super(ms); }

    @Override public Spec α() { throw new AbortException(); }
    @Override public Spec β()  { throw new AbortException(); }
}
