package driver.jvm;

import java.io.*;
import java.util.*;

/**
 * Interpreter.java — SNOBOL4 tree-walk interpreter for the JVM.
 *
 * Mirrors scrip-interp.c exactly for non-pattern statements:
 *   Phase 1: resolve subject (E_VAR → var store, or eval)
 *   Phase 5: assignment (has_eq) → eval replacement → NV_SET
 *   Goto: uncond / :S / :F → label dispatch
 *   OUTPUT: flush to PrintStream
 *   INPUT: read from BufferedReader
 *
 * Pattern matching (Phase 2–4) is stubbed — returns FAIL for now.
 * That gates M-JVM-INTERP-A04.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
public class Interpreter {

    // ══════════════════════════════════════════════════════════════════════════
    // SnoVal — runtime value (mirrors DESCR_t from snobol4.h)
    // ══════════════════════════════════════════════════════════════════════════

    public enum VType { SNUL, STR, INT, REAL, FAIL }

    public static final class SnoVal {
        public final VType  type;
        public final String sval;   // STR / SNUL
        public final long   ival;   // INT
        public final double dval;   // REAL

        private SnoVal(VType t, String s, long i, double d) {
            type = t; sval = s; ival = i; dval = d;
        }

        public static final SnoVal NUL  = new SnoVal(VType.SNUL, "", 0, 0);
        public static final SnoVal FAIL = new SnoVal(VType.FAIL, null, 0, 0);

        public static SnoVal str(String s)  { return new SnoVal(VType.STR,  s != null ? s : "", 0, 0); }
        public static SnoVal intv(long i)   { return new SnoVal(VType.INT,  null, i, 0); }
        public static SnoVal realv(double d){ return new SnoVal(VType.REAL, null, 0, d); }

        public boolean isFail() { return type == VType.FAIL; }
        public boolean isNull() { return type == VType.SNUL || (type == VType.STR && (sval == null || sval.isEmpty())); }

        /** Convert to SNOBOL4 string representation (mirrors VARVAL_fn). */
        public String toSnoStr() {
            switch (type) {
                case SNUL: return "";
                case STR:  return sval != null ? sval : "";
                case INT:  return Long.toString(ival);
                case REAL: {
                    // SNOBOL4 real format: trailing dot for whole numbers, no trailing zeros
                    if (dval == Math.floor(dval) && !Double.isInfinite(dval))
                        return Long.toString((long)dval) + ".";
                    // strip trailing zeros after decimal point
                    String s = Double.toString(dval);
                    // Java gives "1.5E10" etc; SNOBOL4 uses fixed notation
                    if (s.contains("E") || s.contains("e")) {
                        // format as fixed
                        s = String.format("%.10g", dval).replaceAll("0+$", "").replaceAll("\\.$", ".");
                    } else {
                        s = s.replaceAll("0+$", "").replaceAll("\\.$", ".");
                    }
                    return s;
                }
                default: return "";
            }
        }

        @Override public String toString() {
            switch (type) {
                case FAIL: return "<FAIL>";
                case SNUL: return "<NUL>";
                default:   return toSnoStr();
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Variable store (mirrors NV_GET_fn / NV_SET_fn)
    // ══════════════════════════════════════════════════════════════════════════

    private final Map<String,SnoVal> nv = new HashMap<>();

    private SnoVal nvGet(String name) {
        if (name == null) return SnoVal.NUL;
        SnoVal v = nv.get(name.toUpperCase());
        return v != null ? v : SnoVal.NUL;
    }

    private void nvSet(String name, SnoVal val) {
        if (name == null) return;
        String key = name.toUpperCase();
        // OUTPUT association: write to output stream
        if (key.equals("OUTPUT")) {
            out.println(val.toSnoStr());
            return;
        }
        nv.put(key, val);
    }

    // ══════════════════════════════════════════════════════════════════════════
    // I/O streams
    // ══════════════════════════════════════════════════════════════════════════

    private final PrintStream   out;
    private final BufferedReader in;

    public Interpreter() {
        this(System.out, new BufferedReader(new InputStreamReader(System.in)));
    }

    public Interpreter(PrintStream out, BufferedReader in) {
        this.out = out;
        this.in  = in;
        // Pre-populate keywords with defaults
        nv.put("ANCHOR",   SnoVal.intv(0));
        nv.put("TRIM",     SnoVal.intv(0));
        nv.put("MAXLNGTH", SnoVal.intv(5000));
        nv.put("STLIMIT",  SnoVal.intv(1000000));
        nv.put("STCOUNT",  SnoVal.intv(0));
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Label table
    // ══════════════════════════════════════════════════════════════════════════

    private final Map<String,Integer> labelIndex = new HashMap<>();

    private void buildLabels(Parser.StmtNode[] stmts) {
        for (int i = 0; i < stmts.length; i++) {
            if (stmts[i].label != null) {
                labelIndex.put(stmts[i].label.toUpperCase(), i);
            }
        }
    }

    private int labelLookup(String name) {
        if (name == null) return -1;
        Integer idx = labelIndex.get(name.toUpperCase());
        return idx != null ? idx : -1;
    }

    // ══════════════════════════════════════════════════════════════════════════
    // eval — mirrors interp_eval (non-pattern expressions)
    // ══════════════════════════════════════════════════════════════════════════

    public SnoVal eval(Parser.ExprNode e) {
        if (e == null) return SnoVal.NUL;

        switch (e.kind) {
            case E_ILIT: return SnoVal.intv(e.ival);
            case E_FLIT: return SnoVal.realv(e.dval);
            case E_QLIT: return SnoVal.str(e.sval != null ? e.sval : "");
            case E_NUL:  return SnoVal.NUL;

            case E_VAR: {
                if (e.sval == null || e.sval.isEmpty()) return SnoVal.NUL;
                // INPUT association
                if (e.sval.equalsIgnoreCase("INPUT")) {
                    try {
                        String line = in.readLine();
                        if (line == null) return SnoVal.FAIL;
                        return SnoVal.str(line);
                    } catch (IOException ex) { return SnoVal.FAIL; }
                }
                return nvGet(e.sval);
            }

            case E_KEYWORD:
                return nvGet(e.sval);

            case E_INTERROGATE: {
                if (e.children.isEmpty()) return SnoVal.FAIL;
                SnoVal v = eval(e.children.get(0));
                return v.isFail() ? SnoVal.FAIL : SnoVal.NUL;
            }

            case E_NAME: {
                if (e.children.isEmpty()) return SnoVal.FAIL;
                Parser.ExprNode child = e.children.get(0);
                if (child.kind == Parser.EKind.E_VAR && child.sval != null)
                    return SnoVal.str(child.sval);
                return eval(child);
            }

            case E_MNS: {
                if (e.children.isEmpty()) return SnoVal.FAIL;
                SnoVal v = eval(e.children.get(0));
                if (v.isFail()) return SnoVal.FAIL;
                return neg(v);
            }

            case E_PLS: {
                if (e.children.isEmpty()) return SnoVal.FAIL;
                SnoVal v = eval(e.children.get(0));
                if (v.isFail()) return SnoVal.FAIL;
                return toNumeric(v);
            }

            case E_ADD: return arith2(e, '+');
            case E_SUB: return arith2(e, '-');
            case E_MUL: return arith2(e, '*');
            case E_DIV: return arith2(e, '/');
            case E_POW: return arith2(e, '^');

            case E_CAT:
            case E_SEQ: {
                if (e.children.isEmpty()) return SnoVal.NUL;
                SnoVal acc = eval(e.children.get(0));
                if (acc.isFail()) return SnoVal.FAIL;
                for (int i = 1; i < e.children.size(); i++) {
                    SnoVal nxt = eval(e.children.get(i));
                    if (nxt.isFail()) return SnoVal.FAIL;
                    acc = concat(acc, nxt);
                }
                return acc;
            }

            case E_ALT: {
                // Pattern alternation — return first non-failing child value
                for (Parser.ExprNode child : e.children) {
                    SnoVal v = eval(child);
                    if (!v.isFail()) return v;
                }
                return SnoVal.FAIL;
            }

            case E_INDIRECT: {
                if (e.children.isEmpty()) return SnoVal.FAIL;
                Parser.ExprNode child = e.children.get(0);
                // Unwrap E_NAME wrapper (from $.var parse)
                if (child.kind == Parser.EKind.E_NAME && !child.children.isEmpty())
                    child = child.children.get(0);
                if (child.kind == Parser.EKind.E_VAR && child.sval != null)
                    return nvGet(child.sval);
                SnoVal nameVal = eval(child);
                if (nameVal.isFail()) return SnoVal.FAIL;
                return nvGet(nameVal.toSnoStr());
            }

            case E_ASSIGN: {
                if (e.children.size() < 2) return SnoVal.FAIL;
                SnoVal val = eval(e.children.get(1));
                if (val.isFail()) return SnoVal.FAIL;
                assignTo(e.children.get(0), val);
                return val;
            }

            case E_FNC: {
                if (e.sval == null) return SnoVal.FAIL;
                List<SnoVal> args = new ArrayList<>();
                for (Parser.ExprNode c : e.children) args.add(eval(c));
                return callBuiltin(e.sval.toUpperCase(), args);
            }

            case E_IDX: {
                // arr<i> or arr<i,j>
                if (e.children.size() < 2) return SnoVal.FAIL;
                SnoVal base = eval(e.children.get(0));
                if (base.isFail()) return SnoVal.FAIL;
                // For strings: SUBSTR-like access — SNOBOL4 doesn't directly do this
                // but arrays are represented as string-keyed maps stored in NV
                // For now: return NUL (array support is M-JVM-INTERP-A05+)
                return SnoVal.NUL;
            }

            // Captures — used in pattern context; in value context just eval child
            case E_CAPT_IMMED_ASGN:
            case E_CAPT_COND_ASGN: {
                if (e.children.size() >= 2) {
                    SnoVal v = eval(e.children.get(0));
                    if (!v.isFail()) {
                        Parser.ExprNode lv = e.children.get(1);
                        if (lv.kind == Parser.EKind.E_VAR && lv.sval != null)
                            nvSet(lv.sval, v);
                    }
                    return v.isFail() ? SnoVal.FAIL : eval(e.children.get(0));
                }
                if (!e.children.isEmpty()) return eval(e.children.get(0));
                return SnoVal.FAIL;
            }

            case E_CAPT_CURSOR:
                return SnoVal.NUL; // cursor capture — pattern context only

            case E_DEFER:
                // *X — deferred expression (eval X, then eval result as code)
                if (!e.children.isEmpty()) {
                    SnoVal v = eval(e.children.get(0));
                    if (v.isFail()) return SnoVal.FAIL;
                    // Simple case: v is a variable name — dereference it
                    return nvGet(v.toSnoStr());
                }
                return SnoVal.FAIL;

            default:
                return SnoVal.NUL;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Arithmetic helpers
    // ══════════════════════════════════════════════════════════════════════════

    private SnoVal arith2(Parser.ExprNode e, char op) {
        if (e.children.size() < 2) return SnoVal.FAIL;
        SnoVal l = eval(e.children.get(0));
        SnoVal r = eval(e.children.get(1));
        if (l.isFail() || r.isFail()) return SnoVal.FAIL;
        return arith(l, r, op);
    }

    private SnoVal arith(SnoVal l, SnoVal r, char op) {
        // Coerce to numeric
        double lv = toDouble(l), rv = toDouble(r);
        boolean useInt = isIntVal(l) && isIntVal(r);
        double result;
        switch (op) {
            case '+': result = lv + rv; break;
            case '-': result = lv - rv; break;
            case '*': result = lv * rv; break;
            case '/':
                if (rv == 0) return SnoVal.FAIL; // div by zero → fail
                // SNOBOL4: integer / integer = integer (truncated toward zero)
                if (isIntVal(l) && isIntVal(r)) {
                    return SnoVal.intv((long)lv / (long)rv);
                }
                result = lv / rv;
                break;
            case '^': result = Math.pow(lv, rv); break;
            default:  return SnoVal.FAIL;
        }
        if (useInt) return SnoVal.intv((long)result);
        // If result is whole number but operands were real, keep real
        return SnoVal.realv(result);
    }

    private boolean isIntVal(SnoVal v) {
        if (v.type == VType.INT) return true;
        if (v.type == VType.STR || v.type == VType.SNUL) {
            String s = v.toSnoStr().trim();
            if (s.isEmpty()) return true;
            try { Long.parseLong(s); return true; } catch (NumberFormatException e) {}
        }
        return false;
    }

    private double toDouble(SnoVal v) {
        switch (v.type) {
            case INT:  return (double) v.ival;
            case REAL: return v.dval;
            case STR: case SNUL: {
                String s = v.toSnoStr().trim();
                if (s.isEmpty()) return 0;
                try { return (double) Long.parseLong(s); } catch (NumberFormatException e) {}
                try { return Double.parseDouble(s); } catch (NumberFormatException e2) {}
                return 0;
            }
            default: return 0;
        }
    }

    private SnoVal neg(SnoVal v) {
        switch (v.type) {
            case INT:  return SnoVal.intv(-v.ival);
            case REAL: return SnoVal.realv(-v.dval);
            case STR: case SNUL: {
                String s = v.toSnoStr().trim();
                if (s.isEmpty()) return SnoVal.intv(0);
                try { return SnoVal.intv(-Long.parseLong(s)); } catch (NumberFormatException e) {}
                try { return SnoVal.realv(-Double.parseDouble(s)); } catch (NumberFormatException e2) {}
                return SnoVal.intv(0);
            }
            default: return SnoVal.FAIL;
        }
    }

    private SnoVal toNumeric(SnoVal v) {
        switch (v.type) {
            case INT: case REAL: return v;
            case STR: case SNUL: {
                String s = v.toSnoStr().trim();
                if (s.isEmpty()) return SnoVal.intv(0);
                try { return SnoVal.intv(Long.parseLong(s)); } catch (NumberFormatException e) {}
                try { return SnoVal.realv(Double.parseDouble(s)); } catch (NumberFormatException e2) {}
                return SnoVal.intv(0);
            }
            default: return SnoVal.FAIL;
        }
    }

    private SnoVal concat(SnoVal a, SnoVal b) {
        return SnoVal.str(a.toSnoStr() + b.toSnoStr());
    }

    // ══════════════════════════════════════════════════════════════════════════
    // LValue assignment helper
    // ══════════════════════════════════════════════════════════════════════════

    private void assignTo(Parser.ExprNode lv, SnoVal val) {
        if (lv == null) return;
        switch (lv.kind) {
            case E_VAR:
                if (lv.sval != null) nvSet(lv.sval, val);
                break;
            case E_KEYWORD:
                if (lv.sval != null) nv.put(lv.sval.toUpperCase(), val);
                break;
            case E_INDIRECT: {
                Parser.ExprNode child = lv.children.isEmpty() ? null : lv.children.get(0);
                if (child != null && child.kind == Parser.EKind.E_NAME && !child.children.isEmpty())
                    child = child.children.get(0);
                if (child != null && child.kind == Parser.EKind.E_VAR && child.sval != null) {
                    nvSet(child.sval, val);
                } else if (child != null) {
                    SnoVal nameVal = eval(child);
                    if (!nameVal.isFail()) nvSet(nameVal.toSnoStr(), val);
                }
                break;
            }
            default:
                // complex lvalue — best effort eval (captures etc.)
                break;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Built-in functions (minimal set for gate corpus)
    // ══════════════════════════════════════════════════════════════════════════

    private SnoVal callBuiltin(String name, List<SnoVal> args) {
        SnoVal a0 = args.size() > 0 ? args.get(0) : SnoVal.NUL;
        SnoVal a1 = args.size() > 1 ? args.get(1) : SnoVal.NUL;
        SnoVal a2 = args.size() > 2 ? args.get(2) : SnoVal.NUL;

        switch (name) {
            // String functions
            case "SIZE":   return SnoVal.intv(a0.toSnoStr().length());
            case "TRIM":   return SnoVal.str(a0.toSnoStr().trim());
            case "DUPL": {
                int n = (int) toDouble(a1);
                if (n <= 0) return SnoVal.str("");
                StringBuilder sb = new StringBuilder();
                String s = a0.toSnoStr();
                for (int i = 0; i < n; i++) sb.append(s);
                return SnoVal.str(sb.toString());
            }
            case "SUBSTR": {
                String s = a0.toSnoStr();
                int start = (int) toDouble(a1) - 1; // 1-based
                int len   = args.size() > 2 ? (int) toDouble(a2) : s.length() - start;
                if (start < 0) start = 0;
                int end = start + len;
                if (end > s.length()) end = s.length();
                if (start >= s.length()) return SnoVal.str("");
                return SnoVal.str(s.substring(start, end));
            }
            case "REPLACE": {
                // REPLACE(str, from, to) — char-by-char translation
                String s    = a0.toSnoStr();
                String from = a1.toSnoStr();
                String to   = a2.toSnoStr();
                StringBuilder sb = new StringBuilder(s.length());
                for (char c : s.toCharArray()) {
                    int idx = from.indexOf(c);
                    sb.append(idx >= 0 && idx < to.length() ? to.charAt(idx) : c);
                }
                return SnoVal.str(sb.toString());
            }
            case "REVERSE": {
                return SnoVal.str(new StringBuilder(a0.toSnoStr()).reverse().toString());
            }
            case "LPAD": {
                String s = a0.toSnoStr();
                int w = (int) toDouble(a1);
                String pad = args.size() > 2 ? a2.toSnoStr() : " ";
                if (pad.isEmpty()) pad = " ";
                while (s.length() < w) s = pad + s;
                return SnoVal.str(s);
            }
            case "RPAD": {
                String s = a0.toSnoStr();
                int w = (int) toDouble(a1);
                String pad = args.size() > 2 ? a2.toSnoStr() : " ";
                if (pad.isEmpty()) pad = " ";
                while (s.length() < w) s = s + pad;
                return SnoVal.str(s);
            }

            // Type conversion
            case "INTEGER": {
                String s = a0.toSnoStr().trim();
                try { return SnoVal.intv(Long.parseLong(s)); } catch (NumberFormatException e) {}
                return SnoVal.FAIL;
            }
            case "REAL": {
                String s = a0.toSnoStr().trim();
                try { return SnoVal.realv(Double.parseDouble(s)); } catch (NumberFormatException e) {}
                return SnoVal.FAIL;
            }
            case "STRING": return SnoVal.str(a0.toSnoStr());

            // Comparison functions (succeed by returning arg, fail otherwise)
            case "IDENT":  return a0.toSnoStr().equals(a1.toSnoStr()) ? a0 : SnoVal.FAIL;
            case "DIFFER": return a0.toSnoStr().equals(a1.toSnoStr()) ? SnoVal.FAIL : a0;
            case "EQ":     return toDouble(a0) == toDouble(a1) ? a0 : SnoVal.FAIL;
            case "NE":     return toDouble(a0) != toDouble(a1) ? a0 : SnoVal.FAIL;
            case "LT":     return toDouble(a0) <  toDouble(a1) ? a0 : SnoVal.FAIL;
            case "LE":     return toDouble(a0) <= toDouble(a1) ? a0 : SnoVal.FAIL;
            case "GT":     return toDouble(a0) >  toDouble(a1) ? a0 : SnoVal.FAIL;
            case "GE":     return toDouble(a0) >= toDouble(a1) ? a0 : SnoVal.FAIL;

            // I/O
            case "OUTPUT": out.println(a0.toSnoStr()); return a0;
            case "INPUT": {
                try {
                    String line = in.readLine();
                    return line != null ? SnoVal.str(line) : SnoVal.FAIL;
                } catch (IOException ex) { return SnoVal.FAIL; }
            }

            // Type predicates
            case "DATATYPE": {
                switch (a0.type) {
                    case INT:  return SnoVal.str("INTEGER");
                    case REAL: return SnoVal.str("REAL");
                    default:   return SnoVal.str("STRING");
                }
            }

            // Math
            case "ABS": {
                if (a0.type == VType.INT)  return SnoVal.intv(Math.abs(a0.ival));
                if (a0.type == VType.REAL) return SnoVal.realv(Math.abs(a0.dval));
                double d = toDouble(a0); return SnoVal.intv(Math.abs((long)d));
            }
            case "EXP":   return SnoVal.realv(Math.exp(toDouble(a0)));
            case "LOG":   { double v = toDouble(a0); return v <= 0 ? SnoVal.FAIL : SnoVal.realv(Math.log(v)); }
            case "SQRT":  { double v = toDouble(a0); return v < 0  ? SnoVal.FAIL : SnoVal.realv(Math.sqrt(v)); }
            case "SIN":   return SnoVal.realv(Math.sin(toDouble(a0)));
            case "COS":   return SnoVal.realv(Math.cos(toDouble(a0)));
            case "ATAN":  return SnoVal.realv(Math.atan(toDouble(a0)));

            // Succeed / Fail builtins
            case "SUCCEED": return SnoVal.NUL;
            case "FAIL":    return SnoVal.FAIL;
            case "FENCE":   return SnoVal.NUL;
            case "ABORT":   throw new SnobolAbort("ABORT called");

            default:
                // Unknown function — return NUL (not fail) for unknown calls
                // This lets programs with forward-declared functions not break
                return SnoVal.NUL;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Abort exception
    // ══════════════════════════════════════════════════════════════════════════

    public static class SnobolAbort extends RuntimeException {
        public SnobolAbort(String msg) { super(msg); }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // execute — run a compiled program
    // ══════════════════════════════════════════════════════════════════════════

    public int execute(Parser.StmtNode[] stmts) {
        buildLabels(stmts);
        int pc   = 0;
        int limit = 1_000_000; // mirrors &STLIMIT default

        while (pc < stmts.length && limit-- > 0) {
            Parser.StmtNode s = stmts[pc];

            if (s.isEnd) break;

            // Increment &STCOUNT
            SnoVal stcount = nvGet("STCOUNT");
            nv.put("STCOUNT", SnoVal.intv((stcount.type == VType.INT ? stcount.ival : 0) + 1));

            // ── Phase 1: resolve subject ──────────────────────────────────────
            String  subjName = null;
            SnoVal  subjVal  = SnoVal.NUL;

            if (s.subject != null) {
                if (s.subject.kind == Parser.EKind.E_VAR && s.subject.sval != null) {
                    subjName = s.subject.sval;
                    subjVal  = nvGet(subjName);
                } else {
                    subjVal = eval(s.subject);
                }
            }

            boolean succeeded = true;

            // ── Phase 2–4: pattern match (stub — FAIL for now) ───────────────
            if (s.pattern != null) {
                // Pattern matching not yet implemented — always fail
                // TODO M-JVM-INTERP-A04: integrate Byrd box executor
                succeeded = false;
            }
            // ── Phase 5: assignment ───────────────────────────────────────────
            else if (s.hasEq && subjName != null) {
                SnoVal replVal = s.replacement != null ? eval(s.replacement) : SnoVal.NUL;
                if (replVal.isFail()) {
                    succeeded = false;
                } else {
                    nvSet(subjName, replVal);
                    succeeded = true;
                }
            } else if (s.hasEq && s.subject != null
                       && s.subject.kind == Parser.EKind.E_KEYWORD && s.subject.sval != null) {
                SnoVal replVal = s.replacement != null ? eval(s.replacement) : SnoVal.NUL;
                if (replVal.isFail()) succeeded = false;
                else { nv.put(s.subject.sval.toUpperCase(), replVal); succeeded = true; }
            } else if (s.hasEq && s.subject != null
                       && s.subject.kind == Parser.EKind.E_INDIRECT) {
                Parser.ExprNode child = s.subject.children.isEmpty() ? null : s.subject.children.get(0);
                if (child != null && child.kind == Parser.EKind.E_NAME && !child.children.isEmpty())
                    child = child.children.get(0);
                String nm = null;
                if (child != null && child.kind == Parser.EKind.E_VAR && child.sval != null) {
                    nm = nvGet(child.sval).toSnoStr();
                } else if (child != null) {
                    nm = eval(child).toSnoStr();
                }
                if (nm == null || nm.isEmpty()) {
                    succeeded = false;
                } else {
                    SnoVal replVal = s.replacement != null ? eval(s.replacement) : SnoVal.NUL;
                    if (replVal.isFail()) succeeded = false;
                    else { nvSet(nm, replVal); succeeded = true; }
                }
            } else if (s.subject != null && !s.hasEq) {
                // Value statement — just evaluates subject; fails if subject fails
                if (subjVal.isFail()) succeeded = false;
            }

            // ── Goto dispatch ─────────────────────────────────────────────────
            int next = pc + 1;
            if (s.gotoField != null) {
                String target = null;
                if (s.gotoField.uncond != null)
                    target = s.gotoField.uncond;
                else if (succeeded && s.gotoField.onsuccess != null)
                    target = s.gotoField.onsuccess;
                else if (!succeeded && s.gotoField.onfailure != null)
                    target = s.gotoField.onfailure;

                if (target != null) {
                    if (target.equalsIgnoreCase("END")) break;
                    int dest = labelLookup(target);
                    if (dest >= 0) { pc = dest; continue; }
                    // label not found — treat as normal next
                }
            }
            pc = next;
        }
        return 0;
    }

    // ══════════════════════════════════════════════════════════════════════════
    // main — run a .sno file
    // ══════════════════════════════════════════════════════════════════════════

    public static void main(String[] args) throws IOException {
        if (args.length == 0) {
            System.err.println("usage: Interpreter <file.sno>");
            System.exit(1);
        }
        Parser.StmtNode[] stmts = Parser.parseFile(args[0]);
        new Interpreter().execute(stmts);
    }
}
