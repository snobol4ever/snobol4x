/*
 * test_blk_reloc.c — unit test for blk_relocate()
 *
 * Builds synthetic TEXT blocks with known REL32 and ABS64 fields,
 * applies blk_relocate with a known delta, and verifies the patched values.
 *
 * Also exercises the full copy→relocate→flush→mprotect_rx pipeline
 * to confirm the idiom is correct end-to-end.
 *
 * Compile:
 *   gcc -Wall -o /tmp/test_blk_reloc \
 *       test/t2/test_blk_reloc.c \
 *       src/runtime/asm/blk_reloc.c \
 *       src/runtime/asm/blk_alloc.c
 * Run:
 *   /tmp/test_blk_reloc  → PASS for each test, exit 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../../src/runtime/asm/blk_alloc.h"
#include "../../src/runtime/asm/blk_reloc.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else          { printf("PASS: %s\n", msg); } \
} while(0)

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static void put_i32(uint8_t *buf, size_t off, int32_t v) {
    memcpy(buf + off, &v, 4);
}
static void put_u64(uint8_t *buf, size_t off, uint64_t v) {
    memcpy(buf + off, &v, 8);
}
static int32_t get_i32(const uint8_t *buf, size_t off) {
    int32_t v; memcpy(&v, buf + off, 4); return v;
}
static uint64_t get_u64(const uint8_t *buf, size_t off) {
    uint64_t v; memcpy(&v, buf + off, 8); return v;
}

/* -----------------------------------------------------------------------
 * Test 1: single REL32 — forward jump, small delta
 * --------------------------------------------------------------------- */
static void test_rel32_forward(void) {
    /* Synthetic TEXT: 16 bytes.  Bytes 4..7 hold a rel32 = 0x00000010 (jump fwd 16) */
    uint8_t text[16] = {0};
    put_i32(text, 4, 0x10);  /* original rel32 */

    blk_reloc_entry e = { .offset = 4, .kind = BLK_RELOC_REL32 };
    ptrdiff_t delta = 0x1000;  /* block moved forward 4096 bytes */

    int r = blk_relocate(text, sizeof text, delta, &e, 1);
    CHECK(r == 0, "rel32_forward: blk_relocate returns 0");

    /* new_rel32 = 0x10 - 0x1000 = -0xFF0 */
    int32_t expected = 0x10 - 0x1000;
    CHECK(get_i32(text, 4) == expected, "rel32_forward: patched value correct");
}

/* -----------------------------------------------------------------------
 * Test 2: single REL32 — negative delta (block moved backward)
 * --------------------------------------------------------------------- */
static void test_rel32_backward(void) {
    uint8_t text[16] = {0};
    put_i32(text, 0, -0x80);   /* rel32 = -128 */

    blk_reloc_entry e = { .offset = 0, .kind = BLK_RELOC_REL32 };
    ptrdiff_t delta = -0x200;  /* block moved backward 512 bytes */

    int r = blk_relocate(text, sizeof text, delta, &e, 1);
    CHECK(r == 0, "rel32_backward: returns 0");

    int32_t expected = -0x80 - (-0x200);   /* = 0x180 */
    CHECK(get_i32(text, 0) == expected, "rel32_backward: patched value correct");
}

/* -----------------------------------------------------------------------
 * Test 3: single ABS64
 * --------------------------------------------------------------------- */
static void test_abs64(void) {
    uint8_t text[16] = {0};
    uint64_t orig_ptr = 0xDEADBEEF12340000ULL;
    put_u64(text, 4, orig_ptr);

    blk_reloc_entry e = { .offset = 4, .kind = BLK_RELOC_ABS64 };
    ptrdiff_t delta = 0x8000;

    int r = blk_relocate(text, sizeof text, delta, &e, 1);
    CHECK(r == 0, "abs64: returns 0");
    CHECK(get_u64(text, 4) == orig_ptr + 0x8000, "abs64: patched value correct");
}

/* -----------------------------------------------------------------------
 * Test 4: mixed table — two REL32 + one ABS64
 * --------------------------------------------------------------------- */
static void test_mixed_table(void) {
    uint8_t text[32] = {0};
    put_i32(text,  0, 0x100);
    put_i32(text,  8, -0x40);
    put_u64(text, 16, 0xCAFEBABE00000000ULL);

    blk_reloc_entry table[3] = {
        { .offset =  0, .kind = BLK_RELOC_REL32 },
        { .offset =  8, .kind = BLK_RELOC_REL32 },
        { .offset = 16, .kind = BLK_RELOC_ABS64 },
    };
    ptrdiff_t delta = 0x4000;

    int r = blk_relocate(text, sizeof text, delta, table, 3);
    CHECK(r == 0, "mixed_table: returns 0");
    CHECK(get_i32(text,  0) == 0x100  - 0x4000, "mixed_table: rel32[0] correct");
    CHECK(get_i32(text,  8) == -0x40  - 0x4000, "mixed_table: rel32[1] correct");
    CHECK(get_u64(text, 16) == 0xCAFEBABE00000000ULL + 0x4000,
          "mixed_table: abs64 correct");
}

/* -----------------------------------------------------------------------
 * Test 5: zero delta — nothing changes
 * --------------------------------------------------------------------- */
static void test_zero_delta(void) {
    uint8_t text[16] = {0};
    put_i32(text, 0, 0x55AA);
    put_u64(text, 8, 0x1234567890ABCDEFULL);

    blk_reloc_entry table[2] = {
        { .offset = 0, .kind = BLK_RELOC_REL32 },
        { .offset = 8, .kind = BLK_RELOC_ABS64 },
    };
    int r = blk_relocate(text, sizeof text, 0, table, 2);
    CHECK(r == 0, "zero_delta: returns 0");
    CHECK(get_i32(text, 0) == 0x55AA, "zero_delta: rel32 unchanged");
    CHECK(get_u64(text, 8) == 0x1234567890ABCDEFULL, "zero_delta: abs64 unchanged");
}

/* -----------------------------------------------------------------------
 * Test 6: out-of-range REL32 overflow → returns -1
 * --------------------------------------------------------------------- */
static void test_rel32_overflow(void) {
    uint8_t text[16] = {0};
    put_i32(text, 0, INT32_MAX);   /* max positive */

    blk_reloc_entry e = { .offset = 0, .kind = BLK_RELOC_REL32 };
    /* delta = -1 → new_rel32 = INT32_MAX - (-1) = INT32_MAX+1 → overflow */
    int r = blk_relocate(text, sizeof text, -1, &e, 1);
    CHECK(r == -1, "rel32_overflow: returns -1 on overflow");
}

/* -----------------------------------------------------------------------
 * Test 7: out-of-bounds offset → returns -1
 * --------------------------------------------------------------------- */
static void test_oob_offset(void) {
    uint8_t text[8] = {0};
    blk_reloc_entry e = { .offset = 6, .kind = BLK_RELOC_REL32 };  /* 6+4=10 > 8 */
    int r = blk_relocate(text, sizeof text, 0, &e, 1);
    CHECK(r == -1, "oob_offset: returns -1 for out-of-bounds REL32");

    blk_reloc_entry e2 = { .offset = 4, .kind = BLK_RELOC_ABS64 };  /* 4+8=12 > 8 */
    int r2 = blk_relocate(text, sizeof text, 0, &e2, 1);
    CHECK(r2 == -1, "oob_offset: returns -1 for out-of-bounds ABS64");
}

/* -----------------------------------------------------------------------
 * Test 8: null/empty table — no-op
 * --------------------------------------------------------------------- */
static void test_empty_table(void) {
    uint8_t text[8] = {0xCC};
    CHECK(blk_relocate(text, 8, 100, NULL, 0) == 0, "empty_table: NULL table returns 0");
    CHECK(blk_relocate(text, 8, 100, (blk_reloc_entry*)text, 0) == 0,
          "empty_table: n=0 returns 0");
}

/* -----------------------------------------------------------------------
 * Test 9: full pipeline — alloc, memcpy, relocate, flush, mprotect_rx
 *          Uses a real x64 snippet: push rax; mov eax,0x42; pop rax; ret
 * --------------------------------------------------------------------- */
static void test_full_pipeline(void) {
    /* Tiny x64 function — mov eax, 0x42; ret
     * No relocations needed; just verifies copy→flush→mprotect_rx→call. */
    static const uint8_t snippet[] = {
        0xB8, 0x42, 0x00, 0x00, 0x00,  /* mov eax, 66  */
        0xC3                            /* ret          */
    };
    size_t sz = sizeof snippet;

    uint8_t *text = blk_alloc(sz);
    CHECK(text != NULL, "pipeline: blk_alloc succeeds");

    memcpy(text, snippet, sz);

    /* No relocations in this snippet */
    int r = blk_relocate(text, sz, 0, NULL, 0);
    CHECK(r == 0, "pipeline: blk_relocate (no entries) succeeds");

    /* Flush i$/d$ before marking executable */
    blk_flush_icache(text, sz);
    CHECK(1, "pipeline: blk_flush_icache called");

    int rx = blk_mprotect_rx(text, sz);
    CHECK(rx == 0, "pipeline: blk_mprotect_rx succeeds");

    /* Call the snippet — expect return value 0x42 */
    typedef int (*fn_t)(void);
    fn_t fn = (fn_t)(void *)text;
    int val = fn();
    CHECK(val == 0x42, "pipeline: JIT'd snippet returns 0x42");

    blk_free(text, sz);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(void) {
    test_rel32_forward();
    test_rel32_backward();
    test_abs64();
    test_mixed_table();
    test_zero_delta();
    test_rel32_overflow();
    test_oob_offset();
    test_empty_table();
    test_full_pipeline();

    printf("\n%s — %d test(s) failed\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
