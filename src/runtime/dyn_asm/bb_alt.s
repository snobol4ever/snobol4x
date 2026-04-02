; bb_alt.s   _XOR        alternation: try each child on α; β retries same child
; spec_t  bb_alt(void *zeta, int entry)
;   rdi = zeta (alt_t*)    esi = entry
; alt_t: { int n @0; [pad4]; bb_altchild_t children[16] @8  (each 16 bytes: fn@+0,state@+8);
;           int current @264; int position @268; spec_t result @272{σ@272,δ@280} }
; sizeof(alt_t) = 288

section .note.GNU-stack noalloc noexec nowrite progbits

extern Σ, Δ

section .text
global bb_alt

; children[i] offset = 8 + i*16
; children[i].fn    = 8  + i*16
; children[i].state = 16 + i*16

bb_alt:
        push    rbx
        push    r12
        push    r13
        sub     rsp, 8                  ; align
        mov     rbx, rdi                ; rbx = ζ (alt_t*)
        cmp     esi, 0
        je      ALT_α
        jmp     ALT_β

ALT_α:  ; ζ->position = Δ;  ζ->current = 1
        mov     eax, dword [rel Δ]
        mov     dword [rbx+268], eax    ; ζ->position = Δ
        mov     dword [rbx+264], 1      ; ζ->current = 1
        ; cr = ζ->children[0].fn(ζ->children[0].state, α)
        mov     rdi, qword [rbx+16]     ; children[0].state
        xor     esi, esi
        call    qword [rbx+8]           ; children[0].fn
        mov     r12, rax                ; cr.σ
        movsxd  r13, edx                ; cr.δ
        test    r12, r12
        jz      child_α_ω
        jmp     child_α_γ

ALT_β:  ; cr = ζ->children[ζ->current-1].fn(..., β)
        mov     eax, dword [rbx+264]    ; ζ->current
        dec     eax                     ; current-1
        ; offset = 8 + (current-1)*16
        imul    eax, 16
        add     eax, 8                  ; base offset of children[current-1]
        movsxd  rcx, eax
        mov     rdi, qword [rbx+rcx+8]  ; .state
        mov     esi, 1
        call    qword [rbx+rcx]         ; .fn
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      ALT_ω
        jmp     child_β_γ

child_α_γ:
        ; ζ->result = cr
        mov     qword [rbx+272], r12    ; result.σ
        mov     dword [rbx+280], r13d   ; result.δ
        jmp     ALT_γ

child_α_ω:
        ; ζ->current++
        add     dword [rbx+264], 1
        ; if (ζ->current > ζ->n) goto ω
        mov     eax, dword [rbx+264]
        cmp     eax, dword [rbx+0]      ; ζ->n
        jg      ALT_ω
        ; Δ = ζ->position
        mov     eax, dword [rbx+268]
        mov     dword [rel Δ], eax
        ; cr = ζ->children[ζ->current-1].fn(..., α)
        mov     eax, dword [rbx+264]
        dec     eax
        imul    eax, 16
        add     eax, 8
        movsxd  rcx, eax
        mov     rdi, qword [rbx+rcx+8]
        xor     esi, esi
        call    qword [rbx+rcx]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      child_α_ω
        jmp     child_α_γ

child_β_γ:
        mov     qword [rbx+272], r12
        mov     dword [rbx+280], r13d
        jmp     ALT_γ

ALT_γ:  mov     rax, qword [rbx+272]    ; return ζ->result
        movsxd  rdx, dword [rbx+280]
        add     rsp, 8
        pop     r13
        pop     r12
        pop     rbx
        ret
ALT_ω:  xor     eax, eax
        xor     edx, edx
        add     rsp, 8
        pop     r13
        pop     r12
        pop     rbx
        ret
