; bb_capture.s  _XNME/_XFNME  $ writes on every γ; . buffers for Phase-5 commit
; spec_t  bb_capture(void *zeta, int entry)
;   rdi = zeta (capture_t*)  esi = entry
; capture_t: { bb_box_fn fn @0; void *state @8; const char *varname @16;
;              int immediate @24; spec_t pending @32{σ@32,δ@40}; int has_pending @48 }
; sizeof(capture_t) = 56

section .note.GNU-stack noalloc noexec nowrite progbits

extern NV_SET_fn
extern GC_MALLOC
extern memcpy

section .text
global bb_capture

bb_capture:
        push    rbx
        push    r12
        push    r13
        sub     rsp, 32                 ; scratch: DESCR_t @[rsp+0..15], align
        mov     rbx, rdi                ; rbx = ζ (capture_t*)
        cmp     esi, 0
        je      CAP_α
        jmp     CAP_β

CAP_α:  ; cr = ζ->fn(ζ->state, α)
        mov     rdi, qword [rbx+8]
        xor     esi, esi
        call    qword [rbx+0]
        mov     r12, rax                ; cr.σ
        movsxd  r13, edx                ; cr.δ
        test    r12, r12
        jz      CAP_ω
        jmp     CAP_γ_core

CAP_β:  ; cr = ζ->fn(ζ->state, β)
        mov     rdi, qword [rbx+8]
        mov     esi, 1
        call    qword [rbx+0]
        mov     r12, rax
        movsxd  r13, edx
        test    r12, r12
        jz      CAP_ω
        jmp     CAP_γ_core

CAP_γ_core:
        ; if (ζ->varname && *ζ->varname && ζ->immediate)
        mov     rax, qword [rbx+16]     ; ζ->varname
        test    rax, rax
        jz      .cap_no_imm
        cmp     byte [rax], 0
        je      .cap_no_imm
        cmp     dword [rbx+24], 0       ; ζ->immediate
        je      .cap_cond
        ; immediate: s = GC_MALLOC(cr.δ+1); memcpy(s,cr.σ,cr.δ); s[cr.δ]=0
        movsxd  rdi, r13d
        inc     rdi                     ; cr.δ+1
        call    GC_MALLOC
        mov     qword [rsp+0], rax      ; save s
        mov     rdi, rax
        mov     rsi, r12                ; cr.σ
        movsxd  rdx, r13d
        call    memcpy
        mov     rax, qword [rsp+0]
        movsxd  rcx, r13d
        mov     byte [rax+rcx], 0       ; s[cr.δ] = 0
        ; DESCR_t v = { .v=DT_S=1, .slen=cr.δ, .s=s }
        mov     dword [rsp+16], 1       ; v.v = DT_S
        mov     dword [rsp+20], r13d    ; v.slen = cr.δ
        mov     qword [rsp+24], rax     ; v.s = s
        ; NV_SET_fn(ζ->varname, v)
        mov     rdi, qword [rbx+16]
        mov     rsi, qword [rsp+16]     ; v low qword
        mov     rdx, qword [rsp+24]     ; v high qword
        call    NV_SET_fn
        jmp     .cap_done
.cap_cond:
        ; conditional: ζ->pending = cr; ζ->has_pending = 1
        mov     qword [rbx+32], r12     ; pending.σ = cr.σ
        mov     dword [rbx+40], r13d    ; pending.δ = cr.δ
        mov     dword [rbx+48], 1       ; has_pending = 1
        jmp     .cap_done
.cap_no_imm:
.cap_done:
        ; return cr (r12=σ, r13=δ)
        mov     rax, r12
        movsxd  rdx, r13d
        add     rsp, 32
        pop     r13
        pop     r12
        pop     rbx
        ret

CAP_ω:  ; ζ->has_pending = 0
        mov     dword [rbx+48], 0
        xor     eax, eax
        xor     edx, edx
        add     rsp, 32
        pop     r13
        pop     r12
        pop     rbx
        ret
