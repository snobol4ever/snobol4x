; bb_arb.s   _XFARB      match 0..n chars lazily; β extends by 1
; spec_t  bb_arb(void *zeta, int entry)
;   rdi = zeta (arb_t*)    esi = entry
; arb_t: { int count @0; int start @4 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_arb

bb_arb:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (arb_t*)
        cmp     esi, 0
        je      ARB_α
        jmp     ARB_β
ARB_α:  ; ζ->count=0; ζ->start=Δ; ARB=spec(Σ+Δ,0)
        mov     dword [rbx+0], 0        ; ζ->count = 0
        mov     eax, dword [rel Δ]
        mov     dword [rbx+4], eax      ; ζ->start = Δ
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        xor     edx, edx                ; δ = 0
        jmp     ARB_γ
ARB_β:  ; ζ->count++
        add     dword [rbx+0], 1
        ; if (ζ->start + ζ->count > Ω) goto ω
        mov     eax, dword [rbx+4]      ; ζ->start
        add     eax, dword [rbx+0]      ; + ζ->count
        cmp     eax, dword [rel Ω]
        jg      ARB_ω
        ; Δ = ζ->start;  ARB = spec(Σ+Δ, ζ->count);  Δ += ζ->count
        mov     eax, dword [rbx+4]
        mov     dword [rel Δ], eax      ; Δ = ζ->start
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rbx+4]
        add     rax, rcx                ; σ = Σ+ζ->start
        mov     edx, dword [rbx+0]      ; δ = ζ->count
        add     dword [rel Δ], edx      ; Δ += ζ->count
        jmp     ARB_γ
ARB_γ:  pop     rbx
        ret
ARB_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret
