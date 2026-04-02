; bb_fail.s  _XFAIL      always ω — force backtrack
; spec_t  bb_fail(void *zeta, int entry)
;   rdi = zeta (ignored)   esi = entry (ignored)

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global bb_fail

bb_fail:
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        ret
