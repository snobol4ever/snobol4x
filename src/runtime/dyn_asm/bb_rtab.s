; bb_rtab.s  _XRTB       advance cursor TO position Ω-n
; spec_t  bb_rtab(void *zeta, int entry)
;   rdi = zeta (rtab_t*)   esi = entry
; rtab_t: { int n @0; int advance @4 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_rtab

bb_rtab:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (rtab_t*)
        cmp     esi, 0
        je      RTAB_α
        jmp     RTAB_β
RTAB_α: ; if (Δ > Ω-ζ->n) goto ω
        mov     eax, dword [rel Ω]
        sub     eax, dword [rbx+0]      ; Ω-ζ->n
        cmp     dword [rel Δ], eax      ; Δ > Ω-n ?
        jg      RTAB_ω
        ; ζ->advance = (Ω-ζ->n) - Δ
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rbx+0]      ; Ω-ζ->n
        sub     ecx, dword [rel Δ]      ; (Ω-n)-Δ
        mov     dword [rbx+4], ecx      ; ζ->advance
        ; RTAB = spec(Σ+Δ, ζ->advance);  Δ = Ω-ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+4]      ; δ = ζ->advance
        mov     ecx, dword [rel Ω]
        sub     ecx, dword [rbx+0]
        mov     dword [rel Δ], ecx      ; Δ = Ω-ζ->n
        jmp     RTAB_γ
RTAB_β: mov     ecx, dword [rbx+4]      ; ζ->advance
        sub     dword [rel Δ], ecx      ; Δ -= ζ->advance
        jmp     RTAB_ω
RTAB_γ: pop     rbx
        ret
RTAB_ω: xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret
