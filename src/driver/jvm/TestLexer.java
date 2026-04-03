package driver.jvm;

import java.io.*;
import java.util.*;

/**
 * TestLexer.java — Gate test for M-JVM-INTERP-A01.
 *
 * Verifies all 19 NET-INTERP parse test inputs tokenize without error.
 * Mirrors the TDD approach from dotnet TestLexer.
 *
 * Run:
 *   cd /home/claude/one4all/src/driver/jvm
 *   javac -d /tmp/jvm_cls driver/jvm/Lexer.java driver/jvm/TestLexer.java
 *   java -cp /tmp/jvm_cls driver.jvm.TestLexer /home/claude/corpus
 */
public class TestLexer {

    static int pass = 0, fail = 0;

    // ── individual assertions ────────────────────────────────────────────────

    static void assertTokenises(String path) {
        try {
            Lexer lx = new Lexer();
            lx.openFile(path);
            // drain all tokens
            int count = 0;
            Lexer.Token t;
            while ((t = lx.next()).kind != Lexer.TokKind.T_EOF) count++;
            if (lx.nerrors > 0) {
                System.out.println("FAIL  " + path + "  (" + lx.nerrors + " lex errors)");
                fail++;
            } else {
                System.out.println("PASS  " + path + "  (" + count + " tokens)");
                pass++;
            }
        } catch (IOException e) {
            System.out.println("FAIL  " + path + "  (IOException: " + e.getMessage() + ")");
            fail++;
        }
    }

    // ── specific token-type checks (mirrors dotnet Test_214 etc.) ────────────

    static void assertContains(String label, String src, Lexer.TokKind expected, String expectedSval) {
        Lexer lx = new Lexer();
        lx.openBodyString(src, 1);
        Lexer.Token t;
        while ((t = lx.next()).kind != Lexer.TokKind.T_EOF) {
            if (t.kind == expected) {
                if (expectedSval == null || expectedSval.equalsIgnoreCase(t.sval)) {
                    System.out.println("PASS  " + label);
                    pass++; return;
                }
            }
        }
        System.out.println("FAIL  " + label + "  (expected " + expected + "(" + expectedSval + ") in: " + src);
        fail++;
    }

    static void assertFirstKind(String label, String src, Lexer.TokKind expected) {
        Lexer lx = new Lexer();
        lx.openBodyString(src, 1);
        Lexer.Token t = lx.next();
        if (t.kind == expected) {
            System.out.println("PASS  " + label);
            pass++;
        } else {
            System.out.println("FAIL  " + label + "  got " + t.kind + " expected " + expected + " in: " + src);
            fail++;
        }
    }

    // ── main ─────────────────────────────────────────────────────────────────

    public static void main(String[] args) throws IOException {
        String corpusRoot = args.length > 0 ? args[0] : "/home/claude/corpus";
        String frontendRoot = args.length > 1 ? args[1] : "/home/claude/one4all/test/frontend";

        System.out.println("=== TestLexer — M-JVM-INTERP-A01 gate ===\n");

        // ── Unit tests (mirrors dotnet TestLexer) ─────────────────────────────

        System.out.println("--- Unit tests ---");

        // Test_214: label at col 1
        {
            Lexer lx = new Lexer();
            lx.openString("START OUTPUT = 'hello'\n");
            // emitLogical treats a string given to openString as a body, not a file
            // so use a minimal file-like test via tokeniseBody approach
            // For label detection we need to go through openFile path; use a temp file
            File tmp = File.createTempFile("tlex214_", ".sno");
            tmp.deleteOnExit();
            try (PrintWriter pw = new PrintWriter(tmp)) { pw.println("START OUTPUT = 'hello'"); }
            lx = new Lexer(); lx.openFile(tmp.getPath());
            Lexer.Token t = lx.next();
            if (t.kind == Lexer.TokKind.T_LABEL && t.sval.equals("START")) {
                System.out.println("PASS  Test_214 (label at col-1)"); pass++;
            } else {
                System.out.println("FAIL  Test_214 (label at col-1) got " + t); fail++;
            }
        }

        // Test_218: goto field
        {
            File tmp = File.createTempFile("tlex218_", ".sno");
            tmp.deleteOnExit();
            try (PrintWriter pw = new PrintWriter(tmp)) { pw.println("        X = Y   :S(DONE)"); }
            Lexer lx = new Lexer(); lx.openFile(tmp.getPath());
            List<Lexer.Token> toks = new ArrayList<>();
            Lexer.Token t;
            while ((t = lx.next()).kind != Lexer.TokKind.T_EOF) toks.add(t);
            boolean found = toks.stream().anyMatch(x -> x.kind == Lexer.TokKind.T_GOTO && "S(DONE)".equals(x.sval));
            if (found) { System.out.println("PASS  Test_218 (goto field)"); pass++; }
            else       { System.out.println("FAIL  Test_218 (goto field) tokens=" + toks); fail++; }
        }

        // Test_231: integer literal
        assertFirstKind("Test_231 (integer literal)", "42", Lexer.TokKind.T_INT);

        // Test_232: string literal
        assertFirstKind("Test_232 (string literal)", "'hello'", Lexer.TokKind.T_STR);

        // Test_220: T_PLUS
        assertFirstKind("Test_220 (T_PLUS)",  "+", Lexer.TokKind.T_PLUS);
        // Test_221: T_STAR
        assertFirstKind("Test_221 (T_STAR)",  "*", Lexer.TokKind.T_STAR);
        // Test_233: T_STARSTAR
        assertFirstKind("Test_233 (T_STARSTAR)", "**", Lexer.TokKind.T_STARSTAR);

        // T_KEYWORD
        assertFirstKind("Test_keyword (&ANCHOR)", "&ANCHOR", Lexer.TokKind.T_KEYWORD);

        // T_END in body
        assertContains("Test_END_body", "END", Lexer.TokKind.T_END, "END");

        // T_REAL
        assertFirstKind("Test_real (3.14)", "3.14", Lexer.TokKind.T_REAL);

        // T_STMT_END emitted
        {
            File tmp = File.createTempFile("tlex_stmtend_", ".sno");
            tmp.deleteOnExit();
            try (PrintWriter pw = new PrintWriter(tmp)) { pw.println("        OUTPUT = 'hi'"); }
            Lexer lx = new Lexer(); lx.openFile(tmp.getPath());
            List<Lexer.Token> toks = new ArrayList<>();
            Lexer.Token t;
            while ((t = lx.next()).kind != Lexer.TokKind.T_EOF) toks.add(t);
            boolean found = toks.stream().anyMatch(x -> x.kind == Lexer.TokKind.T_STMT_END);
            if (found) { System.out.println("PASS  Test_STMT_END emitted"); pass++; }
            else       { System.out.println("FAIL  Test_STMT_END not found, tokens=" + toks); fail++; }
        }

        // continuation lines joined
        {
            File tmp = File.createTempFile("tlex_cont_", ".sno");
            tmp.deleteOnExit();
            try (PrintWriter pw = new PrintWriter(tmp)) {
                pw.println("        X = 'hel'");
                pw.println("+            'lo'");
            }
            Lexer lx = new Lexer(); lx.openFile(tmp.getPath());
            List<Lexer.Token> toks = new ArrayList<>();
            Lexer.Token t;
            while ((t = lx.next()).kind != Lexer.TokKind.T_EOF) toks.add(t);
            // should produce exactly one T_STMT_END (one logical line)
            long stmtEnds = toks.stream().filter(x -> x.kind == Lexer.TokKind.T_STMT_END).count();
            if (stmtEnds == 1) { System.out.println("PASS  Test_continuation (1 stmt_end)"); pass++; }
            else               { System.out.println("FAIL  Test_continuation got " + stmtEnds + " stmt_ends"); fail++; }
        }

        // ── File gate: 19 corpus inputs tokenize without error ────────────────

        System.out.println("\n--- File gate (19 inputs) ---");

        // smoke tests (in corpus/crosscheck/hello/)
        String smokeDir = corpusRoot + "/crosscheck/hello";
        String[] smokeSno = {"hello", "empty_string", "multi", "literals"};
        for (String name : smokeSno) {
            assertTokenises(smokeDir + "/" + name + ".sno");
        }

        // crosscheck assign rung
        for (int i = 9; i <= 16; i++) {
            String num = String.format("%03d", i);
            String[] names = {
                "009_assign_string", "010_assign_integer", "011_assign_chain",
                "012_assign_null",   "013_assign_overwrite", "014_assign_indirect_dollar",
                "015_assign_indirect_var", "016_assign_to_output"
            };
            assertTokenises(corpusRoot + "/crosscheck/assign/" + names[i - 9] + ".sno");
        }

        // crosscheck arith_new
        String[] arithNames = {
            "023_arith_add", "024_arith_subtract", "025_arith_multiply",
            "026_arith_divide", "027_arith_exponent", "028_arith_unary_minus",
            "029_arith_precedence"
        };
        for (String name : arithNames) {
            assertTokenises(corpusRoot + "/crosscheck/arith_new/" + name + ".sno");
        }

        System.out.println("\n=== " + pass + " PASS  " + fail + " FAIL ===");
        System.exit(fail > 0 ? 1 : 0);
    }
}
