; bb_tab.s   _XTB        advance cursor TO absolute position n
; spec_t  bb_tab(void *zeta, int entry)
;   rdi = zeta (tab_t*)    esi = entry
; tab_t: { int n @0; int advance @4 }

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ, Ω

section .text
global bb_tab

bb_tab:
        push    rbx
        mov     rbx, rdi                ; rbx = ζ (tab_t*)
        cmp     esi, 0
        je      TAB_α
        jmp     TAB_β
TAB_α:  mov     eax, dword [rel Δ]      ; Δ
        cmp     eax, dword [rbx+0]      ; Δ > ζ->n ?
        jg      TAB_ω
        ; ζ->advance = ζ->n - Δ
        mov     ecx, dword [rbx+0]      ; ζ->n
        sub     ecx, dword [rel Δ]      ; ζ->n - Δ
        mov     dword [rbx+4], ecx      ; ζ->advance = ζ->n-Δ
        ; TAB = spec(Σ+Δ, ζ->advance);  Δ = ζ->n
        mov     rax, qword [rel Σ]
        movsxd  rcx, dword [rel Δ]
        add     rax, rcx                ; σ = Σ+Δ
        mov     edx, dword [rbx+4]      ; δ = ζ->advance
        mov     ecx, dword [rbx+0]
        mov     dword [rel Δ], ecx      ; Δ = ζ->n
        jmp     TAB_γ
TAB_β:  mov     ecx, dword [rbx+4]      ; ζ->advance
        sub     dword [rel Δ], ecx      ; Δ -= ζ->advance
        jmp     TAB_ω
TAB_γ:  pop     rbx
        ret
TAB_ω:  xor     eax, eax
        xor     edx, edx
        pop     rbx
        ret
