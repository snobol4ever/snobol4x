package driver.jvm;

import java.io.*;
import java.util.*;

/**
 * Lexer.java — SNOBOL4 one-pass lexer for the JVM interpreter.
 *
 * Mirrors lex.c / lex.h exactly:
 *   - Single pass over source: reads lines, joins continuations (+/.),
 *     strips comments (*, !, #, |, ; at col 1), handles -INCLUDE.
 *   - Emits structural tokens: T_LABEL, T_GOTO, T_STMT_END.
 *   - T_WS is emitted inside the body as the field separator.
 *   - Token queue drained by next()/peek().
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
public class Lexer {

    // ── Token kinds (mirrors TokKind enum in lex.h) ─────────────────────────

    public enum TokKind {
        T_IDENT, T_INT, T_REAL, T_STR, T_KEYWORD,
        T_WS,
        T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT,
        T_CARET, T_BANG, T_STARSTAR,
        T_AMP, T_AT, T_TILDE, T_DOLLAR, T_DOT,
        T_HASH, T_PIPE, T_EQ, T_QMARK,
        T_COMMA, T_LPAREN, T_RPAREN,
        T_LBRACKET, T_RBRACKET,
        T_LANGLE, T_RANGLE,
        T_COLON, T_SGOTO, T_FGOTO,
        T_END,
        // Statement-structure tokens
        T_LABEL,     // col-1 identifier; sval=name, ival=1 if END
        T_GOTO,      // goto field string; sval=goto_str
        T_STMT_END,  // end of one logical statement
        T_EOF, T_ERR
    }

    // ── Token (mirrors Token struct in lex.h) ────────────────────────────────

    public static class Token {
        public final TokKind kind;
        public final String  sval;
        public final long    ival;
        public final double  dval;
        public final int     lineno;

        public Token(TokKind kind, String sval, long ival, double dval, int lineno) {
            this.kind   = kind;
            this.sval   = sval;
            this.ival   = ival;
            this.dval   = dval;
            this.lineno = lineno;
        }

        @Override public String toString() {
            StringBuilder sb = new StringBuilder(kind.name());
            if (sval  != null)   sb.append("(").append(sval).append(")");
            if (kind == TokKind.T_INT)  sb.append("(").append(ival).append(")");
            if (kind == TokKind.T_REAL) sb.append("(").append(dval).append(")");
            return sb.toString();
        }
    }

    // ── Token queue ──────────────────────────────────────────────────────────

    private final ArrayDeque<Token> queue = new ArrayDeque<>();
    private Token peeked = null;
    public  int   nerrors = 0;
    private String filename = "<stdin>";

    // ── Include dirs ─────────────────────────────────────────────────────────

    private final List<String> incDirs = new ArrayList<>();

    public void addIncludeDir(String d) { incDirs.add(d); }

    // ── Public entry points ──────────────────────────────────────────────────

    /** Tokenise a source file and fill the queue. */
    public void openFile(String path) throws IOException {
        filename = path;
        try (BufferedReader br = new BufferedReader(new FileReader(path))) {
            processReader(br, path);
        }
    }

    /** Tokenise a source string as a mini-file (used by tests). */
    public void openString(String src) {
        try {
            processReader(new BufferedReader(new java.io.StringReader(src)), "<string>");
        } catch (IOException e) {
            // StringReader never throws IOException
            throw new RuntimeException(e);
        }
    }

    /** Inject a pre-built token directly into the queue (used by Parser for body re-parsing). */
    public void injectToken(Token t) { queue.add(t); }

    /** Tokenise a body substring directly (used internally for goto-field re-parsing). */
    public void openBodyString(String src, int lineno) {
        tokeniseBody(src, 0, src.length(), lineno);
    }

    /** Drain one token from the queue. */
    public Token next() {
        if (peeked != null) { Token t = peeked; peeked = null; return t; }
        if (queue.isEmpty()) return new Token(TokKind.T_EOF, null, 0, 0, 0);
        return queue.poll();
    }

    /** Peek without consuming. */
    public Token peek() {
        if (peeked == null) peeked = next();
        return peeked;
    }

    public boolean atEnd() {
        return peeked == null && queue.isEmpty();
    }

    // ── Error ─────────────────────────────────────────────────────────────────

    private void error(int lineno, String msg) {
        System.err.println(filename + ":" + lineno + ": error: " + msg);
        nerrors++;
    }

    // ── Queue push ────────────────────────────────────────────────────────────

    private void qPush(TokKind kind, String sval, long ival, double dval, int ln) {
        queue.add(new Token(kind, sval, ival, dval, ln));
    }

    // ── find_goto_colon ───────────────────────────────────────────────────────
    //
    // Returns:
    //   >= 0  : position of ':' (goto separator)
    //   -1    : not found
    //   -(i+1): semicolon multi-stmt split at position i

    private int findGotoColon(String s, int start, int end) {
        int depth = 0;
        boolean inq = false;
        char qch = 0;
        for (int i = start; i < end; i++) {
            char c = s.charAt(i);
            if (inq) { if (c == qch) inq = false; continue; }
            if (c == '\'' || c == '"') { inq = true; qch = c; continue; }
            if (c == '(') { depth++; continue; }
            if (c == ')') { depth--; continue; }
            if (c == ';' && depth == 0) return -(i + 1);
            if (c == ':' && depth == 0) return i;
        }
        return -1;
    }

    // ── tokenise_body ─────────────────────────────────────────────────────────

    private void tokeniseBody(String s, int start, int end, int ln) {
        int pos = start;
        while (pos < end) {
            char c = s.charAt(pos);

            // whitespace → T_WS
            if (c == ' ' || c == '\t') {
                while (pos < end && (s.charAt(pos) == ' ' || s.charAt(pos) == '\t')) pos++;
                qPush(TokKind.T_WS, null, 0, 0, ln);
                continue;
            }

            // single-quoted string
            if (c == '\'') {
                int st = pos + 1; pos++;
                while (pos < end && s.charAt(pos) != '\'') pos++;
                String sv = s.substring(st, pos);
                if (pos < end) pos++;
                qPush(TokKind.T_STR, sv, 0, 0, ln);
                continue;
            }

            // double-quoted string
            if (c == '"') {
                int st = pos + 1; pos++;
                while (pos < end && s.charAt(pos) != '"') pos++;
                String sv = s.substring(st, pos);
                if (pos < end) pos++;
                qPush(TokKind.T_STR, sv, 0, 0, ln);
                continue;
            }

            // number
            if (Character.isDigit(c)) {
                int st = pos;
                while (pos < end && Character.isDigit(s.charAt(pos))) pos++;
                boolean isReal = false;
                // N.N
                if (pos < end && s.charAt(pos) == '.' &&
                        pos + 1 < end && Character.isDigit(s.charAt(pos + 1))) {
                    isReal = true; pos++;
                    while (pos < end && Character.isDigit(s.charAt(pos))) pos++;
                }
                // exponent
                if (pos < end && (s.charAt(pos) == 'e' || s.charAt(pos) == 'E')) {
                    isReal = true; pos++;
                    if (pos < end && (s.charAt(pos) == '+' || s.charAt(pos) == '-')) pos++;
                    while (pos < end && Character.isDigit(s.charAt(pos))) pos++;
                }
                // integer-only exponent  e.g. 1e5
                if (!isReal && pos < end && (s.charAt(pos) == 'e' || s.charAt(pos) == 'E')) {
                    isReal = true;
                }
                String raw = s.substring(st, pos);
                if (isReal) {
                    qPush(TokKind.T_REAL, raw, 0, Double.parseDouble(raw), ln);
                } else {
                    qPush(TokKind.T_INT, null, Long.parseLong(raw), 0, ln);
                }
                continue;
            }

            // &KEYWORD
            if (c == '&') {
                pos++;
                int st = pos;
                while (pos < end && (Character.isLetterOrDigit(s.charAt(pos)) || s.charAt(pos) == '_')) pos++;
                if (pos > st) {
                    qPush(TokKind.T_KEYWORD, s.substring(st, pos), 0, 0, ln);
                } else {
                    qPush(TokKind.T_AMP, null, 0, 0, ln);
                }
                continue;
            }

            // identifier (includes non-ASCII start byte >= 0x80)
            if (Character.isLetter(c) || (c & 0xFF) >= 0x80) {
                int st = pos;
                while (pos < end) {
                    char ch = s.charAt(pos);
                    if (Character.isLetterOrDigit(ch) || ch == '_' || ch == '.' || (ch & 0xFF) >= 0x80)
                        pos++;
                    else break;
                }
                String sv = s.substring(st, pos);
                if (sv.equalsIgnoreCase("END")) {
                    qPush(TokKind.T_END, sv, 0, 0, ln);
                } else {
                    qPush(TokKind.T_IDENT, sv, 0, 0, ln);
                }
                continue;
            }

            // **
            if (c == '*' && pos + 1 < end && s.charAt(pos + 1) == '*') {
                pos += 2;
                qPush(TokKind.T_STARSTAR, null, 0, 0, ln);
                continue;
            }

            // single-char operators
            pos++;
            switch (c) {
                case '+': qPush(TokKind.T_PLUS,     null, 0, 0, ln); break;
                case '-': qPush(TokKind.T_MINUS,    null, 0, 0, ln); break;
                case '*': qPush(TokKind.T_STAR,     null, 0, 0, ln); break;
                case '/': qPush(TokKind.T_SLASH,    null, 0, 0, ln); break;
                case '%': qPush(TokKind.T_PCT,      null, 0, 0, ln); break;
                case '^': qPush(TokKind.T_CARET,    null, 0, 0, ln); break;
                case '!': qPush(TokKind.T_BANG,     null, 0, 0, ln); break;
                case '@': qPush(TokKind.T_AT,       null, 0, 0, ln); break;
                case '~': qPush(TokKind.T_TILDE,    null, 0, 0, ln); break;
                case '$': qPush(TokKind.T_DOLLAR,   null, 0, 0, ln); break;
                case '.': qPush(TokKind.T_DOT,      null, 0, 0, ln); break;
                case '#': qPush(TokKind.T_HASH,     null, 0, 0, ln); break;
                case '|': qPush(TokKind.T_PIPE,     null, 0, 0, ln); break;
                case '=': qPush(TokKind.T_EQ,       null, 0, 0, ln); break;
                case '?': qPush(TokKind.T_QMARK,    null, 0, 0, ln); break;
                case ',': qPush(TokKind.T_COMMA,     null, 0, 0, ln); break;
                case '(': qPush(TokKind.T_LPAREN,   null, 0, 0, ln); break;
                case ')': qPush(TokKind.T_RPAREN,   null, 0, 0, ln); break;
                case '[': qPush(TokKind.T_LBRACKET, null, 0, 0, ln); break;
                case ']': qPush(TokKind.T_RBRACKET, null, 0, 0, ln); break;
                case '<': qPush(TokKind.T_LANGLE,   null, 0, 0, ln); break;
                case '>': qPush(TokKind.T_RANGLE,   null, 0, 0, ln); break;
                default:  error(ln, "unexpected character '" + c + "'");
                          qPush(TokKind.T_ERR, null, 0, 0, ln); break;
            }
        }
    }

    // ── emit_logical ──────────────────────────────────────────────────────────
    //
    // Emits one logical (continuation-joined) line as tokens:
    //   [T_LABEL] body-tokens... [T_GOTO] T_STMT_END
    // Handles semicolon multi-statement splitting.

    private void emitLogical(String line, int ln) {
        int srcStart = 0;
        int srcEnd   = line.length();
        boolean firstSeg = true;

        while (srcEnd >= 0) {
            int ci = findGotoColon(line, srcStart, srcEnd);
            int semiEnd = (ci < -1) ? ((-ci) - 1) : -1;
            int pieceLen = (semiEnd < 0) ? (srcEnd - srcStart) : (semiEnd - srcStart);

            // Next segment after semicolon
            int nextStart = -1, nextEnd = -1;
            if (semiEnd >= 0) {
                nextStart = semiEnd + 1;
                // skip leading whitespace
                while (nextStart < srcEnd && (line.charAt(nextStart) == ' ' || line.charAt(nextStart) == '\t'))
                    nextStart++;
                // skip if comment
                if (nextStart < srcEnd && line.charAt(nextStart) != '*' && line.charAt(nextStart) != '\0') {
                    nextEnd = srcEnd;
                }
            }

            int segStart = srcStart;
            int segEnd   = srcStart + pieceLen;
            int pos      = segStart;

            // Label — col-1 identifier, first segment only
            if (firstSeg && segEnd > segStart && line.charAt(segStart) != ' ' && line.charAt(segStart) != '\t') {
                int le = segStart;
                while (le < segEnd && line.charAt(le) != ' ' && line.charAt(le) != '\t') le++;
                String lbl = line.substring(segStart, le);
                qPush(TokKind.T_LABEL, lbl, lbl.equalsIgnoreCase("END") ? 1 : 0, 0, ln);
                pos = le;
                while (pos < segEnd && (line.charAt(pos) == ' ' || line.charAt(pos) == '\t')) pos++;
            }

            // Body + goto
            int rStart = pos, rEnd = segEnd;
            if (rEnd > rStart) {
                int gci = findGotoColon(line, rStart, rEnd);
                int bodyEnd;
                String gotoTok = null;

                if (gci >= 0) {
                    // found ':' goto separator
                    bodyEnd = gci;
                    while (bodyEnd > rStart && (line.charAt(bodyEnd - 1) == ' ' || line.charAt(bodyEnd - 1) == '\t'))
                        bodyEnd--;
                    int gs = gci + 1, ge = rEnd;
                    while (gs < ge && (line.charAt(gs) == ' ' || line.charAt(gs) == '\t')) gs++;
                    while (ge > gs && (line.charAt(ge - 1) == ' ' || line.charAt(ge - 1) == '\t')) ge--;
                    // strip trailing semicolon in goto field
                    for (int k = gs; k < ge; k++) {
                        if (line.charAt(k) == ';') { ge = k; break; }
                    }
                    while (ge > gs && (line.charAt(ge - 1) == ' ' || line.charAt(ge - 1) == '\t')) ge--;
                    if (ge > gs) gotoTok = line.substring(gs, ge);
                } else if (gci < -1) {
                    // semicolon split in body
                    bodyEnd = (-gci) - 1;
                    while (bodyEnd > rStart && (line.charAt(bodyEnd - 1) == ' ' || line.charAt(bodyEnd - 1) == '\t'))
                        bodyEnd--;
                } else {
                    bodyEnd = rEnd;
                    while (bodyEnd > rStart && (line.charAt(bodyEnd - 1) == ' ' || line.charAt(bodyEnd - 1) == '\t'))
                        bodyEnd--;
                }

                if (bodyEnd > rStart) tokeniseBody(line, rStart, bodyEnd, ln);
                if (gotoTok != null)  qPush(TokKind.T_GOTO, gotoTok, 0, 0, ln);
            }

            qPush(TokKind.T_STMT_END, null, 0, 0, ln);

            firstSeg = false;
            if (nextEnd < 0) break;
            srcStart = nextStart;
            srcEnd   = nextEnd;
        }
    }

    // ── processReader ─────────────────────────────────────────────────────────

    private void processReader(BufferedReader br, String fname) throws IOException {
        StringBuilder logical = new StringBuilder();
        int logicalLineno = 0;
        int lineno = 0;
        String rawLine;

        while ((rawLine = br.readLine()) != null) {
            lineno++;
            // strip trailing CR/LF (readLine already strips \n but not \r on some platforms)
            if (rawLine.endsWith("\r")) rawLine = rawLine.substring(0, rawLine.length() - 1);

            if (rawLine.isEmpty()) {
                if (logical.length() > 0) {
                    emitLogical(logical.toString(), logicalLineno);
                    logical.setLength(0);
                }
                continue;
            }

            char c1 = rawLine.charAt(0);

            // comment line: *, !, #, |, ; at col 1
            if (c1 == '*' || c1 == '!' || c1 == '#' || c1 == '|' || c1 == ';') {
                if (logical.length() > 0) {
                    emitLogical(logical.toString(), logicalLineno);
                    logical.setLength(0);
                }
                continue;
            }

            // -INCLUDE directive
            if (c1 == '-') {
                if (logical.length() > 0) {
                    emitLogical(logical.toString(), logicalLineno);
                    logical.setLength(0);
                }
                int p = 1;
                while (p < rawLine.length() && (rawLine.charAt(p) == ' ' || rawLine.charAt(p) == '\t')) p++;
                if (rawLine.regionMatches(true, p, "INCLUDE", 0, 7)) {
                    p += 7;
                    while (p < rawLine.length() && (rawLine.charAt(p) == ' ' || rawLine.charAt(p) == '\t')) p++;
                    StringBuilder iname = new StringBuilder();
                    if (p < rawLine.length() && (rawLine.charAt(p) == '\'' || rawLine.charAt(p) == '"')) {
                        char q = rawLine.charAt(p++);
                        while (p < rawLine.length() && rawLine.charAt(p) != q) iname.append(rawLine.charAt(p++));
                    } else {
                        while (p < rawLine.length() && rawLine.charAt(p) != ' ' && rawLine.charAt(p) != '\t')
                            iname.append(rawLine.charAt(p++));
                    }
                    if (iname.length() > 0) openInclude(iname.toString(), fname, lineno);
                }
                continue;
            }

            // continuation line: + or . at col 1
            if (c1 == '+' || c1 == '.') {
                // trim trailing whitespace from current logical
                int len = logical.length();
                while (len > 0 && (logical.charAt(len - 1) == ' ' || logical.charAt(len - 1) == '\t')) len--;
                logical.setLength(len);
                int s = 1;
                while (s < rawLine.length() && (rawLine.charAt(s) == ' ' || rawLine.charAt(s) == '\t')) s++;
                logical.append(' ');
                logical.append(rawLine.substring(s));
                continue;
            }

            // ordinary line — flush previous logical, start new one
            if (logical.length() > 0) {
                emitLogical(logical.toString(), logicalLineno);
                logical.setLength(0);
            }
            logicalLineno = lineno;
            logical.append(rawLine);
        }

        if (logical.length() > 0) {
            emitLogical(logical.toString(), logicalLineno);
        }
    }

    // ── openInclude ───────────────────────────────────────────────────────────

    private void openInclude(String iname, String fromFile, int lineno) {
        File f = new File(iname);
        if (!f.exists()) {
            for (String dir : incDirs) {
                File candidate = new File(dir, iname);
                if (candidate.exists()) { f = candidate; break; }
            }
        }
        if (!f.exists()) {
            System.err.println(fromFile + ": cannot open include '" + iname + "'");
            nerrors++;
            return;
        }
        try (BufferedReader br = new BufferedReader(new FileReader(f))) {
            processReader(br, f.getPath());
        } catch (IOException e) {
            System.err.println(fromFile + ":" + lineno + ": error reading include '" + iname + "': " + e.getMessage());
            nerrors++;
        }
    }

    // ── Convenience: tokenise a file and return all tokens ───────────────────

    public static List<Token> tokeniseFile(String path) throws IOException {
        Lexer lx = new Lexer();
        lx.openFile(path);
        List<Token> out = new ArrayList<>();
        Token t;
        while ((t = lx.next()).kind != TokKind.T_EOF) out.add(t);
        return out;
    }

    // ── main — quick smoke (tokenise stdin or a file) ─────────────────────────

    public static void main(String[] args) throws IOException {
        Lexer lx = new Lexer();
        if (args.length > 0) {
            lx.openFile(args[0]);
        } else {
            // read stdin as a single string
            StringBuilder sb = new StringBuilder();
            BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
            String line;
            while ((line = br.readLine()) != null) sb.append(line).append("\n");
            lx.openString(sb.toString());
        }
        Token t;
        while ((t = lx.next()).kind != TokKind.T_EOF) {
            System.out.println(t.lineno + "\t" + t);
        }
        if (lx.nerrors > 0) {
            System.err.println(lx.nerrors + " error(s)");
            System.exit(1);
        }
    }
}
