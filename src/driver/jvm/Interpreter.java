package driver.jvm;

import java.io.*;
import java.util.*;
import java.util.Arrays;

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
    // DESCR — runtime value (mirrors DESCR_t from snobol4.h)
    // ══════════════════════════════════════════════════════════════════════════

    public enum VType { SNUL, STR, INT, REAL, FAIL, PAT }

    public static final class DESCR {
        public final VType  type;
        public final String sval;        // STR / SNUL
        public final long   ival;        // INT
        public final double dval;        // REAL
        public final Parser.ExprNode patNode; // PAT — pattern-valued variable

        private DESCR(VType t, String s, long i, double d, Parser.ExprNode p) {
            type = t; sval = s; ival = i; dval = d; patNode = p;
        }

        public static final DESCR NUL  = new DESCR(VType.SNUL, "", 0, 0, null);
        public static final DESCR FAIL = new DESCR(VType.FAIL, null, 0, 0, null);

        public static DESCR str(String s)          { return new DESCR(VType.STR,  s != null ? s : "", 0, 0, null); }
        public static DESCR intv(long i)           { return new DESCR(VType.INT,  null, i, 0, null); }
        public static DESCR realv(double d)        { return new DESCR(VType.REAL, null, 0, d, null); }
        public static DESCR pat(Parser.ExprNode n) { return new DESCR(VType.PAT,  null, 0, 0, n); }

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
    // User-defined function table (DEFINE)
    // ══════════════════════════════════════════════════════════════════════════

    private static final class FuncDef {
        final String   name;
        final String[] params;
        final String[] locals;
        final String   entryLabel;

        FuncDef(String name, String[] params, String[] locals, String entryLabel) {
            this.name       = name;
            this.params     = params;
            this.locals     = locals;
            this.entryLabel = entryLabel;
        }
    }

    private final Map<String,FuncDef> funcTable = new HashMap<>();

    // Return/FReturn exceptions used for SNOBOL4 function return
    static class SnobolReturn  extends RuntimeException {
        final DESCR val;
        SnobolReturn(DESCR v)  { super(null,null,true,false); val = v; }
    }
    static class SnobolFReturn extends RuntimeException {
        SnobolFReturn()        { super(null,null,true,false); }
    }
    static class SnobolNReturn extends RuntimeException {
        SnobolNReturn()        { super(null,null,true,false); }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Variable store (mirrors NV_GET_fn / NV_SET_fn)
    // ══════════════════════════════════════════════════════════════════════════

    private final Map<String,DESCR> nv = new HashMap<>();

    private DESCR nvGet(String name) {
        if (name == null) return DESCR.NUL;
        DESCR v = nv.get(name.toUpperCase());
        return v != null ? v : DESCR.NUL;
    }

    private void nvSet(String name, DESCR val) {
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
        nv.put("ANCHOR",   DESCR.intv(0));
        nv.put("TRIM",     DESCR.intv(0));
        nv.put("MAXLNGTH", DESCR.intv(5000));
        nv.put("STLIMIT",  DESCR.intv(1000000));
        nv.put("STCOUNT",  DESCR.intv(0));
        // Standard character-set keywords
        nv.put("LCASE",    DESCR.str("abcdefghijklmnopqrstuvwxyz"));
        nv.put("UCASE",    DESCR.str("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
        // Build &ALPHABET: all 256 chars
        StringBuilder alpha = new StringBuilder(256);
        for (int i = 0; i < 256; i++) alpha.append((char)i);
        nv.put("ALPHABET", DESCR.str(alpha.toString()));
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

    public DESCR eval(Parser.ExprNode e) {
        if (e == null) return DESCR.NUL;

        switch (e.kind) {
            case E_ILIT: return DESCR.intv(e.ival);
            case E_FLIT: return DESCR.realv(e.dval);
            case E_QLIT: return DESCR.str(e.sval != null ? e.sval : "");
            case E_NUL:  return DESCR.NUL;

            case E_VAR: {
                if (e.sval == null || e.sval.isEmpty()) return DESCR.NUL;
                // INPUT association
                if (e.sval.equalsIgnoreCase("INPUT")) {
                    try {
                        String line = in.readLine();
                        if (line == null) return DESCR.FAIL;
                        return DESCR.str(line);
                    } catch (IOException ex) { return DESCR.FAIL; }
                }
                return nvGet(e.sval);
            }

            case E_KEYWORD:
                return nvGet(e.sval);

            case E_INTERROGATE: {
                if (e.children.isEmpty()) return DESCR.FAIL;
                DESCR v = eval(e.children.get(0));
                return v.isFail() ? DESCR.FAIL : DESCR.NUL;
            }

            case E_NAME: {
                if (e.children.isEmpty()) return DESCR.FAIL;
                Parser.ExprNode child = e.children.get(0);
                if (child.kind == Parser.EKind.E_VAR && child.sval != null)
                    return DESCR.str(child.sval);
                return eval(child);
            }

            case E_MNS: {
                if (e.children.isEmpty()) return DESCR.FAIL;
                DESCR v = eval(e.children.get(0));
                if (v.isFail()) return DESCR.FAIL;
                return neg(v);
            }

            case E_PLS: {
                if (e.children.isEmpty()) return DESCR.FAIL;
                DESCR v = eval(e.children.get(0));
                if (v.isFail()) return DESCR.FAIL;
                return toNumeric(v);
            }

            case E_ADD: return arith2(e, '+');
            case E_SUB: return arith2(e, '-');
            case E_MUL: return arith2(e, '*');
            case E_DIV: return arith2(e, '/');
            case E_POW: return arith2(e, '^');

            case E_CAT:
            case E_SEQ: {
                if (e.children.isEmpty()) return DESCR.NUL;
                DESCR acc = eval(e.children.get(0));
                if (acc.isFail()) return DESCR.FAIL;
                for (int i = 1; i < e.children.size(); i++) {
                    DESCR nxt = eval(e.children.get(i));
                    if (nxt.isFail()) return DESCR.FAIL;
                    acc = concat(acc, nxt);
                }
                return acc;
            }

            case E_ALT: {
                // Pattern alternation stored as a pattern-valued DESCR
                return DESCR.pat(e);
            }

            case E_INDIRECT: {
                if (e.children.isEmpty()) return DESCR.FAIL;
                Parser.ExprNode child = e.children.get(0);
                // Unwrap E_NAME wrapper (from $.var parse)
                if (child.kind == Parser.EKind.E_NAME && !child.children.isEmpty())
                    child = child.children.get(0);
                if (child.kind == Parser.EKind.E_VAR && child.sval != null)
                    return nvGet(child.sval);
                DESCR nameVal = eval(child);
                if (nameVal.isFail()) return DESCR.FAIL;
                return nvGet(nameVal.toSnoStr());
            }

            case E_ASSIGN: {
                if (e.children.size() < 2) return DESCR.FAIL;
                DESCR val = eval(e.children.get(1));
                if (val.isFail()) return DESCR.FAIL;
                assignTo(e.children.get(0), val);
                return val;
            }

            case E_FNC: {
                if (e.sval == null) return DESCR.FAIL;
                List<DESCR> args = new ArrayList<>();
                for (Parser.ExprNode c : e.children) args.add(eval(c));
                return callBuiltin(e.sval.toUpperCase(), args);
            }

            case E_IDX: {
                // arr<i> or arr<i,j>
                if (e.children.size() < 2) return DESCR.FAIL;
                DESCR base = eval(e.children.get(0));
                if (base.isFail()) return DESCR.FAIL;
                // For strings: SUBSTR-like access — SNOBOL4 doesn't directly do this
                // but arrays are represented as string-keyed maps stored in NV
                // For now: return NUL (array support is M-JVM-INTERP-A05+)
                return DESCR.NUL;
            }

            // Captures — used in pattern context; in value context just eval child
            case E_CAPT_IMMED_ASGN:
            case E_CAPT_COND_ASGN: {
                if (e.children.size() >= 2) {
                    DESCR v = eval(e.children.get(0));
                    if (!v.isFail()) {
                        Parser.ExprNode lv = e.children.get(1);
                        if (lv.kind == Parser.EKind.E_VAR && lv.sval != null)
                            nvSet(lv.sval, v);
                    }
                    return v.isFail() ? DESCR.FAIL : eval(e.children.get(0));
                }
                if (!e.children.isEmpty()) return eval(e.children.get(0));
                return DESCR.FAIL;
            }

            case E_CAPT_CURSOR:
                return DESCR.NUL; // cursor capture — pattern context only

            case E_DEFER:
                // *X — deferred expression (eval X, then eval result as code)
                if (!e.children.isEmpty()) {
                    DESCR v = eval(e.children.get(0));
                    if (v.isFail()) return DESCR.FAIL;
                    // Simple case: v is a variable name — dereference it
                    return nvGet(v.toSnoStr());
                }
                return DESCR.FAIL;

            default:
                return DESCR.NUL;
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Arithmetic helpers
    // ══════════════════════════════════════════════════════════════════════════

    private DESCR arith2(Parser.ExprNode e, char op) {
        if (e.children.size() < 2) return DESCR.FAIL;
        DESCR l = eval(e.children.get(0));
        DESCR r = eval(e.children.get(1));
        if (l.isFail() || r.isFail()) return DESCR.FAIL;
        return arith(l, r, op);
    }

    private DESCR arith(DESCR l, DESCR r, char op) {
        // Coerce to numeric
        double lv = toDouble(l), rv = toDouble(r);
        boolean useInt = isIntVal(l) && isIntVal(r);
        double result;
        switch (op) {
            case '+': result = lv + rv; break;
            case '-': result = lv - rv; break;
            case '*': result = lv * rv; break;
            case '/':
                if (rv == 0) return DESCR.FAIL; // div by zero → fail
                // SNOBOL4: integer / integer = integer (truncated toward zero)
                if (isIntVal(l) && isIntVal(r)) {
                    return DESCR.intv((long)lv / (long)rv);
                }
                result = lv / rv;
                break;
            case '^': result = Math.pow(lv, rv); break;
            default:  return DESCR.FAIL;
        }
        if (useInt) return DESCR.intv((long)result);
        // If result is whole number but operands were real, keep real
        return DESCR.realv(result);
    }

    private boolean isIntVal(DESCR v) {
        if (v.type == VType.INT) return true;
        if (v.type == VType.STR || v.type == VType.SNUL) {
            String s = v.toSnoStr().trim();
            if (s.isEmpty()) return true;
            try { Long.parseLong(s); return true; } catch (NumberFormatException e) {}
        }
        return false;
    }

    private double toDouble(DESCR v) {
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

    private DESCR neg(DESCR v) {
        switch (v.type) {
            case INT:  return DESCR.intv(-v.ival);
            case REAL: return DESCR.realv(-v.dval);
            case STR: case SNUL: {
                String s = v.toSnoStr().trim();
                if (s.isEmpty()) return DESCR.intv(0);
                try { return DESCR.intv(-Long.parseLong(s)); } catch (NumberFormatException e) {}
                try { return DESCR.realv(-Double.parseDouble(s)); } catch (NumberFormatException e2) {}
                return DESCR.intv(0);
            }
            default: return DESCR.FAIL;
        }
    }

    private DESCR toNumeric(DESCR v) {
        switch (v.type) {
            case INT: case REAL: return v;
            case STR: case SNUL: {
                String s = v.toSnoStr().trim();
                if (s.isEmpty()) return DESCR.intv(0);
                try { return DESCR.intv(Long.parseLong(s)); } catch (NumberFormatException e) {}
                try { return DESCR.realv(Double.parseDouble(s)); } catch (NumberFormatException e2) {}
                return DESCR.intv(0);
            }
            default: return DESCR.FAIL;
        }
    }

    private DESCR concat(DESCR a, DESCR b) {
        return DESCR.str(a.toSnoStr() + b.toSnoStr());
    }

    // ══════════════════════════════════════════════════════════════════════════
    // LValue assignment helper
    // ══════════════════════════════════════════════════════════════════════════

    private void assignTo(Parser.ExprNode lv, DESCR val) {
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
                    DESCR nameVal = eval(child);
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

    private DESCR callBuiltin(String name, List<DESCR> args) {
        DESCR a0 = args.size() > 0 ? args.get(0) : DESCR.NUL;
        DESCR a1 = args.size() > 1 ? args.get(1) : DESCR.NUL;
        DESCR a2 = args.size() > 2 ? args.get(2) : DESCR.NUL;

        // Propagate FAIL from any argument for most builtins
        // (IDENT/DIFFER/EQ/NE/etc. handle their own fail logic)
        switch (name) {
            case "IDENT": case "DIFFER": case "EQ": case "NE":
            case "LT": case "LE": case "GT": case "GE":
            case "DEFINE": case "PROTOTYPE": case "INPUT":
            case "OUTPUT": case "SUCCEED": case "FAIL": case "ABORT":
                break; // these handle FAIL themselves
            default:
                for (DESCR a : args) if (a.isFail()) return DESCR.FAIL;
        }

        switch (name) {
            // String functions
            case "SIZE":   return DESCR.intv(a0.toSnoStr().length());
            case "TRIM":   return DESCR.str(a0.toSnoStr().trim());
            case "DUPL": {
                int n = (int) toDouble(a1);
                if (n <= 0) return DESCR.str("");
                StringBuilder sb = new StringBuilder();
                String s = a0.toSnoStr();
                for (int i = 0; i < n; i++) sb.append(s);
                return DESCR.str(sb.toString());
            }
            case "SUBSTR": {
                String s = a0.toSnoStr();
                int start = (int) toDouble(a1) - 1; // 1-based
                int len   = args.size() > 2 ? (int) toDouble(a2) : s.length() - start;
                if (start < 0) start = 0;
                int end = start + len;
                if (end > s.length()) end = s.length();
                if (start >= s.length()) return DESCR.str("");
                return DESCR.str(s.substring(start, end));
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
                return DESCR.str(sb.toString());
            }
            case "REVERSE": {
                return DESCR.str(new StringBuilder(a0.toSnoStr()).reverse().toString());
            }
            case "LPAD": {
                String s = a0.toSnoStr();
                int w = (int) toDouble(a1);
                String pad = args.size() > 2 ? a2.toSnoStr() : " ";
                if (pad.isEmpty()) pad = " ";
                while (s.length() < w) s = pad + s;
                return DESCR.str(s);
            }
            case "RPAD": {
                String s = a0.toSnoStr();
                int w = (int) toDouble(a1);
                String pad = args.size() > 2 ? a2.toSnoStr() : " ";
                if (pad.isEmpty()) pad = " ";
                while (s.length() < w) s = s + pad;
                return DESCR.str(s);
            }

            // Type conversion
            case "INTEGER": {
                String s = a0.toSnoStr().trim();
                try { return DESCR.intv(Long.parseLong(s)); } catch (NumberFormatException e) {}
                return DESCR.FAIL;
            }
            case "REAL": {
                String s = a0.toSnoStr().trim();
                try { return DESCR.realv(Double.parseDouble(s)); } catch (NumberFormatException e) {}
                return DESCR.FAIL;
            }
            case "STRING": return DESCR.str(a0.toSnoStr());

            // Comparison functions (succeed by returning arg, fail otherwise)
            case "IDENT":  return a0.toSnoStr().equals(a1.toSnoStr()) ? a0 : DESCR.FAIL;
            case "DIFFER": return a0.toSnoStr().equals(a1.toSnoStr()) ? DESCR.FAIL : a0;
            case "EQ":     return toDouble(a0) == toDouble(a1) ? a0 : DESCR.FAIL;
            case "NE":     return toDouble(a0) != toDouble(a1) ? a0 : DESCR.FAIL;
            case "LT":     return toDouble(a0) <  toDouble(a1) ? a0 : DESCR.FAIL;
            case "LE":     return toDouble(a0) <= toDouble(a1) ? a0 : DESCR.FAIL;
            case "GT":     return toDouble(a0) >  toDouble(a1) ? a0 : DESCR.FAIL;
            case "GE":     return toDouble(a0) >= toDouble(a1) ? a0 : DESCR.FAIL;

            // I/O
            case "OUTPUT": out.println(a0.toSnoStr()); return a0;
            case "INPUT": {
                try {
                    String line = in.readLine();
                    return line != null ? DESCR.str(line) : DESCR.FAIL;
                } catch (IOException ex) { return DESCR.FAIL; }
            }

            // Type predicates
            case "DATATYPE": {
                switch (a0.type) {
                    case INT:  return DESCR.str("INTEGER");
                    case REAL: return DESCR.str("REAL");
                    default:   return DESCR.str("STRING");
                }
            }

            // Math
            case "ABS": {
                if (a0.type == VType.INT)  return DESCR.intv(Math.abs(a0.ival));
                if (a0.type == VType.REAL) return DESCR.realv(Math.abs(a0.dval));
                double d = toDouble(a0); return DESCR.intv(Math.abs((long)d));
            }
            case "EXP":   return DESCR.realv(Math.exp(toDouble(a0)));
            case "LOG":   { double v = toDouble(a0); return v <= 0 ? DESCR.FAIL : DESCR.realv(Math.log(v)); }
            case "SQRT":  { double v = toDouble(a0); return v < 0  ? DESCR.FAIL : DESCR.realv(Math.sqrt(v)); }
            case "SIN":   return DESCR.realv(Math.sin(toDouble(a0)));
            case "COS":   return DESCR.realv(Math.cos(toDouble(a0)));
            case "ATAN":  return DESCR.realv(Math.atan(toDouble(a0)));

            // Succeed / Fail builtins
            case "SUCCEED": return DESCR.NUL;
            case "FAIL":    return DESCR.FAIL;
            case "FENCE":   return DESCR.NUL;
            case "ABORT":   throw new SnobolAbort("ABORT called");

            // Integer remainder
            case "REMDR": {
                long lv = (long) toDouble(a0);
                long rv = (long) toDouble(a1);
                if (rv == 0) return DESCR.FAIL;
                return DESCR.intv(lv % rv);
            }

            // DEFINE('name(p1,p2)local1,local2', 'entryLabel')
            case "DEFINE": {
                String spec = a0.toSnoStr().trim();
                String entry = a1.isNull() ? null : a1.toSnoStr().trim();
                // Parse: name(p1,...) local1,...
                int lp = spec.indexOf('(');
                int rp = spec.indexOf(')');
                if (lp < 0) {
                    // no params: DEFINE('name')
                    String fname = spec.toUpperCase();
                    if (entry == null || entry.isEmpty()) entry = fname;
                    funcTable.put(fname, new FuncDef(fname, new String[0], new String[0], entry));
                } else {
                    String fname = spec.substring(0, lp).trim().toUpperCase();
                    String pStr  = lp + 1 <= rp - 1 ? spec.substring(lp + 1, rp).trim() : "";
                    String lStr  = rp + 1 < spec.length() ? spec.substring(rp + 1).trim() : "";
                    if (lStr.startsWith(",")) lStr = lStr.substring(1).trim();
                    String[] params = pStr.isEmpty() ? new String[0]
                        : Arrays.stream(pStr.split(",")).map(String::trim).map(String::toUpperCase).toArray(String[]::new);
                    String[] locs   = lStr.isEmpty() ? new String[0]
                        : Arrays.stream(lStr.split(",")).map(String::trim).map(String::toUpperCase).toArray(String[]::new);
                    if (entry == null || entry.isEmpty()) entry = fname;
                    funcTable.put(fname, new FuncDef(fname, params, locs, entry));
                }
                return DESCR.NUL;
            }

            // PROTOTYPE — synonym for DEFINE in some usages
            case "PROTOTYPE": return DESCR.NUL;

            default: {
                // Check user-defined function table
                FuncDef fd = funcTable.get(name);
                if (fd != null) {
                    return callUserFunc(fd, args);
                }
                // Unknown function — return NUL
                return DESCR.NUL;
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    // User-defined function call
    // ══════════════════════════════════════════════════════════════════════════

    private Parser.StmtNode[] programStmts; // set by execute() before running

    private DESCR callUserFunc(FuncDef fd, List<DESCR> args) {
        if (programStmts == null) return DESCR.FAIL;

        // Save caller's copies of param/local names
        Map<String,DESCR> saved = new HashMap<>();
        // Save function-name variable (return value slot)
        saved.put(fd.name, nvGet(fd.name));
        for (String p : fd.params) saved.put(p, nvGet(p));
        for (String l : fd.locals)  saved.put(l, nvGet(l));

        // Bind parameters
        for (int i = 0; i < fd.params.length; i++) {
            DESCR val = i < args.size() ? args.get(i) : DESCR.NUL;
            nv.put(fd.params[i], val);
        }
        // Clear locals
        for (String l : fd.locals) nv.put(l, DESCR.NUL);
        // Clear return slot
        nv.put(fd.name, DESCR.NUL);

        // Find entry label
        int entryPc = labelLookup(fd.entryLabel);
        if (entryPc < 0) {
            // restore and fail
            restore(saved);
            return DESCR.FAIL;
        }

        // Execute from entry label until RETURN/FRETURN/NReturn or END
        DESCR retVal = DESCR.NUL;
        boolean freturn = false;

        int pc    = entryPc;
        int limit = 1_000_000;
        try {
            while (pc < programStmts.length && limit-- > 0) {
                Parser.StmtNode s = programStmts[pc];
                if (s.isEnd) break;

                // Increment &STCOUNT
                DESCR stcount = nvGet("STCOUNT");
                nv.put("STCOUNT", DESCR.intv((stcount.type == VType.INT ? stcount.ival : 0) + 1));

                // Phase 1
                String  subjName = null;
                DESCR  subjVal  = DESCR.NUL;
                if (s.subject != null) {
                    if (s.subject.kind == Parser.EKind.E_VAR && s.subject.sval != null) {
                        subjName = s.subject.sval;
                        subjVal  = nvGet(subjName);
                    } else {
                        subjVal = eval(s.subject);
                    }
                }

                boolean succeeded = true;

                // Pattern matching (reuse outer logic)
                if (s.pattern != null) {
                    String sv = subjVal.toSnoStr();
                    boolean anchor = false;
                    DESCR anchorVal = nvGet("ANCHOR");
                    if (anchorVal.type == VType.INT && anchorVal.ival != 0) anchor = true;
                    bb_box.MatchState pms = new bb_box.MatchState(sv);
                    PatternBuilder pb = new PatternBuilder(
                        pms,
                        (nm, v) -> nvSet(nm, DESCR.str(v)),
                        (nm, v) -> nvSet(nm, DESCR.intv((long) v)),
                        (varName, pms2) -> {
                            DESCR d = nvGet(varName);
                            if (d.type == VType.PAT && d.patNode != null) {
                                PatternBuilder inner = new PatternBuilder(pms2,
                                    (n, v) -> nvSet(n, DESCR.str(v)),
                                    (n, v) -> nvSet(n, DESCR.intv((long) v)),
                                    (vn, pms3) -> new bb_lit(pms3, nvGet(vn).toSnoStr()),
                                    nm2 -> nvGet(nm2).toSnoStr());
                                return inner.build(d.patNode);
                            }
                            return new bb_lit(pms2, d.toSnoStr());
                        },
                        nm -> nvGet(nm).toSnoStr()
                    );
                    bb_box root = pb.build(s.pattern);
                    // hasEq + null replacement = delete (replace with "")
                    String replStr = null;
                    boolean hasRepl = s.hasEq;
                    if (s.hasEq && s.replacement != null) {
                        DESCR rv = eval(s.replacement);
                        if (rv.isFail()) { hasRepl = false; } else { replStr = rv.toSnoStr(); }
                    } else if (s.hasEq) {
                        replStr = ""; // bare = deletes matched portion
                    }
                    bb_executor ex = new bb_executor(new bb_executor.VarStore() {
                        public String get(String n) { return nvGet(n).toSnoStr(); }
                        public void   set(String n, String v) { nvSet(n, DESCR.str(v)); }
                    });
                    for (bb_capture cap : pb.deferredCaptures()) ex.registerCapture(cap);
                    try {
                        succeeded = ex.exec(subjName, sv, pms, root,
                                            hasRepl, replStr != null ? replStr : "", anchor);
                    } catch (bb_abort.AbortException ae) { succeeded = false; }
                } else if (s.hasEq && subjName != null) {
                    DESCR replVal = s.replacement != null ? eval(s.replacement) : DESCR.NUL;
                    if (replVal.isFail()) succeeded = false;
                    else { nvSet(subjName, replVal); succeeded = true; }
                } else if (s.hasEq && s.subject != null
                           && s.subject.kind == Parser.EKind.E_KEYWORD && s.subject.sval != null) {
                    DESCR replVal = s.replacement != null ? eval(s.replacement) : DESCR.NUL;
                    if (replVal.isFail()) succeeded = false;
                    else { nv.put(s.subject.sval.toUpperCase(), replVal); succeeded = true; }
                } else if (s.subject != null && !s.hasEq) {
                    if (subjVal.isFail()) succeeded = false;
                }

                // Goto dispatch — handle RETURN/FRETURN
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
                        if (target.equalsIgnoreCase("RETURN")) {
                            retVal = nvGet(fd.name); break;
                        }
                        if (target.equalsIgnoreCase("FRETURN")) {
                            freturn = true; break;
                        }
                        if (target.equalsIgnoreCase("NRETURN")) break; // treat as RETURN for now
                        int dest = labelLookup(target);
                        if (dest >= 0) { pc = dest; continue; }
                    }
                }
                pc = next;
            }
        } finally {
            restore(saved);
        }

        return freturn ? DESCR.FAIL : retVal;
    }

    private void restore(Map<String,DESCR> saved) {
        for (Map.Entry<String,DESCR> e : saved.entrySet())
            nv.put(e.getKey(), e.getValue());
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
        programStmts = stmts;
        buildLabels(stmts);
        int pc   = 0;
        int limit = 1_000_000; // mirrors &STLIMIT default

        while (pc < stmts.length && limit-- > 0) {
            Parser.StmtNode s = stmts[pc];

            if (s.isEnd) break;

            // Increment &STCOUNT
            DESCR stcount = nvGet("STCOUNT");
            nv.put("STCOUNT", DESCR.intv((stcount.type == VType.INT ? stcount.ival : 0) + 1));

            // ── Phase 1: resolve subject ──────────────────────────────────────
            String  subjName = null;
            DESCR  subjVal  = DESCR.NUL;

            if (s.subject != null) {
                if (s.subject.kind == Parser.EKind.E_VAR && s.subject.sval != null) {
                    subjName = s.subject.sval;
                    subjVal  = nvGet(subjName);
                } else {
                    subjVal = eval(s.subject);
                }
            }

            boolean succeeded = true;

            // ── Phase 2–4: pattern match (M-JVM-INTERP-A04) ──────────────────
            if (s.pattern != null) {
                String sv = subjVal.toSnoStr();
                boolean anchor = false;
                DESCR anchorVal = nvGet("ANCHOR");
                if (anchorVal.type == VType.INT && anchorVal.ival != 0) anchor = true;

                bb_box.MatchState pms = new bb_box.MatchState(sv);

                PatternBuilder pb = new PatternBuilder(
                    pms,
                    (name, val) -> nvSet(name, DESCR.str(val)),
                    (name, val) -> nvSet(name, DESCR.intv((long) val)),
                    (varName, pms2) -> {
                        // Resolve pattern-valued variable at match time
                        DESCR d = nvGet(varName);
                        if (d.type == VType.PAT && d.patNode != null) {
                            // Re-build from stored pattern ExprNode (shares pms2)
                            PatternBuilder inner = new PatternBuilder(
                                pms2,
                                (n, v) -> nvSet(n, DESCR.str(v)),
                                (n, v) -> nvSet(n, DESCR.intv((long) v)),
                                (vn, pms3) -> {
                                    DESCR d2 = nvGet(vn);
                                    if (d2.type == VType.PAT && d2.patNode != null) return new bb_fail(pms3);
                                    return new bb_lit(pms3, d2.toSnoStr());
                                }
                            );
                            return inner.build(d.patNode);
                        }
                        // String-valued: match as literal
                        return new bb_lit(pms2, d.toSnoStr());
                    }
                );

                bb_box root = pb.build(s.pattern);

                // Evaluate replacement before match (Phase 4 value)
                String replStr = null;
                if (s.hasEq && s.replacement != null) {
                    DESCR rv = eval(s.replacement);
                    replStr = rv.isFail() ? null : rv.toSnoStr();
                }

                bb_executor ex = new bb_executor(
                    new bb_executor.VarStore() {
                        public String get(String n) { return nvGet(n).toSnoStr(); }
                        public void   set(String n, String v) { nvSet(n, DESCR.str(v)); }
                    }
                );
                for (bb_capture cap : pb.deferredCaptures()) ex.registerCapture(cap);

                try {
                    succeeded = ex.exec(subjName, sv, pms, root,
                                        replStr != null, replStr != null ? replStr : "",
                                        anchor);
                } catch (bb_abort.AbortException ae) {
                    succeeded = false;
                }
            }
            // ── Phase 5: assignment ───────────────────────────────────────────
            else if (s.hasEq && subjName != null) {
                DESCR replVal = s.replacement != null ? eval(s.replacement) : DESCR.NUL;
                if (replVal.isFail()) {
                    succeeded = false;
                } else {
                    nvSet(subjName, replVal);
                    succeeded = true;
                }
            } else if (s.hasEq && s.subject != null
                       && s.subject.kind == Parser.EKind.E_KEYWORD && s.subject.sval != null) {
                DESCR replVal = s.replacement != null ? eval(s.replacement) : DESCR.NUL;
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
                    DESCR replVal = s.replacement != null ? eval(s.replacement) : DESCR.NUL;
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
