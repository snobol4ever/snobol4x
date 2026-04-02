; bb_bal.s   _XBAL       balanced parens — STUB; M-DYN-BAL pending
; spec_t  bb_bal(void *zeta, int entry)
;   rdi = zeta (bal_t*)    esi = entry (ignored)
; bal_t: { int δ @0; int start @4 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern stderr
extern fprintf

section .data
.bal_msg: db "bb_bal: unimplemented — ω", 10, 0

section .text
global bb_bal

bb_bal:
        push    rbx
        sub     rsp, 8
        ; fprintf(stderr, "bb_bal: unimplemented — ω\n")
        mov     rdi, qword [rel stderr]
        lea     rsi, [rel .bal_msg]
        xor     eax, eax
        call    fprintf
        xor     eax, eax                ; return spec_empty
        xor     edx, edx
        add     rsp, 8
        pop     rbx
        ret
