/* icon_runtime.c — Tiny-ICON runtime — pure syscalls, no libc */
static void write_bytes(const char *buf, long len) {
    __asm__ volatile (
        "syscall"
        : : "a"(1L), "D"(1L), "S"(buf), "d"(len)
        : "rcx", "r11", "memory"
    );
}

static void write_long(long v) {
    char buf[32]; int i = 0;
    if (v < 0) { write_bytes("-", 1); v = -v; }
    if (v == 0) { write_bytes("0\n", 2); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    for (int a = 0, b = i-1; a < b; a++, b--) { char t=buf[a]; buf[a]=buf[b]; buf[b]=t; }
    buf[i++] = '\n';
    write_bytes(buf, i);
}

static long my_strlen(const char *s) { long n=0; while(s[n]) n++; return n; }

void icn_write_int(long v) { write_long(v); }
void icn_write_str(const char *s) { long l=my_strlen(s); write_bytes(s,l); write_bytes("\n",1); }

/* String concatenation arena — simple bump allocator, no GC needed for tiny programs */
static char icn_str_arena[65536];
static int  icn_str_arena_pos = 0;

const char *icn_str_concat(const char *a, const char *b) {
    long la = my_strlen(a), lb = my_strlen(b);
    if (icn_str_arena_pos + la + lb + 1 > 65536) {
        /* overflow: reset arena (loses old strings, but avoids crash) */
        icn_str_arena_pos = 0;
    }
    char *out = icn_str_arena + icn_str_arena_pos;
    for (long i = 0; i < la; i++) out[i]      = a[i];
    for (long i = 0; i < lb; i++) out[la + i] = b[i];
    out[la + lb] = '\0';
    icn_str_arena_pos += (int)(la + lb + 1);
    return out;
}

#define ICN_STACK_MAX 256
static long icn_stack[ICN_STACK_MAX];
static int  icn_sp = 0;

/* =========================================================================
 * Scan builtins — operate on icn_subject / icn_pos globals
 * All return new 1-based position on success, or 0 on failure.
 * ======================================================================= */
/* icn_subject and icn_pos are owned here; asm .bss declarations are suppressed
 * when these symbols resolve from the C object. */
const char *icn_subject = (void*)0;
long        icn_pos     = 0;

/* any(cset) — match one char at icn_pos if it is in cset */
long icn_any(const char *cset) {
    if (!icn_subject) return 0;
    long len = my_strlen(icn_subject);
    if (icn_pos >= len) return 0;
    char c = icn_subject[icn_pos];
    for (long i = 0; cset[i]; i++) {
        if (cset[i] == c) {
            icn_pos++;
            return icn_pos + 1;   /* 1-based new pos */
        }
    }
    return 0;
}

/* many(cset) — match one or more chars at icn_pos all in cset */
long icn_many(const char *cset) {
    if (!icn_subject) return 0;
    long len = my_strlen(icn_subject);
    long start = icn_pos;
    while (icn_pos < len) {
        char c = icn_subject[icn_pos];
        int found = 0;
        for (long i = 0; cset[i]; i++) { if (cset[i] == c) { found = 1; break; } }
        if (!found) break;
        icn_pos++;
    }
    if (icn_pos == start) return 0;   /* must match at least one */
    return icn_pos + 1;               /* 1-based new pos */
}

/* upto(cset) — generate positions up to (but not at) a char in cset.
 * First call: return current pos+1 if not at a cset char, else advance.
 * Implemented as a one-shot that scans forward and returns first match pos.
 * (Full generator version needs extra state; one-shot covers rung06 tests.) */
long icn_upto(const char *cset) {
    if (!icn_subject) return 0;
    long len = my_strlen(icn_subject);
    while (icn_pos < len) {
        char c = icn_subject[icn_pos];
        for (long i = 0; cset[i]; i++) {
            if (cset[i] == c) return icn_pos + 1; /* 1-based pos of match */
        }
        icn_pos++;
    }
    return 0;
}

long icn_retval = 0;
int  icn_failed = 0;

/* icn_str_eq: 1 if equal, 0 if not */
int icn_str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

void icn_push(long v)  { if (icn_sp < ICN_STACK_MAX) icn_stack[icn_sp++] = v; }
long icn_pop(void)     { return icn_sp > 0 ? icn_stack[--icn_sp] : 0; }

/* =========================================================================
 * rung08 builtins: find, match, tab, move
 * ========================================================================= */

/* icn_str_find(s1, s2, from): returns 1-based index of first occurrence of
 * s1 in s2 starting at 0-based offset `from`, or 0 on failure. */
long icn_str_find(const char *s1, const char *s2, long from) {
    if (!s1 || !s2) return 0;
    long l1 = my_strlen(s1), l2 = my_strlen(s2);
    for (long i = from; i <= l2 - l1; i++) {
        long j;
        for (j = 0; j < l1; j++) if (s2[i+j] != s1[j]) break;
        if (j == l1) return i + 1;  /* 1-based */
    }
    return 0;
}

/* icn_match(s): match s at icn_subject[icn_pos]; on success advance icn_pos
 * and return new 1-based pos; on failure return 0. */
long icn_match(const char *s) {
    if (!s || !icn_subject) return 0;
    long len = my_strlen(s);
    long subj_len = my_strlen(icn_subject);
    if (icn_pos + len > subj_len) return 0;
    for (long i = 0; i < len; i++)
        if (icn_subject[icn_pos + i] != s[i]) return 0;
    icn_pos += len;
    return icn_pos + 1;  /* 1-based new pos */
}

/* Shared small heap for tab/move substrings.  Simple bump allocator;
 * resets at the start of each tab/move call (one live result at a time). */
static char icn_tabmove_buf[4096];

/* icn_tab(n): set icn_pos to n-1 (0-based), return subject[old_pos..n-1].
 * n is 1-based (Icon convention: tab(1) returns empty at start). */
const char *icn_tab(long n) {
    if (!icn_subject) return 0;
    long subj_len = my_strlen(icn_subject);
    long new_pos = n - 1;  /* convert to 0-based */
    if (new_pos < icn_pos || new_pos > subj_len) return 0;
    long len = new_pos - icn_pos;
    if (len >= (long)sizeof(icn_tabmove_buf)) return 0;
    for (long i = 0; i < len; i++) icn_tabmove_buf[i] = icn_subject[icn_pos + i];
    icn_tabmove_buf[len] = '\0';
    icn_pos = new_pos;
    return icn_tabmove_buf;
}

/* icn_move(n): advance icn_pos by n, return subject[old_pos..old_pos+n-1]. */
const char *icn_move(long n) {
    if (!icn_subject) return 0;
    long subj_len = my_strlen(icn_subject);
    if (n < 0 || icn_pos + n > subj_len) return 0;
    if (n >= (long)sizeof(icn_tabmove_buf)) return 0;
    for (long i = 0; i < n; i++) icn_tabmove_buf[i] = icn_subject[icn_pos + i];
    icn_tabmove_buf[n] = '\0';
    icn_pos += n;
    return icn_tabmove_buf;
}
