; bb_abort.s  _XABRT     always ω — force match failure
; spec_t  bb_abort(void *zeta, int entry)
;   rdi = zeta (ignored)   esi = entry

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global bb_abort

bb_abort:
        cmp     esi, 0
        je      ABORT_α
        jmp     ABORT_β
ABORT_α:                                jmp     ABORT_ω
ABORT_β:                                jmp     ABORT_ω
ABORT_ω:
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        ret
