; bb_len.s   _XLNTH      match exactly n characters
; spec_t  bb_len(void *zeta, int entry)
;   rdi = zeta (len_t*)    esi = entry
; len_t: { int n @0 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_len

bb_len:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (len_t*)
        cmp     esi, 0
        je      LEN_α
        jmp     LEN_β
LEN_α:  mov     eax, dword [rel Δ]      ; Δ
        add     eax, dword [rbx+0]      ; Δ + ζ->n
        cmp     eax, dword [rel Ω]      ; > Ω ?
        jg      LEN_ω
        ; LEN = spec(Σ+Δ, ζ->n);  Δ += ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+0]      ; δ = ζ->n
        add     dword [rel Δ], edx      ; Δ += ζ->n
        jmp     LEN_γ
LEN_β:  mov     ecx, dword [rbx+0]      ; ζ->n
        sub     dword [rel Δ], ecx      ; Δ -= ζ->n
        jmp     LEN_ω
LEN_γ:  pop     rbx
        ret
LEN_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret
