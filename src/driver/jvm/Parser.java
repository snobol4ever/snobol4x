package driver.jvm;

import java.util.*;

/**
 * Parser.java — SNOBOL4 recursive-descent parser for the JVM interpreter.
 *
 * Mirrors parse.c exactly:
 *   - Same 17-level expression grammar (snoExpr0 – snoExpr17)
 *   - Same T_WS-gated binary operator rule (binary ops require WS on both sides)
 *   - Same statement structure: subject [WS pattern [WS = replacement]]
 *   - Same goto field parser: unconditional, S(label), F(label)
 *   - Same E_SEQ / E_CAT fixup logic
 *
 * Output: StmtNode[] (program) and ExprNode (expressions).
 * Node kinds mirror IrKind / EKind from IrNode.cs / ir.h.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
public class Parser {

    // ── EKind — mirrors IrKind / EKind from ir.h ─────────────────────────────

    public enum EKind {
        // Literals
        E_QLIT, E_ILIT, E_FLIT, E_NUL,
        // References
        E_VAR, E_KEYWORD, E_INDIRECT, E_DEFER,
        // Operators
        E_INTERROGATE, E_NAME, E_MNS, E_PLS,
        E_ADD, E_SUB, E_MUL, E_DIV, E_MOD, E_POW,
        // Sequence / alternation
        E_SEQ, E_CAT, E_ALT, E_OPSYN,
        // Pattern primitives (used as E_VAR nodes for bare keywords)
        E_ARB, E_ARBNO, E_REM, E_FAIL, E_SUCCEED, E_FENCE, E_ABORT,
        // Captures
        E_CAPT_COND_ASGN, E_CAPT_IMMED_ASGN, E_CAPT_CURSOR,
        // Call / access / assignment
        E_FNC, E_IDX, E_ASSIGN, E_SCAN,
    }

    // ── ExprNode ──────────────────────────────────────────────────────────────

    public static class ExprNode {
        public EKind        kind;
        public String       sval;
        public long         ival;
        public double       dval;
        public List<ExprNode> children = new ArrayList<>();

        public ExprNode(EKind kind) { this.kind = kind; }

        public void addChild(ExprNode c) { children.add(c); }

        @Override public String toString() { return prettyPrint(0); }

        private String prettyPrint(int depth) {
            StringBuilder sb = new StringBuilder("  ".repeat(depth));
            sb.append(kind.name());
            if (sval  != null)                  sb.append("(").append(sval).append(")");
            if (kind == EKind.E_ILIT)           sb.append("(").append(ival).append(")");
            if (kind == EKind.E_FLIT)           sb.append("(").append(dval).append(")");
            for (ExprNode c : children) {
                sb.append("\n").append(c.prettyPrint(depth + 1));
            }
            return sb.toString();
        }
    }

    // ── SnoGoto ───────────────────────────────────────────────────────────────

    public static class SnoGoto {
        public String uncond;
        public String onsuccess;
        public String onfailure;

        @Override public String toString() {
            StringBuilder sb = new StringBuilder("GOTO{");
            if (uncond    != null) sb.append("uncond=").append(uncond).append(" ");
            if (onsuccess != null) sb.append("S=").append(onsuccess).append(" ");
            if (onfailure != null) sb.append("F=").append(onfailure).append(" ");
            sb.append("}");
            return sb.toString();
        }
    }

    // ── StmtNode ──────────────────────────────────────────────────────────────

    public static class StmtNode {
        public String   label;
        public boolean  isEnd;
        public ExprNode subject;
        public ExprNode pattern;
        public ExprNode replacement;
        public boolean  hasEq;
        public SnoGoto  gotoField;
        public int      lineno;

        @Override public String toString() {
            StringBuilder sb = new StringBuilder("STMT{");
            if (label    != null)  sb.append("label=").append(label).append(" ");
            if (isEnd)             sb.append("END ");
            if (subject  != null)  sb.append("subj=").append(subject.kind).append(" ");
            if (pattern  != null)  sb.append("pat=").append(pattern.kind).append(" ");
            if (hasEq)             sb.append("hasEq ");
            if (replacement != null) sb.append("repl=").append(replacement.kind).append(" ");
            if (gotoField != null) sb.append(gotoField);
            sb.append("}");
            return sb.toString();
        }
    }

    // ── Parser state ──────────────────────────────────────────────────────────

    private final Lexer lx;
    public  int nerrors = 0;

    public Parser(Lexer lx) { this.lx = lx; }

    // ── helpers ───────────────────────────────────────────────────────────────

    private static final class Mark {
        final Lexer.Token peeked;
        final boolean hasPeeked;
        Mark(Lexer lx) {
            // We capture via a secondary peek — but since Lexer has only one
            // peek slot, we need to expose position. We abuse the fact that
            // the peeked field is accessible via peek() / next():
            // Instead, we snapshot by draining the peek token.
            // Real implementation: expose a mark on the Lexer.
            // For now we use the mark adapter below.
            this.peeked = null; this.hasPeeked = false;
        }
    }

    // Lexer mark/restore — delegate to Lexer's internal state.
    // We achieve speculative lookahead by tracking position in a token list.
    // Since Lexer streams from a queue, we buffer ahead on demand.

    private final List<Lexer.Token> buf = new ArrayList<>();
    private int pos = 0;

    /** Peek at current position without consuming. */
    private Lexer.Token peek() {
        ensureBuf(pos);
        return buf.get(pos);
    }

    /** Consume current token. */
    private Lexer.Token next() {
        ensureBuf(pos);
        return buf.get(pos++);
    }

    private void ensureBuf(int n) {
        while (buf.size() <= n) {
            buf.add(lx.next());
        }
    }

    private int mark()          { return pos; }
    private void restore(int m) { pos = m; }

    private void skipWs() {
        while (peek().kind == Lexer.TokKind.T_WS) next();
    }

    private boolean eatWs() {
        if (peek().kind == Lexer.TokKind.T_WS) { next(); skipWs(); return true; }
        return false;
    }

    private boolean atEnd() {
        Lexer.TokKind k = peek().kind;
        return k == Lexer.TokKind.T_EOF || k == Lexer.TokKind.T_STMT_END;
    }

    // ── ExprNode constructors ─────────────────────────────────────────────────

    private static ExprNode binop(EKind k, ExprNode l, ExprNode r) {
        ExprNode e = new ExprNode(k);
        e.addChild(l); e.addChild(r);
        return e;
    }
    private static ExprNode unop(EKind k, ExprNode operand) {
        ExprNode e = new ExprNode(k);
        e.addChild(operand);
        return e;
    }
    private static ExprNode nul() { return new ExprNode(EKind.E_NUL); }

    // ── expr17 — atom ─────────────────────────────────────────────────────────

    private void parseArglist(ExprNode node) {
        skipWs();
        Lexer.TokKind k = peek().kind;
        if (k != Lexer.TokKind.T_RPAREN && k != Lexer.TokKind.T_RBRACKET &&
            k != Lexer.TokKind.T_RANGLE && k != Lexer.TokKind.T_EOF) {
            ExprNode e = parseExpr0();
            node.addChild(e != null ? e : nul());
            while (peek().kind == Lexer.TokKind.T_COMMA) {
                next(); skipWs();
                Lexer.TokKind k2 = peek().kind;
                if (k2 == Lexer.TokKind.T_RPAREN || k2 == Lexer.TokKind.T_RBRACKET ||
                    k2 == Lexer.TokKind.T_RANGLE  || k2 == Lexer.TokKind.T_EOF) {
                    node.addChild(nul()); break;
                }
                ExprNode a = parseExpr0();
                node.addChild(a != null ? a : nul());
            }
        }
        skipWs();
    }

    private ExprNode parseExpr17() {
        Lexer.Token t = peek();

        // ( expr ) or ( expr, expr, ... ) alternation group
        if (t.kind == Lexer.TokKind.T_LPAREN) {
            next(); skipWs();
            ExprNode inner = parseExpr0();
            skipWs();
            if (peek().kind == Lexer.TokKind.T_COMMA) {
                ExprNode alt = new ExprNode(EKind.E_ALT);
                alt.addChild(inner != null ? inner : nul());
                while (peek().kind == Lexer.TokKind.T_COMMA) {
                    next(); skipWs();
                    ExprNode r = parseExpr0();
                    alt.addChild(r != null ? r : nul());
                    skipWs();
                }
                if (peek().kind == Lexer.TokKind.T_RPAREN) next();
                if (alt.children.size() == 1) return alt.children.get(0);
                return alt;
            }
            skipWs();
            if (peek().kind == Lexer.TokKind.T_RPAREN) next();
            return inner;
        }

        // String literal
        if (t.kind == Lexer.TokKind.T_STR) {
            next();
            ExprNode e = new ExprNode(EKind.E_QLIT);
            e.sval = t.sval;
            return e;
        }

        // Real literal
        if (t.kind == Lexer.TokKind.T_REAL) {
            next();
            ExprNode e = new ExprNode(EKind.E_FLIT);
            e.dval = t.dval;
            e.sval = t.sval;
            return e;
        }

        // Integer literal
        if (t.kind == Lexer.TokKind.T_INT) {
            next();
            ExprNode e = new ExprNode(EKind.E_ILIT);
            e.ival = t.ival;
            return e;
        }

        // &KEYWORD
        if (t.kind == Lexer.TokKind.T_KEYWORD) {
            next();
            ExprNode e = new ExprNode(EKind.E_KEYWORD);
            e.sval = t.sval;
            return e;
        }

        // T_END — treat like identifier "END"
        if (t.kind == Lexer.TokKind.T_END) {
            next();
            ExprNode e = new ExprNode(EKind.E_VAR);
            e.sval = t.sval != null ? t.sval : "END";
            return e;
        }

        // Identifier: bare name or function call NAME(...)
        if (t.kind == Lexer.TokKind.T_IDENT) {
            next();
            if (peek().kind == Lexer.TokKind.T_LPAREN) {
                next(); // consume '('
                ExprNode e = new ExprNode(EKind.E_FNC);
                e.sval = t.sval;
                parseArglist(e);
                if (peek().kind == Lexer.TokKind.T_RPAREN) next();
                return e;
            }
            ExprNode e = new ExprNode(EKind.E_VAR);
            e.sval = t.sval;
            return e;
        }

        return null;
    }

    // ── expr15 — postfix subscript [ ] or < > ────────────────────────────────

    private ExprNode parseExpr15() {
        ExprNode e = parseExpr17();
        if (e == null) return null;
        for (;;) {
            Lexer.TokKind open = peek().kind;
            Lexer.TokKind close;
            if      (open == Lexer.TokKind.T_LBRACKET) close = Lexer.TokKind.T_RBRACKET;
            else if (open == Lexer.TokKind.T_LANGLE)   close = Lexer.TokKind.T_RANGLE;
            else break;
            next(); // consume open bracket
            ExprNode tmp = new ExprNode(EKind.E_NUL);
            parseArglist(tmp);
            if (peek().kind == close) next();
            ExprNode idx = new ExprNode(EKind.E_IDX);
            idx.addChild(e);
            for (ExprNode c : tmp.children) idx.addChild(c);
            e = idx;
        }
        return e;
    }

    // ── expr14 — unary prefix ─────────────────────────────────────────────────

    private ExprNode parseExpr14() {
        Lexer.Token t = peek();
        EKind uk;
        switch (t.kind) {
            case T_AT:     uk = EKind.E_CAPT_CURSOR;    break;
            case T_TILDE:  uk = EKind.E_CAPT_COND_ASGN; break; // unary ~ = conditional
            case T_QMARK:  uk = EKind.E_INTERROGATE;    break;
            case T_AMP:    uk = EKind.E_OPSYN;           break;
            case T_PLUS:   uk = EKind.E_PLS;             break;
            case T_MINUS:  uk = EKind.E_MNS;             break;
            case T_STAR:   uk = EKind.E_DEFER;           break;
            case T_DOLLAR: uk = EKind.E_INDIRECT;        break;
            case T_DOT:    uk = EKind.E_NAME;            break;
            case T_BANG:   uk = EKind.E_POW;             break;
            case T_PCT:    uk = EKind.E_DIV;             break;
            case T_SLASH:  uk = EKind.E_DIV;             break;
            case T_HASH:   uk = EKind.E_MUL;             break;
            case T_EQ:     uk = EKind.E_ASSIGN;          break;
            case T_PIPE:   uk = EKind.E_ALT;             break;
            default:       return parseExpr15();
        }
        next();
        ExprNode operand = parseExpr14();
        if (operand == null) {
            nerrors++;
            return nul();
        }
        return unop(uk, operand);
    }

    // ── expr13 — ~ binary (WS ~ WS) ──────────────────────────────────────────

    private ExprNode parseExpr13() {
        ExprNode l = parseExpr14();
        if (l == null) return null;
        for (;;) {
            int m = mark();
            if (peek().kind != Lexer.TokKind.T_WS) break;
            next();
            if (peek().kind != Lexer.TokKind.T_TILDE) { restore(m); break; }
            next(); skipWs();
            ExprNode r = parseExpr13();
            l = binop(EKind.E_CAPT_COND_ASGN, l, r);
        }
        return l;
    }

    // ── expr12 — $ . binary ───────────────────────────────────────────────────

    private ExprNode parseExpr12() {
        ExprNode l = parseExpr13();
        if (l == null) return null;
        for (;;) {
            int m = mark();
            if (peek().kind != Lexer.TokKind.T_WS) break;
            next();
            Lexer.TokKind op = peek().kind;
            if (op == Lexer.TokKind.T_DOLLAR || op == Lexer.TokKind.T_DOT) {
                next(); skipWs();
                ExprNode r = parseExpr13();
                EKind k = (op == Lexer.TokKind.T_DOLLAR)
                        ? EKind.E_CAPT_IMMED_ASGN : EKind.E_CAPT_COND_ASGN;
                l = binop(k, l, r);
            } else {
                restore(m); break;
            }
        }
        return l;
    }

    // ── Generic left-associative binary helper ────────────────────────────────

    @FunctionalInterface interface ParseFn { ExprNode parse(); }

    private ExprNode parseLbin(ParseFn next, Lexer.TokKind[] ops, EKind[] kinds) {
        ExprNode l = next.parse();
        if (l == null) return null;
        for (;;) {
            int m = mark();
            if (peek().kind != Lexer.TokKind.T_WS) break;
            next(); // consume WS
            Lexer.TokKind k = peek().kind;
            int found = -1;
            for (int i = 0; i < ops.length; i++) if (k == ops[i]) { found = i; break; }
            if (found < 0) { restore(m); break; }
            // Binary * only when followed by WS on right side too
            if (k == Lexer.TokKind.T_STAR) {
                int m2 = mark();
                next(); // consume * tentatively
                Lexer.TokKind k2 = peek().kind;
                restore(m2);
                if (k2 != Lexer.TokKind.T_WS) { restore(m); break; }
            }
            next(); skipWs(); // consume operator + trailing WS
            ExprNode r = next.parse();
            l = binop(kinds[found], l, r != null ? r : nul());
        }
        return l;
    }

    // ── Generic right-associative binary helper ───────────────────────────────

    private ExprNode parseRbin(ParseFn next, Lexer.TokKind[] ops, EKind[] kinds) {
        ExprNode l = next.parse();
        if (l == null) return null;
        int m = mark();
        if (peek().kind != Lexer.TokKind.T_WS) return l;
        next(); // consume WS
        Lexer.TokKind k = peek().kind;
        int found = -1;
        for (int i = 0; i < ops.length; i++) if (k == ops[i]) { found = i; break; }
        if (found < 0) { restore(m); return l; }
        next(); skipWs();
        ExprNode r = parseRbin(next, ops, kinds); // recurse right
        return binop(kinds[found], l, r != null ? r : nul());
    }

    // ── expr11 — ^ ! ** (right-assoc) ────────────────────────────────────────

    private ExprNode parseExpr11() {
        return parseRbin(this::parseExpr12,
            new Lexer.TokKind[]{ Lexer.TokKind.T_CARET, Lexer.TokKind.T_BANG, Lexer.TokKind.T_STARSTAR },
            new EKind[]{ EKind.E_POW, EKind.E_POW, EKind.E_POW });
    }

    // ── expr10 — % ────────────────────────────────────────────────────────────

    private ExprNode parseExpr10() {
        return parseLbin(this::parseExpr11,
            new Lexer.TokKind[]{ Lexer.TokKind.T_PCT },
            new EKind[]{ EKind.E_DIV });
    }

    // ── expr9 — * ─────────────────────────────────────────────────────────────

    private ExprNode parseExpr9() {
        return parseLbin(this::parseExpr10,
            new Lexer.TokKind[]{ Lexer.TokKind.T_STAR },
            new EKind[]{ EKind.E_MUL });
    }

    // ── expr8 — / ─────────────────────────────────────────────────────────────

    private ExprNode parseExpr8() {
        return parseLbin(this::parseExpr9,
            new Lexer.TokKind[]{ Lexer.TokKind.T_SLASH },
            new EKind[]{ EKind.E_DIV });
    }

    // ── expr7 — # ─────────────────────────────────────────────────────────────

    private ExprNode parseExpr7() {
        return parseLbin(this::parseExpr8,
            new Lexer.TokKind[]{ Lexer.TokKind.T_HASH },
            new EKind[]{ EKind.E_MUL });
    }

    // ── expr6 — + - ──────────────────────────────────────────────────────────

    private ExprNode parseExpr6() {
        return parseLbin(this::parseExpr7,
            new Lexer.TokKind[]{ Lexer.TokKind.T_PLUS, Lexer.TokKind.T_MINUS },
            new EKind[]{ EKind.E_ADD, EKind.E_SUB });
    }

    // ── expr5 — @ ─────────────────────────────────────────────────────────────

    private ExprNode parseExpr5() {
        return parseLbin(this::parseExpr6,
            new Lexer.TokKind[]{ Lexer.TokKind.T_AT },
            new EKind[]{ EKind.E_CAPT_CURSOR });
    }

    // ── expr4 — concatenation (whitespace-separated) ──────────────────────────

    private static boolean isConcatStart(Lexer.TokKind k) {
        switch (k) {
            case T_AT: case T_PLUS: case T_MINUS: case T_HASH:
            case T_SLASH: case T_PCT: case T_CARET:
            case T_BANG: case T_STARSTAR: case T_DOT:
            case T_TILDE: case T_EQ: case T_QMARK: case T_AMP: case T_PIPE:
            case T_COMMA: case T_RPAREN: case T_RBRACKET: case T_RANGLE:
            case T_COLON: case T_EOF: case T_ERR: case T_STMT_END: case T_GOTO:
                return false;
            default:
                return true;
        }
    }

    private ExprNode parseExpr4() {
        ExprNode first = parseExpr5();
        if (first == null) return null;

        List<ExprNode> items = new ArrayList<>();
        items.add(first);

        for (;;) {
            int mc = mark();
            if (peek().kind != Lexer.TokKind.T_WS) break;
            next(); // consume WS tentatively
            if (!isConcatStart(peek().kind)) { restore(mc); break; }
            ExprNode nx = parseExpr5();
            if (nx == null) { restore(mc); break; }
            items.add(nx);
        }

        if (items.size() == 1) return first;

        ExprNode e = new ExprNode(EKind.E_SEQ);
        for (ExprNode item : items) e.addChild(item);
        return e;
    }

    // ── expr3 — | alternation (n-ary) ────────────────────────────────────────

    private ExprNode parseExpr3() {
        ExprNode first = parseExpr4();
        if (first == null) return null;

        // peek ahead for |
        int m3check = mark();
        boolean hasPipe = false;
        if (peek().kind == Lexer.TokKind.T_WS) {
            next();
            if (peek().kind == Lexer.TokKind.T_PIPE) hasPipe = true;
        }
        restore(m3check);
        if (!hasPipe) return first;

        ExprNode e = new ExprNode(EKind.E_ALT);
        e.addChild(first);
        for (;;) {
            int m3 = mark();
            if (peek().kind != Lexer.TokKind.T_WS) break;
            next();
            if (peek().kind != Lexer.TokKind.T_PIPE) { restore(m3); break; }
            next(); skipWs();
            ExprNode r = parseExpr4();
            e.addChild(r != null ? r : nul());
        }
        return e;
    }

    // ── expr2 — & ─────────────────────────────────────────────────────────────

    private ExprNode parseExpr2() {
        return parseLbin(this::parseExpr3,
            new Lexer.TokKind[]{ Lexer.TokKind.T_AMP },
            new EKind[]{ EKind.E_OPSYN });
    }

    // ── expr0 — = assignment, ? scan (right-assoc) ───────────────────────────

    private ExprNode parseExpr0() {
        ExprNode l = parseExpr2();
        if (l == null) return null;
        int m0 = mark();
        if (peek().kind != Lexer.TokKind.T_WS) return l;
        next();
        Lexer.TokKind k = peek().kind;
        if (k == Lexer.TokKind.T_EQ) {
            next(); skipWs();
            ExprNode r = parseExpr0();
            return binop(EKind.E_ASSIGN, l, r != null ? r : nul());
        }
        if (k == Lexer.TokKind.T_QMARK) {
            next(); skipWs();
            ExprNode r = parseExpr0();
            return binop(EKind.E_CAPT_COND_ASGN, l, r != null ? r : nul());
        }
        restore(m0);
        return l;
    }

    private ExprNode parseExpr() {
        skipWs();
        return parseExpr0();
    }

    // ── Goto field parser ─────────────────────────────────────────────────────

    private String parseGotoLabel() {
        Lexer.TokKind open = peek().kind;
        Lexer.TokKind close;
        if      (open == Lexer.TokKind.T_LPAREN) close = Lexer.TokKind.T_RPAREN;
        else if (open == Lexer.TokKind.T_LANGLE) close = Lexer.TokKind.T_RANGLE;
        else return null;
        next(); skipWs();

        Lexer.Token t = peek();
        String label = null;

        if (t.kind == Lexer.TokKind.T_IDENT || t.kind == Lexer.TokKind.T_KEYWORD ||
            t.kind == Lexer.TokKind.T_END) {
            next();
            label = t.sval != null ? t.sval : "END";
        } else if (t.kind == Lexer.TokKind.T_DOLLAR) {
            next();
            if (peek().kind == Lexer.TokKind.T_LPAREN) {
                // $(expr) — computed goto
                int depth = 1; next();
                StringBuilder sb = new StringBuilder("$COMPUTED:");
                while (peek().kind != Lexer.TokKind.T_EOF && depth > 0) {
                    Lexer.Token tok = peek();
                    if (tok.kind == Lexer.TokKind.T_LPAREN) depth++;
                    else if (tok.kind == Lexer.TokKind.T_RPAREN) {
                        depth--; if (depth == 0) break;
                    }
                    if (tok.sval != null) sb.append(tok.sval);
                    else sb.append(tok.kind.name());
                    next();
                }
                label = sb.toString();
            } else if (peek().kind == Lexer.TokKind.T_STR) {
                Lexer.Token n2 = next();
                label = "$COMPUTED:'" + (n2.sval != null ? n2.sval : "") + "'";
            } else {
                Lexer.Token n2 = next();
                label = "$" + (n2.sval != null ? n2.sval : "?");
            }
        }

        skipWs();
        if (peek().kind == close) next();
        return label;
    }

    private SnoGoto parseGotoField(String gotoStr, int lineno) {
        if (gotoStr == null || gotoStr.isEmpty()) return null;

        Lexer gotoLex = new Lexer();
        gotoLex.openBodyString(gotoStr, lineno);

        // Use a nested parser over the goto lexer
        Parser gp = new Parser(gotoLex);
        gp.skipWs();

        SnoGoto g = new SnoGoto();
        while (gp.peek().kind != Lexer.TokKind.T_EOF) {
            Lexer.Token t = gp.peek();
            if (t.kind == Lexer.TokKind.T_IDENT && t.sval != null) {
                if (t.sval.equalsIgnoreCase("S")) {
                    gp.next();
                    g.onsuccess = gp.parseGotoLabel();
                    gp.skipWs(); continue;
                }
                if (t.sval.equalsIgnoreCase("F")) {
                    gp.next();
                    g.onfailure = gp.parseGotoLabel();
                    gp.skipWs(); continue;
                }
            }
            if (t.kind == Lexer.TokKind.T_LPAREN || t.kind == Lexer.TokKind.T_LANGLE) {
                g.uncond = gp.parseGotoLabel();
                gp.skipWs(); continue;
            }
            nerrors++;
            gp.next();
        }

        if (g.uncond == null && g.onsuccess == null && g.onfailure == null) return null;
        return g;
    }

    // ── E_SEQ / E_CAT fixup (mirrors fixup_val_tree) ─────────────────────────

    private static void fixupValTree(ExprNode e) {
        if (e == null) return;
        if (e.kind == EKind.E_SEQ) e.kind = EKind.E_CAT;
        for (ExprNode c : e.children) fixupValTree(c);
    }

    private static boolean replIsPatTree(ExprNode e) {
        if (e == null) return false;
        switch (e.kind) {
            case E_ARB: case E_ARBNO:
            case E_CAPT_COND_ASGN: case E_CAPT_IMMED_ASGN: case E_CAPT_CURSOR: case E_DEFER:
                return true;
            default:
                for (ExprNode c : e.children) if (replIsPatTree(c)) return true;
                return false;
        }
    }

    // ── Body field parser ─────────────────────────────────────────────────────

    private StmtNode parseBodyField(List<Lexer.Token> bodyToks, int lineno) {
        if (bodyToks.isEmpty()) return null;

        // Feed body tokens into a sub-lexer queue via a synthetic Lexer
        Lexer bodyLex = new Lexer();
        for (Lexer.Token t : bodyToks) bodyLex.injectToken(t);
        bodyLex.injectToken(new Lexer.Token(Lexer.TokKind.T_EOF, null, 0, 0, lineno));

        Parser bp = new Parser(bodyLex);
        bp.skipWs();

        StmtNode s = new StmtNode();
        s.lineno = lineno;

        // Subject — parsed at unary level
        s.subject = bp.parseExpr14();

        boolean haveWs    = (bp.peek().kind == Lexer.TokKind.T_WS);
        boolean haveQmark = false;

        if (haveWs) {
            bp.next(); bp.skipWs();
            if (bp.peek().kind == Lexer.TokKind.T_QMARK) {
                haveQmark = true;
                bp.next(); bp.skipWs();
            }
        } else if (bp.peek().kind == Lexer.TokKind.T_QMARK) {
            haveQmark = true;
            bp.next(); bp.skipWs();
        }

        if (haveWs || haveQmark) {
            if (haveWs && !haveQmark && bp.peek().kind == Lexer.TokKind.T_EQ) {
                // subject = [replacement]
                bp.next(); // consume '='
                s.hasEq = true;
                bp.skipWs();
                if (!bp.atEnd()) s.replacement = bp.parseExpr();
            } else if (!bp.atEnd()) {
                // subject WS pattern [WS = replacement]
                s.pattern = bp.parseExpr3();
                if (bp.peek().kind == Lexer.TokKind.T_WS) {
                    bp.next(); bp.skipWs();
                    if (bp.peek().kind == Lexer.TokKind.T_EQ) {
                        bp.next(); bp.skipWs();
                        s.hasEq = true;
                        if (!bp.atEnd()) s.replacement = bp.parseExpr();
                    } else {
                        // put WS back
                        bp.buf.add(bp.pos, new Lexer.Token(Lexer.TokKind.T_WS, null, 0, 0, lineno));
                    }
                }
            }
        }

        // fixup E_SEQ → E_CAT in value-context trees
        fixupValTree(s.subject);
        if (s.replacement != null && !replIsPatTree(s.replacement))
            fixupValTree(s.replacement);

        return s;
    }

    // ── Top-level: parse token stream → StmtNode[] ───────────────────────────

    public StmtNode[] parseProgram() {
        List<StmtNode> stmts = new ArrayList<>();

        while (true) {
            Lexer.Token t = peek();
            if (t.kind == Lexer.TokKind.T_EOF) break;

            String label    = null;
            String gotoStr  = null;
            boolean isEnd   = false;
            int lineno      = t.lineno;

            // Optional label
            if (t.kind == Lexer.TokKind.T_LABEL) {
                next();
                label  = t.sval;
                isEnd  = (t.ival != 0);
                lineno = t.lineno;
                t = peek();
            }

            // Empty statement (label only)
            if (t.kind == Lexer.TokKind.T_STMT_END || t.kind == Lexer.TokKind.T_EOF) {
                if (t.kind == Lexer.TokKind.T_STMT_END) next();
                if (label != null || isEnd) {
                    StmtNode s = new StmtNode();
                    s.label = label; s.isEnd = isEnd; s.lineno = lineno;
                    stmts.add(s);
                }
                if (t.kind == Lexer.TokKind.T_EOF) break;
                continue;
            }

            // Collect body tokens (up to T_GOTO or T_STMT_END)
            List<Lexer.Token> bodyToks = new ArrayList<>();
            while (true) {
                Lexer.Token bt = peek();
                if (bt.kind == Lexer.TokKind.T_GOTO ||
                    bt.kind == Lexer.TokKind.T_STMT_END ||
                    bt.kind == Lexer.TokKind.T_EOF) break;
                bodyToks.add(bt);
                next();
            }

            if (peek().kind == Lexer.TokKind.T_GOTO) {
                gotoStr = peek().sval;
                next();
            }
            if (peek().kind == Lexer.TokKind.T_STMT_END) next();

            // Parse body
            StmtNode s = bodyToks.isEmpty() ? new StmtNode() : parseBodyField(bodyToks, lineno);
            if (s == null) s = new StmtNode();
            s.label    = label;
            s.isEnd    = isEnd;
            s.lineno   = lineno;
            s.gotoField = parseGotoField(gotoStr, lineno);
            stmts.add(s);
        }

        return stmts.toArray(new StmtNode[0]);
    }

    // ── Convenience: parse a file ─────────────────────────────────────────────

    public static StmtNode[] parseFile(String path) throws java.io.IOException {
        Lexer lx = new Lexer();
        lx.openFile(path);
        return new Parser(lx).parseProgram();
    }

    // ── main — quick smoke ────────────────────────────────────────────────────

    public static void main(String[] args) throws java.io.IOException {
        if (args.length == 0) { System.err.println("usage: Parser <file.sno>"); return; }
        StmtNode[] stmts = parseFile(args[0]);
        for (int i = 0; i < stmts.length; i++) {
            StmtNode s = stmts[i];
            System.out.println("stmt " + i + "  " + s);
            if (s.subject != null) {
                System.out.println("  subject:\n" + s.subject.prettyPrint(2));
            }
            if (s.pattern != null) {
                System.out.println("  pattern:\n" + s.pattern.prettyPrint(2));
            }
            if (s.replacement != null) {
                System.out.println("  replacement:\n" + s.replacement.prettyPrint(2));
            }
        }
    }
}
