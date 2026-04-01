/*
 * bb_emit_test.c — Unit test for bb_emit (M-DYN-1 gate)
 *
 * Build:
 *   gcc -o bb_emit_test src/runtime/asm/bb_pool.c \
 *                        src/runtime/asm/bb_emit.c \
 *                        src/runtime/asm/bb_emit_test.c
 * Gate: all cases PASS, exits 0.
 *
 * Tests:
 *   [1] Byte primitives — emit known sequences, verify exact bytes
 *   [2] Label define — backward and forward references, rel8 and rel32
 *   [3] Instruction helpers — spot-check encodings
 *   [4] Build a working 'hello' matcher in binary mode — same semantics
 *       as bb_poc.c but via bb_emit API.  6/6 subject cases correct.
 *   [5] Text mode — same 'hello' matcher emitted as NASM text, spot-check
 *       that expected mnemonics appear in the output.
 */

#include "bb_pool.h"
#include "bb_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int failures = 0;
#define PASS(msg)        printf("  PASS  %s\n", msg)
#define FAIL(msg)        do { printf("  FAIL  %s\n", msg); failures++; } while(0)
#define CHECK(cond, msg) do { if(cond) PASS(msg); else FAIL(msg); } while(0)

/* box function: (subject_ptr, subject_len) -> 1=match 0=no-match */
typedef int (*box_fn)(const char *, int);

/* ── [1] byte primitives ─────────────────────────────────────────────────── */

static void test_byte_primitives(void)
{
    printf("\n[1] byte primitives\n");

    bb_buf_t buf = bb_alloc(64);
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, 64);

    bb_emit_byte(0xAA);
    bb_emit_u16(0x1234);
    bb_emit_u32(0xDEADBEEF);
    bb_emit_u64(0x0102030405060708ULL);
    bb_emit_i8(-1);
    bb_emit_i32(-1);

    int n = bb_emit_end();
    CHECK(n == 1+2+4+8+1+4, "correct byte count: 20");
    CHECK(buf[0]  == 0xAA,          "byte 0xAA");
    CHECK(buf[1]  == 0x34,          "u16 low byte");
    CHECK(buf[2]  == 0x12,          "u16 high byte");
    CHECK(buf[3]  == 0xEF && buf[4] == 0xBE &&
          buf[5]  == 0xAD && buf[6] == 0xDE, "u32 0xDEADBEEF LE");
    CHECK(buf[7]  == 0x08 && buf[8]  == 0x07 &&
          buf[9]  == 0x06 && buf[10] == 0x05 &&
          buf[11] == 0x04 && buf[12] == 0x03 &&
          buf[13] == 0x02 && buf[14] == 0x01, "u64 LE");
    CHECK((int8_t)buf[15] == -1,    "i8 -1");
    CHECK(buf[16] == 0xFF && buf[17] == 0xFF &&
          buf[18] == 0xFF && buf[19] == 0xFF, "i32 -1");

    bb_free(buf, 64);
}

/* ── [2] label / patch system ────────────────────────────────────────────── */

static void test_labels(void)
{
    printf("\n[2] label / patch system\n");

    bb_buf_t buf = bb_alloc(64);
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, 64);

    /*
     * Backward reference layout:
     *   offset 0: nop  (0x90)
     *   offset 1: EB   (jmp opcode)
     *   offset 2: disp (relative to next instruction = offset 3)
     *   disp = target(0) - (disp_site(2) + 1) = 0 - 3 = -3 = 0xFD
     */
    bb_label_t backward;
    bb_label_init(&backward, "backward");
    bb_label_define(&backward);          /* offset = 0, emits nothing */
    bb_emit_byte(0x90);                  /* offset 0: nop */
    bb_insn_jmp_rel8(&backward);         /* offset 1: EB, offset 2: disp */

    CHECK(buf[1] == 0xEB,           "backward jmp opcode EB at buf[1]");
    CHECK((int8_t)buf[2] == -3,     "backward jmp disp8 = -3 (lands at offset 0)");

    /*
     * Forward reference layout:
     *   offset 3: EB   (jmp opcode)
     *   offset 4: disp (unresolved → placeholder 0x00)
     *   offset 5: nop  (skipped by jump)
     *   offset 6: nop  (jump target defined here)
     *   disp = target(6) - (disp_site(4) + 1) = 6 - 5 = 1
     */
    bb_label_t forward;
    bb_label_init(&forward, "forward");
    bb_insn_jmp_rel8(&forward);         /* offset 3: EB, offset 4: ?? */
    bb_emit_byte(0x90);                 /* offset 5: nop (skipped) */
    bb_label_define(&forward);          /* offset 6: define → patches disp at 4 */
    bb_emit_byte(0x90);                 /* offset 6: nop at target */

    int n = bb_emit_end();
    CHECK(n == 7,                   "correct total bytes: 7");
    CHECK(buf[3] == 0xEB,           "forward jmp opcode EB at buf[3]");
    CHECK((int8_t)buf[4] == 1,      "forward jmp disp8 patched = 1");
    CHECK(forward.offset == 6,      "forward label offset == 6");

    bb_free(buf, 64);
}

/* ── [3] instruction encodings ───────────────────────────────────────────── */

static void test_instruction_encodings(void)
{
    printf("\n[3] instruction encodings\n");

    bb_buf_t buf = bb_alloc(64);
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, 64);

    bb_insn_ret();                           /* C3 */
    bb_insn_nop();                           /* 90 */
    bb_insn_xor_eax_eax();                   /* 31 C0 */
    bb_insn_mov_eax_imm32(42);               /* B8 2A 00 00 00 */
    bb_insn_cmp_esi_imm8(5);                 /* 83 FE 05 */
    bb_insn_cmp_al_imm8(0x68);              /* 3C 68  ('h') */
    bb_insn_movzx_eax_rdi_off8(3);           /* 0F B6 47 03 */
    bb_insn_mov_rax_imm64(0xDEADBEEFCAFEBABEULL); /* 48 B8 ... */

    bb_emit_end();

    int p = 0;
    CHECK(buf[p++] == 0xC3, "ret = C3");
    CHECK(buf[p++] == 0x90, "nop = 90");
    CHECK(buf[p] == 0x31 && buf[p+1] == 0xC0, "xor eax,eax = 31 C0"); p+=2;
    CHECK(buf[p] == 0xB8 && buf[p+1] == 42,   "mov eax,42: B8 2A"); p+=5;
    CHECK(buf[p] == 0x83 && buf[p+1] == 0xFE && buf[p+2] == 5,
          "cmp esi,5 = 83 FE 05"); p+=3;
    CHECK(buf[p] == 0x3C && buf[p+1] == 0x68, "cmp al,0x68 = 3C 68"); p+=2;
    CHECK(buf[p] == 0x0F && buf[p+1] == 0xB6 &&
          buf[p+2] == 0x47 && buf[p+3] == 3,
          "movzx eax,byte[rdi+3] = 0F B6 47 03"); p+=4;
    CHECK(buf[p] == 0x48 && buf[p+1] == 0xB8, "mov rax,imm64 = 48 B8"); p+=2;
    uint64_t got;
    memcpy(&got, buf+p, 8);
    CHECK(got == 0xDEADBEEFCAFEBABEULL, "mov rax,imm64 value correct");

    bb_free(buf, 64);
}

/* ── [4] binary mode: working 'hello' matcher ───────────────────────────── */
/*
 * Builds the same LIT 'hello' Byrd box as bb_poc.c, but via bb_emit API.
 * Layout:
 *   α entry: cmp esi,5 / jl ω
 *   for i in 0..4: movzx eax,byte[rdi+i] / cmp al,lit[i] / jne ω
 *   γ: mov eax,1 / ret
 *   ω: mov eax,0 / ret
 */
static box_fn build_hello_box_binary(void)
{
    bb_buf_t buf = bb_alloc(256);
    bb_emit_mode = EMIT_BINARY;
    bb_emit_begin(buf, 256);

    const char *lit = "hello";
    int litlen = 5;

    bb_label_t lbl_ω;
    bb_label_init(&lbl_ω, "omega");

    /* α: length check */
    bb_insn_cmp_esi_imm8((uint8_t)litlen);
    bb_insn_jl_rel8(&lbl_ω);

    /* byte-by-byte literal check (unrolled) */
    bb_label_t lbl_jne[5];
    for (int i = 0; i < litlen; i++) {
        bb_label_initf(&lbl_jne[i], "jne_%d", i);
        bb_insn_movzx_eax_rdi_off8((uint8_t)i);
        bb_insn_cmp_al_imm8((uint8_t)(unsigned char)lit[i]);
        bb_insn_jne_rel8(&lbl_ω);
    }

    /* γ port */
    bb_insn_mov_eax_imm32(1);
    bb_insn_ret();

    /* ω port */
    bb_label_define(&lbl_ω);
    bb_insn_mov_eax_imm32(0);
    bb_insn_ret();

    int n = bb_emit_end();
    bb_seal(buf, (size_t)n);
    return (box_fn)buf;
}

static void test_binary_hello_box(void)
{
    printf("\n[4] binary mode: 'hello' Byrd box via bb_emit API\n");

    box_fn box = build_hello_box_binary();

    struct { const char *subj; int expect; } cases[] = {
        { "hello world", 1 },
        { "hello",       1 },
        { "goodbye",     0 },
        { "hell",        0 },
        { "HELLO",       0 },
        { "say hello",   0 },
    };
    int n = (int)(sizeof cases / sizeof cases[0]);

    int all_ok = 1;
    for (int i = 0; i < n; i++) {
        int got = box(cases[i].subj, (int)strlen(cases[i].subj));
        int ok  = (got == cases[i].expect);
        if (!ok) all_ok = 0;
        printf("    subject=%-14s  expect=%d  got=%d  %s\n",
               cases[i].subj, cases[i].expect, got, ok ? "✓" : "✗");
    }
    CHECK(all_ok, "all 6 subject cases correct via bb_emit API");
}

/* ── [5] text mode: 'hello' matcher emitted as NASM text ─────────────────── */

static void build_hello_box_text(FILE *f)
{
    bb_emit_mode = EMIT_TEXT;
    bb_emit_out  = f;

    const char *lit = "hello";
    int litlen = 5;

    bb_label_t lbl_α, lbl_γ, lbl_ω;
    bb_label_init(&lbl_α, "hello_alpha");
    bb_label_init(&lbl_γ, "hello_gamma");
    bb_label_init(&lbl_ω, "hello_omega");

    bb_text_comment("LIT 'hello' Byrd box");
    bb_label_define(&lbl_α);
    bb_insn_cmp_esi_imm8((uint8_t)litlen);
    bb_insn_jl_rel8(&lbl_ω);

    for (int i = 0; i < litlen; i++) {
        bb_insn_movzx_eax_rdi_off8((uint8_t)i);
        bb_insn_cmp_al_imm8((uint8_t)(unsigned char)lit[i]);
        bb_insn_jne_rel8(&lbl_ω);
    }

    bb_label_define(&lbl_γ);
    bb_insn_mov_eax_imm32(1);
    bb_insn_ret();

    bb_label_define(&lbl_ω);
    bb_insn_mov_eax_imm32(0);
    bb_insn_ret();
}

static void test_text_mode(void)
{
    printf("\n[5] text mode: NASM output spot-check\n");

    char outbuf[4096] = {0};
    FILE *f = fmemopen(outbuf, sizeof(outbuf) - 1, "w");
    if (!f) { FAIL("fmemopen failed"); return; }

    build_hello_box_text(f);
    fclose(f);

    CHECK(strstr(outbuf, "hello_alpha:") != NULL, "alpha label emitted");
    CHECK(strstr(outbuf, "hello_gamma:") != NULL, "gamma label emitted");
    CHECK(strstr(outbuf, "hello_omega:") != NULL, "omega label emitted");
    CHECK(strstr(outbuf, "cmp     esi")  != NULL, "cmp esi emitted");
    CHECK(strstr(outbuf, "jl ")          != NULL, "jl emitted");
    CHECK(strstr(outbuf, "movzx")        != NULL, "movzx emitted");
    CHECK(strstr(outbuf, "jne ")         != NULL, "jne emitted");
    CHECK(strstr(outbuf, "mov     eax, 1") != NULL, "mov eax,1 (γ) emitted");
    CHECK(strstr(outbuf, "mov     eax, 0") != NULL, "mov eax,0 (ω) emitted");
    CHECK(strstr(outbuf, "ret")          != NULL, "ret emitted");

    printf("    --- text output ---\n");
    /* Print it with leading spaces so it's easy to read */
    char *line = outbuf;
    char *end;
    while ((end = strchr(line, '\n')) != NULL) {
        *end = '\0';
        printf("    %s\n", line);
        line = end + 1;
    }
    printf("    --- end ---\n");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("bb_emit_test — M-DYN-1 unit test\n");

    bb_pool_init();

    test_byte_primitives();
    test_labels();
    test_instruction_encodings();
    test_binary_hello_box();
    test_text_mode();

    bb_pool_destroy();

    printf("\n%s  (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
