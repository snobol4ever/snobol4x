; bb_lit.s   _XCHR       literal string match
; Three-column Byrd box — pure x86-64, no macros
; spec_t  bb_lit(void *zeta, int entry)
;   rdi = zeta (lit_t*)    esi = entry (0=α, 1=β)
; lit_t: { const char *lit @0; int len @8 }
; spec_t return: rax=σ, rdx=δ   |   spec_empty: rax=0, rdx=0

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_lit

bb_lit:                                                                 ; rdi=zeta, esi=entry
        push    rbx
        push    r12
        mov     rbx, rdi                ; rbx = ζ (lit_t*)
        ; spec_t LIT (stored on stack as σ:ptr @[rsp+0], δ:int @[rsp+8])
        sub     rsp, 16
        cmp     esi, 0
        je      LIT_α
        jmp     LIT_β
LIT_α:  mov     eax, dword [rel Δ]      ; Δ
        mov     ecx, dword [rbx+8]      ; ζ->len
        add     eax, ecx                ; Δ + ζ->len
        cmp     eax, dword [rel Ω]      ; > Ω ?
        jg      LIT_ω
        ; memcmp(Σ+Δ, ζ->lit, ζ->len)
        mov     r12, qword [rel Σ]      ; r12 = Σ
        movsxd  rax, dword [rel Δ]      ; Δ (sign-extended)
        lea     rdi, [r12+rax]          ; Σ+Δ
        mov     rsi, qword [rbx+0]      ; ζ->lit
        movsxd  rdx, dword [rbx+8]      ; ζ->len
        call    memcmp
        test    eax, eax
        jne     LIT_ω
        ; LIT = spec(Σ+Δ, ζ->len); Δ += ζ->len
        mov     r12, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        lea     rax, [r12+rcx]          ; σ = Σ+Δ
        mov     ecx, dword [rbx+8]      ; δ = ζ->len
        mov     qword [rsp+0], rax      ; LIT.σ
        mov     dword [rsp+8], ecx      ; LIT.δ
        add     dword [rel Δ], ecx      ; Δ += ζ->len
        jmp     LIT_γ
LIT_β:  mov     ecx, dword [rbx+8]      ; ζ->len
        sub     dword [rel Δ], ecx      ; Δ -= ζ->len
        jmp     LIT_ω
LIT_γ:  mov     rax, qword [rsp+0]      ; return LIT.σ
        movsxd  rdx, dword [rsp+8]      ; return LIT.δ
        add     rsp, 16
        pop     r12
        pop     rbx
        ret
LIT_ω:  xor     eax, eax                ; return spec_empty
        xor     edx, edx
        add     rsp, 16
        pop     r12
        pop     rbx
        ret

extern memcmp
