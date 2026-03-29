/* =========================================================================
 * emit_x64_prolog.c — Prolog Byrd-Box x64 ASM emitter
 *
 * Included from emit_x64.c via #include at end of file.
 * All declarations (pl_safe, pl_atom_*, emit_pl_*, emit_prolog_*,
 * asm_emit_prolog) live here.
 *
 * See GRAND_MASTER_REORG.md M-G2-MOVE-PROLOG-ASM-a/b.
 * ======================================================================= */

/* =========================================================================
 * Prolog Byrd-Box ASM emitter  (M-PROLOG-HELLO)
 *
 * Calling convention for emitted predicates — matches C emitter resumable:
 *   int pl_NAME_ARITY_r(Term *arg0, ..., Trail *trail, int start)
 * In System V x64:
 *   rdi..rsi..rdx..rcx..r8..r9 = first 6 args
 *   For arity 0: rdi=Trail*, rsi=start
 *   For arity 1: rdi=arg0, rsi=Trail*, rdx=start
 *   For arity 2: rdi=arg0, rsi=arg1, rdx=Trail*, rcx=start
 *   etc.
 * Returns: clause index (>=0) on success, -1 on failure.
 *
 * Each predicate function uses a switch on 'start' to resume at the
 * right clause — same as the C emitter's switch(_start){case N:}.
 *
 * Head unification: load call arg from frame, load head pattern term
 * (static atom or freshly allocated var), call unify(arg, head, trail).
 *
 * Body goals: for user calls, call the _r function and check return.
 * For builtins: call pl_builtin_* helpers from prolog_builtin.c.
 *
 * Static data: atom Terms are emitted as .data initialized structs.
 * Per-clause var Terms are freshly allocated each clause entry via
 * term_new_var() calls — cheap malloc, reset on backtrack via trail.
 *
 * Trail: a single global Trail pl_trail, init'd once in pl_rt_init().
 * ======================================================================= */

#include "../../frontend/prolog/prolog_atom.h"
#include "../../frontend/prolog/prolog_runtime.h"
#include "../../frontend/prolog/term.h"

/* -------------------------------------------------------------------------
 * Name sanitisation: foo/2 -> foo_sl_2,  . -> _dt_,  ! -> _ct_
 * ---------------------------------------------------------------------- */
static char _pl_safe_buf[256];
static const char *pl_safe(const char *s) {
    if (!s) return "unknown";
    char *d = _pl_safe_buf;
    int   n = 0;
    for (const char *p = s; *p && n < 250; p++) {
        char c = *p;
        if      (c == '/')  { *d++='_'; *d++='s'; *d++='l'; *d++='_'; n+=4; }
        else if (c == '.')  { *d++='_'; *d++='d'; *d++='t'; *d++='_'; n+=4; }
        else if (c == '-')  { *d++='_'; n++; }
        else if (c == '!')  { *d++='_'; *d++='c'; *d++='t'; *d++='_'; n+=4; }
        else if (c == ',')  { *d++='_'; *d++='c'; *d++='m'; *d++='_'; n+=4; }
        else if (c == ';')  { *d++='_'; *d++='s'; *d++='c'; *d++='_'; n+=4; }
        else if (c == '\\') { *d++='_'; *d++='b'; *d++='s'; *d++='_'; n+=4; }
        else if (c == '+')  { *d++='_'; *d++='p'; *d++='l'; *d++='_'; n+=4; }
        else if (c == '=')  { *d++='_'; *d++='e'; *d++='q'; *d++='_'; n+=4; }
        else if (c == '<')  { *d++='_'; *d++='l'; *d++='t'; *d++='_'; n+=4; }
        else if (c == '>')  { *d++='_'; *d++='g'; *d++='t'; *d++='_'; n+=4; }
        else { *d++ = (char)c; n++; }
    }
    *d = '\0';
    return _pl_safe_buf;
}

/* -------------------------------------------------------------------------
 * Atom table collected during emit pass — each unique atom string gets
 * one .data Term struct: pl_atom_NAME (TT_ATOM, atom_id resolved at init).
 *
 * We emit .data stubs (tag=TT_ATOM, id=0) and fix ids up in pl_rt_init()
 * which calls prolog_atom_intern at runtime for each one.
 * ---------------------------------------------------------------------- */
#define PL_MAX_ATOMS 512
/* Round up to 16-byte boundary for SysV ABI stack alignment */
#define ALIGN16(n) (((n)+15)&~15)
static char  *pl_atom_strings[PL_MAX_ATOMS];
static char   pl_atom_labels[PL_MAX_ATOMS][128];
static int    pl_atom_count_emit = 0;
/* Set before emit_prolog_clause_block so emit_pl_term_load can compute correct var offsets */
static int    pl_cur_max_ucalls = 0;
static int    pl_compound_uid_ctr = 0;
static int    pl_compound_uid(void) { return pl_compound_uid_ctr++; }

static const char *pl_intern_atom_label(const char *name) {
    /* return existing label if already interned */
    for (int i = 0; i < pl_atom_count_emit; i++)
        if (strcmp(pl_atom_strings[i], name) == 0)
            return pl_atom_labels[i];
    if (pl_atom_count_emit >= PL_MAX_ATOMS) return "pl_atom_overflow";
    int idx = pl_atom_count_emit++;
    pl_atom_strings[idx] = strdup(name);
    /* build safe suffix from atom name, then prefix pl_atom_ */
    char tmp[100]; int j = 0;
    for (const char *p = name; *p && j < 90; p++) {
        unsigned char c = (unsigned char)*p;
        tmp[j++] = (isalnum(c)||c=='_') ? (char)c : '_';
    }
    tmp[j] = '\0';
    if (j == 0) { tmp[0]='a'; tmp[1]='\0'; }
    snprintf(pl_atom_labels[idx], sizeof pl_atom_labels[idx],
             "pl_atom_%s_%d", tmp, idx);
    return pl_atom_labels[idx];
}

/* Emit .data section for all collected atoms */
static void emit_pl_atom_data(void) {
    if (pl_atom_count_emit == 0) return;
    A("\n; --- Prolog atom Term structs (TT_ATOM=0, id fixed by pl_rt_init) ---\n");
    A("section .data\n");
    /* Term layout: { TermTag tag(4), int saved_slot(4), union(8) } = 16 bytes */
    for (int i = 0; i < pl_atom_count_emit; i++) {
        A("%-32s dd 0, 0       ; tag=TT_ATOM, saved_slot=0\n", pl_atom_labels[i]);
        A("                         dq 0           ; atom_id (filled by pl_rt_init)\n");
        /* make it a proper label: */
    }
}

/* Better: emit as proper labeled records */
static void emit_pl_atom_data_v2(void) {
    if (pl_atom_count_emit == 0) return;
    A("\nsection .data\n");
    A("; TT_ATOM=0 — term_tag(4B) + saved_slot(4B) + atom_id(8B) = 16 bytes\n");
    for (int i = 0; i < pl_atom_count_emit; i++) {
        A("%s:\n", pl_atom_labels[i]);
        A("    dd      0               ; tag = TT_ATOM\n");
        A("    dd      0               ; saved_slot\n");
        A("    dd      0               ; atom_id — filled by pl_rt_init\n");
        A("    dd      0               ; atom_id high dword (padding)\n");
        A("    dq      0               ; union padding (compound.args* slot)\n");
    }
}

/* -------------------------------------------------------------------------
 * emit_pl_header — globals, externs, .bss trail
 * ---------------------------------------------------------------------- */
static void emit_pl_header(Program *prog) {
    A("; generated by scrip-cc -pl -asm\n");
    A("; compile: nasm -f elf64 prog.s -o prog.o\n");
    A("; link:    gcc -no-pie prog.o \\\n");
    A(";            src/frontend/prolog/prolog_atom.o \\\n");
    A(";            src/frontend/prolog/prolog_unify.o \\\n");
    A(";            src/frontend/prolog/prolog_builtin.o \\\n");
    A(";            -o prog\n");
    A("\n");
    A("    global  main\n");
    A("    extern  prolog_atom_init, prolog_atom_intern\n");
    A("    extern  trail_init, trail_mark_fn, trail_unwind, trail_push\n");
    A("    extern  unify\n");
    A("    extern  term_new_atom, term_new_var, term_new_int, term_new_float\n");
    A("    extern  term_new_compound\n");
    A("    extern  pl_write, pl_writeln\n");
    A("    extern  pl_is, pl_num_lt, pl_num_gt, pl_num_le, pl_num_ge, pl_num_eq, pl_num_ne\n");
    A("    extern  pl_functor, pl_arg, pl_univ\n");
    A("    extern  pl_atom, pl_integer, pl_is_float, pl_var, pl_nonvar, pl_compound\n");
    A("    extern  printf, fflush, putchar, exit\n");
    A("\n");
    A("\n");

    /* .bss — global Trail + SNOBOL4 runtime stubs required by stmt_rt.c */
    A("section .note.GNU-stack noalloc noexec nowrite progbits\n\n");
    A("section .bss\n");
    A("%-32s resb 32    ; Trail struct (top+cap+stack ptr)\n", "pl_trail");
    A("%-32s resq 1     ; scratch qword\n", "pl_tmp");
    A("%-32s resq 8     ; head arg scratch (8 slots, max arity)\n", "pl_head_args");
    /* stmt_rt.c externs these — provide stubs so Prolog binaries link cleanly */
    A("    global  cursor, subject_data, subject_len_val\n");
    A("%-32s resq 1     ; SNOBOL4 pattern cursor (unused in Prolog)\n", "cursor");
    A("%-32s resq 1     ; SNOBOL4 subject length (unused in Prolog)\n", "subject_len_val");
    A("%-32s resb 65536 ; SNOBOL4 subject buffer  (unused in Prolog)\n", "subject_data");
    A("\n");
}

/* -------------------------------------------------------------------------
 * emit_pl_term_load — emit instructions that leave a Term* in rax.
 * For atoms: lea rax, [rel ATOM_LABEL]
 * For vars:  call term_new_var with slot index
 * For ints:  call term_new_int with value
 * For compound: build args array then call term_new_compound (stub: use atom)
 * ---------------------------------------------------------------------- */
static void emit_pl_term_load(EXPR_t *e, int frame_base_words) {
    if (!e) { A("    xor     rax, rax\n"); return; }
    switch (e->kind) {
        case E_QLIT: {
            const char *lbl = pl_intern_atom_label(e->sval ? e->sval : "");
            A("    lea     rax, [rel %s]\n", lbl);
            break;
        }
        case E_ILIT:
            A("    mov     rdi, %ld\n", e->ival);
            A("    call    term_new_int\n");
            break;
        case E_VART: {
            /* var slot is in e->ival; fresh Term* in frame at [rbp - (5+max_ucalls+slot)*8] */
            int slot = (int)e->ival;
            if (slot < 0) {
                /* anonymous wildcard _ — allocate a fresh unbound var each time */
                A("    mov     rdi, -1\n");
                A("    call    term_new_var    ; anon wildcard\n");
            } else {
                A("    mov     rax, [rbp - %d]  ; var slot %d (%s)\n",
                  (5 + pl_cur_max_ucalls + pl_cur_max_ucalls + slot)*8, slot, e->sval ? e->sval : "_");
            }
            break;
        }
        case E_FNC:
            if (e->nchildren == 0) {
                /* nullary functor = atom */
                const char *lbl = pl_intern_atom_label(e->sval ? e->sval : "");
                A("    lea     rax, [rel %s]\n", lbl);
            } else {
                /* compound: build Term*[] on stack, call term_new_compound */
                int cuid = pl_compound_uid();
                int arity = e->nchildren;
                const char *lbl = pl_intern_atom_label(e->sval ? e->sval : "f");
                /* allocate args array on stack */
                A("    sub     rsp, %d            ; args[%d] for compound %s/%d\n",
                  arity * 8, arity, e->sval ? e->sval : "?", arity);
                for (int ci = 0; ci < arity; ci++) {
                    emit_pl_term_load(e->children[ci], frame_base_words);
                    A("    mov     [rsp + %d], rax  ; compound arg %d\n", ci * 8, ci);
                }
                /* term_new_compound(int functor_atom_id, int arity, Term **args) */
                /* We have the atom label; atom_id is stored at label+8 (dword) */
                A("    mov     edi, dword [rel %s + 8]  ; functor atom_id\n", lbl);
                A("    mov     esi, %d                   ; arity\n", arity);
                A("    mov     rdx, rsp                   ; args ptr\n");
                A("    call    term_new_compound\n");
                A("    add     rsp, %d\n", arity * 8);
            }
            break;
        case E_FLIT: {
            /* Float literal — load IEEE 754 bits into xmm0, call term_new_float */
            union { double d; uint64_t u; } fconv;
            fconv.d = e->dval;
            A("    mov     rax, 0x%"PRIx64"   ; float %.17g\n", fconv.u, e->dval);
            A("    movq    xmm0, rax\n");
            A("    call    term_new_float\n");
            break;
        }
        case E_ADD: case E_SUB: case E_MPY: case E_DIV: {
            /* Build a compound Term for pl_eval_arith: +(L,R) etc. */
            const char *opname = (e->kind==E_ADD) ? "+" :
                                 (e->kind==E_SUB) ? "-" :
                                 (e->kind==E_MPY) ? "*" : "/";
            const char *oplbl = pl_intern_atom_label(opname);
            int cuid2 = pl_compound_uid();
            A("    sub     rsp, 16          ; arith compound args[2] uid%d\n", cuid2);
            emit_pl_term_load(e->children[0], frame_base_words);
            A("    mov     [rsp + 0], rax\n");
            emit_pl_term_load(e->children[1], frame_base_words);
            A("    mov     [rsp + 8], rax\n");
            A("    mov     edi, dword [rel %s + 8]  ; functor atom_id '%s'\n", oplbl, opname);
            A("    mov     esi, 2\n");
            A("    mov     rdx, rsp\n");
            A("    call    term_new_compound\n");
            A("    add     rsp, 16\n");
            break;
        }
        default:
            A("    xor     rax, rax            ; unknown term kind %d\n", (int)e->kind);
            break;
    }
}

/* -------------------------------------------------------------------------
 * emit_prolog_clause — one clause: α through body through γ, then β
 *
 * Frame layout (rbp-based):
 *   [rbp -  8]  = saved trail mark (int, 8B slot)
 *   [rbp - 16]  = saved start value (for switch dispatch)
 *   [rbp - (k+5)*8] for k in 0..n_vars-1 = fresh Term* for var slot k
 *
 * The function is NOT generated inline here — the whole predicate is one
 * C-ABI function (emitted by emit_prolog_choice) using a switch table.
 * This function emits the per-clause block within that switch.
 * ---------------------------------------------------------------------- */
/* Continuation encoding for Prolog _r functions.
 *
 * Each clause has a "base" start value. Dispatch finds the right clause.
 * - Fact / builtin-only clause: occupies exactly 1 slot (base, base+1 → next clause).
 *   γ returns base+1 (= next clause's base).
 * - Body-user-call clause: open-ended. base = start of this clause's range.
 *   inner = start - base (0=fresh, k+1 = resume sub-call at sub_cs=k).
 *   γ returns base + sub_cs + 1.
 *
 * emit_prolog_choice computes base[] at emit time and passes it into
 * emit_prolog_clause_block via the base parameter. */

static void emit_prolog_clause_block(EXPR_t *clause, int idx, int total,
                                     const char *pred_safe, int arity,
                                     const char *ω_lbl, int base,
                                     int max_ucalls) {
    if (!clause) return;
    int n_vars = (int)clause->ival;
    int nbody  = clause->nchildren - arity;
    if (nbody < 0) nbody = 0;
    /* Var slot k is at [rbp - (5 + max_ucalls + max_ucalls + k)*8].
     * Ucall sub_cs slot bi is at [rbp - (5 + bi)*8].
     * Ucall trail-mark slot bi is at [rbp - (5 + max_ucalls + bi)*8].
     * This keeps all slots non-overlapping. */
#define VAR_SLOT_OFFSET(k)        ((5 + max_ucalls + max_ucalls + (k)) * 8)
#define UCALL_SLOT_OFFSET(bi)     ((5 + (bi)) * 8)
#define UCALL_MARK_OFFSET(bi)     ((5 + max_ucalls + (bi)) * 8)
#define PL_RESUME_BIG 4096  /* stride between ucall levels in return encoding */

    char this_α[128], next_clause[128];
    snprintf(this_α,  sizeof this_α,  "pl_%s_c%d_α", pred_safe, idx);
    if (idx + 1 < total)
        snprintf(next_clause, sizeof next_clause, "pl_%s_c%d_α", pred_safe, idx+1);
    else
        snprintf(next_clause, sizeof next_clause, "%s",            ω_lbl);

    /* Count body user-calls (non-builtin E_FNC goals) to decide γ encoding */
    int body_user_call_count = 0;
    for (int bi = 0; bi < nbody; bi++) {
        EXPR_t *g = clause->children[arity + bi];
        if (!g || g->kind != E_FNC || !g->sval) continue;
        const char *gn = g->sval;
        if (strcmp(gn,"write")==0||strcmp(gn,"nl")==0||strcmp(gn,"writeln")==0||
            strcmp(gn,"true")==0||strcmp(gn,"fail")==0||strcmp(gn,"halt")==0||
            strcmp(gn,"is")==0||strcmp(gn,"=")==0||strcmp(gn,"!")==0||
            strcmp(gn,"<")==0||strcmp(gn,">")==0||strcmp(gn,"=<")==0||
            strcmp(gn,">=")==0||strcmp(gn,"=:=")==0||strcmp(gn,"=\\=")==0||
            strcmp(gn,",")==0||strcmp(gn,";")==0||strcmp(gn,"functor")==0||strcmp(gn,"arg")==0||strcmp(gn,"=..")==0||
            strcmp(gn,"atom")==0||strcmp(gn,"integer")==0||strcmp(gn,"float")==0||
            strcmp(gn,"var")==0||strcmp(gn,"nonvar")==0||strcmp(gn,"compound")==0||strcmp(gn,"\\+")==0||strcmp(gn,"\\=")==0) continue;
        if (g->kind == E_CUT) continue;
        body_user_call_count++;
    }

    A("\n; --- clause %d/%d  n_vars=%d body_ucalls=%d ---\n",
      idx+1, total, n_vars, body_user_call_count);
    A("; clause %d entry\n", idx);
    A("%s:\n", this_α);

    /* Allocate fresh Term* for each variable in this clause */
    for (int k = 0; k < n_vars; k++) {
        A("    mov     rdi, %d\n", k);
        A("    call    term_new_var\n");
        A("    mov     [rbp - %d], rax    ; var slot %d\n", (5 + max_ucalls + max_ucalls + k)*8, k);
    }

    /* Trail mark — save in [rbp-8] for head-unification failure handlers.
     * UCALL_MARK_OFFSET(0) will be taken after head unification succeeds,
     * so β0 unwinds only body bindings, not head bindings.
     * Zero all sub_cs slots so restore-before-call emits 0 on first entry. */
    A("    lea     rdi, [rel pl_trail]\n");
    A("    call    trail_mark_fn\n");
    A("    mov     [rbp - 8], eax          ; clause trail mark\n");
    if (max_ucalls > 0) {
        for (int s = 0; s < max_ucalls; s++)
            A("    mov     dword [rbp - %d], 0  ; init sub_cs slot %d\n",
              UCALL_SLOT_OFFSET(s), s);
    }

    /* Head unification — for each head arg child[i], unify with call arg.
     * Fall through all args sequentially. Only jump on failure. */
    for (int i = 0; i < arity && i < clause->nchildren; i++) {
        EXPR_t *head_arg = clause->children[i];
        char hω_lbl[128];
        snprintf(hω_lbl, sizeof hω_lbl, "pl_%s_c%d_hω%d", pred_safe, idx, i);

        /* Load call arg — save to pl_head_args[i] before building head pattern.
         * emit_pl_term_load calls functions (term_new_var, term_new_compound)
         * that clobber all caller-saved registers; static slot survives. */
        A("    mov     rax, [rbp - 24]     ; args array ptr\n");
        A("    mov     rax, [rax + %d]     ; args[%d]\n", i*8, i);
        A("    mov     [rel pl_head_args + %d], rax  ; save call arg[%d]\n", i*8, i);
        /* Build head pattern term into rax */
        emit_pl_term_load(head_arg, n_vars);
        A("    mov     rsi, rax            ; head pattern\n");
        A("    mov     rdi, [rel pl_head_args + %d]  ; restore call arg[%d]\n", i*8, i);
        /* unify(call_arg=rdi, head_pattern=rsi, trail=rdx) */
        A("    lea     rdx, [rel pl_trail]\n");
        A("    call    unify\n");
        A("    test    eax, eax\n");
        A("    jnz     pl_%s_c%d_hγ%d\n", pred_safe, idx, i);
        A("%s:\n", hω_lbl);
        /* unify failed — unwind trail and try next clause */
        A("    lea     rdi, [rel pl_trail]\n");
        A("    mov     esi, [rbp - 8]\n");
        A("    call    trail_unwind\n");
        A("    jmp     %s\n", next_clause);
        A("pl_%s_c%d_hγ%d:\n", pred_safe, idx, i);
        /* fall through to next arg check */
    }

    A("pl_%s_c%d_body:\n", pred_safe, idx);
    /* Take UCALL_MARK_OFFSET(0) here — after all head unification — so that
     * β0 unwinds only body bindings.  For β>0 these marks are taken on the
     * success paths of prior ucalls (same rationale: only undo later work). */
    if (max_ucalls > 0) {
        A("    lea     rdi, [rel pl_trail]\n");
        A("    call    trail_mark_fn\n");
        A("    mov     [rbp - %d], eax     ; UCALL_MARK_OFFSET(0) = β0 unwind target\n",
          UCALL_MARK_OFFSET(0));
    }
    /* On fresh entry (start==base): edx=0, slots pre-zeroed — fall through to α0.
     * On re-entry (start > base): decode inner = start - base into per-ucall sub_cs slots.
     * inner encodes: ucall_0_sub_cs + ucall_1_sub_cs*STRIDE + ucall_2_sub_cs*STRIDE^2 ...
     * Pre-load each slot; α0 restores from slot 0, γ0 resets slot 1 to 0 (correct —
     * when ucall 0 gives a *new* answer we start ucall 1 fresh), but α1 restores
     * from slot 1 for the case where β1 fired and drove us back to α0 which then
     * succeeded with the same answer, arriving at γ0 which re-zeros slot 1 — that
     * zero is what we want because ucall 1 is starting fresh for the new ucall 0 answer.
     *
     * The re-entry path jumps past var-alloc/head-unif so we need a resume label. */
    if (body_user_call_count > 0) {
        A("    ; re-entry decode: inner = start - %d\n", base);
        A("    mov     eax, [rbp - 32]        ; start\n");
        A("    sub     eax, %d                ; inner = start - base\n", base);
        A("    jle     pl_%s_c%d_body_fresh\n", pred_safe, idx);  /* inner<=0: fresh (incl head-fail fall-through) */
        /* inner > 0: decode stride-packed sub_cs into slots */
        A("    dec     eax                    ; inner - 1 = packed sub_cs\n");
        A("    mov     ecx, eax               ; work register\n");
        for (int _di = 0; _di < max_ucalls; _di++) {
            if (_di == 0) {
                A("    mov     edx, ecx\n");
                A("    and     edx, %d        ; ucall 0 sub_cs = packed %% STRIDE\n",
                  PL_RESUME_BIG - 1);
            } else {
                A("    shr     ecx, 12        ; >> log2(STRIDE) per level\n");
                A("    mov     edx, ecx\n");
                A("    and     edx, %d        ; ucall %d sub_cs\n",
                  PL_RESUME_BIG - 1, _di);
            }
            A("    mov     [rbp - %d], edx    ; pre-load slot %d\n",
              UCALL_SLOT_OFFSET(_di), _di);
        }
        A("    ; fall into α0 — it restores edx from slot 0\n");
        A("    jmp     pl_%s_c%d_α0\n", pred_safe, idx);
        A("pl_%s_c%d_body_fresh:\n", pred_safe, idx);
    }
    A("    xor     edx, edx               ; sub_cs=0 for first ucall\n");

    /* Body goals */
    int ucall_seq = 0;   /* sequential index of user-calls emitted so far */
    char last_β_lbl[128];  /* most recent user-call fail label, for fail/0 retry */
    snprintf(last_β_lbl, sizeof last_β_lbl, "%s", next_clause);  /* default: no ucalls yet */
    for (int bi = 0; bi < nbody; bi++) {
        EXPR_t *goal = clause->children[arity + bi];
        if (!goal) continue;

        /* Cut */
        if (goal->kind == E_CUT) {
            A("    ; cut — seal β: _cut=1, redirect failures to ω\n");
            A("    mov     byte [rbp - 17], 1    ; _cut = 1\n");
            /* After cut, any subsequent fail/retry goes to ω, not next clause */
            snprintf(next_clause, sizeof next_clause, "%s", ω_lbl);
            continue;
        }

        /* E_UNIFY — =/2 lowered by pl_lower (goal->kind == E_UNIFY) */
        if (goal->kind == E_UNIFY && goal->nchildren == 2) {
            char ufail[128];
            snprintf(ufail, sizeof ufail, "pl_%s_c%d_ufail%d", pred_safe, idx, bi);
            emit_pl_term_load(goal->children[0], n_vars);
            A("    mov     [rel pl_tmp], rax\n");
            emit_pl_term_load(goal->children[1], n_vars);
            A("    mov     rsi, rax\n");
            A("    mov     rdi, [rel pl_tmp]\n");
            A("    lea     rdx, [rel pl_trail]\n");
            A("    call    unify\n");
            A("    test    eax, eax\n");
            A("    jnz     pl_%s_c%d_ug%d\n", pred_safe, idx, bi);
            A("%s:\n", ufail);
            A("    lea     rdi, [rel pl_trail]\n");
            A("    mov     esi, [rbp - 8]\n");
            A("    call    trail_unwind\n");
            A("    jmp     %s\n", next_clause);
            A("pl_%s_c%d_ug%d:\n", pred_safe, idx, bi);
            continue;
        }

        /* E_FNC — builtin or user call */
        if (goal->kind == E_FNC && goal->sval) {
            const char *fn = goal->sval;
            int garity = goal->nchildren;

            /* --- write/1 --- */
            if (strcmp(fn, "write") == 0 && garity == 1) {
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     rdi, rax\n");
                A("    call    pl_write\n");
                continue;
            }
            /* --- nl/0 --- */
            if (strcmp(fn, "nl") == 0 && garity == 0) {
                A("    mov     edi, 10         ; '\\n'\n");
                A("    call    putchar\n");
                continue;
            }
            /* --- halt/0 and halt/1 --- */
            if (strcmp(fn, "halt") == 0) {
                if (garity == 1) {
                    emit_pl_term_load(goal->children[0], n_vars);
                    /* extract ival from Term* if TT_INT, else 0 */
                    A("    mov     edi, 0\n");
                } else {
                    A("    xor     edi, edi\n");
                }
                A("    call    exit\n");
                continue;
            }
            /* --- true/0 --- */
            if (strcmp(fn, "true") == 0 && garity == 0) {
                continue;  /* no-op */
            }
            /* --- fail/0 — retry innermost ucall (Proebsting E2.fail→E1.resume) --- */
            if (strcmp(fn, "fail") == 0 && garity == 0) {
                if (ucall_seq > 0) {
                    /* Unwind to the innermost ucall's own mark, then retry it.
                     * Uses UCALL_MARK_OFFSET(ucall_seq-1) — same slot βN-1 would use. */
                    A("    lea     rdi, [rel pl_trail]\n");
                    A("    mov     esi, [rbp - %d]    ; own mark ucall %d\n",
                      UCALL_MARK_OFFSET(ucall_seq - 1), ucall_seq - 1);
                    A("    call    trail_unwind\n");
                    A("    mov     edx, [rbp - %d]    ; restore sub_cs for retry\n",
                      UCALL_SLOT_OFFSET(ucall_seq - 1));
                    A("    jmp     pl_%s_c%d_α%d\n", pred_safe, idx, ucall_seq - 1);
                } else {
                    /* No ucalls yet — unwind clause mark and fail */
                    A("    lea     rdi, [rel pl_trail]\n");
                    A("    mov     esi, [rbp - 8]\n");
                    A("    call    trail_unwind\n");
                    A("    jmp     %s\n", next_clause);
                }
                continue;
            }
            /* --- \+/1  negation-as-failure ---
             * Semantics: call inner goal with fresh start=0.  If it succeeds
             * (eax >= 0) \+ fails; if it fails (eax == -1) \+ succeeds.
             * Trail is unwound regardless so inner bindings never escape.
             * The trail mark is pushed on the stack before arg-building so it
             * survives the inner _r call (which may clobber all caller-saved
             * registers).  We use push/pop rather than a named frame slot to
             * avoid touching the carefully-sized frame layout. */
            if (strcmp(fn, "\\+") == 0 && garity == 1) {
                EXPR_t *inner = goal->children[0];
                const char *ifn   = (inner && inner->sval) ? inner->sval : NULL;
                int         ia    = inner ? (int)inner->nchildren : 0;
                char naf_ok[128], naf_fail[128];
                snprintf(naf_ok,   sizeof naf_ok,   "pl_%s_c%d_naf_ok%d",   pred_safe, idx, bi);
                snprintf(naf_fail, sizeof naf_fail,  "pl_%s_c%d_naf_fail%d", pred_safe, idx, bi);

                /* 1. Take a trail mark and push it (survives inner call). */
                A("    lea     rdi, [rel pl_trail]\n");
                A("    call    trail_mark_fn\n");
                A("    push    rax                    ; NAF trail mark\n");

                /* 2. Call inner goal deterministically (start=0 → fresh). */
                if (!ifn) {
                    /* degenerate: no functor → treat as fail */
                    A("    mov     eax, -1\n");
                } else if (strcmp(ifn, "fail") == 0 && ia == 0) {
                    A("    mov     eax, -1            ; \\+ fail → will succeed\n");
                } else if (strcmp(ifn, "true") == 0 && ia == 0) {
                    A("    mov     eax, 0             ; \\+ true → will fail\n");
                } else {
                    /* General inner goal: build args on stack, call _r with start=0. */
                    char icall_fa[300];
                    snprintf(icall_fa, sizeof icall_fa, "%s/%d", ifn, ia);
                    char icall_safe[256];
                    snprintf(icall_safe, sizeof icall_safe, "%s", pl_safe(icall_fa));
                    if (ia > 0) {
                        int ialloc = ALIGN16(ia * 8);
                        A("    sub     rsp, %d              ; args for \\+ inner %s/%d\n", ialloc, ifn, ia);
                        A("    mov     rbx, rsp               ; stable base\n");
                        for (int ai2 = 0; ai2 < ia; ai2++) {
                            emit_pl_term_load(inner->children[ai2], n_vars);
                            A("    mov     [rbx + %d], rax    ; inner arg[%d]\n", ai2*8, ai2);
                        }
                        A("    mov     rdi, rbx               ; args ptr\n");
                        A("    lea     rsi, [rel pl_trail]\n");
                        A("    xor     edx, edx               ; start=0 (fresh)\n");
                        A("    call    pl_%s_r\n", icall_safe);
                        A("    add     rsp, %d\n", ialloc);
                    } else {
                        A("    xor     rdi, rdi               ; no args\n");
                        A("    lea     rsi, [rel pl_trail]\n");
                        A("    xor     edx, edx               ; start=0\n");
                        A("    call    pl_%s_r\n", icall_safe);
                    }
                }

                /* 3. Unwind trail regardless of result (pop mark into rsi). */
                A("    push    rax                    ; save inner result\n");
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     rsi, [rsp + 8]         ; NAF mark (below saved rax)\n");
                A("    call    trail_unwind\n");
                A("    pop     rax                    ; restore inner result\n");
                A("    add     rsp, 8                 ; discard NAF mark\n");

                /* 4. Invert: inner succeeded (eax >= 0) → \+ fails. */
                A("    cmp     eax, -1\n");
                A("    je      %s\n", naf_ok);   /* inner failed → \+ succeeds */
                /* inner succeeded → \+ fails → unwind clause mark, try next clause */
                A("%s:\n", naf_fail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("%s:\n", naf_ok);
                /* inner failed → \+ succeeds → fall through to next body goal */
                continue;
            }
            /* --- \=/2  not-unifiable --- */
            if (strcmp(fn, "\\=") == 0 && garity == 2) {
                char neq_ok[128], neq_fail[128];
                snprintf(neq_ok,   sizeof neq_ok,   "pl_%s_c%d_neq_ok%d",   pred_safe, idx, bi);
                snprintf(neq_fail, sizeof neq_fail,  "pl_%s_c%d_neq_fail%d", pred_safe, idx, bi);

                /* 1. Take a trail mark and push it. */
                A("    lea     rdi, [rel pl_trail]\n");
                A("    call    trail_mark_fn\n");
                A("    push    rax                    ; \\= trail mark\n");

                /* 2. Attempt unification. */
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    mov     rsi, rax\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    lea     rdx, [rel pl_trail]\n");
                A("    call    unify\n");
                A("    push    rax                    ; save unify result\n");

                /* 3. Unwind trail regardless (pop result, then mark). */
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     rsi, [rsp + 8]         ; \\= mark (below saved result)\n");
                A("    call    trail_unwind\n");
                A("    pop     rax                    ; restore unify result\n");
                A("    add     rsp, 8                 ; discard \\= mark\n");

                /* 4. Invert: unify succeeded (eax != 0) → \= fails. */
                A("    test    eax, eax\n");
                A("    jz      %s\n", neq_ok);   /* unify failed → \= succeeds */
                A("%s:\n", neq_fail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("%s:\n", neq_ok);
                continue;
            }
            /* --- writeln/1 --- */
            if (strcmp(fn, "writeln") == 0 && garity == 1) {
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     rdi, rax\n");
                A("    call    pl_write\n");
                A("    mov     edi, 10\n");
                A("    call    putchar\n");
                continue;
            }
            /* --- ,/2 — conjunction: flatten and emit each goal inline --- */
            if (strcmp(fn, ",") == 0 && garity == 2) {
                /* Flatten into a temporary goals array and recurse */
                EXPR_t *flat[64]; int nflat = 0;
                EXPR_t *cur = goal;
                while (cur && cur->kind == E_FNC && cur->sval &&
                       strcmp(cur->sval, ",") == 0 && cur->nchildren == 2 &&
                       nflat < 63) {
                    flat[nflat++] = cur->children[0];
                    cur = cur->children[1];
                }
                if (cur) flat[nflat++] = cur;
                for (int fi = 0; fi < nflat; fi++) {
                    EXPR_t *sub = flat[fi];
                    /* Re-enter body goal emitter for each sub-goal */
                    /* Simplest: recurse by creating a temporary 1-element array */
                    /* Instead emit recursively via a nested call with same pred context */
                    /* For now: emit each sub-goal directly */
                    if (sub->kind == E_FNC && sub->sval) {
                        /* temporarily set goal = sub and re-run this loop iteration */
                        /* We can't easily recurse here without refactoring, so fall through
                         * to user-call — but that would break too. Use a label trick: */
                        /* Actually the cleanest fix: emit a sub-goal by reusing the same code.
                         * Since we can't recurse in a loop, push onto a worklist.
                         * For the common case (atom goals), handle directly: */
                        const char *sfn = sub->sval; int sa = sub->nchildren;
                        if (strcmp(sfn,"nl")==0 && sa==0) {
                            A("    mov     edi, 10\n"); A("    call    putchar\n");
                        } else if (strcmp(sfn,"write")==0 && sa==1) {
                            emit_pl_term_load(sub->children[0], n_vars);
                            A("    mov     rdi, rax\n"); A("    call    pl_write\n");
                        } else if (strcmp(sfn,"fail")==0 && sa==0) {
                            A("    lea     rdi, [rel pl_trail]\n");
                            A("    mov     esi, [rbp - 8]\n");
                            A("    call    trail_unwind\n");
                            A("    jmp     %s\n", next_clause);
                        } else if (strcmp(sfn,"true")==0 && sa==0) {
                            /* no-op */
                        } else {
                            /* user call — build args and call _r */
                            char cfa[300]; snprintf(cfa, sizeof cfa, "%s/%d", sfn, sa);
                            char csafe[256]; strncpy(csafe, pl_safe(cfa), 255);
                            char cfail[128];
                            snprintf(cfail, sizeof cfail, "pl_%s_c%d_cfail%d_%d", pred_safe, idx, bi, fi);
                            if (sa > 0) {
                                A("    sub     rsp, %d\n", ALIGN16(sa*8));
                                for (int ai = 0; ai < sa; ai++) {
                                    emit_pl_term_load(sub->children[ai], n_vars);
                                    A("    mov     [rsp + %d], rax\n", ai*8);
                                }
                                A("    mov     rdi, rsp\n");
                            } else { A("    xor     rdi, rdi\n"); }
                            A("    lea     rsi, [rel pl_trail]\n");
                            A("    xor     edx, edx\n");
                            A("    call    pl_%s_r\n", csafe);
                            if (sa > 0) A("    add     rsp, %d\n", ALIGN16(sa*8));
                            A("    test    eax, eax\n");
                            A("    jns     pl_%s_c%d_cok%d_%d\n", pred_safe, idx, bi, fi);
                            A("%s:\n", cfail);
                            A("    lea     rdi, [rel pl_trail]\n");
                            A("    mov     esi, [rbp - 8]\n");
                            A("    call    trail_unwind\n");
                            A("    jmp     %s\n", next_clause);
                            A("pl_%s_c%d_cok%d_%d:\n", pred_safe, idx, bi, fi);
                        }
                    }
                }
                continue;
            }
            /* --- ;/2 — disjunction: flatten left conj, emit with backtrack retry --- */
            if (strcmp(fn, ";") == 0 && garity == 2) {
                EXPR_t *left  = goal->children[0];
                EXPR_t *right = goal->children[1];
                char else_lbl[128], done_lbl[128];
                snprintf(else_lbl, sizeof else_lbl, "disj_%s_%d_%d_else", pred_safe, idx, bi);
                snprintf(done_lbl, sizeof done_lbl, "disj_%s_%d_%d_done", pred_safe, idx, bi);

                /* if-then-else: (Cond -> Then ; Else) */
                if (left && left->kind == E_FNC && left->sval &&
                    strcmp(left->sval, "->") == 0 && left->nchildren == 2) {
                    EXPR_t *cond = left->children[0];
                    EXPR_t *then = left->children[1];
                    /* Emit condition inline: numeric comparison or unify */
                    int cond_handled = 0;
                    if (cond && cond->kind == E_FNC && cond->sval && cond->nchildren == 2) {
                        const char *cop = cond->sval;
                        const char *cfn = NULL;
                        if      (strcmp(cop,"<")   == 0) cfn = "pl_num_lt";
                        else if (strcmp(cop,">")   == 0) cfn = "pl_num_gt";
                        else if (strcmp(cop,"=<")  == 0) cfn = "pl_num_le";
                        else if (strcmp(cop,">=")  == 0) cfn = "pl_num_ge";
                        else if (strcmp(cop,"=:=") == 0) cfn = "pl_num_eq";
                        else if (strcmp(cop,"=\\=") == 0) cfn = "pl_num_ne";
                        if (cfn) {
                            emit_pl_term_load(cond->children[0], n_vars);
                            A("    mov     [rel pl_tmp], rax\n");
                            emit_pl_term_load(cond->children[1], n_vars);
                            A("    mov     rsi, rax\n");
                            A("    mov     rdi, [rel pl_tmp]\n");
                            A("    call    %s\n", cfn);
                            A("    test    eax, eax\n");
                            A("    jz      %s\n", else_lbl);
                            cond_handled = 1;
                        }
                        if (!cond_handled && strcmp(cop,"=") == 0) {
                            emit_pl_term_load(cond->children[0], n_vars);
                            A("    mov     [rel pl_tmp], rax\n");
                            emit_pl_term_load(cond->children[1], n_vars);
                            A("    mov     rsi, rax\n");
                            A("    mov     rdi, [rel pl_tmp]\n");
                            A("    lea     rdx, [rel pl_trail]\n");
                            A("    call    unify\n");
                            A("    test    eax, eax\n");
                            A("    jz      %s\n", else_lbl);
                            cond_handled = 1;
                        }
                    }
                    /* Type-test builtins as condition: atom/1, integer/1, etc. */
                    if (!cond_handled && cond && cond->kind == E_FNC && cond->sval &&
                        cond->nchildren == 1) {
                        const char *cop1 = cond->sval;
                        const char *rtfn = NULL;
                        if      (strcmp(cop1,"atom")==0)     rtfn = "pl_atom";
                        else if (strcmp(cop1,"integer")==0)  rtfn = "pl_integer";
                        else if (strcmp(cop1,"float")==0)    rtfn = "pl_is_float";
                        else if (strcmp(cop1,"var")==0)      rtfn = "pl_var";
                        else if (strcmp(cop1,"nonvar")==0)   rtfn = "pl_nonvar";
                        else if (strcmp(cop1,"compound")==0) rtfn = "pl_compound";
                        if (rtfn) {
                            emit_pl_term_load(cond->children[0], n_vars);
                            A("    mov     rdi, rax\n");
                            A("    call    %s\n", rtfn);
                            A("    test    eax, eax\n");
                            A("    jz      %s\n", else_lbl);
                            cond_handled = 1;
                        }
                    }
                    /* \+/1 as condition: take else_lbl if inner succeeds */
                    if (!cond_handled && cond && cond->kind == E_FNC && cond->sval &&
                        strcmp(cond->sval, "\\+") == 0 && cond->nchildren == 1) {
                        EXPR_t *ci = cond->children[0];
                        const char *cifn = (ci && ci->sval) ? ci->sval : NULL;
                        int cia = ci ? (int)ci->nchildren : 0;
                        /* Take trail mark, call inner, unwind, invert */
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    call    trail_mark_fn\n");
                        A("    push    rax                    ; NAF cond mark\n");
                        if (!cifn || (strcmp(cifn,"fail")==0 && cia==0)) {
                            A("    mov     eax, -1\n");  /* \+ fail → cond true */
                        } else if (strcmp(cifn,"true")==0 && cia==0) {
                            A("    mov     eax, 0\n");   /* \+ true → cond false */
                        } else {
                            char cifa[300]; snprintf(cifa, sizeof cifa, "%s/%d", cifn, cia);
                            char cisafe[256]; strncpy(cisafe, pl_safe(cifa), 255);
                            if (cia > 0) {
                                int cialloc = ALIGN16(cia*8);
                                A("    sub     rsp, %d\n", cialloc);
                                A("    mov     rbx, rsp\n");
                                for (int ai=0; ai<cia; ai++) {
                                    emit_pl_term_load(ci->children[ai], n_vars);
                                    A("    mov     [rbx + %d], rax\n", ai*8);
                                }
                                A("    mov     rdi, rbx\n");
                                A("    lea     rsi, [rel pl_trail]\n");
                                A("    xor     edx, edx\n");
                                A("    call    pl_%s_r\n", cisafe);
                                A("    add     rsp, %d\n", cialloc);
                            } else {
                                A("    xor     rdi, rdi\n");
                                A("    lea     rsi, [rel pl_trail]\n");
                                A("    xor     edx, edx\n");
                                A("    call    pl_%s_r\n", cisafe);
                            }
                        }
                        A("    push    rax\n");
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    mov     rsi, [rsp + 8]\n");
                        A("    call    trail_unwind\n");
                        A("    pop     rax\n");
                        A("    add     rsp, 8\n");
                        /* inner failed (eax==-1) → \+ true → condition holds → fall through */
                        /* inner succeeded (eax>=0) → \+ false → goto else */
                        A("    cmp     eax, -1\n");
                        A("    jne     %s\n", else_lbl);
                        cond_handled = 1;
                    }
                    /* \=/2 as condition: take else_lbl if unify succeeds */
                    if (!cond_handled && cond && cond->kind == E_FNC && cond->sval &&
                        strcmp(cond->sval, "\\=") == 0 && cond->nchildren == 2) {
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    call    trail_mark_fn\n");
                        A("    push    rax                    ; \\= cond mark\n");
                        emit_pl_term_load(cond->children[0], n_vars);
                        A("    mov     [rel pl_tmp], rax\n");
                        emit_pl_term_load(cond->children[1], n_vars);
                        A("    mov     rsi, rax\n");
                        A("    mov     rdi, [rel pl_tmp]\n");
                        A("    lea     rdx, [rel pl_trail]\n");
                        A("    call    unify\n");
                        A("    push    rax\n");
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    mov     rsi, [rsp + 8]\n");
                        A("    call    trail_unwind\n");
                        A("    pop     rax\n");
                        A("    add     rsp, 8\n");
                        /* unify succeeded (eax!=0) → \= false → goto else */
                        A("    test    eax, eax\n");
                        A("    jnz     %s\n", else_lbl);
                        cond_handled = 1;
                    }
                    /* User-defined predicate call as condition */
                    if (!cond_handled && cond && cond->kind == E_FNC && cond->sval) {
                        const char *cfn2 = cond->sval; int ca2 = cond->nchildren;
                        char cfa2[300]; snprintf(cfa2, sizeof cfa2, "%s/%d", cfn2, ca2);
                        char csafe2[256]; strncpy(csafe2, pl_safe(cfa2), 255);
                        if (ca2 > 0) {
                            A("    sub     rsp, %d\n", ALIGN16(ca2*8));
                            for (int ai = 0; ai < ca2; ai++) {
                                emit_pl_term_load(cond->children[ai], n_vars);
                                A("    mov     [rsp + %d], rax\n", ai*8);
                            }
                            A("    mov     rdi, rsp\n");
                        } else { A("    xor     rdi, rdi\n"); }
                        A("    lea     rsi, [rel pl_trail]\n");
                        A("    xor     edx, edx\n");
                        A("    call    pl_%s_r\n", csafe2);
                        if (ca2 > 0) A("    add     rsp, %d\n", ALIGN16(ca2*8));
                        /* return < 0 means failure */
                        A("    test    eax, eax\n");
                        A("    js      %s\n", else_lbl);
                        cond_handled = 1;
                    }
                    if (!cond_handled) {
                        /* fallback: always take then-branch */
                        A("    ; if-then-else cond unhandled — assuming true\n");
                    }
                    /* Then branch */
                    if (then && then->kind == E_FNC && then->sval) {
                        const char *tfn = then->sval; int ta = then->nchildren;
                        if (strcmp(tfn,"write")==0 && ta==1) {
                            emit_pl_term_load(then->children[0], n_vars);
                            A("    mov     rdi, rax\n");
                            A("    call    pl_write\n");
                        } else if (strcmp(tfn,"nl")==0 && ta==0) {
                            A("    mov     edi, 10\n"); A("    call    putchar\n");
                        } else if (strcmp(tfn,"writeln")==0 && ta==1) {
                            emit_pl_term_load(then->children[0], n_vars);
                            A("    mov     rdi, rax\n");
                            A("    call    pl_writeln\n");
                        } else if (strcmp(tfn,"true")==0) { /* no-op */ }
                        else if (strcmp(tfn,"fail")==0) {
                            A("    jmp     %s\n", next_clause);
                        } else if (strcmp(tfn,"\\+")==0 && ta==1) {
                            /* \+ in then-branch */
                            EXPR_t *ti = then->children[0];
                            const char *tifn = (ti && ti->sval) ? ti->sval : NULL;
                            int tia = ti ? (int)ti->nchildren : 0;
                            A("    lea     rdi, [rel pl_trail]\n"); A("    call    trail_mark_fn\n"); A("    push    rax\n");
                            if (!tifn||(strcmp(tifn,"fail")==0&&tia==0)){A("    mov     eax, -1\n");}
                            else if(strcmp(tifn,"true")==0&&tia==0){A("    mov     eax, 0\n");}
                            else {
                                char tifa[300]; snprintf(tifa,sizeof tifa,"%s/%d",tifn,tia);
                                char tisafe[256]; strncpy(tisafe,pl_safe(tifa),255);
                                if(tia>0){int ta2=ALIGN16(tia*8);A("    sub     rsp, %d\n",ta2);A("    mov     rbx, rsp\n");for(int ai=0;ai<tia;ai++){emit_pl_term_load(ti->children[ai],n_vars);A("    mov     [rbx + %d], rax\n",ai*8);}A("    mov     rdi, rbx\n");}else{A("    xor     rdi, rdi\n");}
                                A("    lea     rsi, [rel pl_trail]\n"); A("    xor     edx, edx\n"); A("    call    pl_%s_r\n",tisafe);
                                if(tia>0)A("    add     rsp, %d\n",ALIGN16(tia*8));
                            }
                            A("    push    rax\n");A("    lea     rdi, [rel pl_trail]\n");A("    mov     rsi, [rsp + 8]\n");A("    call    trail_unwind\n");A("    pop     rax\n");A("    add     rsp, 8\n");
                            A("    cmp     eax, -1\n"); A("    jne     %s\n", next_clause); /* inner succeeded → \+ fails */
                        } else if (strcmp(tfn,"\\=")==0 && ta==2) {
                            /* \= in then-branch */
                            A("    lea     rdi, [rel pl_trail]\n"); A("    call    trail_mark_fn\n"); A("    push    rax\n");
                            emit_pl_term_load(then->children[0], n_vars); A("    mov     [rel pl_tmp], rax\n");
                            emit_pl_term_load(then->children[1], n_vars); A("    mov     rsi, rax\n"); A("    mov     rdi, [rel pl_tmp]\n"); A("    lea     rdx, [rel pl_trail]\n"); A("    call    unify\n");
                            A("    push    rax\n");A("    lea     rdi, [rel pl_trail]\n");A("    mov     rsi, [rsp + 8]\n");A("    call    trail_unwind\n");A("    pop     rax\n");A("    add     rsp, 8\n");
                            A("    test    eax, eax\n"); A("    jnz     %s\n", next_clause); /* unified → \= fails */
                        } else {
                            /* user call — call once, deterministic */
                            char tfa[300]; snprintf(tfa, sizeof tfa, "%s/%d", tfn, ta);
                            char tsafe[256]; strncpy(tsafe, pl_safe(tfa), 255);
                            if (ta > 0) {
                                A("    sub     rsp, %d\n", ALIGN16(ta*8));
                                for (int ai = 0; ai < ta; ai++) {
                                    emit_pl_term_load(then->children[ai], n_vars);
                                    A("    mov     [rsp + %d], rax\n", ai*8);
                                }
                                A("    mov     rdi, rsp\n");
                            } else { A("    xor     rdi, rdi\n"); }
                            A("    lea     rsi, [rel pl_trail]\n");
                            A("    xor     edx, edx\n");
                            A("    call    pl_%s_r\n", tsafe);
                            if (ta > 0) A("    add     rsp, %d\n", ALIGN16(ta*8));
                        }
                    }
                    A("    jmp     %s\n", done_lbl);
                    /* Else branch */
                    A("%s:\n", else_lbl);
                    if (right && right->kind == E_FNC && right->sval) {
                        const char *efn = right->sval; int ea = right->nchildren;
                        if (strcmp(efn,"write")==0 && ea==1) {
                            emit_pl_term_load(right->children[0], n_vars);
                            A("    mov     rdi, rax\n");
                            A("    call    pl_write\n");
                        } else if (strcmp(efn,"nl")==0 && ea==0) {
                            A("    mov     edi, 10\n"); A("    call    putchar\n");
                        } else if (strcmp(efn,"writeln")==0 && ea==1) {
                            emit_pl_term_load(right->children[0], n_vars);
                            A("    mov     rdi, rax\n");
                            A("    call    pl_writeln\n");
                        } else if (strcmp(efn,"true")==0) { /* no-op */ }
                        else if (strcmp(efn,"fail")==0) {
                            A("    jmp     %s\n", next_clause);
                        } else if (strcmp(efn,"\\+")==0 && ea==1) {
                            /* \+ in else-branch */
                            EXPR_t *ei = right->children[0];
                            const char *eifn = (ei && ei->sval) ? ei->sval : NULL;
                            int eia = ei ? (int)ei->nchildren : 0;
                            A("    lea     rdi, [rel pl_trail]\n"); A("    call    trail_mark_fn\n"); A("    push    rax\n");
                            if (!eifn||(strcmp(eifn,"fail")==0&&eia==0)){A("    mov     eax, -1\n");}
                            else if(strcmp(eifn,"true")==0&&eia==0){A("    mov     eax, 0\n");}
                            else {
                                char eifa[300]; snprintf(eifa,sizeof eifa,"%s/%d",eifn,eia);
                                char eisafe[256]; strncpy(eisafe,pl_safe(eifa),255);
                                if(eia>0){int ea2=ALIGN16(eia*8);A("    sub     rsp, %d\n",ea2);A("    mov     rbx, rsp\n");for(int ai=0;ai<eia;ai++){emit_pl_term_load(ei->children[ai],n_vars);A("    mov     [rbx + %d], rax\n",ai*8);}A("    mov     rdi, rbx\n");}else{A("    xor     rdi, rdi\n");}
                                A("    lea     rsi, [rel pl_trail]\n"); A("    xor     edx, edx\n"); A("    call    pl_%s_r\n",eisafe);
                                if(eia>0)A("    add     rsp, %d\n",ALIGN16(eia*8));
                            }
                            A("    push    rax\n");A("    lea     rdi, [rel pl_trail]\n");A("    mov     rsi, [rsp + 8]\n");A("    call    trail_unwind\n");A("    pop     rax\n");A("    add     rsp, 8\n");
                            A("    cmp     eax, -1\n"); A("    jne     %s\n", next_clause);
                        } else if (strcmp(efn,"\\=")==0 && ea==2) {
                            /* \= in else-branch */
                            A("    lea     rdi, [rel pl_trail]\n"); A("    call    trail_mark_fn\n"); A("    push    rax\n");
                            emit_pl_term_load(right->children[0], n_vars); A("    mov     [rel pl_tmp], rax\n");
                            emit_pl_term_load(right->children[1], n_vars); A("    mov     rsi, rax\n"); A("    mov     rdi, [rel pl_tmp]\n"); A("    lea     rdx, [rel pl_trail]\n"); A("    call    unify\n");
                            A("    push    rax\n");A("    lea     rdi, [rel pl_trail]\n");A("    mov     rsi, [rsp + 8]\n");A("    call    trail_unwind\n");A("    pop     rax\n");A("    add     rsp, 8\n");
                            A("    test    eax, eax\n"); A("    jnz     %s\n", next_clause);
                        } else {
                            char efa[300]; snprintf(efa, sizeof efa, "%s/%d", efn, ea);
                            char esafe[256]; strncpy(esafe, pl_safe(efa), 255);
                            if (ea > 0) {
                                A("    sub     rsp, %d\n", ALIGN16(ea*8));
                                for (int ai = 0; ai < ea; ai++) {
                                    emit_pl_term_load(right->children[ai], n_vars);
                                    A("    mov     [rsp + %d], rax\n", ai*8);
                                }
                                A("    mov     rdi, rsp\n");
                            } else { A("    xor     rdi, rdi\n"); }
                            A("    lea     rsi, [rel pl_trail]\n");
                            A("    xor     edx, edx\n");
                            A("    call    pl_%s_r\n", esafe);
                            if (ea > 0) A("    add     rsp, %d\n", ALIGN16(ea*8));
                        }
                    }
                    A("%s:\n", done_lbl);
                    continue;
                }

                /* Flatten left conjunction */
                EXPR_t *lgoals[64]; int nlg = 0;
                EXPR_t *cur2 = left;
                while (cur2 && cur2->kind == E_FNC && cur2->sval &&
                       strcmp(cur2->sval, ",") == 0 && cur2->nchildren == 2 && nlg < 63) {
                    lgoals[nlg++] = cur2->children[0];
                    cur2 = cur2->children[1];
                }
                if (cur2) lgoals[nlg++] = cur2;

                /* Find the backtrackable user call in left goals (first non-builtin) */
                int user_call_idx = -1;
                for (int li = 0; li < nlg; li++) {
                    EXPR_t *lg = lgoals[li];
                    if (!lg || lg->kind != E_FNC || !lg->sval) continue;
                    const char *lfn = lg->sval;
                    if (strcmp(lfn,"nl")==0||strcmp(lfn,"write")==0||
                        strcmp(lfn,"writeln")==0||strcmp(lfn,"true")==0||
                        strcmp(lfn,"fail")==0||strcmp(lfn,"halt")==0) continue;
                    user_call_idx = li; break;
                }

                if (user_call_idx >= 0) {
                    EXPR_t *ucall = lgoals[user_call_idx];

                    /* If the "user call" is itself a conjunction compound (','/N),
                     * flatten it into lgoals in-place so we don't generate
                     * 'call pl__cm__sl_N_r' — a label that is never defined.
                     * prolog_lower.c emits n-ary E_FNC(",") nodes (nchildren >= 2),
                     * so we splice children[0..N-1] directly — no recursive flatten. */
                    if (ucall->sval && strcmp(ucall->sval, ",") == 0 && ucall->nchildren >= 2) {
                        EXPR_t *newgoals[128]; int nng = 0;
                        for (int li = 0; li < user_call_idx; li++) newgoals[nng++] = lgoals[li];
                        for (int ci = 0; ci < ucall->nchildren && nng < 127; ci++)
                            newgoals[nng++] = ucall->children[ci];
                        for (int li = user_call_idx+1; li < nlg && nng < 127; li++)
                            newgoals[nng++] = lgoals[li];
                        for (int li = 0; li < nng && li < 64; li++) lgoals[li] = newgoals[li];
                        nlg = nng < 64 ? nng : 64;
                        /* Re-find user_call_idx in the expanded list */
                        user_call_idx = -1;
                        for (int li = 0; li < nlg; li++) {
                            EXPR_t *lg = lgoals[li];
                            if (!lg || lg->kind != E_FNC || !lg->sval) continue;
                            const char *lfn2 = lg->sval;
                            if (strcmp(lfn2,"nl")==0||strcmp(lfn2,"write")==0||
                                strcmp(lfn2,"writeln")==0||strcmp(lfn2,"true")==0||
                                strcmp(lfn2,"fail")==0||strcmp(lfn2,"halt")==0||
                                strcmp(lfn2,",")==0) continue;
                            user_call_idx = li; break;
                        }
                        if (user_call_idx < 0) goto disj_no_ucall;
                        ucall = lgoals[user_call_idx];
                    }

                    int uca = ucall->nchildren;
                    char ucfa[300]; snprintf(ucfa, sizeof ucfa, "%s/%d", ucall->sval, uca);
                    char ucsafe[256]; strncpy(ucsafe, pl_safe(ucfa), 255);
                    char retry_lbl[128];
                    snprintf(retry_lbl, sizeof retry_lbl, "disj_%s_%d_%d_retry", pred_safe, idx, bi);

                    /* Emit pre-call goals (deterministic) */
                    for (int li = 0; li < user_call_idx; li++) {
                        EXPR_t *lg = lgoals[li]; if (!lg||!lg->sval) continue;
                        if (strcmp(lg->sval,"nl")==0) { A("    mov edi,10\n"); A("    call putchar\n"); }
                        else if (strcmp(lg->sval,"write")==0 && lg->nchildren==1) {
                            emit_pl_term_load(lg->children[0], n_vars);
                            A("    mov rdi, rax\n"); A("    call pl_write\n");
                        }
                    }

                    /* Retry loop: _cs in [rbp-32] */
                    char retry_back_lbl[128];
                    snprintf(retry_back_lbl, sizeof retry_back_lbl, "disj_%s_%d_%d_retry_back", pred_safe, idx, bi);
                    A("    mov     dword [rbp - 32], 0    ; _cs\n");
                    A("%s:\n", retry_lbl);
                    /* call predicate with current _cs */
                    if (uca > 0) {
                        int alloc2 = ALIGN16(uca*8);
                        A("    sub     rsp, %d\n", alloc2);
                        for (int ai = 0; ai < uca; ai++) {
                            emit_pl_term_load(ucall->children[ai], n_vars);
                            A("    mov     [rsp + %d], rax\n", ai*8);
                        }
                        A("    mov     rdi, rsp\n");
                    } else { A("    xor     rdi, rdi\n"); }
                    A("    lea     rsi, [rel pl_trail]\n");
                    A("    mov     edx, [rbp - 32]\n");
                    A("    call    pl_%s_r\n", ucsafe);
                    if (uca > 0) A("    add     rsp, %d\n", ALIGN16(uca*8));
                    A("    test    eax, eax\n");
                    A("    js      %s\n", else_lbl);
                    /* eax is already the STRIDE-encoded next resume point — save directly */
                    A("    mov     [rbp - 32], eax\n");

                    /* Post-call goals — failure loops back to retry */
                    for (int li = user_call_idx+1; li < nlg; li++) {
                        EXPR_t *lg = lgoals[li]; if (!lg||!lg->sval) continue;
                        const char *pgn = lg->sval; int pga = lg->nchildren;
                        if (strcmp(pgn,"nl")==0) { A("    mov edi,10\n"); A("    call putchar\n"); }
                        else if (strcmp(pgn,"write")==0 && pga==1) {
                            emit_pl_term_load(lg->children[0], n_vars);
                            A("    mov rdi, rax\n"); A("    call pl_write\n");
                        } else if (strcmp(pgn,"fail")==0 && pga==0) {
                            A("    jmp     %s\n", retry_back_lbl);
                        } else if (strcmp(pgn,"true")==0) { /* no-op */
                        } else {
                            /* another user call — call once, fail -> retry outer */
                            char ifa[300]; snprintf(ifa, sizeof ifa, "%s/%d", pgn, pga);
                            char isafe[256]; strncpy(isafe, pl_safe(ifa), 255);
                            if (pga > 0) {
                                A("    sub     rsp, %d\n", ALIGN16(pga*8));
                                for (int ai = 0; ai < pga; ai++) {
                                    emit_pl_term_load(lg->children[ai], n_vars);
                                    A("    mov     [rsp + %d], rax\n", ai*8);
                                }
                                A("    mov     rdi, rsp\n");
                            } else { A("    xor     rdi, rdi\n"); }
                            A("    lea     rsi, [rel pl_trail]\n");
                            A("    xor     edx, edx\n");
                            A("    call    pl_%s_r\n", isafe);
                            if (pga > 0) A("    add     rsp, %d\n", ALIGN16(pga*8));
                            A("    test    eax, eax\n");
                            A("    js      %s\n", retry_back_lbl);
                        }
                    }
                    /* retry_back: unwind trail then loop */
                    A("%s:\n", retry_back_lbl);
                    A("    lea     rdi, [rel pl_trail]\n");
                    A("    mov     esi, [rbp - 8]\n");
                    A("    call    trail_unwind\n");
                    A("    jmp     %s\n", retry_lbl);
                    A("    jmp     %s\n", done_lbl);
                } else {
                    disj_no_ucall:;
                    /* No user call in left — just emit deterministic goals */
                    for (int li = 0; li < nlg; li++) {
                        EXPR_t *lg = lgoals[li]; if (!lg||!lg->sval) continue;
                        if (strcmp(lg->sval,"fail")==0) { A("    jmp %s\n", else_lbl); break; }
                        if (strcmp(lg->sval,"nl")==0) { A("    mov edi,10\n"); A("    call putchar\n"); }
                        else if (strcmp(lg->sval,"write")==0 && lg->nchildren==1) {
                            emit_pl_term_load(lg->children[0], n_vars);
                            A("    mov rdi, rax\n"); A("    call pl_write\n");
                        }
                    }
                    A("    jmp     %s\n", done_lbl);
                }

                A("%s:\n", else_lbl);
                /* Right branch */
                if (right && right->kind == E_FNC && right->sval) {
                    if (strcmp(right->sval,"true")==0) { /* no-op */ }
                    else if (strcmp(right->sval,"fail")==0) { A("    jmp %s\n", next_clause); }
                    else {
                        int ra2 = right->nchildren;
                        char rfa2[300]; snprintf(rfa2, sizeof rfa2, "%s/%d", right->sval, ra2);
                        char rsafe2[256]; strncpy(rsafe2, pl_safe(rfa2), 255);
                        if (ra2 > 0) {
                            A("    sub     rsp, %d\n", ALIGN16(ra2*8));
                            for (int ai = 0; ai < ra2; ai++) {
                                emit_pl_term_load(right->children[ai], n_vars);
                                A("    mov     [rsp + %d], rax\n", ai*8);
                            }
                            A("    mov     rdi, rsp\n");
                        } else { A("    xor     rdi, rdi\n"); }
                        A("    lea     rsi, [rel pl_trail]\n");
                        A("    xor     edx, edx\n");
                        A("    call    pl_%s_r\n", rsafe2);
                        if (ra2 > 0) A("    add     rsp, %d\n", ALIGN16(ra2*8));
                        A("    test    eax, eax\n");
                        A("    js      %s\n", next_clause);
                    }
                }
                A("%s:\n", done_lbl);
                continue;
            }
            if (strcmp(fn, "=") == 0 && garity == 2) {
                char ufail[128];
                snprintf(ufail, sizeof ufail, "pl_%s_c%d_ufail%d", pred_safe, idx, bi);
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    mov     rsi, rax\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    lea     rdx, [rel pl_trail]\n");
                A("    call    unify\n");
                A("    test    eax, eax\n");
                A("    jnz     pl_%s_c%d_ug%d\n", pred_safe, idx, bi);
                A("%s:\n", ufail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("pl_%s_c%d_ug%d:\n", pred_safe, idx, bi);
                continue;
            }
            /* --- write/1 in goal position --- */
            if (strcmp(fn, "write") == 0 && garity == 1) {
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     rdi, rax\n");
                A("    call    pl_write\n");
                continue;
            }
            /* --- nl/0 in goal position --- */
            if (strcmp(fn, "nl") == 0 && garity == 0) {
                A("    mov     edi, 10\n");
                A("    call    putchar\n");
                continue;
            }
            /* --- is/2: Result is Expr --- */
            if (strcmp(fn, "is") == 0 && garity == 2) {
                char isfail[128];
                snprintf(isfail, sizeof isfail, "pl_%s_c%d_isfail%d", pred_safe, idx, bi);
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    mov     rsi, rax\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    lea     rdx, [rel pl_trail]\n");
                A("    call    pl_is\n");
                A("    test    eax, eax\n");
                A("    jnz     pl_%s_c%d_isok%d\n", pred_safe, idx, bi);
                A("%s:\n", isfail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("pl_%s_c%d_isok%d:\n", pred_safe, idx, bi);
                continue;
            }
            /* --- numeric comparisons --- */
            #define EMIT_CMP(op, fn_name) \
            if (strcmp(fn, op) == 0 && garity == 2) { \
                char cmpfail[128]; \
                snprintf(cmpfail, sizeof cmpfail, "pl_%s_c%d_cmpfail%d", pred_safe, idx, bi); \
                emit_pl_term_load(goal->children[0], n_vars); \
                A("    mov     [rel pl_tmp], rax\n"); \
                emit_pl_term_load(goal->children[1], n_vars); \
                A("    mov     rsi, rax\n"); \
                A("    mov     rdi, [rel pl_tmp]\n"); \
                A("    call    " fn_name "\n"); \
                A("    test    eax, eax\n"); \
                A("    jnz     pl_%s_c%d_cmpok%d\n", pred_safe, idx, bi); \
                A("%s:\n", cmpfail); \
                A("    lea     rdi, [rel pl_trail]\n"); \
                A("    mov     esi, [rbp - 8]\n"); \
                A("    call    trail_unwind\n"); \
                A("    jmp     %s\n", next_clause); \
                A("pl_%s_c%d_cmpok%d:\n", pred_safe, idx, bi); \
                continue; \
            }
            EMIT_CMP("<",   "pl_num_lt")
            EMIT_CMP(">",   "pl_num_gt")
            EMIT_CMP("=<",  "pl_num_le")
            EMIT_CMP(">=",  "pl_num_ge")
            EMIT_CMP("=:=", "pl_num_eq")
            EMIT_CMP("=\\=", "pl_num_ne")
            #undef EMIT_CMP
            /* --- functor/3: functor(Term, Name, Arity) --- */
            if (strcmp(fn, "functor") == 0 && garity == 3) {
                char ffail[128], fok[128];
                snprintf(ffail, sizeof ffail, "pl_%s_c%d_ffail%d", pred_safe, idx, bi);
                snprintf(fok,   sizeof fok,   "pl_%s_c%d_fok%d",   pred_safe, idx, bi);
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    push    rax\n");
                emit_pl_term_load(goal->children[2], n_vars);
                A("    mov     rcx, rax\n");
                A("    pop     rsi\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    mov     rdx, rcx\n");
                A("    lea     rcx, [rel pl_trail]\n");
                A("    call    pl_functor\n");
                A("    test    eax, eax\n");
                A("    jnz     %s\n", fok);
                A("%s:\n", ffail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("%s:\n", fok);
                continue;
            }
            /* --- arg/3: arg(N, Compound, Arg) --- */
            if (strcmp(fn, "arg") == 0 && garity == 3) {
                char afail[128], aok[128];
                snprintf(afail, sizeof afail, "pl_%s_c%d_afail%d", pred_safe, idx, bi);
                snprintf(aok,   sizeof aok,   "pl_%s_c%d_aok%d",   pred_safe, idx, bi);
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    push    rax\n");
                emit_pl_term_load(goal->children[2], n_vars);
                A("    mov     rcx, rax\n");
                A("    pop     rsi\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    mov     rdx, rcx\n");
                A("    lea     rcx, [rel pl_trail]\n");
                A("    call    pl_arg\n");
                A("    test    eax, eax\n");
                A("    jnz     %s\n", aok);
                A("%s:\n", afail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("%s:\n", aok);
                continue;
            }
            /* --- =../2: Term =.. List (univ) --- */
            if (strcmp(fn, "=..") == 0 && garity == 2) {
                char ufail[128], uok[128];
                snprintf(ufail, sizeof ufail, "pl_%s_c%d_ufail%d", pred_safe, idx, bi);
                snprintf(uok,   sizeof uok,   "pl_%s_c%d_uok%d",   pred_safe, idx, bi);
                emit_pl_term_load(goal->children[0], n_vars);
                A("    mov     [rel pl_tmp], rax\n");
                emit_pl_term_load(goal->children[1], n_vars);
                A("    mov     rsi, rax\n");
                A("    mov     rdi, [rel pl_tmp]\n");
                A("    lea     rdx, [rel pl_trail]\n");
                A("    call    pl_univ\n");
                A("    test    eax, eax\n");
                A("    jnz     %s\n", uok);
                A("%s:\n", ufail);
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - 8]\n");
                A("    call    trail_unwind\n");
                A("    jmp     %s\n", next_clause);
                A("%s:\n", uok);
                continue;
            }
            /* --- type tests: atom/1, integer/1, float/1, var/1, nonvar/1, compound/1 --- */
            #define EMIT_TYPETEST(name, rt_fn) \
            if (strcmp(fn, name) == 0 && garity == 1) { \
                char ttfail[128], ttok[128]; \
                snprintf(ttfail, sizeof ttfail, "pl_%s_c%d_ttfail%d", pred_safe, idx, bi); \
                snprintf(ttok,   sizeof ttok,   "pl_%s_c%d_ttok%d",   pred_safe, idx, bi); \
                emit_pl_term_load(goal->children[0], n_vars); \
                A("    mov     rdi, rax\n"); \
                A("    call    " rt_fn "\n"); \
                A("    test    eax, eax\n"); \
                A("    jnz     %s\n", ttok); \
                A("%s:\n", ttfail); \
                A("    lea     rdi, [rel pl_trail]\n"); \
                A("    mov     esi, [rbp - 8]\n"); \
                A("    call    trail_unwind\n"); \
                A("    jmp     %s\n", next_clause); \
                A("%s:\n", ttok); \
                continue; \
            }
            EMIT_TYPETEST("atom",     "pl_atom")
            EMIT_TYPETEST("integer",  "pl_integer")
            EMIT_TYPETEST("float",    "pl_is_float")
            EMIT_TYPETEST("var",      "pl_var")
            EMIT_TYPETEST("nonvar",   "pl_nonvar")
            EMIT_TYPETEST("compound", "pl_compound")
            #undef EMIT_TYPETEST
            /* --- conjunction compound in goal position: flatten and re-emit each sub-goal ---
             * Guards against pl__cm__sl_N_r being generated when a ,/N compound reaches
             * the generic user-call path (e.g. inside a disjunction retry loop). */
            if (strcmp(fn, ",") == 0 && garity >= 2) {
                EXPR_t *flat2[64]; int nf2 = 0;
                EXPR_t *cur3 = goal;
                while (cur3 && cur3->kind == E_FNC && cur3->sval &&
                       strcmp(cur3->sval, ",") == 0 && cur3->nchildren == 2 && nf2 < 63) {
                    flat2[nf2++] = cur3->children[0];
                    cur3 = cur3->children[1];
                }
                if (cur3) flat2[nf2++] = cur3;
                for (int fi2 = 0; fi2 < nf2; fi2++) {
                    EXPR_t *sub2 = flat2[fi2];
                    if (!sub2 || sub2->kind != E_FNC || !sub2->sval) continue;
                    const char *sfn2 = sub2->sval; int sa2 = sub2->nchildren;
                    if (strcmp(sfn2,"nl")==0 && sa2==0) {
                        A("    mov     edi, 10\n"); A("    call    putchar\n");
                    } else if (strcmp(sfn2,"write")==0 && sa2==1) {
                        emit_pl_term_load(sub2->children[0], n_vars);
                        A("    mov     rdi, rax\n"); A("    call    pl_write\n");
                    } else if (strcmp(sfn2,"fail")==0 && sa2==0) {
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    mov     esi, [rbp - 8]\n");
                        A("    call    trail_unwind\n");
                        A("    jmp     %s\n", next_clause);
                    } else if (strcmp(sfn2,"true")==0 && sa2==0) {
                        /* no-op */
                    } else {
                        char cfa3[300]; snprintf(cfa3, sizeof cfa3, "%s/%d", sfn2, sa2);
                        char csafe3[256]; strncpy(csafe3, pl_safe(cfa3), 255);
                        char cfail3[128];
                        snprintf(cfail3, sizeof cfail3, "pl_%s_c%d_cfail3_%d_%d", pred_safe, idx, bi, fi2);
                        if (sa2 > 0) {
                            A("    sub     rsp, %d\n", ALIGN16(sa2*8));
                            for (int ai2 = 0; ai2 < sa2; ai2++) {
                                emit_pl_term_load(sub2->children[ai2], n_vars);
                                A("    mov     [rsp + %d], rax\n", ai2*8);
                            }
                            A("    mov     rdi, rsp\n");
                        } else { A("    xor     rdi, rdi\n"); }
                        A("    lea     rsi, [rel pl_trail]\n");
                        A("    xor     edx, edx\n");
                        A("    call    pl_%s_r\n", csafe3);
                        if (sa2 > 0) A("    add     rsp, %d\n", ALIGN16(sa2*8));
                        A("    test    eax, eax\n");
                        A("    jns     pl_%s_c%d_cok3_%d_%d\n", pred_safe, idx, bi, fi2);
                        A("%s:\n", cfail3);
                        A("    lea     rdi, [rel pl_trail]\n");
                        A("    mov     esi, [rbp - 8]\n");
                        A("    call    trail_unwind\n");
                        A("    jmp     %s\n", next_clause);
                        A("pl_%s_c%d_cok3_%d_%d:\n", pred_safe, idx, bi, fi2);
                    }
                }
                continue;
            }
            /* --- user-defined predicate call --- */
            {
                /* Build args array on stack, call pl_NAME_ARITY_r
                 * Pass inner_start from [rbp-32] so recursive calls resume correctly.
                 * On success, return encoded start = clause_idx * PL_STRIDE + (sub_ret + 1).
                 * On re-entry with inner_start > 0, pass sub_start = inner_start - 1 to sub-call. */
                char call_safe_fa[300]; snprintf(call_safe_fa, sizeof call_safe_fa, "%s/%d", fn, garity);
                char call_safe[256];
                snprintf(call_safe, sizeof call_safe, "%s", pl_safe(call_safe_fa));
                char β_lbl[128];
                snprintf(β_lbl, sizeof β_lbl, "pl_%s_c%d_β%d", pred_safe, idx, bi);
                /* αN: re-entry for resume after prior ucall exhausts.
                 * Trail mark for THIS ucall was taken at γ_{N-1} time (see γN block below),
                 * NOT here — taking it at αN re-entry would over-unwind on resume.
                 * Sub_cs is pre-zeroed; restored from slot before call so edx survives
                 * arg-building (term_new_compound is a C function that clobbers rdx). */
                A("pl_%s_c%d_α%d:\n", pred_safe, idx, ucall_seq);
                /* Build args with rbx as stable base.
                 * emit_pl_term_load may do sub/add rsp for nested compounds;
                 * [rbx+N] indexing is immune to those rsp shifts. */
                if (garity > 0) {
                    int alloc = ALIGN16(garity*8);
                    A("    sub     rsp, %d              ; args array for %s/%d\n",
                      alloc, fn, garity);
                    A("    mov     rbx, rsp               ; stable base (immune to rsp shifts)\n");
                    for (int ai = 0; ai < garity; ai++) {
                        emit_pl_term_load(goal->children[ai], n_vars);
                        A("    mov     [rbx + %d], rax    ; args[%d]\n", ai*8, ai);
                    }
                    A("    mov     rdi, rbx               ; args[] ptr\n");
                } else {
                    A("    xor     rdi, rdi               ; no args\n");
                }
                A("    lea     rsi, [rel pl_trail]\n");
                /* Restore edx after arg-building — term_new_compound clobbers rdx */
                A("    mov     edx, [rbp - %d]    ; restore sub_cs ucall %d\n",
                  UCALL_SLOT_OFFSET(ucall_seq), ucall_seq);
                A("    call    pl_%s_r\n", call_safe);
                if (garity > 0)
                    A("    add     rsp, %d\n", ALIGN16(garity*8));
                A("    test    eax, eax\n");
                A("    js      %s\n", β_lbl);
                /* success path: save returned sub_cs, then take trail mark for the
                 * NEXT ucall NOW (while we know ucall N's bindings are live).
                 * βN+1 will unwind to this mark, cleanly reverting only ucall N+1's
                 * bindings without touching ucall N's.  Must be done before γN so
                 * the mark is always initialised when βN+1 fires.
                 * Guard: only when ucall_seq+1 < max_ucalls to prevent
                 * UCALL_MARK_OFFSET(max_ucalls) == VAR_SLOT_OFFSET(0) collision. */
                A("    mov     [rbp - %d], eax        ; ucall slot %d sub_cs\n",
                  UCALL_SLOT_OFFSET(ucall_seq), ucall_seq);
                /* sub_cs_acc is recomputed at γN from all slots — no per-ucall accumulation needed */
                if (ucall_seq + 1 < max_ucalls) {
                    A("    lea     rdi, [rel pl_trail]\n");
                    A("    call    trail_mark_fn\n");
                    A("    mov     [rbp - %d], eax    ; trail mark for ucall %d\n",
                      UCALL_MARK_OFFSET(ucall_seq + 1), ucall_seq + 1);
                }
                /* jump to γN, skipping the β failure block below */
                A("    jmp     pl_%s_c%d_γ%d\n", pred_safe, idx, bi);
                A("%s:\n", β_lbl);
                /* βN: ucall N failed.  Undo ucall N-1's bindings (and N's) by
                 * unwinding to UCALL_MARK_OFFSET(N-1) for N>0, or UCALL_MARK_OFFSET(0)
                 * (body-entry mark, after head unif) for N=0 → jump next clause. */
                A("    lea     rdi, [rel pl_trail]\n");
                A("    mov     esi, [rbp - %d]    ; unwind to before ucall %d\n",
                  UCALL_MARK_OFFSET(ucall_seq > 0 ? ucall_seq - 1 : 0), ucall_seq);
                A("    call    trail_unwind\n");
                if (ucall_seq > 0) {
                    A("    mov     edx, [rbp - %d]    ; restore sub_cs ucall %d\n",
                      UCALL_SLOT_OFFSET(ucall_seq - 1), ucall_seq - 1);
                    A("    jmp     pl_%s_c%d_α%d\n", pred_safe, idx, ucall_seq - 1);
                } else {
                    A("    jmp     %s\n", next_clause);
                }
                A("pl_%s_c%d_γ%d:\n", pred_safe, idx, bi);
                /* Recompute sub_cs_acc from slots 0..ucall_seq so retry paths
                 * that arrive here with stale acc get a clean partial sum. */
                A("    mov     eax, [rbp - %d]    ; sub_cs_acc = slot 0\n",
                  UCALL_SLOT_OFFSET(0));
                for (int _ri = 1; _ri <= ucall_seq; _ri++) {
                    A("    push    rax\n");
                    A("    mov     eax, [rbp - %d]    ; slot %d\n",
                      UCALL_SLOT_OFFSET(_ri), _ri);
                    for (int _si = 0; _si < _ri; _si++)
                        A("    imul    eax, %d            ; * STRIDE^%d\n",
                          PL_RESUME_BIG, _si + 1);
                    A("    pop     rcx\n");
                    A("    add     eax, ecx\n");
                }
                A("    mov     [rbp - 16], eax    ; store recomputed sub_cs_acc\n");
                /* Zero slot ucall_seq+1 so ucall N+1 starts fresh when ucall N
                 * gives a new answer.  Known limitation: this also wipes pre-loaded
                 * values from the re-entry decode when ucall N re-succeeds at the
                 * same answer — the 3+ucall re-entry case needs a different approach
                 * (e.g. separate fresh/resume entry labels per clause).  For now
                 * correct for ≤2 ucalls and the common queens/crypt pattern. */
                if (ucall_seq + 1 < max_ucalls)
                    A("    mov     dword [rbp - %d], 0  ; init sub_cs slot ucall %d\n",
                      UCALL_SLOT_OFFSET(ucall_seq + 1), ucall_seq + 1);
                A("    xor     edx, edx               ; next ucall starts fresh\n");
                snprintf(last_β_lbl, sizeof last_β_lbl,
                         "pl_%s_c%d_β%d", pred_safe, idx, bi);
                ucall_seq++;
            }
        }
    }

    /* γ — body succeeded */
    A("    ; γ — success clause %d (base=%d body_ucalls=%d)\n",
      idx, base, body_user_call_count);
    if (body_user_call_count == 0) {
        /* Fact / builtin-only: 1 slot. γ returns base+1 = next clause's base. */
        A("    mov     eax, %d               ; base+1 → next clause\n", base + 1);
    } else {
        /* Body-call clause: γ returns base + sub_cs + 1.
         * sub_cs_acc accumulated in [rbp-16] (separate from [rbp-32] = read-only start). */
        A("    mov     eax, [rbp - 16]       ; sub_cs_acc from body call\n");
        A("    inc     eax                   ; sub_cs + 1\n");
        A("    add     eax, %d               ; + base\n", base);
    }
    A("    mov     rsp, rbp\n");
    A("    pop     rbp\n");
    A("    ret\n");
}

/* -------------------------------------------------------------------------
 * emit_prolog_choice — emit complete resumable function for one predicate
 *
 * Generated function signature (C-ABI):
 *   int pl_NAME_ARITY_r(Term *arg0, ..., Trail *trail_unused, int start)
 *
 * The trail is the global pl_trail — trail arg ignored (use global).
 * 'start' selects which clause to try first (0 = fresh call).
 * Returns clause_idx on success, -1 on total failure.
 *
 * Frame layout (after push rbp; mov rbp,rsp):
 *   [rbp +  8]   = return address  (standard)
 *   [rbp +  0]   = saved rbp       (standard)
 *   [rbp - 8]    = trail mark      (int stored as 8B)
 *   [rbp - 16]   = _cut flag       (1B, padded to 8B slot; at [rbp-17])
 *   [rbp - (k+5)*8] for k=0..n_vars-1 = fresh Term* for var slot k
 *   [rbp - 40]   = first arg (arg0) saved in frame (arity>0 case)
 *   [rbp + 24]   = second arg (arg1 or Trail*)
 *   ...
 * ---------------------------------------------------------------------- */
static void emit_prolog_choice(EXPR_t *choice) {
    if (!choice || choice->kind != E_CHOICE) return;

    const char *pred = choice->sval ? choice->sval : "unknown/0";
    int arity = 0;
    const char *sl = strrchr(pred, '/');
    if (sl) arity = atoi(sl+1);
    int nclauses = choice->nchildren;

    /* Find max n_vars across clauses for frame allocation */
    int max_vars = 0;
    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *ec = choice->children[ci];
        if (ec && (int)ec->ival > max_vars) max_vars = (int)ec->ival;
    }

    /* Make a stable copy of safe name (pl_safe uses static buffer) */
    char safe[256]; strncpy(safe, pl_safe(pred), 255); safe[255]='\0';

    char ω_lbl[128];
    snprintf(ω_lbl, sizeof ω_lbl, "pl_%s_ω", safe);

    A("\n");
    A("; ============================================================\n");
    A("; predicate %s  (%d clause%s)\n", pred, nclauses, nclauses==1?"":"s");
    A("; ============================================================\n");

    /* frame: mark(8) + _cut(8) + args_ptr(8) + start(8) + arity*8 + n_vars*8, align 16
     * [rbp-8]         = trail mark
     * [rbp-16]        = var slot 0  (if any)
     * [rbp-17]        = _cut byte
     * [rbp-24]        = args array ptr
     * [rbp-32]        = start
     * [rbp-40-ai*8]   = saved arg ai  (arity slots)
     * [rbp-40-arity*8 - k*8] = var slot k
     */
    /* Frame layout:
     * [rbp- 8] trail mark
     * [rbp-16] sub_cs_acc (last ucall return, for γ encoding)
     * [rbp-17] _cut byte
     * [rbp-24] args array ptr
     * [rbp-32] start (read-only after save)
     * [rbp-40 .. rbp-40-(max_ucalls-1)*8] per-ucall sub_cs slots (ucall 0..N-1)
     * [rbp-40-max_ucalls*8 .. ] var slots
     */
    /* Count max body user calls across all clauses for frame sizing */
    int max_body_ucalls = 0;
    for (int ci = 0; ci < nclauses; ci++) {
        EXPR_t *ec = choice->children[ci];
        if (!ec) continue;
        int nb = (int)ec->nchildren - arity; if (nb < 0) nb = 0;
        int nuc = 0;
        for (int bi = 0; bi < nb; bi++) {
            EXPR_t *g = ec->children[arity + bi];
            if (!g || g->kind != E_FNC || !g->sval) continue;
            const char *gn = g->sval;
            if (strcmp(gn,"write")==0||strcmp(gn,"nl")==0||strcmp(gn,"writeln")==0||
                strcmp(gn,"true")==0||strcmp(gn,"fail")==0||strcmp(gn,"halt")==0||
                strcmp(gn,"is")==0||strcmp(gn,"=")==0||strcmp(gn,"!")== 0||
                strcmp(gn,"<")==0||strcmp(gn,">")==0||strcmp(gn,"=<")==0||
                strcmp(gn,">=")==0||strcmp(gn,"=:=")==0||strcmp(gn,"=\\=")==0||
                strcmp(gn,",")==0||strcmp(gn,";")==0||strcmp(gn,"functor")==0||
                strcmp(gn,"arg")==0||strcmp(gn,"=..")==0||strcmp(gn,"atom")==0||
                strcmp(gn,"integer")==0||strcmp(gn,"float")==0||
                strcmp(gn,"var")==0||strcmp(gn,"nonvar")==0||strcmp(gn,"compound")==0||strcmp(gn,"\\+")==0||strcmp(gn,"\\=")==0) continue;
            if (g->kind == E_CUT) continue;
            nuc++;
        }
        if (nuc > max_body_ucalls) max_body_ucalls = nuc;
    }
    int frame = 40 + max_body_ucalls*8 + max_body_ucalls*8 + max_vars*8;
    /* 40 base (trail+cut+args_ptr+start+sub_cs_acc) + ucall sub_cs slots
     * + ucall trail-mark slots + var slots */
    if (frame % 16) frame = (frame/16+1)*16;

    /* Resumable function */
    A("global pl_%s_r\n", safe);
    A("pl_%s_r:\n", safe);
    A("    push    rbp\n");
    A("    mov     rbp, rsp\n");
    A("    sub     rsp, %d\n", frame);
    A("    mov     byte [rbp - 17], 0    ; _cut = 0\n");

    /* 'start' argument: ABI is always pl_NAME_r(Term **args, Trail *trail, int start)
     * = (rdi, rsi, rdx) regardless of predicate arity.
     * args is a pointer to the caller-built Term*[] array.
     * trail is the global trail pointer.
     * start is always in rdx. */
    static const char *argregs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
    int start_reg_idx = 2;  /* start is always rdx (index 2) */

    /* Save args array pointer to [rbp+16] slot — push all args onto caller frame */
    /* Actually with C-ABI the args are already in registers at function entry.
     * We need to preserve 'start' and the args array.
     * Strategy: push all register args to a local args array in frame. */
    if (arity > 0) {
        /* rdi = args array ptr (caller built Term*[] and passed pointer).
         * Just save rdi directly into the args_ptr slot. */
        A("    mov     [rbp - 24], rdi     ; save args array ptr\n");
    } else if (arity == 0) {
        A("    ; arity 0 — no args to save\n");
        A("    xor     rax, rax\n");
        A("    mov     [rbp - 24], rax     ; args array ptr = NULL\n");
    }

    /* Save 'start' to stack */
    if (start_reg_idx < 6) {
        A("    mov     [rbp - 32], %s     ; save start\n", argregs[start_reg_idx]);
    }
    /* [rbp-16] = sub_cs_acc: separate from [rbp-32] (start) so original start
     * stays readable for last-ucall sub_cs dispatch in multi-ucall bodies. */
    A("    xor     eax, eax\n");
    A("    mov     [rbp - 16], eax        ; sub_cs_acc = 0\n");

    /* Compute base[] for each clause.
     * Fact/builtin-only clause: 1 slot → next base = base + 1.
     * Body-call clause: open-ended → next base = base + 1 (conservative: base
     * just marks entry, inner is open). We use base+1 as the next clause's base
     * regardless, since a body-call clause can return any value >= base+1 and
     * the next clause dispatch checks start >= base[next]. */
    int base[64];
    if (nclauses > 64) nclauses = 64;  /* safety */
    base[0] = 0;
    for (int ci = 0; ci < nclauses - 1; ci++) {
        EXPR_t *ec = choice->children[ci];
        int nb = ec ? (int)(ec->nchildren) - arity : 0;
        if (nb < 0) nb = 0;
        /* Check if this clause has any body user-calls */
        int has_ucall = 0;
        for (int bi = 0; bi < nb && !has_ucall; bi++) {
            EXPR_t *g = ec ? ec->children[arity + bi] : NULL;
            if (!g || g->kind != E_FNC || !g->sval) continue;
            const char *gn = g->sval;
            if (strcmp(gn,"write")==0||strcmp(gn,"nl")==0||strcmp(gn,"writeln")==0||
                strcmp(gn,"true")==0||strcmp(gn,"fail")==0||strcmp(gn,"halt")==0||
                strcmp(gn,"is")==0||strcmp(gn,"=")==0||
                strcmp(gn,"<")==0||strcmp(gn,">")==0||strcmp(gn,"=<")==0||
                strcmp(gn,">=")==0||strcmp(gn,"=:=")==0||strcmp(gn,"=\\=")==0||
                strcmp(gn,",")==0||strcmp(gn,";")==0||strcmp(gn,"functor")==0||strcmp(gn,"arg")==0||strcmp(gn,"=..")==0||
                strcmp(gn,"atom")==0||strcmp(gn,"integer")==0||strcmp(gn,"float")==0||
                strcmp(gn,"var")==0||strcmp(gn,"nonvar")==0||strcmp(gn,"compound")==0||strcmp(gn,"\\+")==0||strcmp(gn,"\\=")==0) continue;
            if (g->kind == E_CUT) continue;
            has_ucall = 1;
        }
        /* Both fact and body-call clauses: next base = this base + 1.
         * Body-call clause range is [base[ci], base[ci+1]) = [ci, ci+1) + open.
         * The dispatch identifies the clause by: largest ci where start >= base[ci]. */
        base[ci + 1] = base[ci] + 1;
    }

    /* Compute ω_base: if last clause is a fact, start >= base[last]+1 → ω.
     * For body-call last clause, any start >= base[last] dispatches there (open-ended). */
    {
        EXPR_t *last_ec = choice->children[nclauses - 1];
        int last_nb = last_ec ? (int)(last_ec->nchildren) - arity : 0;
        if (last_nb < 0) last_nb = 0;
        int last_has_ucall = 0;
        for (int bi = 0; bi < last_nb && !last_has_ucall; bi++) {
            EXPR_t *g = last_ec ? last_ec->children[arity + bi] : NULL;
            if (!g || g->kind != E_FNC || !g->sval) continue;
            const char *gn = g->sval;
            if (strcmp(gn,"write")==0||strcmp(gn,"nl")==0||strcmp(gn,"writeln")==0||
                strcmp(gn,"true")==0||strcmp(gn,"fail")==0||strcmp(gn,"halt")==0||
                strcmp(gn,"is")==0||strcmp(gn,"=")==0||
                strcmp(gn,"<")==0||strcmp(gn,">")==0||strcmp(gn,"=<")==0||
                strcmp(gn,">=")==0||strcmp(gn,"=:=")==0||strcmp(gn,"=\\=")==0||
                strcmp(gn,",")==0||strcmp(gn,";")==0||strcmp(gn,"functor")==0||strcmp(gn,"arg")==0||strcmp(gn,"=..")==0||
                strcmp(gn,"atom")==0||strcmp(gn,"integer")==0||strcmp(gn,"float")==0||
                strcmp(gn,"var")==0||strcmp(gn,"nonvar")==0||strcmp(gn,"compound")==0||strcmp(gn,"\\+")==0||strcmp(gn,"\\=")==0) continue;
            if (g->kind == E_CUT) continue;
            last_has_ucall = 1;
        }
        int ω_base = base[nclauses - 1] + 1;  /* only meaningful for fact last clause */

        /* switch on start — linear scan from last to first */
        A("    mov     eax, [rbp - 32]       ; start value\n");
        if (!last_has_ucall) {
            /* Last clause is a fact: values >= ω_base go directly to ω */
            A("    cmp     eax, %d\n", ω_base);
            A("    jge     %s                 ; past all clauses\n", ω_lbl);
        }
        for (int ci = nclauses - 1; ci >= 0; ci--) {
            A("    cmp     eax, %d\n", base[ci]);
            A("    jge     pl_%s_c%d_α\n", safe, ci);
        }
        A("    jmp     %s\n", ω_lbl);
    }

    /* Emit each clause block with its base */
    for (int ci = 0; ci < nclauses; ci++) {
        pl_cur_max_ucalls = max_body_ucalls;
        emit_prolog_clause_block(choice->children[ci], ci, nclauses,
                                 safe, arity, ω_lbl, base[ci],
                                 max_body_ucalls);
    }

    /* ω port */
    A("\n%s:\n", ω_lbl);
    A("    ; ω — all clauses exhausted\n");
    A("    lea     rdi, [rel pl_trail]\n");
    A("    mov     esi, [rbp - 8]         ; trail mark\n");
    A("    call    trail_unwind\n");
    A("    mov     rsp, rbp\n");
    A("    pop     rbp\n");
    A("    mov     eax, -1\n");
    A("    ret\n");

    /* Single-shot wrapper (no start arg — always starts at 0) */
    A("\nglobal pl_%s\n", safe);
    A("pl_%s:\n", safe);
    /* shift args: trail goes into extra reg slot, start=0 appended */
    if (arity < 5) {
        /* push 0 (start) into the right register */
        /* Trail* is already at arg[arity] register; just add start=0 */
        A("    ; single-shot wrapper: push start=0\n");
        A("    xor     %s, %s\n", argregs[start_reg_idx], argregs[start_reg_idx]);
        A("    jmp     pl_%s_r\n", safe);
    } else {
        A("    push    0\n");
        A("    jmp     pl_%s_r\n", safe);
    }
}

/* -------------------------------------------------------------------------
 * emit_prolog_main — emit pl_rt_init + main
 * Scans for :- initialization(X) directive to find entry predicate.
 * ---------------------------------------------------------------------- */
static void emit_prolog_main(Program *prog) {
    /* Find initialization predicate name */
    const char *init_pred = "main";
    for (STMT_t *s = prog->head; s; s = s->next) {
        EXPR_t *e = s->subject;
        if (!e || e->kind != E_FNC || !e->sval) continue;
        if (strcmp(e->sval, "initialization") == 0 && e->nchildren >= 1) {
            EXPR_t *goal = e->children[0];
            if (goal && goal->sval) init_pred = goal->sval;
        }
    }
    /* init_pred is just the functor name (e.g. "main"), arity is 0 */
    char init_full[280]; snprintf(init_full, sizeof init_full, "%s/0", init_pred);
    char safe_init[256]; strncpy(safe_init, pl_safe(init_full), 255);

    A("\n; ============================================================\n");
    A("; pl_rt_init — fix up atom_ids in .data Term structs\n");
    A("; ============================================================\n");
    A("pl_rt_init:\n");
    A("    push    rbp\n");
    A("    mov     rbp, rsp\n");
    /* For each atom, call prolog_atom_intern(name_str) and store into atom struct+8 */
    for (int i = 0; i < pl_atom_count_emit; i++) {
        /* We need the string pointer — emit string literals in .rodata */
        A("    lea     rdi, [rel pl_astr_%d]\n", i);
        A("    call    prolog_atom_intern\n");
        /* store int result (eax) into the dq slot of the atom struct */
        A("    mov     dword [rel %s + 8], eax\n", pl_atom_labels[i]);
    }
    A("    pop     rbp\n");
    A("    ret\n");

    A("\n; ============================================================\n");
    A("; main\n");
    A("; ============================================================\n");
    A("main:\n");
    A("    push    rbp\n");
    A("    mov     rbp, rsp\n");
    A("    ; init atom table\n");
    A("    call    prolog_atom_init\n");
    A("    ; init trail\n");
    A("    lea     rdi, [rel pl_trail]\n");
    A("    call    trail_init\n");
    A("    ; fix up atom_id fields\n");
    A("    call    pl_rt_init\n");
    A("    ; call initialization predicate: %s/0\n", init_pred);
    A("    xor     rdi, rdi               ; args=NULL (arity 0)\n");
    A("    lea     rsi, [rel pl_trail]   ; Trail*\n");
    A("    xor     edx, edx               ; start=0\n");
    A("    call    pl_%s_r\n", safe_init);
    A("    ; exit 0\n");
    A("    xor     edi, edi\n");
    A("    call    exit\n");
    A("    pop     rbp\n");
    A("    ret\n");

    /* .rodata — atom name strings */
    A("\nsection .rodata\n");
    for (int i = 0; i < pl_atom_count_emit; i++) {
        /* Emit atom string safely: use hex bytes for any non-printable or
         * backtick/backslash chars to avoid NASM parse errors. */
        {
            const char *s = pl_atom_strings[i];
            A("pl_astr_%d: db ", i);
            if (*s == '\0') {
                A("0");  /* empty string: just the null terminator */
            } else {
                int first = 1;
                while (*s) {
                    unsigned char c = (unsigned char)*s++;
                    if (!first) A(",");
                    first = 0;
                    if (c == '`' || c == '\\' || c < 0x20 || c > 0x7e)
                        A("0x%02x", c);
                    else
                        A("`%c`", c);
                }
                A(",0");
            }
            A("\n");
        }
    }
}

/* -------------------------------------------------------------------------
 * emit_prolog_program — top-level entry for -pl -asm
 * ---------------------------------------------------------------------- */
static void emit_prolog_program(Program *prog) {
    if (!prog) return;

    /* Reset atom table */
    for (int i = 0; i < pl_atom_count_emit; i++) free(pl_atom_strings[i]);
    pl_atom_count_emit = 0;

    /* Pass 1: pre-intern all atoms that appear in head/body terms */
    /* (intern happens lazily via emit_pl_term_load, so just emit) */

    emit_pl_header(prog);

    A("section .text\n\n");

    /* Emit each predicate */
    for (STMT_t *s = prog->head; s; s = s->next) {
        EXPR_t *e = s->subject;
        if (!e) continue;
        if (e->kind == E_CHOICE) {
            emit_prolog_choice(e);
        }
        /* directives (E_FNC initialization etc.) handled in emit_prolog_main */
    }

    emit_prolog_main(prog);

    /* .data — atom Term structs (collected during emit_prolog_choice passes) */
    emit_pl_atom_data_v2();
}

/* end Prolog ASM emitter */
/* ======================================================================= */

/* -------------------------------------------------------------------------
 * asm_emit_prolog — public entry point for -pl -asm path (called from main.c)
 * ---------------------------------------------------------------------- */
void asm_emit_prolog(Program *prog, FILE *f) {
    out = f;
    emit_prolog_program(prog);
}
