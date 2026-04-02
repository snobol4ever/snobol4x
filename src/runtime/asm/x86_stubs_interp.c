/* x86_stubs.c — satisfy snobol4_stmt_rt.c's asm-side extern references
 * for non-x86 builds (scrip-interp). These are never called by the
 * interpreter path; they exist only to satisfy the linker. */
#include <stdint.h>
uint64_t cursor        = 0;
uint64_t subject_len_val = 0;
char     subject_data[65536] = {0};
