package driver.jvm;


import bb.bb_box;
import bb.bb_executor;
import bb.bb_dvar;
import bb.bb_capture;
import bb.bb_atp;
import bb.bb_abort;
import bb.bb_lit;
import bb.bb_alt;
import bb.bb_seq;
import bb.bb_arb;
import bb.bb_rem;
import bb.bb_fail;
import bb.bb_succeed;
import bb.bb_fence;
import bb.bb_eps;
import bb.bb_arbno;
import bb.bb_any;
import bb.bb_notany;
import bb.bb_span;
import bb.bb_brk;
import bb.bb_breakx;
import bb.bb_len;
import bb.bb_pos;
import bb.bb_rpos;
import bb.bb_tab;
import bb.bb_rtab;
import bb.bb_interr;
import bb.bb_bal;
import bb.bb_not;
import bb.bb_atp;
import bb.bb_dvar;
import java.util.List;

/**
 * PatternBuilder.java — walks a Parser.ExprNode pattern tree and
 * instantiates a bb_box graph using the Java oracle classes.
 *
 * All box classes are in the default package (same compilation unit).
 * The Interpreter supplies VarSetter/IntSetter callbacks for captures.
 *
 * EKind → bb_box mapping:
 *   E_QLIT / E_ILIT / E_FLIT  → bb_lit
 *   E_NUL                      → bb_lit("") zero-width
 *   E_ARB  / E_VAR "ARB"       → bb_arb
 *   E_REM  / E_VAR "REM"       → bb_rem
 *   E_FAIL / E_VAR "FAIL"      → bb_fail
 *   E_SUCCEED / E_VAR "SUCCEED"→ bb_succeed
 *   E_FENCE / E_VAR "FENCE"    → bb_fence
 *   E_ABORT / E_VAR "ABORT"    → bb_abort
 *   E_SEQ / E_CAT              → bb_seq chain (left-fold binary tree)
 *   E_ALT                      → bb_alt(children...)
 *   E_FNC ANY                  → bb_any
 *   E_FNC NOTANY               → bb_notany
 *   E_FNC SPAN                 → bb_span
 *   E_FNC BREAK                → bb_brk
 *   E_FNC BREAKX               → bb_breakx
 *   E_FNC LEN                  → bb_len
 *   E_FNC POS                  → bb_pos
 *   E_FNC RPOS                 → bb_rpos
 *   E_FNC TAB                  → bb_tab
 *   E_FNC RTAB                 → bb_rtab
 *   E_FNC ARBNO                → bb_arbno
 *   E_CAPT_IMMED_ASGN ($var)   → bb_capture(immediate=true)
 *   E_CAPT_COND_ASGN  (.var)   → bb_capture(immediate=false)
 *   E_CAPT_CURSOR     (@var)   → bb_atp
 */
class PatternBuilder {

    /** Callback for string variable assignment (used by bb_capture). */
    interface VarSetter {
        void set(String name, String value);
    }

    /** Callback for integer variable assignment (used by bb_atp). */
    interface IntSetter {
        void set(String name, int value);
    }

    /** Callback for variable lookup (used by intArg for POS/LEN/TAB with var args). */
    interface VarGetter {
        String get(String name);
    }

    private final bb.bb_box.MatchState ms;
    private final VarSetter         varSetter;
    private final IntSetter         intSetter;
    private final bb.bb_dvar.BoxResolver varResolver;
    private final VarGetter         varGetter;
    /** Deferred (.var) captures registered for Phase-5 commit.
     *  May be shared with inner PatternBuilders (for PAT-valued variable expansion)
     *  so that inner .var captures are committed when the outer match succeeds.
     */
    private final java.util.List<bb_capture> deferred;

    PatternBuilder(bb.bb_box.MatchState ms, VarSetter varSetter, IntSetter intSetter,
                   bb.bb_dvar.BoxResolver varResolver) {
        this(ms, varSetter, intSetter, varResolver, null, null);
    }

    PatternBuilder(bb.bb_box.MatchState ms, VarSetter varSetter, IntSetter intSetter,
                   bb.bb_dvar.BoxResolver varResolver, VarGetter varGetter) {
        this(ms, varSetter, intSetter, varResolver, varGetter, null);
    }

    /** Constructor with shared external deferred list — inner builders use this
     *  so their .var captures are visible to the outer executor. */
    PatternBuilder(bb.bb_box.MatchState ms, VarSetter varSetter, IntSetter intSetter,
                   bb.bb_dvar.BoxResolver varResolver, VarGetter varGetter,
                   java.util.List<bb_capture> sharedDeferred) {
        this.ms          = ms;
        this.varSetter   = varSetter;
        this.intSetter   = intSetter;
        this.varResolver = varResolver;
        this.varGetter   = varGetter;
        this.deferred    = sharedDeferred != null ? sharedDeferred : new java.util.ArrayList<>();
    }

    /** Return list of deferred captures to commit on :S (Phase 5). */
    java.util.List<bb_capture> deferredCaptures() { return deferred; }

    /** Entry point: build a bb_box graph from a pattern ExprNode. */
    bb_box build(Parser.ExprNode e) {
        if (e == null) return new bb_eps(ms);

        switch (e.kind) {

            // ── Literals ─────────────────────────────────────────────────────
            case E_QLIT:
                return new bb_lit(ms, e.sval != null ? e.sval : "");
            case E_ILIT:
                return new bb_lit(ms, e.sval != null ? e.sval :
                                      String.valueOf(e.ival));
            case E_FLIT:
                return new bb_lit(ms, e.sval != null ? e.sval :
                                      String.valueOf(e.dval));
            case E_NUL:
                return new bb_lit(ms, "");

            // ── Bare pattern keyword nodes ────────────────────────────────────
            case E_ARB:     return new bb_arb(ms);
            case E_REM:     return new bb_rem(ms);
            case E_FAIL:    return new bb_fail(ms);
            case E_SUCCEED: return new bb_succeed(ms);
            case E_FENCE:   return new bb_fence(ms);
            case E_ABORT:   return new bb_abort(ms);

            // ── E_VAR: may be a bare keyword pattern or a pattern-valued var ──
            case E_VAR: {
                String name = e.sval != null ? e.sval.toUpperCase() : "";
                switch (name) {
                    case "ARB":     return new bb_arb(ms);
                    case "REM":     return new bb_rem(ms);
                    case "FAIL":    return new bb_fail(ms);
                    case "SUCCEED": return new bb_succeed(ms);
                    case "FENCE":   return new bb_fence(ms);
                    case "ABORT":   return new bb_abort(ms);
                    default:
                        // Pattern-valued variable: dereference at match time via bb_dvar
                        // bb_dvar looks up the variable each time α() is called.
                        // We pass a supplier lambda as the resolver.
                        final String varName = e.sval;
                        return new bb_dvar(ms, varName, varResolver);
                }
            }

            // ── Sequence / concatenation ──────────────────────────────────────
            case E_SEQ:
            case E_CAT:
                return buildSeqChain(e.children);

            // ── Alternation ───────────────────────────────────────────────────
            case E_ALT: {
                bb_box[] kids = e.children.stream()
                    .map(this::build)
                    .toArray(bb_box[]::new);
                return new bb_alt(ms, kids);
            }

            // ── Function calls: ANY NOTANY SPAN BREAK BREAKX LEN POS RPOS TAB RTAB ARBNO ──
            case E_FNC: {
                String fn = e.sval != null ? e.sval.toUpperCase() : "";
                List<Parser.ExprNode> args = e.children;
                switch (fn) {
                    case "ANY":
                        return new bb_any(ms, strArg(args, 0));
                    case "NOTANY":
                        return new bb_notany(ms, strArg(args, 0));
                    case "SPAN":
                        return new bb_span(ms, strArg(args, 0));
                    case "BREAK":
                        return new bb_brk(ms, strArg(args, 0));
                    case "BREAKX":
                        return new bb_breakx(ms, strArg(args, 0));
                    case "LEN":
                        return new bb_len(ms, dynIntArg(args, 0));
                    case "POS":
                        return new bb_pos(ms, dynIntArg(args, 0));
                    case "RPOS":
                        return new bb_rpos(ms, dynIntArg(args, 0));
                    case "TAB":
                        return new bb_tab(ms, dynIntArg(args, 0));
                    case "RTAB":
                        return new bb_rtab(ms, dynIntArg(args, 0));
                    case "ARBNO": {
                        bb_box body = args.isEmpty() ? new bb_eps(ms) : build(args.get(0));
                        return new bb_arbno(ms, body);
                    }
                    default:
                        // Unknown function in pattern context — fail safe
                        return new bb_fail(ms);
                }
            }

            // ── Captures ─────────────────────────────────────────────────────
            case E_CAPT_IMMED_ASGN: {
                // $var — immediate assign on every γ
                String varName = captureVarName(e);
                bb_box child   = e.children.size() > 0 ? build(e.children.get(0)) : new bb_eps(ms);
                bb.bb_capture.VarSetter vs = (n, v) -> varSetter.set(n, v);
                return new bb_capture(ms, child, varName, true, vs);
            }
            case E_CAPT_COND_ASGN: {
                // .var — deferred assign on :S
                String varName = captureVarName(e);
                bb_box child   = e.children.size() > 0 ? build(e.children.get(0)) : new bb_eps(ms);
                bb.bb_capture.VarSetter vs = (n, v) -> varSetter.set(n, v);
                bb_capture cap = new bb_capture(ms, child, varName, false, vs);
                deferred.add(cap);
                return cap;
            }
            case E_CAPT_CURSOR: {
                // @var — write cursor position as integer
                String varName = captureVarName(e);
                bb.bb_atp.IntSetter is = (n, v) -> intSetter.set(n, v);
                return new bb_atp(ms, varName, is);
            }

            // ── Interrogate (?pat) — succeed/fail inversion ───────────────────
            case E_INTERROGATE: {
                bb_box child = e.children.isEmpty() ? new bb_eps(ms) : build(e.children.get(0));
                return new bb_interr(ms, child);
            }

            // ── *var — indirect pattern dereference (E_DEFER) ────────────────
            case E_DEFER: {
                if (e.children.isEmpty()) return new bb_fail(ms);
                Parser.ExprNode inner = e.children.get(0);
                String varName = (inner.kind == Parser.EKind.E_VAR && inner.sval != null)
                                 ? inner.sval : "";
                if (varName.isEmpty()) return new bb_fail(ms);
                return new bb_dvar(ms, varName, varResolver);
            }

            // ── Fallback ─────────────────────────────────────────────────────
            default:
                // Unrecognised node in pattern context — treat as failure
                return new bb_fail(ms);
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** Left-fold a list of children into a binary bb_seq chain. */
    private bb_box buildSeqChain(List<Parser.ExprNode> children) {
        if (children == null || children.isEmpty()) return new bb_eps(ms);
        bb_box acc = build(children.get(0));
        for (int i = 1; i < children.size(); i++) {
            acc = new bb_seq(ms, acc, build(children.get(i)));
        }
        return acc;
    }

    /** Extract string argument from arg list (literal only). */
    private String strArg(List<Parser.ExprNode> args, int idx) {
        if (idx >= args.size()) return "";
        Parser.ExprNode a = args.get(idx);
        if (a.sval != null) return a.sval;
        return "";
    }

    /** Extract integer argument — static version (literal int only). */
    private int intArg(List<Parser.ExprNode> args, int idx) {
        if (idx >= args.size()) return 0;
        Parser.ExprNode a = args.get(idx);
        if (a.kind == Parser.EKind.E_ILIT) return (int) a.ival;
        if (a.sval != null) {
            try { return Integer.parseInt(a.sval.trim()); } catch (NumberFormatException ex) {}
        }
        return 0;
    }

    /** Extract integer argument — dynamic version.
     *  Returns an IntSupplier so POS/LEN/TAB/RPOS/RTAB re-evaluate at match time
     *  when the argument is a variable name.
     */
    private java.util.function.IntSupplier dynIntArg(List<Parser.ExprNode> args, int idx) {
        if (idx >= args.size()) return () -> 0;
        Parser.ExprNode a = args.get(idx);
        if (a.kind == Parser.EKind.E_ILIT) {
            int v = (int) a.ival;
            return () -> v;
        }
        if (a.kind == Parser.EKind.E_VAR && a.sval != null && varGetter != null) {
            final String nm = a.sval;
            final VarGetter vg = varGetter;
            return () -> {
                String s = vg.get(nm);
                try { return Integer.parseInt(s.trim()); } catch (NumberFormatException ex) { return 0; }
            };
        }
        if (a.sval != null) {
            try { int v = Integer.parseInt(a.sval.trim()); return () -> v; }
            catch (NumberFormatException ex) {}
        }
        return () -> 0;
    }

    /** Extract capture variable name from a capture node.
     *  Binary form: E_CAPT_COND_ASGN(left=patternChild, right=varNode)
     *  The variable is always the LAST child (index 1 for binary nodes).
     */
    private String captureVarName(Parser.ExprNode e) {
        // sval directly on node (unary form)
        if (e.sval != null && !e.sval.isEmpty()) return e.sval;
        // Binary form: last child is the variable node
        if (!e.children.isEmpty()) {
            Parser.ExprNode last = e.children.get(e.children.size() - 1);
            if (last.kind == Parser.EKind.E_VAR && last.sval != null) return last.sval;
        }
        return "";
    }
}
