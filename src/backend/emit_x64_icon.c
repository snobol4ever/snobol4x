/*
 * icon_emit.c — Tiny-ICON Byrd Box → x64 NASM emitter (Sprint I-1/I-2)
 *
 */
#define _POSIX_C_SOURCE 200809L
/*
 * Tier 0 (Rung 1-2): ICN_INT, ICN_STR, ICN_ADD/SUB/MUL/DIV/MOD,
 *   ICN_LT/LE/GT/GE/EQ/NE, ICN_TO/TO_BY, ICN_EVERY, ICN_CALL(write), ICN_PROC
 *
 * Tier 1 (Rung 3): user procedures with params/locals, ICN_RETURN, ICN_FAIL,
 *   ICN_VAR (local/param), user function calls, ICN_ASSIGN, ICN_IF
 *
 * Calling convention for user procs:
 *   - Args pushed onto icn_stack (rightmost first) before call
 *   - Callee pops args, stores in rbx-saved frame slots (rbp-relative)
 *   - Return value: icn_retval global; icn_failed=0 on return, 1 on fail
 *   - Local vars: frame slots above params (rbp - 8*(param+local+1))
 */

#include "icon_emit.h"
#include "icon_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* =========================================================================
 * Output helpers
 * ======================================================================= */
static void E(IcnEmitter *em, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(em->out, fmt, ap); va_end(ap);
}
static void Ldef(IcnEmitter *em, const char *l) { fprintf(em->out, "%s:\n", l); }
static void Jmp (IcnEmitter *em, const char *t) { fprintf(em->out, "    jmp     %s\n", t); }

/* =========================================================================
 * Label utilities
 * ======================================================================= */
int  icn_next_uid(IcnEmitter *em)                          { return em->uid++; }
void icn_label_α(int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_α",id); }
void icn_label_β (int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_β",id); }
void icn_lbl_code (int id, char *b, size_t s)          { snprintf(b,s,"icn_%d_code",id); }
static void label_val(int id, char *b, size_t s)         { snprintf(b,s,"icn_%d_val",id); }
static void label_I  (int id, char *b, size_t s)         { snprintf(b,s,"icn_%d_I",id); }

/* =========================================================================
 * BSS / rodata
 * ======================================================================= */
#define MAX_BSS    512
#define MAX_RODATA 256
typedef struct { char name[64]; }           BssEntry;
typedef struct { char name[64]; char *data;} RodataEntry;
static BssEntry    bss_entries[MAX_BSS];    static int bss_count=0;
static RodataEntry rodata_entries[MAX_RODATA]; static int rodata_count=0;
static int         str_counter=0;

static void bss_declare(const char *name) {
    for (int i=0;i<bss_count;i++) if(!strcmp(bss_entries[i].name,name)) return;
    if(bss_count<MAX_BSS) strncpy(bss_entries[bss_count++].name,name,63);
}
static void rodata_declare(const char *label, const char *data) {
    for (int i=0;i<rodata_count;i++) if(!strcmp(rodata_entries[i].name,label)) return;
    if(rodata_count>=MAX_RODATA) return;
    int i=rodata_count++;
    strncpy(rodata_entries[i].name,label,63);
    rodata_entries[i].data=strdup(data);
}
static void alloc_str_label(char *b, size_t s) { snprintf(b,s,"icn_str_%d",str_counter++); }

/* =========================================================================
 * User procedure registry
 * ======================================================================= */
#define MAX_USER_PROCS 64
static char user_procs[MAX_USER_PROCS][64];
static int  user_proc_nparams[MAX_USER_PROCS];
static int  user_proc_is_gen[MAX_USER_PROCS];  /* 1 if proc contains ICN_SUSPEND */
static int  user_proc_count=0;

static void icn_register_proc(const char *name, int nparams, int is_gen) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return;
    if(user_proc_count<MAX_USER_PROCS){
        strncpy(user_procs[user_proc_count],name,63);
        user_proc_nparams[user_proc_count]=nparams;
        user_proc_is_gen[user_proc_count]=is_gen;
        user_proc_count++;
    }
}
static int icn_is_user_proc(const char *name) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return 1;
    return 0;
}
static int icn_is_gen_proc(const char *name) {
    for(int i=0;i<user_proc_count;i++)
        if(!strcmp(user_procs[i],name)) return user_proc_is_gen[i];
    return 0;
}

/* Recursively check if any node in the tree is ICN_SUSPEND */
static int has_suspend(IcnNode *n) {
    if(!n) return 0;
    if(n->kind==ICN_SUSPEND) return 1;
    for(int i=0;i<n->nchildren;i++)
        if(has_suspend(n->children[i])) return 1;
    return 0;
}

/* =========================================================================
 * Per-procedure local variable table
 * ======================================================================= */
#define MAX_LOCALS 32
/* type: '?' unknown, 'I' integer, 'S' string/cset pointer */
typedef struct { char name[32]; int slot; char type; } LocalVar;
static LocalVar cur_locals[MAX_LOCALS];
static int      cur_nlocals=0, cur_nparams=0;
static char     cur_ret_label[64]="";   /* label to jump to for return */
static char     cur_fail_label[64]="";  /* label to jump to for fail */
static char     cur_suspend_ret_label[64]=""; /* bare ret — frame kept alive for suspend */

static void locals_reset(void) { cur_nlocals=0; cur_nparams=0; }

/* Allocate an anonymous frame slot for a temporary (e.g. binop/relop lcache).
 * Returns the slot index; use slot_offset(slot) for rbp-relative access. */
static int locals_alloc_tmp(void) {
    int slot=cur_nlocals;
    if(cur_nlocals<MAX_LOCALS){
        cur_locals[cur_nlocals].name[0]='\0';
        cur_locals[cur_nlocals].type='?';
        cur_locals[cur_nlocals++].slot=slot;
    }
    return slot;
}

static int locals_find(const char *name) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)) return cur_locals[i].slot;
    return -1;
}
static int locals_add(const char *name) {
    int slot=cur_nlocals;
    if(cur_nlocals<MAX_LOCALS){
        strncpy(cur_locals[cur_nlocals].name,name,31);
        cur_locals[cur_nlocals].type='?';
        cur_locals[cur_nlocals++].slot=slot;
    }
    return slot;
}
static void locals_set_type(const char *name, char type) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)){ cur_locals[i].type=type; return; }
}
static char locals_type(const char *name) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)) return cur_locals[i].type;
    return '?';
}
/* Frame offset for slot N: rbp - 8*(N+1) */
static int slot_offset(int slot) { return -8*(slot+1); }

/* Walk an expression node and return its Tiny-ICON value kind:
 * 'S' = string/cset pointer, 'I' = integer, '?' = unknown. */
static char icn_expr_kind(IcnNode *n) {
    if (!n) return '?';
    switch (n->kind) {
        case ICN_STR:    return 'S';
        case ICN_CSET:   return 'S';
        case ICN_INT:    return 'I';
        case ICN_CONCAT: case ICN_LCONCAT: return 'S';
        case ICN_ADD: case ICN_SUB: case ICN_MUL:
        case ICN_DIV: case ICN_MOD:        return 'I';
        case ICN_VAR:
            if (strcmp(n->val.sval, "&subject") == 0) return 'S';
            if (strcmp(n->val.sval, "&pos")     == 0) return 'I';
            return locals_type(n->val.sval);
        case ICN_CALL:
            /* Known string-returning builtins */
            if (n->nchildren >= 1 && n->children[0] &&
                n->children[0]->kind == ICN_VAR) {
                const char *fn = n->children[0]->val.sval;
                /* tab/move return substrings (char*); any/many/upto return long pos */
                if (strcmp(fn,"tab")==0   || strcmp(fn,"move")==0  ||
                    strcmp(fn,"string")==0 || strcmp(fn,"read")==0  ||
                    strcmp(fn,"reads")==0  || strcmp(fn,"repl")==0  ||
                    strcmp(fn,"reverse")==0)
                    return 'S';
                if (strcmp(fn,"any")==0  || strcmp(fn,"many")==0 ||
                    strcmp(fn,"upto")==0 || strcmp(fn,"match")==0 ||
                    strcmp(fn,"find")==0)
                    return 'I';
            }
            return '?';
        default:         return '?';
    }
}

/* Pre-pass: walk top-level and one level of nesting in proc body to record
 * rhs type for each ICN_ASSIGN so write() can dispatch correctly. */
static void infer_local_types(IcnNode *proc, int body_start) {
    int nstmts = proc->nchildren - body_start;
    for (int si = 0; si < nstmts; si++) {
        IcnNode *s = proc->children[body_start + si];
        if (!s) continue;
        if (s->kind == ICN_ASSIGN && s->nchildren >= 2) {
            IcnNode *lhs = s->children[0], *rhs = s->children[1];
            if (lhs && lhs->kind == ICN_VAR) {
                char k = icn_expr_kind(rhs);
                if (k != '?') locals_set_type(lhs->val.sval, k);
            }
        }
        for (int ci = 0; ci < s->nchildren; ci++) {
            IcnNode *c = s->children[ci];
            if (!c) continue;
            if (c->kind == ICN_ASSIGN && c->nchildren >= 2) {
                IcnNode *lhs = c->children[0], *rhs = c->children[1];
                if (lhs && lhs->kind == ICN_VAR) {
                    char k = icn_expr_kind(rhs);
                    if (k != '?') locals_set_type(lhs->val.sval, k);
                }
            }
        }
    }
}

/* =========================================================================
 * Forward declaration
 * ======================================================================= */
static void emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *out_α, char *out_β);

/* Loop control stack — forward declarations (defined near emit_repeat) */
static void loop_push(const char *brk, const char *nxt);
static void loop_pop(void);

/* =========================================================================
 * ICN_INT
 * Stack protocol: α pushes value onto hardware stack then jumps succeed.
 * β pops nothing (re-entry after backtrack) then jumps fail.
 * ======================================================================= */
static void emit_int(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; INT %ld  id=%d\n",n->val.ival,id);
    Ldef(em,a); E(em,"    push    %ld\n",n->val.ival); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_STR
 * ======================================================================= */
static void emit_str(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],sl[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    alloc_str_label(sl,sizeof sl); rodata_declare(sl,n->val.sval);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); E(em,"    lea     rdi, [rel %s]\n",sl); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_VAR — variable reference
 * α: load value into rax (from frame slot or global BSS), push, succeed.
 * β: jump fail (one-shot — variable has no next value).
 * ======================================================================= */
static void emit_var(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; VAR %s  id=%d\n",n->val.sval,id);
    Ldef(em,a);
    /* &subject / &pos keywords */
    if (strcmp(n->val.sval, "&subject") == 0) {
        bss_declare("icn_subject");
        E(em,"    mov     rax, [rel icn_subject]\n");
        E(em,"    push    rax\n");
        Jmp(em,ports.γ);
        Ldef(em,b); Jmp(em,ports.ω);
        return;
    }
    if (strcmp(n->val.sval, "&pos") == 0) {
        bss_declare("icn_pos");
        E(em,"    mov     rax, [rel icn_pos]\n");
        E(em,"    push    rax\n");
        Jmp(em,ports.γ);
        Ldef(em,b); Jmp(em,ports.ω);
        return;
    }
    int slot=locals_find(n->val.sval);
    if(slot>=0) {
        E(em,"    mov     rax, [rbp%+d]\n", slot_offset(slot));
    } else {
        /* Global BSS var */
        char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",n->val.sval);
        bss_declare(gv);
        E(em,"    mov     rax, [rel %s]\n", gv);
    }
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_ASSIGN — E1 := E2
 * Evaluates E2, stores result into E1 (must be ICN_VAR).
 * ======================================================================= */
static void emit_assign(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    if(n->nchildren<2){ emit_expr(em,NULL,ports,oa,ob); return; }
    int id=icn_next_uid(em); char a[64],b[64],store[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(store,sizeof store,"icon_%d_store",id);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnPorts rhs_ports; strncpy(rhs_ports.γ,store,63); strncpy(rhs_ports.ω,ports.ω,63);
    char ra[64],rb[64];
    emit_expr(em,n->children[1],rhs_ports,ra,rb);

    Ldef(em,a); Jmp(em,ra);
    Ldef(em,b); Jmp(em,rb);

    Ldef(em,store);
    /* ICN_STR/ICN_CSET leave pointer in rdi (nothing pushed); all others push a value */
    IcnNode *rhs = n->children[1];
    int rhs_is_str = (rhs && (rhs->kind == ICN_STR || rhs->kind == ICN_CSET));
    if (rhs_is_str) {
        E(em,"    ; str assign: rdi already has pointer\n");
    } else {
        E(em,"    pop     rax\n"); /* consume value pushed by RHS */
    }

    IcnNode *lhs=n->children[0];
    if(lhs && lhs->kind==ICN_VAR){
        int slot=locals_find(lhs->val.sval);
        if(slot>=0){
            if (rhs_is_str)
                E(em,"    mov     [rbp%+d], rdi\n",slot_offset(slot));
            else
                E(em,"    mov     [rbp%+d], rax\n",slot_offset(slot));
        } else {
            char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",lhs->val.sval);
            bss_declare(gv);
            if (rhs_is_str)
                E(em,"    mov     [rel %s], rdi\n",gv);
            else
                E(em,"    mov     [rel %s], rax\n",gv);
        }
    }
    Jmp(em,ports.γ);
}

/* =========================================================================
 * ICN_RETURN
 * Stores value into icn_retval, jumps to cur_ret_label.
 * ======================================================================= */
static void emit_return(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    (void)ports;

    if(n->nchildren>0){
        char after[64]; snprintf(after,sizeof after,"icon_%d_ret_store",id);
        IcnPorts vp; strncpy(vp.γ,after,63); strncpy(vp.ω,after,63);
        char va2[64],vb2[64];
        emit_expr(em,n->children[0],vp,va2,vb2);
        Ldef(em,a); Jmp(em,va2);
        Ldef(em,b); Jmp(em,cur_ret_label[0]?cur_ret_label:"icn_dead");
        Ldef(em,after);
        E(em,"    pop     rax\n"); /* consume value pushed by expr */
        E(em,"    mov     [rel icn_retval], rax\n");
        E(em,"    mov     byte [rel icn_failed], 0\n");
        Jmp(em,cur_ret_label[0]?cur_ret_label:"icn_dead");
    } else {
        Ldef(em,a);
        E(em,"    mov     qword [rel icn_retval], 0\n");
        E(em,"    mov     byte [rel icn_failed], 0\n");
        Jmp(em,cur_ret_label[0]?cur_ret_label:"icn_dead");
        Ldef(em,b); Jmp(em,cur_ret_label[0]?cur_ret_label:"icn_dead");
    }
}

/* =========================================================================
 * ICN_FAIL — procedure failure
 * ======================================================================= */
static void emit_fail_node(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                           char *oa, char *ob) {
    (void)n; (void)ports;
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a);
    if(cur_fail_label[0]){
        E(em,"    mov     byte [rel icn_failed], 1\n");
        Jmp(em,cur_fail_label);
    } else Jmp(em,ports.ω);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_SUSPEND — co-routine yield (user-defined generator)
 *
 * suspend E [do body]
 *
 * α: evaluate E; on succeed → store value in icn_retval, store resume label
 *    address in icn_suspend_resume, jump to cur_ret_label (yield to caller).
 * β: jmp [rel icn_suspend_resume] — resume from where we left off.
 *    If there is a 'do body', β runs the body then resumes E's β.
 *    If E is exhausted (ω), procedure fails (jump cur_fail_label).
 *
 * Single BSS slot icn_suspend_resume holds the resume address.
 * This is a single-active-generator design — only one suspend active at a time.
 * ======================================================================= */
static void emit_suspend(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                         char *oa, char *ob) {
    (void)ports;
    int id = icn_next_uid(em);
    char a[64], b[64];
    icn_label_α(id, a, sizeof a);
    icn_label_β (id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    /* resume_here: label the caller's β jumps back to after we yield */
    char resume_here[64]; snprintf(resume_here, sizeof resume_here, "icon_%d_resume", id);
    /* after_val: entered after E succeeds (value on stack) */
    char after_val[64];   snprintf(after_val,   sizeof after_val,   "icon_%d_yield",  id);

    /* Declare BSS slots (deduped) */
    bss_declare("icn_suspend_resume");
    bss_declare("icn_suspended");      /* byte: 1 if proc suspended, 0 if returned/failed */

    IcnNode *val_node  = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren > 1) ? n->children[1] : NULL;

    /* Emit the value expression; on success jump to after_val */
    char va[80], vb[80];
    if (val_node) {
        IcnPorts vp;
        strncpy(vp.γ, after_val, 63);
        strncpy(vp.ω, cur_fail_label[0] ? cur_fail_label : "icn_dead", 63);
        emit_expr(em, val_node, vp, va, vb);
    } else {
        /* suspend with no value: yield 0 */
        snprintf(va, sizeof va, "%s_noval", a);
        snprintf(vb, sizeof vb, "%s_novalb", a);
        Ldef(em, va); E(em,"    push    0\n"); Jmp(em, after_val);
        Ldef(em, vb); Jmp(em, cur_fail_label[0] ? cur_fail_label : "icn_dead");
    }

    /* α: jump into value evaluation */
    Ldef(em, a); Jmp(em, va);

    /* β: resume — jump through icn_suspend_resume slot */
    Ldef(em, b);
    E(em,"    jmp     [rel icn_suspend_resume]\n");

    /* after_val: E succeeded, value on hw stack */
    Ldef(em, after_val);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel icn_retval], rax\n");
    E(em,"    mov     byte [rel icn_failed], 0\n");
    E(em,"    mov     byte [rel icn_suspended], 1\n");      /* signal: suspended, not returned */
    /* store resume address: after yield, caller's β comes back to resume_here */
    E(em,"    lea     rax, [rel %s]\n", resume_here);
    E(em,"    mov     [rel icn_suspend_resume], rax\n");
    E(em,"    mov     [rel icn_suspend_rbp], rbp\n");
    /* yield to caller — bare ret, frame stays alive */
    Jmp(em, cur_suspend_ret_label[0] ? cur_suspend_ret_label : "icn_dead");

    /* resume_here: execution resumes here after caller's β fires.
     * ORDERING FIX: emit body nodes FIRST (they may emit sub-labels inline),
     * then define resume_here + jmp to body's α.  This ensures that
     * resume_here: is immediately followed by jmp ba — not by a sub-node label
     * that would cause fall-through into the wrong place. */
    if (body_node) {
        char ba[64], bb[64];
        IcnPorts bp;
        strncpy(bp.γ, ports.γ, 63);
        strncpy(bp.ω,    ports.γ, 63);  /* body fail also continues */
        emit_expr(em, body_node, bp, ba, bb);
        Ldef(em, resume_here);
        Jmp(em, ba);
    } else {
        Ldef(em, resume_here);
        Jmp(em, ports.γ);
    }
}

/* =========================================================================
 * ICN_IF — if cond then E2 [else E3]  (paper §4.5 indirect goto)
 * Simple version (no bounded optimization): emit cond, on succeed→E2, fail→E3/ports.ω
 * ======================================================================= */
static void emit_if(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                    char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnNode *cond=n->children[0];
    IcnNode *thenb=(n->nchildren>1)?n->children[1]:NULL;
    IcnNode *elseb=(n->nchildren>2)?n->children[2]:NULL;

    char then_a[64],then_b[64],else_a[64],else_b[64];
    char cond_then[64]; snprintf(cond_then,sizeof cond_then,"icon_%d_then",id);
    char cond_else[64]; snprintf(cond_else,sizeof cond_else,"icon_%d_else",id);

    if(thenb){
        IcnPorts tp; strncpy(tp.γ,ports.γ,63); strncpy(tp.ω,ports.ω,63);
        emit_expr(em,thenb,tp,then_a,then_b);
    } else { strncpy(then_a,ports.γ,63); strncpy(then_b,ports.ω,63); }

    if(elseb){
        IcnPorts ep; strncpy(ep.γ,ports.γ,63); strncpy(ep.ω,ports.ω,63);
        emit_expr(em,elseb,ep,else_a,else_b);
    } else { strncpy(else_a,ports.ω,63); }

    IcnPorts cp; strncpy(cp.γ,cond_then,63); strncpy(cp.ω,cond_else,63);
    char ca[64],cb[64];
    emit_expr(em,cond,cp,ca,cb);

    /* cond_then: condition succeeded and pushed a value — discard it, enter then */
    Ldef(em,cond_then);
    E(em,"    add     rsp, 8\n");  /* discard condition result value */
    Jmp(em,thenb?then_a:ports.γ);
    /* cond_else: condition failed (no value pushed) — enter else */
    Ldef(em,cond_else); Jmp(em,elseb?else_a:ports.ω);

    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,cb);
}

/* =========================================================================
 * ICN_CALL — function call (write built-in OR user procedure)
 * ======================================================================= */
static void emit_call(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    if(n->nchildren<1){ emit_fail_node(em,NULL,ports,oa,ob); return; }
    IcnNode *fn=n->children[0];
    int nargs=n->nchildren-1;
    const char *fname=(fn->kind==ICN_VAR)?fn->val.sval:"unknown";
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    E(em,"    ; CALL %s  id=%d\n",fname,id);

    /* --- built-in write --- */
    if(strcmp(fname,"write")==0){
        if(nargs==0){
            Ldef(em,a); E(em,"    call    icn_write_str\n"); Jmp(em,ports.γ);
            Ldef(em,b); Jmp(em,ports.ω); return;
        }
        IcnNode *arg=n->children[1];
        char after[64]; snprintf(after,sizeof after,"icon_%d_call",id);
        IcnPorts ap2; strncpy(ap2.γ,after,63); strncpy(ap2.ω,ports.ω,63);
        char arg_a[64],arg_b[64];
        emit_expr(em,arg,ap2,arg_a,arg_b);
        Ldef(em,a); Jmp(em,arg_a);
        Ldef(em,b); Jmp(em,arg_b);
        Ldef(em,after);
        if(arg->kind==ICN_STR){
            /* emit_str sets rdi via lea; nothing on hw stack — just call */
            E(em,"    call    icn_write_str\n");
        } else if(arg->kind==ICN_CSET){
            /* emit_cset sets rdi via lea; nothing on hw stack — just call */
            E(em,"    call    icn_write_str\n");
        } else if(arg->kind==ICN_CONCAT || arg->kind==ICN_LCONCAT){
            /* emit_concat pushes result char* onto hw stack */
            E(em,"    pop     rdi\n");
            E(em,"    call    icn_write_str\n");
        } else {
            /* Everything else (VAR, CALL, INT, binop…) pushed a value.
             * Use type inference to pick the right runtime call. */
            E(em,"    pop     rdi\n");
            char k = icn_expr_kind(arg);
            if (k == 'S')
                E(em,"    call    icn_write_str\n");
            else
                E(em,"    call    icn_write_int\n");
        }
        Jmp(em,ports.γ);
        return;
    }

    /* --- scan builtins: any / many / upto --- */
    if(strcmp(fname,"any")==0||strcmp(fname,"many")==0||strcmp(fname,"upto")==0){
        const char *rtfn = strcmp(fname,"any")==0  ? "icn_any"  :
                           strcmp(fname,"many")==0 ? "icn_many" : "icn_upto";
        char after[64]; snprintf(after,sizeof after,"icon_%d_sbuiltin",id);
        if(nargs<1){
            Ldef(em,a); Jmp(em,ports.ω);
            Ldef(em,b); Jmp(em,ports.ω); return;
        }
        IcnNode *arg=n->children[1];
        IcnPorts ap2; strncpy(ap2.γ,after,63); strncpy(ap2.ω,ports.ω,63);
        char arg_a[64],arg_b[64];
        emit_expr(em,arg,ap2,arg_a,arg_b);
        Ldef(em,a); Jmp(em,arg_a);
        Ldef(em,b); Jmp(em,arg_b);
        Ldef(em,after);
        /* arg is cset/str: rdi already set; var/other: pop into rdi */
        if(arg->kind==ICN_CSET||arg->kind==ICN_STR){
            /* rdi already has the cset pointer from emit_str/emit_cset */
        } else {
            E(em,"    pop     rdi\n");
        }
        E(em,"    call    %s\n",rtfn);
        /* returns new pos (1-based int) or 0 on fail */
        E(em,"    test    rax, rax\n");
        E(em,"    jz      %s\n",ports.ω);
        E(em,"    push    rax\n");
        Jmp(em,ports.γ);
        return;
    }

    /* --- match(s): one-shot — match s at current scan pos, return new 1-based pos --- */
    if(strcmp(fname,"match")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_maft",id);
        if(nargs<1){ Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return; }
        IcnNode *arg=n->children[1];
        IcnPorts ap; strncpy(ap.γ,after,63); strncpy(ap.ω,ports.ω,63);
        char aa[64],ab[64]; emit_expr(em,arg,ap,aa,ab);
        Ldef(em,a); Jmp(em,aa);
        Ldef(em,b); Jmp(em,ports.ω);
        Ldef(em,after);
        if(arg->kind==ICN_STR||arg->kind==ICN_CSET){ /* rdi already set */ }
        else { E(em,"    pop     rdi\n"); }
        E(em,"    call    icn_match\n");
        E(em,"    test    rax, rax\n");
        E(em,"    jz      %s\n",ports.ω);
        E(em,"    push    rax\n");
        Jmp(em,ports.γ);
        return;
    }

    /* --- tab(n): one-shot — return subject[pos..n-1], set pos=n-1 (returns char*) --- */
    if(strcmp(fname,"tab")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_taft",id);
        if(nargs<1){ Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return; }
        IcnNode *arg=n->children[1];
        IcnPorts ap; strncpy(ap.γ,after,63); strncpy(ap.ω,ports.ω,63);
        char aa[64],ab[64]; emit_expr(em,arg,ap,aa,ab);
        Ldef(em,a); Jmp(em,aa);
        Ldef(em,b); Jmp(em,ports.ω);
        Ldef(em,after);
        E(em,"    pop     rdi\n");   /* n (long) */
        E(em,"    call    icn_tab\n");
        E(em,"    test    rax, rax\n");
        E(em,"    jz      %s\n",ports.ω);
        E(em,"    push    rax\n");   /* push char* result */
        Jmp(em,ports.γ);
        return;
    }

    /* --- move(n): one-shot — return subject[pos..pos+n-1], advance pos by n (returns char*) --- */
    if(strcmp(fname,"move")==0 && !icn_is_user_proc(fname)){
        char after[64]; snprintf(after,sizeof after,"icon_%d_mvaft",id);
        if(nargs<1){ Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return; }
        IcnNode *arg=n->children[1];
        IcnPorts ap; strncpy(ap.γ,after,63); strncpy(ap.ω,ports.ω,63);
        char aa[64],ab[64]; emit_expr(em,arg,ap,aa,ab);
        Ldef(em,a); Jmp(em,aa);
        Ldef(em,b); Jmp(em,ports.ω);
        Ldef(em,after);
        E(em,"    pop     rdi\n");   /* n (long) */
        E(em,"    call    icn_move\n");
        E(em,"    test    rax, rax\n");
        E(em,"    jz      %s\n",ports.ω);
        E(em,"    push    rax\n");   /* push char* result */
        Jmp(em,ports.γ);
        return;
    }

    /* --- find(s1, s2): generator — successive 1-based positions of s1 in s2 ---
     * State: icn_find_s1_N (char* BSS), icn_find_s2_N (char* BSS),
     *        icn_find_pos_N (long BSS, next 0-based search offset).
     * α: evaluate args, init pos=0, enter check.
     * β: pos = last_result (1-based), re-enter check.
     * check: call icn_str_find(s1,s2,pos); 0→ω, else store result as new pos, push→γ. */
    if(strcmp(fname,"find")==0 && nargs>=2 && !icn_is_user_proc(fname)){
        char s1bss[80],s2bss[80],posbss[80];
        snprintf(s1bss,  sizeof s1bss,  "icn_find_s1_%d",  id);
        snprintf(s2bss,  sizeof s2bss,  "icn_find_s2_%d",  id);
        snprintf(posbss, sizeof posbss, "icn_find_pos_%d", id);
        bss_declare(s1bss); bss_declare(s2bss); bss_declare(posbss);

        char after1[64],after2[64],chk[64];
        snprintf(after1,sizeof after1,"icon_%d_fa1",id);
        snprintf(after2,sizeof after2,"icon_%d_fa2",id);
        snprintf(chk,   sizeof chk,   "icon_%d_fchk",id);

        IcnNode *s1arg=n->children[1], *s2arg=n->children[2];
        IcnPorts ap1; strncpy(ap1.γ,after1,63); strncpy(ap1.ω,ports.ω,63);
        char a1[64],b1[64]; emit_expr(em,s1arg,ap1,a1,b1);
        IcnPorts ap2; strncpy(ap2.γ,after2,63); strncpy(ap2.ω,ports.ω,63);
        char a2[64],b2[64]; emit_expr(em,s2arg,ap2,a2,b2);

        /* α: eval s1 → store, eval s2 → store, init pos=0, check */
        Ldef(em,a); Jmp(em,a1);
        Ldef(em,after1);
        if(s1arg->kind==ICN_STR||s1arg->kind==ICN_CSET)
            E(em,"    mov     [rel %s], rdi\n",s1bss);
        else { E(em,"    pop     rax\n"); E(em,"    mov     [rel %s], rax\n",s1bss); }
        Jmp(em,a2);
        Ldef(em,after2);
        if(s2arg->kind==ICN_STR||s2arg->kind==ICN_CSET)
            E(em,"    mov     [rel %s], rdi\n",s2bss);
        else { E(em,"    pop     rax\n"); E(em,"    mov     [rel %s], rax\n",s2bss); }
        E(em,"    mov     qword [rel %s], 0\n",posbss);
        Jmp(em,chk);

        /* β: last result was 1-based; next search starts at that same index (0-based=result) */
        Ldef(em,b);
        /* pos is already stored as last 1-based result → use as next 0-based from */
        Jmp(em,chk);

        /* check */
        Ldef(em,chk);
        E(em,"    mov     rdi, [rel %s]\n",s1bss);
        E(em,"    mov     rsi, [rel %s]\n",s2bss);
        E(em,"    mov     rdx, [rel %s]\n",posbss);
        E(em,"    call    icn_str_find\n");
        E(em,"    test    rax, rax\n");
        E(em,"    jz      %s\n",ports.ω);
        /* store result as new pos (next β will search from result, i.e. 0-based=result) */
        E(em,"    mov     [rel %s], rax\n",posbss);
        E(em,"    push    rax\n");
        Jmp(em,ports.γ);
        return;
    }

    /* --- user procedure call --- */
    if(icn_is_user_proc(fname)){
        int is_gen = icn_is_gen_proc(fname);
        char do_call[64]; snprintf(do_call,sizeof do_call,"icon_%d_docall",id);

        char (*arg_alphas)[64] = nargs>0 ? malloc(nargs*64) : NULL;
        char (*arg_betas) [64] = nargs>0 ? malloc(nargs*64) : NULL;

        char prev_succ[64]; strncpy(prev_succ,do_call,63);

        for(int i=nargs-1;i>=0;i--){
            char push_relay[64]; snprintf(push_relay,sizeof push_relay,"icon_%d_push%d",id,i);
            IcnPorts ap3; strncpy(ap3.γ,push_relay,63); strncpy(ap3.ω,ports.ω,63);
            emit_expr(em,n->children[i+1],ap3,arg_alphas[i],arg_betas[i]);
            Ldef(em,push_relay);
            E(em,"    pop     rdi\n");
            E(em,"    call    icn_push\n");
            Jmp(em,prev_succ);
            strncpy(prev_succ,arg_alphas[i],63);
        }

        Ldef(em,a);
        if(nargs>0) Jmp(em,prev_succ);
        else Jmp(em,do_call);

        /* β: for generators resume via suspend slot; for normal procs just fail */
        Ldef(em,b);
        if(is_gen){
            E(em,"    ; call β — resume if suspended, fail otherwise\n");
            E(em,"    movzx   rax, byte [rel icn_suspended]\n");
            E(em,"    test    rax, rax\n");
            E(em,"    jz      %s\n", ports.ω);
            E(em,"    mov     byte [rel icn_suspended], 0\n");
            E(em,"    mov     rbp, [rel icn_suspend_rbp]\n");
            E(em,"    jmp     [rel icn_suspend_resume]\n");
        } else {
            Jmp(em,ports.ω);
        }

        /* do_call: all args on icn_stack */
        Ldef(em,do_call);
        if(is_gen){
            /* jmp-based trampoline — frame stays live across suspend/resume */
            char after_call[64]; snprintf(after_call,sizeof after_call,"icon_%d_after_call",id);
            char caller_ret[80]; snprintf(caller_ret,sizeof caller_ret,"icn_%s_caller_ret",fname);
            E(em,"    mov     byte [rel icn_suspended], 0\n");
            E(em,"    lea     rax, [rel %s]\n", after_call);
            E(em,"    mov     [rel %s], rax\n", caller_ret);
            E(em,"    jmp     icn_%s\n", fname);
            Ldef(em,after_call);
            E(em,"    movzx   rax, byte [rel icn_failed]\n");
            E(em,"    test    rax, rax\n");
            E(em,"    jnz     %s\n",ports.ω);
            E(em,"    movzx   rax, byte [rel icn_suspended]\n");
            E(em,"    test    rax, rax\n");
            char did_return[64]; snprintf(did_return,sizeof did_return,"icon_%d_returned",id);
            E(em,"    jz      %s\n",did_return);
            E(em,"    mov     rax, [rel icn_retval]\n");
            E(em,"    push    rax\n");
            Jmp(em,ports.γ);
            Ldef(em,did_return);
            E(em,"    mov     rax, [rel icn_retval]\n");
            E(em,"    push    rax\n");
            Jmp(em,ports.γ);
        } else {
            /* Normal call/ret — safe for recursion */
            E(em,"    call    icn_%s\n",fname);
            E(em,"    movzx   rax, byte [rel icn_failed]\n");
            E(em,"    test    rax, rax\n");
            E(em,"    jnz     %s\n",ports.ω);
            E(em,"    mov     rax, [rel icn_retval]\n");
            E(em,"    push    rax\n");
            Jmp(em,ports.γ);
        }

        if(arg_alphas) free(arg_alphas);
        if(arg_betas)  free(arg_betas);
        return;
    }

    /* Unknown call — just fail */
    Ldef(em,a); Jmp(em,ports.ω);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_ALT — value alternation E1 | E2 | ... | En  (n-ary flat array)
 * α → E1.α; E1.ω → E2.α; ... ; En.ω → node.ω
 * β → E1.β (resume leftmost; irgen.icn simple alternation model)
 * ======================================================================= */
static void emit_alt(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    int nc = n->nchildren;
    /* Allocate α/β label storage for each child */
    char (*ca)[64] = malloc(nc * 64);
    char (*cb)[64] = malloc(nc * 64);

    /* Emit right-to-left so ω of child[i] → α of child[i+1] */
    for (int i = nc - 1; i >= 0; i--) {
        IcnPorts ep;
        strncpy(ep.γ, ports.γ, 63);
        strncpy(ep.ω, (i == nc-1) ? ports.ω : ca[i+1], 63);
        emit_expr(em, n->children[i], ep, ca[i], cb[i]);
    }

    Ldef(em,a); Jmp(em,ca[0]);
    Ldef(em,b); Jmp(em,cb[0]);
    free(ca); free(cb);
}

/* =========================================================================
 * ICN_CSET — cset literal (single-quoted string)
 * Treated identically to ICN_STR for pointer passing: lea rdi, [rel label]
 * ======================================================================= */
static void emit_cset(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    emit_str(em, n, ports, oa, ob);   /* same layout — char* in rdi, nothing pushed */
}

/* =========================================================================
 * ICN_NEG — unary minus: -E
 * Evaluate E, negate result, push, succeed. One-shot (no β retry).
 * ======================================================================= */
static void emit_neg(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char after[64]; snprintf(after, sizeof after, "icon_%d_neg", id);
    IcnPorts cp; strncpy(cp.γ, after, 63); strncpy(cp.ω, ports.ω, 63);
    char ca[64], cb[64];
    emit_expr(em, n->children[0], cp, ca, cb);
    Ldef(em, a); Jmp(em, ca);
    Ldef(em, b); Jmp(em, cb);
    Ldef(em, after);
    E(em, "    pop     rax\n");
    E(em, "    neg     rax\n");
    E(em, "    push    rax\n");
    Jmp(em, ports.γ);
}

/* =========================================================================
 * emit_not  --  /E: succeed iff E fails
 * ======================================================================= */
static void emit_not(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    char e_ok[64];   snprintf(e_ok,   sizeof e_ok,   "icon_%d_eok",   id);
    char e_fail[64]; snprintf(e_fail, sizeof e_fail, "icon_%d_efail", id);
    IcnPorts cp;
    strncpy(cp.γ, e_ok,   63);
    strncpy(cp.ω, e_fail, 63);
    char ca[64], cb[64];
    emit_expr(em, n->children[0], cp, ca, cb);
    Ldef(em, a); Jmp(em, ca);
    Ldef(em, b); Jmp(em, ports.ω);
    Ldef(em, e_ok);
    E(em, "    add     rsp, 8\n");
    Jmp(em, ports.ω);
    Ldef(em, e_fail);
    E(em, "    push    0\n");
    Jmp(em, ports.γ);
}

/* =========================================================================
 * emit_seq  --  string equality E1 == E2
 * ======================================================================= */
static void emit_seq(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    char chk[64];         snprintf(chk,        sizeof chk,        "icon_%d_check",  id);
    char lbfwd[64];       snprintf(lbfwd,       sizeof lbfwd,      "icon_%d_lb",     id);
    char lstore[64];      snprintf(lstore,      sizeof lstore,     "icon_%d_lstore", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay, "icon_%d_lrelay", id);
    char right_relay[64]; snprintf(right_relay, sizeof right_relay,"icon_%d_rrelay", id);
    char lc_ptr[64];      snprintf(lc_ptr,      sizeof lc_ptr,     "icn_sq%d_lptr",  id);
    char rc_ptr[64];      snprintf(rc_ptr,      sizeof rc_ptr,     "icn_sq%d_rptr",  id);
    bss_declare(lc_ptr); bss_declare(rc_ptr);
    strncpy(oa, a, 63); strncpy(ob, b, 63);
    E(em, "    ; SEQ  id=%d\n", id);
    IcnPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; emit_expr(em, n->children[1], rp, ra, rb);
    IcnPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb2[64]; emit_expr(em, n->children[0], lp, la, lb2);
    int lstr=(n->children[0]->kind==ICN_STR||n->children[0]->kind==ICN_CSET);
    int rstr=(n->children[1]->kind==ICN_STR||n->children[1]->kind==ICN_CSET);
    Ldef(em,left_relay);
    if(lstr){E(em,"    mov     [rel %s], rdi\n",lc_ptr);}
    else    {E(em,"    pop     rax\n");E(em,"    mov     [rel %s], rax\n",lc_ptr);}
    Jmp(em,lstore);
    Ldef(em,right_relay);
    if(rstr){E(em,"    mov     [rel %s], rdi\n",rc_ptr);}
    else    {E(em,"    pop     rax\n");E(em,"    mov     [rel %s], rax\n",rc_ptr);}
    Jmp(em,chk);
    Ldef(em,lbfwd);  Jmp(em,lb2);
    Ldef(em,a);      Jmp(em,la);
    Ldef(em,b);      Jmp(em,rb);
    Ldef(em,lstore); Jmp(em,ra);
    Ldef(em,chk);
    E(em,"    mov     rdi, [rel %s]\n",lc_ptr);
    E(em,"    mov     rsi, [rel %s]\n",rc_ptr);
    E(em,"    call    icn_str_eq\n");
    E(em,"    test    rax, rax\n");
    E(em,"    jz      %s\n",rb);
    E(em,"    push    0\n");
    Jmp(em,ports.γ);
}

static void emit_concat(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    char compute[64];     snprintf(compute,     sizeof compute,     "icon_%d_compute", id);
    char lbfwd[64];       snprintf(lbfwd,       sizeof lbfwd,       "icon_%d_lb",      id);
    char lstore[64];      snprintf(lstore,      sizeof lstore,      "icon_%d_lstore",  id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icon_%d_lrelay",  id);
    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icon_%d_rrelay",  id);
    char lc_ptr[64];      snprintf(lc_ptr,      sizeof lc_ptr,      "icn_cc%d_lptr",   id);
    char rc_ptr[64];      snprintf(rc_ptr,      sizeof rc_ptr,      "icn_cc%d_rptr",   id);
    bss_declare(lc_ptr);
    bss_declare(rc_ptr);
    int bf_slot = locals_alloc_tmp();
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    E(em, "    ; CONCAT  id=%d\n", id);

    IcnPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64];
    emit_expr(em, n->children[1], rp, ra, rb);

    IcnPorts lp; strncpy(lp.γ, left_relay, 63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64];
    emit_expr(em, n->children[0], lp, la, lb);

    IcnNode *lch = n->children[0];
    IcnNode *rch = n->children[1];
    int left_is_value = (lch->kind == ICN_VAR || lch->kind == ICN_STR || lch->kind == ICN_CALL);
    int left_is_str   = (lch->kind == ICN_STR);
    int right_is_str  = (rch->kind == ICN_STR);

    /* left_relay: left succeeded → normalise to pointer in lc_ptr */
    Ldef(em, left_relay);
    if (left_is_str) {
        /* emit_str put pointer in rdi, nothing on hw stack */
        E(em, "    mov     [rel %s], rdi\n", lc_ptr);
    } else {
        /* emit_var/emit_call pushed value onto hw stack */
        E(em, "    pop     rax\n");
        E(em, "    mov     [rel %s], rax\n", lc_ptr);
    }
    Jmp(em, lstore);

    /* right_relay: right succeeded → normalise to pointer in rc_ptr */
    Ldef(em, right_relay);
    if (right_is_str) {
        E(em, "    mov     [rel %s], rdi\n", rc_ptr);
    } else {
        E(em, "    pop     rax\n");
        E(em, "    mov     [rel %s], rax\n", rc_ptr);
    }
    Jmp(em, compute);

    /* lbfwd: right exhausted → retry left */
    Ldef(em, lbfwd); Jmp(em, lb);

    /* node α: fresh start */
    Ldef(em, a);
    E(em, "    mov     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    Jmp(em, la);

    /* node β: resume */
    Ldef(em, b);
    if (left_is_value) {
        E(em, "    mov     qword [rbp%+d], 1\n", slot_offset(bf_slot));
        Jmp(em, la);
    } else {
        Jmp(em, rb);
    }

    /* lstore: left cached → start (α) or resume (β) right */
    Ldef(em, lstore);
    E(em, "    cmp     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    E(em, "    je      %s\n", ra);
    Jmp(em, rb);

    /* compute: call icn_str_concat(lc_ptr, rc_ptr) → push result char* */
    Ldef(em, compute);
    E(em, "    mov     rdi, [rel %s]\n", lc_ptr);
    E(em, "    mov     rsi, [rel %s]\n", rc_ptr);
    E(em, "    call    icn_str_concat\n");
    E(em, "    push    rax\n");
    Jmp(em, ports.γ);
}

/* =========================================================================
 * ICN_SCAN — E ? body
 *
 * Byrd-box wiring (following JVM ij_emit_scan):
 *   α → expr.α
 *   expr.γ (new subject — char* on hw stack or rdi for ICN_STR):
 *     save old icn_subject → old_subj_N
 *     save old icn_pos    → old_pos_N
 *     install new subject into icn_subject, reset icn_pos=0
 *     → body.α
 *   body.γ → restore old subject/pos → ports.γ
 *   body.ω → restore old subject/pos → expr.β → ports.ω (one-shot)
 *   β → restore → body.β
 *
 * &subject keyword is handled in emit_var: loads icn_subject.
 * ======================================================================= */
static void emit_scan(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    /* Global subject/pos */
    bss_declare("icn_subject");
    bss_declare("icn_pos");

    /* Per-scan save slots */
    char old_subj[64], old_pos[64];
    snprintf(old_subj, sizeof old_subj, "icn_scan_oldsubj_%d", id);
    snprintf(old_pos,  sizeof old_pos,  "icn_scan_oldpos_%d",  id);
    bss_declare(old_subj);
    bss_declare(old_pos);

    char setup[64], body_ok[64], body_fail[64], beta_restore[64];
    snprintf(setup,        sizeof setup,        "icon_%d_scan_setup", id);
    snprintf(body_ok,      sizeof body_ok,      "icon_%d_scan_bok",   id);
    snprintf(body_fail,    sizeof body_fail,     "icon_%d_scan_bfail", id);
    snprintf(beta_restore, sizeof beta_restore,  "icon_%d_scan_bret",  id);

    IcnNode *expr_node = (n->nchildren >= 1) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren >= 2) ? n->children[1] : NULL;

    /* Wire expr: γ → setup, ω → ports.ω */
    IcnPorts ep; strncpy(ep.γ, setup, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64];
    emit_expr(em, expr_node, ep, ea, eb);

    /* Wire body: γ → body_ok, ω → body_fail */
    IcnPorts bp; strncpy(bp.γ, body_ok, 63); strncpy(bp.ω, body_fail, 63);
    char ba[64], bb[64];
    if (body_node)
        emit_expr(em, body_node, bp, ba, bb);
    else {
        strncpy(ba, body_ok, 63); strncpy(bb, body_fail, 63);
    }

    int expr_is_str = (expr_node && expr_node->kind == ICN_STR);

    /* α → expr.α */
    E(em, "    ; SCAN  id=%d\n", id);
    Ldef(em, a); Jmp(em, ea);

    /* β → restore + body.β */
    Ldef(em, b); Jmp(em, beta_restore);

    /* setup: expr succeeded — new subject pointer in rdi (ICN_STR) or on hw stack */
    Ldef(em, setup);
    if (expr_is_str) {
        E(em, "    ; scan setup: subject in rdi\n");
        /* save old subject */
        E(em, "    mov     rax, [rel icn_subject]\n");
        E(em, "    mov     [rel %s], rax\n", old_subj);
        /* save old pos */
        E(em, "    mov     rax, [rel icn_pos]\n");
        E(em, "    mov     [rel %s], rax\n", old_pos);
        /* install new subject from rdi */
        E(em, "    mov     [rel icn_subject], rdi\n");
    } else {
        /* new subject pointer on hw stack */
        E(em, "    pop     rax\n");
        /* save old subject */
        E(em, "    push    rax\n");   /* keep new on stack while saving old */
        E(em, "    mov     rcx, [rel icn_subject]\n");
        E(em, "    mov     [rel %s], rcx\n", old_subj);
        /* save old pos */
        E(em, "    mov     rcx, [rel icn_pos]\n");
        E(em, "    mov     [rel %s], rcx\n", old_pos);
        /* install new subject */
        E(em, "    pop     rax\n");
        E(em, "    mov     [rel icn_subject], rax\n");
    }
    /* reset pos to 0 */
    E(em, "    mov     qword [rel icn_pos], 0\n");
    Jmp(em, ba);

    /* body.γ — success: restore, → ports.γ */
    Ldef(em, body_ok);
    E(em, "    mov     rax, [rel %s]\n", old_subj);
    E(em, "    mov     [rel icn_subject], rax\n");
    E(em, "    mov     rax, [rel %s]\n", old_pos);
    E(em, "    mov     [rel icn_pos], rax\n");
    Jmp(em, ports.γ);

    /* body.ω — fail: restore, → expr.β → ports.ω */
    Ldef(em, body_fail);
    E(em, "    mov     rax, [rel %s]\n", old_subj);
    E(em, "    mov     [rel icn_subject], rax\n");
    E(em, "    mov     rax, [rel %s]\n", old_pos);
    E(em, "    mov     [rel icn_pos], rax\n");
    Jmp(em, eb);

    /* beta_restore: node β — restore then body.β */
    Ldef(em, beta_restore);
    E(em, "    mov     rax, [rel %s]\n", old_subj);
    E(em, "    mov     [rel icn_subject], rax\n");
    E(em, "    mov     rax, [rel %s]\n", old_pos);
    E(em, "    mov     [rel icn_pos], rax\n");
    Jmp(em, bb);
}

/* =========================================================================
 * Binary arithmetic — funcs-set wiring (§4.3)
 * Frame slot lc_slot: left cache (per-invocation, prevents recursive clobber).
 * BSS bflag: 0=α path (start right from α), 1=β path (resume right from β).
 *   α: bflag=0, run left → lstore → cache → ra
 *   β: bflag=1, run left → lstore → cache → rb
 * right.ω → lbfwd → left.β → binop fails
 * ======================================================================= */
static void emit_binop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],compute[64],lbfwd[64],lstore[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(compute,sizeof compute,"icon_%d_compute",id);
    snprintf(lbfwd,  sizeof lbfwd,  "icon_%d_lb",id);
    snprintf(lstore, sizeof lstore, "icon_%d_lstore",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==ICN_ADD?"ADD":n->kind==ICN_SUB?"SUB":n->kind==ICN_MUL?"MUL":n->kind==ICN_DIV?"DIV":"MOD";
    E(em,"    ; %s  id=%d\n",op,id);

    int lc_slot = locals_alloc_tmp();
    int bf_slot = locals_alloc_tmp();

    IcnPorts rp; strncpy(rp.γ,compute,63); strncpy(rp.ω,lbfwd,63);
    char ra[64],rb[64]; emit_expr(em,n->children[1],rp,ra,rb);

    IcnPorts lp; strncpy(lp.γ,lstore,63); strncpy(lp.ω,ports.ω,63);
    char la[64],lb[64]; emit_expr(em,n->children[0],lp,la,lb);

    /* Heuristic: if left is a simple value (var/int/str/call), β must re-eval
     * left to refresh it (e.g. updated `total`). If left is a generator
     * (TO, binop, relop, etc.), β goes directly to right.β — left cache
     * is still valid and re-running left would reset the generator. */
    IcnNode *left_child = n->children[0];
    int left_is_value = (left_child->kind == ICN_VAR || left_child->kind == ICN_INT ||
                         left_child->kind == ICN_STR || left_child->kind == ICN_CALL);

    Ldef(em,lbfwd); Jmp(em,lb);
    Ldef(em,a);
    E(em,"    mov     qword [rbp%+d], 0\n", slot_offset(bf_slot));  /* α: start right */
    Jmp(em,la);
    Ldef(em,b);
    if (left_is_value) {
        /* value-left: β re-evals left to refresh (e.g. accumulated total) */
        E(em,"    mov     qword [rbp%+d], 1\n", slot_offset(bf_slot));
        Jmp(em,la);
    } else {
        /* generator-left: β goes directly to right.β — left cache still valid */
        Jmp(em,rb);
    }

    /* lstore: cache left, branch on bf_slot → start right (α) or resume right (β) */
    Ldef(em,lstore);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(lc_slot));
    E(em,"    cmp     qword [rbp%+d], 0\n", slot_offset(bf_slot));
    E(em,"    je      %s\n", ra);
    Jmp(em,rb);

    Ldef(em,compute);
    E(em,"    pop     rax\n");
    E(em,"    mov     rcx, [rbp%+d]\n", slot_offset(lc_slot));
    switch(n->kind){
        case ICN_ADD: E(em,"    add     rcx, rax\n"); break;
        case ICN_SUB: E(em,"    sub     rcx, rax\n"); break;
        case ICN_MUL: E(em,"    imul    rcx, rax\n"); break;
        case ICN_DIV: E(em,"    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rax\n"); break;
        case ICN_MOD: E(em,"    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rdx\n"); break;
        default: break;
    }
    E(em,"    push    rcx\n");
    Jmp(em,ports.γ);
}

/* =========================================================================
 * Relational operators — goal-directed retry
 * Same left-cache pattern as binop.
 * On comparison failure → right.β (retry right, left still in lcache).
 * On right exhausted → left.β (retry left, re-stores lcache).
 * ======================================================================= */
static void emit_relop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],chk[64],lbfwd[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(chk,       sizeof chk,       "icon_%d_check",id);
    snprintf(lbfwd,     sizeof lbfwd,     "icon_%d_lb",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==ICN_LT?"LT":n->kind==ICN_LE?"LE":n->kind==ICN_GT?"GT":n->kind==ICN_GE?"GE":n->kind==ICN_EQ?"EQ":"NE";
    E(em,"    ; %s  id=%d\n",op,id);

    char lcache_store[64]; snprintf(lcache_store,sizeof lcache_store,"icon_%d_lstore",id);
    int lc_slot = locals_alloc_tmp();

    IcnPorts rp; strncpy(rp.γ,chk,63); strncpy(rp.ω,lbfwd,63);
    char ra[64],rb[64]; emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.γ,lcache_store,63); strncpy(lp.ω,ports.ω,63);
    char la[64],lb[64]; emit_expr(em,n->children[0],lp,la,lb);

    Ldef(em,lbfwd); Jmp(em,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);

    Ldef(em,lcache_store);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(lc_slot));
    Jmp(em,ra);

    Ldef(em,chk);
    E(em,"    pop     rcx\n");                                    /* right value */
    E(em,"    mov     rax, [rbp%+d]\n", slot_offset(lc_slot));   /* left from frame */
    E(em,"    cmp     rax, rcx\n");
    const char *jfail=n->kind==ICN_LT?"jge":n->kind==ICN_LE?"jg":n->kind==ICN_GT?"jle":n->kind==ICN_GE?"jl":n->kind==ICN_EQ?"jne":"je";
    E(em,"    %s      %s\n",jfail,rb);
    E(em,"    push    rcx\n");
    Jmp(em,ports.γ);
}

/* =========================================================================
 * ICN_TO — range generator inline counter (§4.4)
 * E1 and E2 each push their value onto the stack.
 * init: pop E2 value → BSS e2_bound; pop E1 value → BSS I (counter start)
 * β:   inc I, re-check
 * BSS I and e2_bound are per-TO-node but NOT per-invocation — safe here
 * because TO is only used inside `every` (flat, non-recursive context).
 * ======================================================================= */
static void emit_to(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                    char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    icn_lbl_code(id,code,sizeof code);
    snprintf(init,sizeof init,"icon_%d_init",id);
    snprintf(e1bf,sizeof e1bf,"icon_%d_e1b",id);
    snprintf(e2bf,sizeof e2bf,"icon_%d_e2b",id);
    char I[64],bound[64]; label_I(id,I,sizeof I);
    snprintf(bound,sizeof bound,"icon_%d_bound",id);
    bss_declare(I); bss_declare(bound);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; TO  id=%d\n",id);

    /* e1cur: current E1 value (updated each time E1 succeeds).
     * e2_seen: 0 on first init (E1+E2 both on stack), 1 thereafter (only E2). */
    char e1cur[64];  snprintf(e1cur,  sizeof e1cur,  "icon_%d_e1cur",id);
    char e2seen[64]; snprintf(e2seen, sizeof e2seen, "icon_%d_e2seen",id);
    bss_declare(e1cur); bss_declare(e2seen);

    IcnPorts e2p; strncpy(e2p.γ,init,63); strncpy(e2p.ω,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(em,n->children[1],e2p,e2a,e2b);
    IcnPorts e1p; strncpy(e1p.γ,e2a,63); strncpy(e1p.ω,ports.ω,63);
    char e1a[64],e1b[64]; emit_expr(em,n->children[0],e1p,e1a,e1b);

    /* e1bf: E1 advancing → reset e2_seen so next init pops both from stack */
    Ldef(em,e1bf);
    E(em,"    mov     qword [rel %s], 0\n",e2seen);
    Jmp(em,e1b);
    Ldef(em,e2bf); Jmp(em,e2b);
    Ldef(em,a);
    E(em,"    mov     qword [rel %s], 0\n",e2seen);   /* α: mark fresh start */
    Jmp(em,e1a);
    Ldef(em,b); E(em,"    inc     qword [rel %s]\n",I); Jmp(em,code);

    /* init: E2 just pushed its value on top of stack.
     * e2_seen==0: E1 also on stack (below) — first time.
     * e2_seen==1: only E2 on stack — E2 advanced, reset I to current E1 value. */
    Ldef(em,init);
    E(em,"    pop     rax\n");                          /* E2 value (always on top) */
    E(em,"    mov     [rel %s], rax\n",bound);
    E(em,"    cmp     qword [rel %s], 0\n",e2seen);
    E(em,"    jne     %s_e2adv\n",init);
    /* first time: pop E1 value, save as e1cur, set I */
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel %s], rax\n",e1cur);
    E(em,"    mov     [rel %s], rax\n",I);
    E(em,"    mov     qword [rel %s], 1\n",e2seen);
    Jmp(em,code);
    /* E2 advanced: reset I to current E1 value (not first E1 value) */
    E(em,"%s_e2adv:\n",init);
    E(em,"    mov     rax, [rel %s]\n",e1cur);
    E(em,"    mov     [rel %s], rax\n",I);
    Jmp(em,code);

    /* When E1 advances (e1b fires after e2 exhausts), update e1cur and reset e2_seen */
    /* We intercept by patching e1b's flow: e1b → e1cur_update → original e1b path.
     * But e1b is already emitted inside emit_expr. Instead, emit a wrapper:
     * e1bf already jumps to e1b. When E1 succeeds again (e1p.γ = e2a),
     * E1 pushes new value and jumps to e2a (E2 start). E2's succeed = init.
     * At that point e2_seen=1 → e2adv path which uses e1cur.
     * But e1cur still has the OLD E1 value! We need to update e1cur when E1 succeeds.
     * Solution: intercept E1's succeed: e1p.γ → e1_capture → e2a */
    /* NOTE: e1p was already emitted above with succeed=e2a. We can't change that.
     * Instead: e2_seen reset to 0 when E1 advances (e1bf fires).
     * That way next init will pop E1 from stack and update e1cur correctly. */

    Ldef(em,code);
    E(em,"    mov     rax, [rel %s]\n",I);
    E(em,"    cmp     rax, [rel %s]\n",bound);
    E(em,"    jg      %s\n",e2bf);
    E(em,"    push    rax\n");              /* push current counter value */
    Jmp(em,ports.γ);
}

/* =========================================================================
 * ICN_TO_BY
 * ======================================================================= */
static void emit_to_by(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    icn_lbl_code(id,code,sizeof code);
    snprintf(init, sizeof init, "icon_%d_init", id);
    snprintf(e1bf, sizeof e1bf, "icon_%d_e1b",  id);
    snprintf(e2bf, sizeof e2bf, "icon_%d_e2b",  id);
    /* BSS slots for I (counter), bound, step */
    char I[64], bound[64], step[64];
    label_I(id,I,sizeof I);
    snprintf(bound, sizeof bound, "icon_%d_bound", id);
    snprintf(step,  sizeof step,  "icon_%d_step",  id);
    bss_declare(I); bss_declare(bound); bss_declare(step);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; TO_BY  id=%d\n",id);

    /* Wire E3 (step): succeed → init, fail → e2bf */
    char step_relay[64]; snprintf(step_relay,sizeof step_relay,"icon_%d_stepr",id);
    IcnPorts e3p; strncpy(e3p.γ,step_relay,63); strncpy(e3p.ω,e2bf,63);
    char e3a[64],e3b[64]; emit_expr(em,n->children[2],e3p,e3a,e3b);

    /* Wire E2 (bound): succeed → e3a, fail → e1bf */
    char bound_relay[64]; snprintf(bound_relay,sizeof bound_relay,"icon_%d_boundr",id);
    IcnPorts e2p; strncpy(e2p.γ,bound_relay,63); strncpy(e2p.ω,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(em,n->children[1],e2p,e2a,e2b);

    /* Wire E1 (start): succeed → e2a, fail → ports.ω */
    char start_relay[64]; snprintf(start_relay,sizeof start_relay,"icon_%d_startr",id);
    IcnPorts e1p; strncpy(e1p.γ,start_relay,63); strncpy(e1p.ω,ports.ω,63);
    char e1a[64],e1b[64]; emit_expr(em,n->children[0],e1p,e1a,e1b);

    /* Relay: E1 pushed start → pop into I */
    Ldef(em,start_relay);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel %s], rax\n", I);
    Jmp(em,e2a);

    /* Relay: E2 pushed bound → pop into bound slot */
    Ldef(em,bound_relay);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel %s], rax\n", bound);
    Jmp(em,e3a);

    /* Relay: E3 pushed step → pop into step slot → proceed to code */
    Ldef(em,step_relay);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel %s], rax\n", step);
    Jmp(em,code);

    Ldef(em,e1bf); Jmp(em,e1b);
    Ldef(em,e2bf); Jmp(em,e2b);

    /* α: start from E1 */
    Ldef(em,a); Jmp(em,e1a);

    /* β: advance I by step, re-check */
    Ldef(em,b);
    E(em,"    mov     rax, [rel %s]\n", step);
    E(em,"    add     [rel %s], rax\n", I);
    Jmp(em,code);

    /* init label kept for compatibility — not used in this design */
    Ldef(em,init); Jmp(em,code);

    /* code: check I <= bound, push I, succeed */
    Ldef(em,code);
    E(em,"    mov     rax, [rel %s]\n", I);
    E(em,"    cmp     rax, [rel %s]\n", bound);
    E(em,"    jg      %s\n", e2bf);
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
}

/* =========================================================================
 * ICN_WHILE — while cond do body
 * α → cond.α
 * cond.γ → body.α  (discard cond value)
 * cond.ω → while.ω  (condition failed → loop done)
 * body.γ → cond.α  (loop back regardless of body result)
 * body.ω → cond.α  (loop back)
 * ======================================================================= */
static void emit_while(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; WHILE  id=%d\n",id);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren>1) ? n->children[1] : NULL;

    char cond_ok[64]; snprintf(cond_ok,sizeof cond_ok,"icon_%d_condok",id);
    char loop_top[64]; snprintf(loop_top,sizeof loop_top,"icon_%d_top",id);

    char ca[64],cb[64];
    IcnPorts cp; strncpy(cp.γ,cond_ok,63); strncpy(cp.ω,ports.ω,63);
    emit_expr(em,cond,cp,ca,cb);

    /* cond_ok: condition succeeded, value on stack — discard it, run body */
    Ldef(em,cond_ok);
    E(em,"    add     rsp, 8\n");   /* discard condition result */

    if(body){
        char ba[64],bb[64];
        IcnPorts bp; strncpy(bp.γ,loop_top,63); strncpy(bp.ω,loop_top,63);
        loop_push(ports.ω, loop_top);
        emit_expr(em,body,bp,ba,bb);
        loop_pop();
        Jmp(em,ba);

        /* loop_top: go back to condition */
        Ldef(em,loop_top); Jmp(em,ca);
    } else {
        Jmp(em,ca);
    }

    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_UNTIL — until cond do body
 * α → cond.α
 * cond.γ → discard value → ports.ω  (cond succeeded → done)
 * cond.ω → body.α                   (cond failed → run body)
 * body.γ → cond.α  (loop back)
 * body.ω → cond.α  (loop back)
 * ======================================================================= */
static void emit_until(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; UNTIL  id=%d\n",id);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren>1) ? n->children[1] : NULL;

    char cond_ok[64];   snprintf(cond_ok,  sizeof cond_ok,  "icon_%d_condok", id);
    char cond_fail[64]; snprintf(cond_fail,sizeof cond_fail,"icon_%d_cfail",  id);
    char loop_top[64];  snprintf(loop_top, sizeof loop_top, "icon_%d_top",    id);

    char ca[64],cb[64];
    IcnPorts cp; strncpy(cp.γ,cond_ok,63); strncpy(cp.ω,cond_fail,63);
    emit_expr(em,cond,cp,ca,cb);

    /* cond succeeded → discard value, exit */
    Ldef(em,cond_ok);
    E(em,"    add     rsp, 8\n");   /* discard condition result */
    Jmp(em,ports.ω);

    /* cond failed → run body */
    Ldef(em,cond_fail);
    if(body){
        char ba[64],bb[64];
        IcnPorts bp; strncpy(bp.γ,loop_top,63); strncpy(bp.ω,loop_top,63);
        loop_push(ports.ω, loop_top);
        emit_expr(em,body,bp,ba,bb);
        loop_pop();
        Jmp(em,ba);

        Ldef(em,loop_top); Jmp(em,ca);
    } else {
        Jmp(em,ca);
    }

    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_EVERY
 * ======================================================================= */
static void emit_every(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_next_uid(em); char a[64],b[64],gbfwd[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(gbfwd,sizeof gbfwd,"icon_%d_genb",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; EVERY  id=%d\n",id);

    IcnNode *gen=n->children[0];
    IcnNode *body=(n->nchildren>1)?n->children[1]:NULL;
    char ga[64],gb[64];

    if(body){
        char bstart[64]; snprintf(bstart,sizeof bstart,"icon_%d_body",id);
        IcnPorts bp; strncpy(bp.γ,gbfwd,63); strncpy(bp.ω,gbfwd,63);
        loop_push(ports.ω, gbfwd);
        char ba[64],bb[64]; emit_expr(em,body,bp,ba,bb);
        loop_pop();
        IcnPorts gp; strncpy(gp.γ,bstart,63); strncpy(gp.ω,ports.ω,63);
        emit_expr(em,gen,gp,ga,gb);
        Ldef(em,bstart); Jmp(em,ba);
    } else {
        IcnPorts gp; strncpy(gp.γ,gbfwd,63); strncpy(gp.ω,ports.ω,63);
        emit_expr(em,gen,gp,ga,gb);
    }
    Ldef(em,gbfwd); Jmp(em,gb);
    Ldef(em,a); Jmp(em,ga);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * ICN_AUGOP — E1 op:= E2   (augmented assignment)
 *
 * Semantics: E1 := E1 op E2  (E1 must be ICN_VAR)
 *
 * Strategy: build a synthetic binop/relop/concat node (two children: E1-copy
 * and E2), emit it, then store the result back into E1.
 *
 * The subtype is in n->val.ival (TK_AUGxxx enum value).  Map to the
 * corresponding ICN_xxx kind, emit as that node, then wrap in an assign store.
 * ======================================================================= */
#include "icon_lex.h"   /* TK_AUGPLUS etc. — needed for the subtype map */
static void emit_augop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    if (n->nchildren < 2) { emit_fail_node(em, n, ports, oa, ob); return; }

    IcnNode *lhs = n->children[0];   /* must be ICN_VAR */
    IcnNode *rhs = n->children[1];

    /* Map TK_AUGxxx → ICN_xxx */
    IcnKind op_kind;
    switch ((IcnTkKind)n->val.ival) {
        case TK_AUGPLUS:   op_kind = ICN_ADD;    break;
        case TK_AUGMINUS:  op_kind = ICN_SUB;    break;
        case TK_AUGSTAR:   op_kind = ICN_MUL;    break;
        case TK_AUGSLASH:  op_kind = ICN_DIV;    break;
        case TK_AUGMOD:    op_kind = ICN_MOD;    break;
        case TK_AUGCONCAT: op_kind = ICN_CONCAT; break;
        case TK_AUGEQ:     op_kind = ICN_EQ;     break;
        case TK_AUGSEQ:    op_kind = ICN_SEQ;    break;
        case TK_AUGLT:     op_kind = ICN_LT;     break;
        case TK_AUGLE:     op_kind = ICN_LE;     break;
        case TK_AUGGT:     op_kind = ICN_GT;     break;
        case TK_AUGGE:     op_kind = ICN_GE;     break;
        case TK_AUGNE:     op_kind = ICN_NE;     break;
        default:
            /* Unhandled augop — fall back to fail */
            emit_fail_node(em, n, ports, oa, ob); return;
    }

    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id, a, sizeof a); icn_label_β(id, b, sizeof b);
    strncpy(oa, a, 63); strncpy(ob, b, 63);

    char store[64]; snprintf(store, sizeof store, "icon_%d_aug_store", id);

    /* Build a synthetic two-child node for the op on the stack.
     * We borrow lhs and rhs as children (no ownership transfer — they remain
     * owned by the parent ICN_AUGOP node). */
    IcnNode syn;
    memset(&syn, 0, sizeof syn);
    syn.kind      = op_kind;
    syn.nchildren = 2;
    IcnNode *ch[2] = { lhs, rhs };
    syn.children  = ch;

    /* Wire: op succeeds → store; op fails → ports.ω */
    IcnPorts op_ports;
    strncpy(op_ports.γ, store, 63);
    strncpy(op_ports.ω, ports.ω, 63);

    char opa[64], opb[64];
    emit_expr(em, &syn, op_ports, opa, opb);

    /* α → op.α;  β → op.β */
    Ldef(em, a); Jmp(em, opa);
    Ldef(em, b); Jmp(em, opb);

    /* store: result is on hw stack (all binop/relop/concat push a value).
     * Pop it and write back into LHS variable. */
    Ldef(em, store);
    if (op_kind == ICN_CONCAT) {
        /* concat pushes a char* — pop into rax, store as pointer */
        E(em, "    pop     rax\n");
    } else {
        E(em, "    pop     rax\n");
    }
    if (lhs && lhs->kind == ICN_VAR) {
        int slot = locals_find(lhs->val.sval);
        if (slot >= 0) {
            E(em, "    mov     [rbp%+d], rax\n", slot_offset(slot));
        } else {
            char gv[80]; snprintf(gv, sizeof gv, "icn_gvar_%s", lhs->val.sval);
            bss_declare(gv);
            E(em, "    mov     [rel %s], rax\n", gv);
        }
    }
    Jmp(em, ports.γ);
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
/* =========================================================================
 * G-9 gap-fill: Batch 1 — simple cases
 * ========================================================================= */

/* ICN_NONNULL — \E: succeed (keeping value) iff E succeeds (non-null check).
 * In our unboxed representation every value is "non-null"; just forward. */
static void emit_nonnull(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                         char *oa, char *ob) {
    IcnNode *child = n->nchildren > 0 ? n->children[0] : NULL;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    char ca[64], cb[64];
    IcnPorts cp; strncpy(cp.γ,ports.γ,63); strncpy(cp.ω,ports.ω,63);
    emit_expr(em, child, cp, ca, cb);
    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,cb);
}

/* ICN_REAL — floating-point literal: push truncated integer value.
 * For now: emit as integer (truncated). Full float support deferred. */
static void emit_real(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    long ival = (long)n->val.fval;
    Ldef(em,a);
    E(em,"    push    %ld\n", ival);
    Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* ICN_SIZE — *E: size of string or list. Calls icn_strlen(ptr). */
static void emit_size(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], relay[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(relay,sizeof relay,"icon_%d_size_relay",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    IcnNode *child = n->nchildren > 0 ? n->children[0] : NULL;
    char ca[64], cb[64];
    IcnPorts cp; strncpy(cp.γ,relay,63); strncpy(cp.ω,ports.ω,63);
    emit_expr(em, child, cp, ca, cb);
    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,cb);
    Ldef(em,relay);
    /* value (str ptr or int) on stack — treat as pointer, call icn_strlen */
    E(em,"    pop     rdi\n");
    E(em,"    call    icn_strlen\n");
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
}

/* ICN_POW — E1 ^ E2: integer exponentiation via icn_pow(base, exp). */
static void emit_pow(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], got_r[64], got_l[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_pow_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_pow_gl",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    IcnPorts rp; strncpy(rp.γ,got_r,63); strncpy(rp.ω,ports.ω,63);
    emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.γ,got_l,63); strncpy(lp.ω,ports.ω,63);
    emit_expr(em,n->children[0],lp,la,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);
    Ldef(em,got_r);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(em,la);
    Ldef(em,got_l);
    E(em,"    pop     rdi\n");                             /* base */
    E(em,"    mov     rsi, [rbp%+d]\n", slot_offset(rc_slot)); /* exp */
    E(em,"    call    icn_pow\n");
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
}

/* ICN_SEQ_EXPR — (E1; E2; ...; En): evaluate all, result is last value.
 * Each child's value is discarded except the last. */
static void emit_seq_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                          char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int nc = n->nchildren;
    if (nc == 0) { Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return; }
    /* Chain: child[i].γ → discard → child[i+1].α; child[i].ω → ports.ω */
    char *next_α = NULL;
    char **alphas = malloc(nc * sizeof(char*));
    for (int i = 0; i < nc; i++) { alphas[i] = malloc(64); alphas[i][0] = '\0'; }
    char chain_lbl[64];
    strncpy(chain_lbl, ports.γ, 63);  /* last child → ports.γ */
    /* Emit children in reverse so we know next α */
    char prev_α[64]; strncpy(prev_α, ports.γ, 63);
    for (int i = nc-1; i >= 0; i--) {
        char relay[64]; snprintf(relay,64,"icon_%d_seq_%d",id,i);
        IcnPorts cp;
        if (i == nc-1) { strncpy(cp.γ, ports.γ, 63); }
        else           { strncpy(cp.γ, relay, 63); }
        strncpy(cp.ω, ports.ω, 63);
        char ca[64], cb[64];
        emit_expr(em, n->children[i], cp, ca, cb);
        strncpy(alphas[i], ca, 63);
        /* relay: discard value, jump to next child's α */
        if (i < nc-1) {
            Ldef(em,relay);
            E(em,"    add     rsp, 8\n");  /* discard intermediate value */
            Jmp(em,alphas[i+1]);
        }
        (void)next_α; (void)prev_α;
    }
    Ldef(em,a); Jmp(em,alphas[0]);
    Ldef(em,b); Jmp(em,ports.ω);
    for (int i = 0; i < nc; i++) free(alphas[i]);
    free(alphas);
    (void)chain_lbl;
}

/* ICN_IDENTICAL — E1 === E2: succeed iff same value (ptr equality for strings,
 * integer equality for ints). Uses icn_str_eq for strings. */
static void emit_identical(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                           char *oa, char *ob) {
    if (n->nchildren < 2) {
        int id=icn_next_uid(em); char a[64],b[64];
        icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
        strncpy(oa,a,63); strncpy(ob,b,63);
        Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return;
    }
    int id = icn_next_uid(em); char a[64], b[64], got_r[64], got_l[64], chk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_id_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_id_gl",id);
    snprintf(chk,  sizeof chk,  "icon_%d_id_chk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    IcnPorts rp; strncpy(rp.γ,got_r,63); strncpy(rp.ω,ports.ω,63);
    emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.γ,got_l,63); strncpy(lp.ω,ports.ω,63);
    emit_expr(em,n->children[0],lp,la,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);
    Ldef(em,got_r);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(em,la);
    Ldef(em,got_l);
    /* Compare: both as integers (ptr or int — identical semantics) */
    E(em,"    pop     rax\n");
    E(em,"    cmp     rax, [rbp%+d]\n", slot_offset(rc_slot));
    E(em,"    jne     %s\n", ports.ω);
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
    (void)chk;
}

/* ICN_SWAP — a :=: b: swap two variables, result is new value of lhs. */
static void emit_swap(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    if (n->nchildren < 2 ||
        !n->children[0] || n->children[0]->kind != ICN_VAR ||
        !n->children[1] || n->children[1]->kind != ICN_VAR) {
        Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return;
    }
    IcnNode *lv = n->children[0];
    IcnNode *rv = n->children[1];
    int lslot = locals_find(lv->val.sval);
    int rslot = locals_find(rv->val.sval);
    Ldef(em,a);
    if (lslot >= 0 && rslot >= 0) {
        /* both locals — swap via tmp register */
        E(em,"    mov     rax, [rbp%+d]\n", slot_offset(lslot));
        E(em,"    mov     rcx, [rbp%+d]\n", slot_offset(rslot));
        E(em,"    mov     [rbp%+d], rcx\n", slot_offset(lslot));
        E(em,"    mov     [rbp%+d], rax\n", slot_offset(rslot));
        E(em,"    push    rcx\n");  /* new value of lhs (old rhs) */
    } else {
        /* global(s) — use BSS var_<name> pattern */
        char lbss[80], rbss[80];
        snprintf(lbss,sizeof lbss,"icn_var_%s",lv->val.sval);
        snprintf(rbss,sizeof rbss,"icn_var_%s",rv->val.sval);
        bss_declare(lbss); bss_declare(rbss);
        E(em,"    mov     rax, [rel %s]\n", lbss);
        E(em,"    mov     rcx, [rel %s]\n", rbss);
        E(em,"    mov     [rel %s], rcx\n", lbss);
        E(em,"    mov     [rel %s], rax\n", rbss);
        E(em,"    push    rcx\n");
    }
    Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* ICN_SGT/SGE/SLT/SLE/SNE — string relational operators.
 * Calls icn_str_cmp(a,b) then branches on result. */
static void emit_strrelop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                          char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], got_r[64], got_l[64], chk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_r,sizeof got_r,"icon_%d_sr_gr",id);
    snprintf(got_l,sizeof got_l,"icon_%d_sr_gl",id);
    snprintf(chk,  sizeof chk,  "icon_%d_sr_chk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int rc_slot = locals_alloc_tmp();
    char ra[64],rb[64],la[64],lb[64];
    IcnPorts rp; strncpy(rp.γ,got_r,63); strncpy(rp.ω,ports.ω,63);
    emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.γ,got_l,63); strncpy(lp.ω,ports.ω,63);
    emit_expr(em,n->children[0],lp,la,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);
    Ldef(em,got_r);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(rc_slot));
    Jmp(em,la);
    Ldef(em,got_l);
    E(em,"    pop     rdi\n");                              /* left (a) */
    E(em,"    mov     rsi, [rbp%+d]\n", slot_offset(rc_slot)); /* right (b) */
    E(em,"    call    icn_str_cmp\n");
    /* rax = cmp result: <0, 0, >0 */
    E(em,"    test    eax, eax\n");
    const char *jfail;
    switch(n->kind) {
        case ICN_SGT: jfail="jle"; break;
        case ICN_SGE: jfail="jl";  break;
        case ICN_SLT: jfail="jge"; break;
        case ICN_SLE: jfail="jg";  break;
        case ICN_SNE: jfail="je";  break;
        default:      jfail="jne"; break; /* ICN_SEQ (==) */
    }
    E(em,"    %s     %s\n", jfail, ports.ω);
    /* push right value as result (string ptr still valid) */
    E(em,"    push    qword [rbp%+d]\n", slot_offset(rc_slot));
    Jmp(em,ports.γ);
    (void)chk;
}

/* ICN_REPEAT — repeat { body }: infinite loop, only exits via break.
 * Uses cur_break_label (set by the loop context push/pop below).
 * α → body.α; body.γ → loop_top; body.ω → loop_top (retry).
 * break inside body jumps to ports.γ (or ports.ω — we use ports.γ). */

/* Loop control stack — parallel to loop nesting */
#define ICN_LOOP_MAX 32
static char icn_break_stack[ICN_LOOP_MAX][64];
static char icn_next_stack[ICN_LOOP_MAX][64];
static int  icn_loop_depth = 0;

static void loop_push(const char *brk, const char *nxt) {
    if (icn_loop_depth < ICN_LOOP_MAX) {
        strncpy(icn_break_stack[icn_loop_depth], brk, 63);
        strncpy(icn_next_stack[icn_loop_depth],  nxt, 63);
        icn_loop_depth++;
    }
}
static void loop_pop(void) { if (icn_loop_depth > 0) icn_loop_depth--; }
static const char *loop_break_target(void) {
    return icn_loop_depth > 0 ? icn_break_stack[icn_loop_depth-1] : "icn_loop_err";
}
static const char *loop_next_target(void) {
    return icn_loop_depth > 0 ? icn_next_stack[icn_loop_depth-1] : "icn_loop_err";
}

static void emit_repeat(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], top[64], brk[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(top,sizeof top,"icon_%d_rep_top",id);
    snprintf(brk,sizeof brk,"icon_%d_rep_brk",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    IcnNode *body = n->nchildren > 0 ? n->children[0] : NULL;
    loop_push(brk, top);
    char ba[64], bb[64];
    IcnPorts bp; strncpy(bp.γ,top,63); strncpy(bp.ω,top,63);
    if (body) emit_expr(em, body, bp, ba, bb);
    loop_pop();
    Ldef(em,a);
    Ldef(em,top);
    if (body) Jmp(em,ba); else Jmp(em,top);
    Ldef(em,b); Jmp(em,ports.ω);
    Ldef(em,brk); Jmp(em,ports.γ);
}

/* ICN_BREAK — exit enclosing loop */
static void emit_break_node(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                            char *oa, char *ob) {
    (void)n; (void)ports;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *brk = loop_break_target();
    Ldef(em,a); Jmp(em,brk);
    Ldef(em,b); Jmp(em,brk);
}

/* ICN_NEXT — next iteration of enclosing loop */
static void emit_next_node(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                           char *oa, char *ob) {
    (void)n; (void)ports;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *nxt = loop_next_target();
    Ldef(em,a); Jmp(em,nxt);
    Ldef(em,b); Jmp(em,nxt);
}

/* ICN_INITIAL — initial { body }: runs body exactly once on first entry.
 * Uses a BSS flag per INITIAL node. */
static void emit_initial(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                         char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], skip[64], flag[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(skip,sizeof skip,"icon_%d_init_skip",id);
    snprintf(flag,sizeof flag,"icn_init_flag_%d",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    bss_declare(flag);
    IcnNode *body = n->nchildren > 0 ? n->children[0] : NULL;
    char ba[64], bb[64];
    IcnPorts bp; strncpy(bp.γ,skip,63); strncpy(bp.ω,skip,63);
    if (body) emit_expr(em, body, bp, ba, bb);
    Ldef(em,a);
    E(em,"    cmp     qword [rel %s], 0\n", flag);
    E(em,"    jne     %s\n", skip);
    E(em,"    mov     qword [rel %s], 1\n", flag);
    if (body) Jmp(em,ba); else Jmp(em,skip);
    Ldef(em,skip); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* =========================================================================
 * G-9 Batch 2 — moderate cases: LIMIT, SUBSCRIPT, SECTION, MAKELIST stub,
 *               FIELD stub, RECORD stub, CASE, BANG (generator iteration)
 * ========================================================================= */

/* ICN_LIMIT — E \ n: limit generator E to at most n results.
 * Uses a counter in a frame slot. */
static void emit_limit(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], got_lim[64], got_val[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_lim,sizeof got_lim,"icon_%d_lim_gl",id);
    snprintf(got_val,sizeof got_val,"icon_%d_lim_gv",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int lim_slot = locals_alloc_tmp();  /* stores limit count */
    int cnt_slot = locals_alloc_tmp();  /* stores remaining count */
    /* child[0]=generator, child[1]=limit */
    IcnNode *gen = n->nchildren>0?n->children[0]:NULL;
    IcnNode *lim = n->nchildren>1?n->children[1]:NULL;
    char ga[64],gb[64],la[64],lb[64];
    IcnPorts gp; strncpy(gp.γ,got_val,63); strncpy(gp.ω,ports.ω,63);
    emit_expr(em,gen,gp,ga,gb);
    IcnPorts lp; strncpy(lp.γ,got_lim,63); strncpy(lp.ω,ports.ω,63);
    emit_expr(em,lim,lp,la,lb);
    /* α: eval limit once, store, set counter, start gen */
    Ldef(em,a);
    Jmp(em,la);
    Ldef(em,got_lim);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(lim_slot));
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(cnt_slot));
    Jmp(em,ga);
    /* got_val: gen produced a value; check counter */
    Ldef(em,got_val);
    E(em,"    dec     qword [rbp%+d]\n", slot_offset(cnt_slot));
    E(em,"    jl      %s\n", ports.ω);  /* exhausted */
    Jmp(em,ports.γ);
    /* β: resume gen if counter not exhausted */
    Ldef(em,b);
    E(em,"    cmp     qword [rbp%+d], 0\n", slot_offset(cnt_slot));
    E(em,"    jl      %s\n", ports.ω);
    Jmp(em,gb);
}

/* ICN_SUBSCRIPT — lst[i] or str[i]: return element. Simple 1-based index. */
static void emit_subscript(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                           char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64], got_idx[64], got_obj[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    snprintf(got_idx,sizeof got_idx,"icon_%d_sub_gi",id);
    snprintf(got_obj,sizeof got_obj,"icon_%d_sub_go",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    int idx_slot = locals_alloc_tmp();
    IcnNode *obj = n->nchildren>0?n->children[0]:NULL;
    IcnNode *idx = n->nchildren>1?n->children[1]:NULL;
    char ia[64],ib[64],oa2[64],ob2[64];
    IcnPorts ip; strncpy(ip.γ,got_idx,63); strncpy(ip.ω,ports.ω,63);
    emit_expr(em,idx,ip,ia,ib);
    IcnPorts op2; strncpy(op2.γ,got_obj,63); strncpy(op2.ω,ports.ω,63);
    emit_expr(em,obj,op2,oa2,ob2);
    Ldef(em,a); Jmp(em,oa2);
    Ldef(em,b); Jmp(em,ib);
    Ldef(em,got_idx);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(idx_slot));
    Jmp(em,oa2);
    Ldef(em,got_obj);
    /* obj is a string ptr on stack; idx is 1-based integer */
    E(em,"    pop     rdi\n");                                  /* string ptr */
    E(em,"    mov     rsi, [rbp%+d]\n", slot_offset(idx_slot)); /* 1-based idx */
    E(em,"    dec     rsi\n");                                   /* 0-based */
    E(em,"    call    icn_str_subscript\n");
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
    (void)oa2; (void)ob2;
}

/* ICN_SECTION — s[i:j]: substring (1-based Icon convention). */
static void emit_section(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                         char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    snprintf(a,sizeof a,"icn_%d_α",id); snprintf(b,sizeof b,"icn_%d_β",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    /* children: [obj, i, j] */
    IcnNode *obj = n->nchildren>0?n->children[0]:NULL;
    IcnNode *ifrom= n->nchildren>1?n->children[1]:NULL;
    IcnNode *ito  = n->nchildren>2?n->children[2]:NULL;
    int i_slot = locals_alloc_tmp();
    int j_slot = locals_alloc_tmp();
    char oa2[64],ob2[64],ia[64],ib[64],ja[64],jb[64];
    char got_i[64],got_j[64],got_obj[64];
    snprintf(got_i,  sizeof got_i,   "icon_%d_sec_gi",id);
    snprintf(got_j,  sizeof got_j,   "icon_%d_sec_gj",id);
    snprintf(got_obj,sizeof got_obj, "icon_%d_sec_go",id);
    IcnPorts ip; strncpy(ip.γ,got_i,63); strncpy(ip.ω,ports.ω,63);
    emit_expr(em,ifrom,ip,ia,ib);
    IcnPorts jp; strncpy(jp.γ,got_j,63); strncpy(jp.ω,ports.ω,63);
    emit_expr(em,ito,jp,ja,jb);
    IcnPorts op2; strncpy(op2.γ,got_obj,63); strncpy(op2.ω,ports.ω,63);
    emit_expr(em,obj,op2,oa2,ob2);
    Ldef(em,a); Jmp(em,oa2);
    Ldef(em,b); Jmp(em,ib);
    Ldef(em,got_i);
    E(em,"    pop     rax\n"); E(em,"    mov     [rbp%+d], rax\n", slot_offset(i_slot));
    Jmp(em,ja);
    Ldef(em,got_j);
    E(em,"    pop     rax\n"); E(em,"    mov     [rbp%+d], rax\n", slot_offset(j_slot));
    Jmp(em,oa2);
    Ldef(em,got_obj);
    /* call icn_str_section(ptr, i, j, kind) */
    E(em,"    pop     rdi\n");  /* str ptr */
    E(em,"    mov     rsi, [rbp%+d]\n", slot_offset(i_slot));
    E(em,"    mov     rdx, [rbp%+d]\n", slot_offset(j_slot));
    long kind = (n->kind==ICN_SECTION_PLUS)?1:(n->kind==ICN_SECTION_MINUS)?2:0;
    E(em,"    mov     rcx, %ld\n", kind);
    E(em,"    call    icn_str_section\n");
    E(em,"    push    rax\n");
    Jmp(em,ports.γ);
    (void)oa2; (void)ob2;
}

/* ICN_MAKELIST — [e1,...,en]: stub — push 0 (list support deferred). */
static void emit_makelist(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                          char *oa, char *ob) {
    (void)n;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); E(em,"    push    0\n"); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* ICN_RECORD — record(f1,...) construction: stub — push 0. */
static void emit_record(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    (void)n;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); E(em,"    push    0\n"); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* ICN_FIELD — r.field: stub — push 0. */
static void emit_field(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    (void)n;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); E(em,"    push    0\n"); Jmp(em,ports.γ);
    Ldef(em,b); Jmp(em,ports.ω);
}

/* ICN_CASE — case E of { k1: b1 ... default: bd }
 * children[0]=selector, children[1..n-1]=arm pairs or default.
 * Parser encodes: odd children = key exprs, even = body exprs, last = default.
 * Simple approach: eval selector, eval each key, compare, branch. */
static void emit_case(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    if (n->nchildren < 1) { Ldef(em,a); Jmp(em,ports.ω); Ldef(em,b); Jmp(em,ports.ω); return; }
    int sel_slot = locals_alloc_tmp();
    char got_sel[64]; snprintf(got_sel,sizeof got_sel,"icon_%d_case_sel",id);
    IcnNode *sel = n->children[0];
    char sa[64], sb[64];
    IcnPorts sp; strncpy(sp.γ,got_sel,63); strncpy(sp.ω,ports.ω,63);
    emit_expr(em,sel,sp,sa,sb);
    Ldef(em,a); Jmp(em,sa);
    Ldef(em,b); Jmp(em,sb);
    Ldef(em,got_sel);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rbp%+d], rax\n", slot_offset(sel_slot));
    /* Arms: children[1..] in pairs (key, body), optional trailing default */
    int narms = (n->nchildren - 1);
    int has_default = (narms % 2 == 1);
    int npairs = narms / 2;
    for (int i = 0; i < npairs; i++) {
        IcnNode *key  = n->children[1 + i*2];
        IcnNode *body = n->children[1 + i*2 + 1];
        char next_arm[64]; snprintf(next_arm,sizeof next_arm,"icon_%d_arm_%d",id,i);
        char got_key[64];  snprintf(got_key, sizeof got_key, "icon_%d_key_%d",id,i);
        char ka[64],kb[64];
        IcnPorts kp; strncpy(kp.γ,got_key,63); strncpy(kp.ω,next_arm,63);
        emit_expr(em,key,kp,ka,kb);
        Jmp(em,ka);
        Ldef(em,got_key);
        E(em,"    pop     rax\n");
        E(em,"    cmp     rax, [rbp%+d]\n", slot_offset(sel_slot));
        E(em,"    jne     %s\n", next_arm);
        char ba2[64],bb2[64];
        IcnPorts bp; strncpy(bp.γ,ports.γ,63); strncpy(bp.ω,ports.ω,63);
        emit_expr(em,body,bp,ba2,bb2);
        Jmp(em,ba2);
        Ldef(em,next_arm);
    }
    if (has_default) {
        IcnNode *def_body = n->children[n->nchildren-1];
        char da[64],db[64];
        IcnPorts dp; strncpy(dp.γ,ports.γ,63); strncpy(dp.ω,ports.ω,63);
        emit_expr(em,def_body,dp,da,db);
        Jmp(em,da);
    } else {
        Jmp(em,ports.ω);
    }
}

/* =========================================================================
 * ICN_BANG — !E: string character generator
 * Implemented: 2026-03-29, G-9 s14 (was stub; BACKLOG-BANG-X64)
 *
 * α: eval E once → store char* in BSS str_slot, reset pos=0, fall to check
 * β: resume at check (string already in BSS slot)
 * check: pos >= icn_strlen(str) → ω
 *        else: call icn_bang_char_at(str, pos) → rax (char*)
 *              push rax; pos++; → γ
 *
 * BSS: per-node icn_N_bang_str (8 bytes, char*) + icn_N_bang_pos (8 bytes)
 * Lists (ICN_LIST children): stub as fail — list runtime not yet in x64.
 * ======================================================================= */
static void emit_bang(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnNode *child = (n->nchildren > 0) ? n->children[0] : NULL;

    /* List bang: not yet implemented in x64 runtime — stub */
    if (child && child->kind == ICN_MAKELIST) {
        Ldef(em,a); Jmp(em,ports.ω);
        Ldef(em,b); Jmp(em,ports.ω);
        return;
    }

    char str_bss[64], pos_bss[64], check[64], after_str[64];
    snprintf(str_bss,  sizeof str_bss,  "icon_%d_bang_str", id);
    snprintf(pos_bss,  sizeof pos_bss,  "icon_%d_bang_pos", id);
    snprintf(check,    sizeof check,    "icon_%d_bang_chk", id);
    snprintf(after_str,sizeof after_str,"icon_%d_bang_as",  id);
    bss_declare(str_bss);   /* char* — 8 bytes */
    bss_declare(pos_bss);   /* int64 — 8 bytes */

    /* Emit child expression; success → after_str (char* on stack) */
    char ca[64], cb[64];
    IcnPorts cp; strncpy(cp.γ, after_str, 63); strncpy(cp.ω, ports.ω, 63);
    if (child) emit_expr(em, child, cp, ca, cb);
    else { snprintf(ca,64,"%s_noc",a); Ldef(em,ca); Jmp(em,ports.ω); }

    /* after_str: child produced char* on hw stack → store, reset pos */
    Ldef(em, after_str);
    E(em,"    pop     rax\n");
    E(em,"    mov     [rel %s], rax\n", str_bss);
    E(em,"    mov     qword [rel %s], 0\n", pos_bss);
    /* fall through to check */

    /* check: pos >= strlen(str) → ω; else char_at(str, pos) → push → γ */
    Ldef(em, check);
    E(em,"    mov     rdi, [rel %s]\n", str_bss);
    E(em,"    call    icn_strlen\n");          /* rax = length */
    E(em,"    cmp     [rel %s], rax\n", pos_bss);
    E(em,"    jge     %s\n", ports.ω);
    E(em,"    mov     rdi, [rel %s]\n", str_bss);
    E(em,"    mov     rsi, [rel %s]\n", pos_bss);
    E(em,"    call    icn_bang_char_at\n");    /* rax = char* */
    E(em,"    push    rax\n");
    E(em,"    inc     qword [rel %s]\n", pos_bss);
    Jmp(em, ports.γ);

    /* α: start from child eval */
    Ldef(em,a); Jmp(em,ca);
    /* β: resume — string still in BSS, just re-check */
    Ldef(em,b); Jmp(em,check);
}

/* =========================================================================
 * ICN_MATCH — =E  (scan: succeed if subject starts with E, advance &pos)
 * Implemented: 2026-03-29, G-9 s14 (was stub; BACKLOG-BANG-X64)
 *
 * One-shot: α evals E → call icn_match_pat(pat) → rax (new pos or -1)
 * On success: push matched substring → γ. β → ω.
 *
 * icn_match_pat uses/updates global icn_subject / icn_pos (icon_runtime.c).
 * ======================================================================= */
static void emit_match(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    char pat_bss[64], after_pat[64];
    snprintf(pat_bss,  sizeof pat_bss,  "icon_%d_match_pat", id);
    snprintf(after_pat,sizeof after_pat,"icon_%d_match_ap",  id);
    bss_declare(pat_bss);

    char ca[64], cb[64];
    IcnPorts cp; strncpy(cp.γ, after_pat, 63); strncpy(cp.ω, ports.ω, 63);
    if (n->nchildren > 0) emit_expr(em, n->children[0], cp, ca, cb);
    else { snprintf(ca,64,"%s_noc",a); Ldef(em,ca); Jmp(em,ports.ω); }

    Ldef(em, after_pat);
    E(em,"    pop     rdi\n");                 /* pattern char* */
    E(em,"    mov     [rel %s], rdi\n", pat_bss);
    E(em,"    call    icn_match_pat\n");       /* rax = new pos or -1 */
    E(em,"    cmp     rax, -1\n");
    E(em,"    je      %s\n", ports.ω);
    /* push matched portion (from old pos to new pos) — use icn_str_section */
    /* For now: push pat ptr as matched value (correct for literal patterns) */
    E(em,"    mov     rax, [rel %s]\n", pat_bss);
    E(em,"    push    rax\n");
    Jmp(em, ports.γ);

    Ldef(em,a); Jmp(em,ca);
    Ldef(em,b); Jmp(em,ports.ω);    /* one-shot */
}

/* ICN_BANG_BINARY — stub (binary list iteration, no x64 list runtime) */
static void emit_stub_fail(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                           char *oa, char *ob) {
    (void)n;
    int id = icn_next_uid(em); char a[64], b[64];
    icn_label_α(id,a,sizeof a); icn_label_β(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); Jmp(em,ports.ω);
    Ldef(em,b); Jmp(em,ports.ω);
}

static void emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    if(!n){ emit_fail_node(em,n,ports,oa,ob); return; }
    switch(n->kind){
        case ICN_INT:    emit_int      (em,n,ports,oa,ob); break;
        case ICN_STR:    emit_str      (em,n,ports,oa,ob); break;
        case ICN_CSET:   emit_cset     (em,n,ports,oa,ob); break;
        case ICN_VAR:    emit_var      (em,n,ports,oa,ob); break;
        case ICN_ASSIGN: emit_assign   (em,n,ports,oa,ob); break;
        case ICN_RETURN: emit_return   (em,n,ports,oa,ob); break;
        case ICN_SUSPEND:emit_suspend  (em,n,ports,oa,ob); break;
        case ICN_FAIL:   emit_fail_node(em,n,ports,oa,ob); break;
        case ICN_IF:     emit_if       (em,n,ports,oa,ob); break;
        case ICN_ALT:    emit_alt      (em,n,ports,oa,ob); break;
        case ICN_SCAN:   emit_scan     (em,n,ports,oa,ob); break;
        case ICN_NEG:    emit_neg      (em,n,ports,oa,ob); break;
        case ICN_NOT:    emit_not      (em,n,ports,oa,ob); break;
        case ICN_NULL:   emit_not      (em,n,ports,oa,ob); break;
        case ICN_SEQ:    emit_seq      (em,n,ports,oa,ob); break;
        case ICN_CONCAT: case ICN_LCONCAT:
                         emit_concat   (em,n,ports,oa,ob); break;
        case ICN_ADD: case ICN_SUB: case ICN_MUL: case ICN_DIV: case ICN_MOD:
                         emit_binop    (em,n,ports,oa,ob); break;
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
                         emit_relop    (em,n,ports,oa,ob); break;
        case ICN_TO:     emit_to       (em,n,ports,oa,ob); break;
        case ICN_TO_BY:  emit_to_by    (em,n,ports,oa,ob); break;
        case ICN_EVERY:  emit_every    (em,n,ports,oa,ob); break;
        case ICN_WHILE:  emit_while    (em,n,ports,oa,ob); break;
        case ICN_UNTIL:  emit_until    (em,n,ports,oa,ob); break;
        case ICN_CALL:   emit_call     (em,n,ports,oa,ob); break;
        case ICN_AUGOP:  emit_augop    (em,n,ports,oa,ob); break;
        /* G-9 gap-fill cases */
        case ICN_NONNULL:   emit_nonnull   (em,n,ports,oa,ob); break;
        case ICN_REAL:      emit_real      (em,n,ports,oa,ob); break;
        case ICN_SIZE:      emit_size      (em,n,ports,oa,ob); break;
        case ICN_POW:       emit_pow       (em,n,ports,oa,ob); break;
        case ICN_SEQ_EXPR:  emit_seq_expr  (em,n,ports,oa,ob); break;
        case ICN_IDENTICAL: emit_identical (em,n,ports,oa,ob); break;
        case ICN_SWAP:      emit_swap      (em,n,ports,oa,ob); break;
        case ICN_SGT: case ICN_SGE: case ICN_SLT:
        case ICN_SLE: case ICN_SNE:
                            emit_strrelop  (em,n,ports,oa,ob); break;
        case ICN_REPEAT:    emit_repeat    (em,n,ports,oa,ob); break;
        case ICN_BREAK:     emit_break_node(em,n,ports,oa,ob); break;
        case ICN_NEXT:      emit_next_node (em,n,ports,oa,ob); break;
        case ICN_INITIAL:   emit_initial   (em,n,ports,oa,ob); break;
        case ICN_LIMIT:     emit_limit     (em,n,ports,oa,ob); break;
        case ICN_SUBSCRIPT: emit_subscript (em,n,ports,oa,ob); break;
        case ICN_SECTION: case ICN_SECTION_PLUS: case ICN_SECTION_MINUS:
                            emit_section   (em,n,ports,oa,ob); break;
        case ICN_MAKELIST:  emit_makelist  (em,n,ports,oa,ob); break;
        case ICN_RECORD:    emit_record    (em,n,ports,oa,ob); break;
        case ICN_FIELD:     emit_field     (em,n,ports,oa,ob); break;
        case ICN_CASE:      emit_case      (em,n,ports,oa,ob); break;
        case ICN_BANG:      emit_bang      (em,n,ports,oa,ob); break;
        case ICN_BANG_BINARY: emit_stub_fail(em,n,ports,oa,ob); break;
        case ICN_MATCH:     emit_match     (em,n,ports,oa,ob); break;
        case ICN_AND: {
            /* n-ary conjunction: E1 & E2 & ... & En
             * irgen.icn ir_conjunction wiring:
             *   α → E1.α; E1.γ → E2.α; ...; En.γ → node.γ
             *   Ei.ω → E(i-1).β (backtrack left); E1.ω → node.ω
             *   β → En.β (resume rightmost)
             *
             * Fix: emit LEFT-TO-RIGHT so ccb[i-1] is already populated when
             * we wire Ei.ω.  Ei.γ needs E(i+1).α which isn't known yet, so
             * pre-generate a relay label for each child's γ; emit relay
             * trampolines (pop rax; jmp cca[i+1]) after all children. */
            int nc = n->nchildren;
            int cid = icn_next_uid(em); char ca2[64],cb2[64];
            icn_label_α(cid,ca2,sizeof ca2); icn_label_β(cid,cb2,sizeof cb2);
            strncpy(oa,ca2,63); strncpy(ob,cb2,63);

            char (*cca)[64]     = malloc(nc*64);
            char (*ccb)[64]     = malloc(nc*64);
            char (*relay_g)[64] = malloc(nc*64);
            for (int i = 0; i < nc; i++) {
                snprintf(relay_g[i], 64, "icon_%d_and_rg_%d", cid, i);
                cca[i][0] = '\0'; ccb[i][0] = '\0';
            }

            /* Emit children left-to-right */
            for (int i = 0; i < nc; i++) {
                IcnPorts ep;
                /* γ: last child → ports.γ; otherwise relay_g[i] trampoline */
                strncpy(ep.γ, (i == nc-1) ? ports.γ : relay_g[i], 63);
                /* ω: first child → ports.ω; others ccb[i-1] (already filled) */
                strncpy(ep.ω, (i == 0) ? ports.ω : ccb[i-1], 63);
                emit_expr(em, n->children[i], ep, cca[i], ccb[i]);
            }

            /* Relay trampolines: discard Ei's value then jump to E(i+1).α */
            Jmp(em, ca2);
            for (int i = 0; i < nc-1; i++) {
                Ldef(em, relay_g[i]);
                E(em, "    add     rsp, 8\n");   /* discard Ei result */
                Jmp(em, cca[i+1]);
            }
            Ldef(em,ca2); Jmp(em,cca[0]);
            Ldef(em,cb2); Jmp(em,ccb[nc-1]);
            free(cca); free(ccb); free(relay_g);
            break;
        }
        default:{
            int id=icn_next_uid(em); char a2[64],b2[64];
            icn_label_α(id,a2,sizeof a2); icn_label_β(id,b2,sizeof b2);
            strncpy(oa,a2,63); strncpy(ob,b2,63);
            E(em,"    ; UNIMPL %s id=%d\n",icn_kind_name(n->kind),id);
            Ldef(em,a2); Jmp(em,ports.ω);
            Ldef(em,b2); Jmp(em,ports.ω);
        }
    }
}

/* =========================================================================
 * icn_emit_file — full file emission
 * ======================================================================= */
void icn_emit_file(IcnEmitter *em, IcnNode **nodes, int count) {
    bss_count=0; rodata_count=0; str_counter=0; user_proc_count=0;

    /* Pass 1: register all user procs, detect generators */
    for(int pi=0;pi<count;pi++){
        IcnNode *proc=nodes[pi];
        if(!proc||proc->kind!=ICN_PROC||proc->nchildren<1) continue;
        const char *pname=proc->children[0]->val.sval;
        if(strcmp(pname,"main")==0) continue;
        int gen=0;
        int body_start_p=1+(int)proc->val.ival;
        for(int si=body_start_p;si<proc->nchildren;si++)
            if(has_suspend(proc->children[si])){ gen=1; break; }
        icn_register_proc(pname, (int)proc->val.ival, gen);
    }

    /* Emit to temp buffer */
    FILE *tmp=tmpfile(); FILE *real=em->out; em->out=tmp; em->uid=0;

    /* Declare globals needed by runtime */
    bss_declare("icn_retval");
    /* icn_failed is a byte — declare separately */

    for(int pi=0;pi<count;pi++){
        IcnNode *proc=nodes[pi];
        if(!proc||proc->kind!=ICN_PROC||proc->nchildren<1) continue;
        const char *pname=proc->children[0]->val.sval;
        int is_main=strcmp(pname,"main")==0;

        E(em,"\n; === procedure %s ===\n",pname);

        char proc_done[64]; snprintf(proc_done,sizeof proc_done,"icn_%s_done",pname);
        char proc_ret[64];  snprintf(proc_ret, sizeof proc_ret, "icn_%s_ret", pname);
        char proc_sret[64]; snprintf(proc_sret,sizeof proc_sret,"icn_%s_sret",pname);

        int is_gen = !is_main && icn_is_gen_proc(pname);
        char caller_ret_bss[80];
        if(is_gen) snprintf(caller_ret_bss,sizeof caller_ret_bss,"icn_%s_caller_ret",pname);

        /* Setup local env */
        locals_reset();
        strncpy(cur_ret_label, is_main?"icn_main_done":proc_ret, 63);
        strncpy(cur_fail_label, proc_done, 63);
        strncpy(cur_suspend_ret_label, is_main?"icn_main_done":proc_sret, 63);

        /* Register params as local slots 0..np-1 */
        int np = (int)proc->val.ival;
        cur_nparams = np;
        for (int pi = 0; pi < np; pi++) {
            IcnNode *pv = proc->children[1 + pi];
            if (pv && pv->kind == ICN_VAR)
                locals_add(pv->val.sval);
        }

        /* Scan for additional locals (ICN_GLOBAL nodes in body stmts) */
        int body_start = 1 + np;
        int nstmts = proc->nchildren - body_start;
        for (int si = 0; si < nstmts; si++) {
            IcnNode *s = proc->children[body_start + si];
            if (s && s->kind == ICN_GLOBAL) {
                for (int ci = 0; ci < s->nchildren; ci++) {
                    IcnNode *v = s->children[ci];
                    if (v && v->kind == ICN_VAR && locals_find(v->val.sval) < 0)
                        locals_add(v->val.sval);
                }
            }
        }
        /* Infer local var types from assignments (for write() dispatch) */
        infer_local_types(proc, body_start);

        /* Chain statements in reverse — skip ICN_GLOBAL (local decl) nodes */
        char **alphas=calloc(nstmts,sizeof(char*));
        for(int i=0;i<nstmts;i++) alphas[i]=malloc(64);
        char next_a[64]; strncpy(next_a,proc_done,63);

        for(int i=nstmts-1;i>=0;i--){
            IcnNode *stmt=proc->children[body_start+i];
            if(!stmt||stmt->kind==ICN_GLOBAL){ strncpy(alphas[i],next_a,63); continue; }
            IcnPorts sp; strncpy(sp.γ,next_a,63); strncpy(sp.ω,next_a,63);
            char sa[64],sb[64]; emit_expr(em,stmt,sp,sa,sb);
            strncpy(alphas[i],sa,63); strncpy(next_a,sa,63);
        }

        /* Frame size computed AFTER emit so locals_alloc_tmp() slots are counted */
        int frame_size=(cur_nlocals>0)?8*(cur_nlocals+1):0;
        if(frame_size%16!=0) frame_size=(frame_size+15)&~15;

        /* Emit proc entry */
        E(em,"icn_%s:\n",pname);
        E(em,"    push    rbp\n    mov     rbp, rsp\n");
        if(frame_size>0) E(em,"    sub     rsp, %d\n",frame_size);
        /* Pop params from icn_stack into frame slots.
         * Caller pushes args left-to-right; pop in reverse → slot 0=first param. */
        if(!is_main && np>0){
            for(int pi=np-1;pi>=0;pi--){
                E(em,"    call    icn_pop\n");
                E(em,"    mov     [rbp%+d], rax\n", slot_offset(pi));
            }
        }
        if(nstmts>0) Jmp(em,alphas[0]);

        /* Return label (for non-main) */
        if(!is_main){
            if(is_gen){
                /* Generator: jmp-based — frame stays live between suspend/resume */
                Ldef(em,proc_ret);
                if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
                E(em,"    pop     rbp\n");
                E(em,"    jmp     [rel %s]\n", caller_ret_bss);
                /* Suspend-yield: frame stays live, just jump back to caller */
                Ldef(em,proc_sret);
                E(em,"    jmp     [rel %s]\n", caller_ret_bss);
            } else {
                /* Normal proc: standard call/ret — safe for recursion */
                Ldef(em,proc_ret);
                if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
                E(em,"    pop     rbp\n    ret\n");
            }
        }
        /* proc_done: procedure fell off end or explicit fail — signal failure to caller */
        Ldef(em,proc_done);
        E(em,"    mov     byte [rel icn_failed], 1\n");
        if(is_gen){
            if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
            E(em,"    pop     rbp\n");
            E(em,"    jmp     [rel %s]\n", caller_ret_bss);
        } else if(!is_main){
            if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
            E(em,"    pop     rbp\n    ret\n");
        } else {
            if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
            E(em,"    pop     rbp\n    ret\n");
        }

        for(int i=0;i<nstmts;i++) free(alphas[i]);
        free(alphas);
    }

    /* Read temp body */
    long sz=ftell(tmp); rewind(tmp);
    char *body=malloc(sz+1); fread(body,1,sz,tmp); body[sz]='\0'; fclose(tmp);
    em->out=real;

    /* Emit file header */
    E(em,"; Auto-generated by icon_emit.c — Tiny-ICON Byrd Box x64\n");
    E(em,"bits 64\ndefault rel\n\n");

    if(rodata_count>0){
        E(em,"section .rodata\n");
        for(int i=0;i<rodata_count;i++){
            E(em,"%s:  db  ",rodata_entries[i].name);
            const char *s=rodata_entries[i].data;
            for(int j=0;s[j];j++){ if(j) E(em,", "); E(em,"%d",(unsigned char)s[j]); }
            E(em,", 0\n"); free(rodata_entries[i].data);
        }
        E(em,"\n");
    }

    E(em,"section .bss\n");
    for(int i=0;i<bss_count;i++){
        /* skip symbols owned by C runtime */
        if(strcmp(bss_entries[i].name,"icn_suspended")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_suspend_resume")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_subject")==0) continue;
        if(strcmp(bss_entries[i].name,"icn_pos")==0) continue;
        E(em,"    %s: resq 1\n",bss_entries[i].name);
    }
    /* Per-generator-proc caller_ret slots */
    for(int i=0;i<user_proc_count;i++){
        if(user_proc_is_gen[i])
            E(em,"    icn_%s_caller_ret: resq 1\n", user_procs[i]);
    }
    E(em,"    icn_failed: resb 1\n");
    E(em,"    icn_suspended: resb 1\n");
    E(em,"    icn_suspend_resume: resq 1\n");
    E(em,"    icn_suspend_rbp: resq 1\n\n");

    E(em,"section .text\n    global _start\n    extern icn_write_int\n    extern icn_write_str\n");
    E(em,"    extern icn_push\n    extern icn_pop\n    extern icn_str_concat\n    extern icn_str_eq\n");
    E(em,"    extern icn_any\n    extern icn_many\n    extern icn_upto\n");
    E(em,"    extern icn_str_find\n    extern icn_match\n    extern icn_tab\n    extern icn_move\n");
    E(em,"    extern icn_subject\n    extern icn_pos\n");
    E(em,"    extern icn_str_cmp\n    extern icn_strlen\n    extern icn_pow\n");
    E(em,"    extern icn_str_subscript\n    extern icn_str_section\n    extern icn_bang_char_at\n    extern icn_match_pat\n\n");
    E(em,"_start:\n    call    icn_main\n    mov     rax, 60\n    xor     rdi, rdi\n    syscall\n\n");

    fputs(body,em->out); free(body);
}

/* =========================================================================
 * Public API
 * ======================================================================= */
void icn_emit_init(IcnEmitter *em, FILE *out) { memset(em,0,sizeof(*em)); em->out=out; }
void icn_emit_proc(IcnEmitter *em, IcnNode *proc) { (void)em; (void)proc; }
void icn_emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports, char *oa, char *ob) {
    emit_expr(em,n,ports,oa,ob);
}
