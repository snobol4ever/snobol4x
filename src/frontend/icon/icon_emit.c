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
int  icn_new_id(IcnEmitter *em)                          { return em->node_id++; }
void icn_label_alpha(int id, char *b, size_t s)          { snprintf(b,s,"icon_%d_a",id); }
void icn_label_beta (int id, char *b, size_t s)          { snprintf(b,s,"icon_%d_b",id); }
void icn_label_code (int id, char *b, size_t s)          { snprintf(b,s,"icon_%d_code",id); }
static void label_val(int id, char *b, size_t s)         { snprintf(b,s,"icon_%d_val",id); }
static void label_I  (int id, char *b, size_t s)         { snprintf(b,s,"icon_%d_I",id); }

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
static int  user_proc_count=0;

static void register_user_proc(const char *name, int nparams) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return;
    if(user_proc_count<MAX_USER_PROCS){
        strncpy(user_procs[user_proc_count],name,63);
        user_proc_nparams[user_proc_count]=nparams;
        user_proc_count++;
    }
}
static int is_user_proc(const char *name) {
    for(int i=0;i<user_proc_count;i++) if(!strcmp(user_procs[i],name)) return 1;
    return 0;
}

/* =========================================================================
 * Per-procedure local variable table
 * ======================================================================= */
#define MAX_LOCALS 32
typedef struct { char name[32]; int slot; } LocalVar;
static LocalVar cur_locals[MAX_LOCALS];
static int      cur_nlocals=0, cur_nparams=0;
static char     cur_ret_label[64]="";   /* label to jump to for return */
static char     cur_fail_label[64]="";  /* label to jump to for fail */

static void locals_reset(void) { cur_nlocals=0; cur_nparams=0; }

static int locals_find(const char *name) {
    for(int i=0;i<cur_nlocals;i++)
        if(!strcmp(cur_locals[i].name,name)) return cur_locals[i].slot;
    return -1;
}
static int locals_add(const char *name) {
    int slot=cur_nlocals;
    if(cur_nlocals<MAX_LOCALS){ strncpy(cur_locals[cur_nlocals].name,name,31); cur_locals[cur_nlocals++].slot=slot; }
    return slot;
}
/* Frame offset for slot N: rbp - 8*(N+1) */
static int slot_offset(int slot) { return -8*(slot+1); }

/* =========================================================================
 * Forward declaration
 * ======================================================================= */
static void emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *out_alpha, char *out_beta);

/* =========================================================================
 * ICN_INT
 * ======================================================================= */
static void emit_int(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],v[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    label_val(id,v,sizeof v); bss_declare(v);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; INT %ld  id=%d\n",n->val.ival,id);
    Ldef(em,a); E(em,"    mov     qword [rel %s], %ld\n",v,n->val.ival); Jmp(em,ports.succeed);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * ICN_STR
 * ======================================================================= */
static void emit_str(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],sl[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    alloc_str_label(sl,sizeof sl); rodata_declare(sl,n->val.sval);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a); E(em,"    lea     rdi, [rel %s]\n",sl); Jmp(em,ports.succeed);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * ICN_VAR — variable reference
 * For local/param vars: load from frame slot → BSS val, succeed.
 * For globals/unknowns: emit as BSS slot (assigned elsewhere).
 * ======================================================================= */
static void emit_var(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],v[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    label_val(id,v,sizeof v); bss_declare(v);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; VAR %s  id=%d\n",n->val.sval,id);
    Ldef(em,a);
    int slot=locals_find(n->val.sval);
    if(slot>=0) {
        E(em,"    mov     rax, [rbp%+d]\n", slot_offset(slot));
        E(em,"    mov     [rel %s], rax\n", v);
    } else {
        /* Global BSS var */
        char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",n->val.sval);
        bss_declare(gv);
        E(em,"    mov     rax, [rel %s]\n", gv);
        E(em,"    mov     [rel %s], rax\n", v);
    }
    Jmp(em,ports.succeed);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * ICN_ASSIGN — E1 := E2
 * Evaluates E2, stores result into E1 (must be ICN_VAR).
 * ======================================================================= */
static void emit_assign(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    if(n->nchildren<2){ emit_expr(em,NULL,ports,oa,ob); return; }
    int id=icn_new_id(em); char a[64],b[64],store[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    snprintf(store,sizeof store,"icon_%d_store",id);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnPorts rhs_ports; strncpy(rhs_ports.succeed,store,63); strncpy(rhs_ports.fail,ports.fail,63);
    char ra[64],rb[64];
    emit_expr(em,n->children[1],rhs_ports,ra,rb);

    Ldef(em,a); Jmp(em,ra);
    Ldef(em,b); Jmp(em,rb);

    int rhs_id=-1; sscanf(ra,"icon_%d_a",&rhs_id);
    Ldef(em,store);
    if(rhs_id>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",rhs_id);

    IcnNode *lhs=n->children[0];
    if(lhs && lhs->kind==ICN_VAR){
        int slot=locals_find(lhs->val.sval);
        if(slot>=0){
            E(em,"    mov     [rbp%+d], rax\n",slot_offset(slot));
        } else {
            char gv[80]; snprintf(gv,sizeof gv,"icn_gvar_%s",lhs->val.sval);
            bss_declare(gv);
            E(em,"    mov     [rel %s], rax\n",gv);
        }
    }
    Jmp(em,ports.succeed);
}

/* =========================================================================
 * ICN_RETURN
 * Stores value into icn_retval, jumps to cur_ret_label.
 * ======================================================================= */
static void emit_return(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                        char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    (void)ports;

    if(n->nchildren>0){
        char after[64]; snprintf(after,sizeof after,"icon_%d_ret_store",id);
        IcnPorts vp; strncpy(vp.succeed,after,63); strncpy(vp.fail,after,63);
        char va2[64],vb2[64];
        emit_expr(em,n->children[0],vp,va2,vb2);
        Ldef(em,a); Jmp(em,va2);
        Ldef(em,b); Jmp(em,cur_ret_label[0]?cur_ret_label:"icn_dead");
        int vid=-1; sscanf(va2,"icon_%d_a",&vid);
        Ldef(em,after);
        if(vid>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",vid);
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
    int id=icn_new_id(em); char a[64],b[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);
    Ldef(em,a);
    if(cur_fail_label[0]){
        E(em,"    mov     byte [rel icn_failed], 1\n");
        Jmp(em,cur_fail_label);
    } else Jmp(em,ports.fail);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * ICN_IF — if cond then E2 [else E3]  (paper §4.5 indirect goto)
 * Simple version (no bounded optimization): emit cond, on succeed→E2, fail→E3/ports.fail
 * ======================================================================= */
static void emit_if(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                    char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnNode *cond=n->children[0];
    IcnNode *thenb=(n->nchildren>1)?n->children[1]:NULL;
    IcnNode *elseb=(n->nchildren>2)?n->children[2]:NULL;

    char then_a[64],then_b[64],else_a[64],else_b[64];
    char cond_then[64]; snprintf(cond_then,sizeof cond_then,"icon_%d_then",id);
    char cond_else[64]; snprintf(cond_else,sizeof cond_else,"icon_%d_else",id);

    if(thenb){
        IcnPorts tp; strncpy(tp.succeed,ports.succeed,63); strncpy(tp.fail,ports.fail,63);
        emit_expr(em,thenb,tp,then_a,then_b);
    } else { strncpy(then_a,ports.succeed,63); strncpy(then_b,ports.fail,63); }

    if(elseb){
        IcnPorts ep; strncpy(ep.succeed,ports.succeed,63); strncpy(ep.fail,ports.fail,63);
        emit_expr(em,elseb,ep,else_a,else_b);
    } else { strncpy(else_a,ports.fail,63); }

    IcnPorts cp; strncpy(cp.succeed,cond_then,63); strncpy(cp.fail,cond_else,63);
    char ca[64],cb[64];
    emit_expr(em,cond,cp,ca,cb);

    Ldef(em,cond_then); Jmp(em,thenb?then_a:ports.succeed);
    Ldef(em,cond_else); Jmp(em,elseb?else_a:ports.fail);

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
    int id=icn_new_id(em); char a[64],b[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    E(em,"    ; CALL %s  id=%d\n",fname,id);

    /* --- built-in write --- */
    if(strcmp(fname,"write")==0){
        if(nargs==0){
            Ldef(em,a); E(em,"    call    icn_write_str\n"); Jmp(em,ports.succeed);
            Ldef(em,b); Jmp(em,ports.fail); return;
        }
        IcnNode *arg=n->children[1];
        char after[64]; snprintf(after,sizeof after,"icon_%d_call",id);
        IcnPorts ap2; strncpy(ap2.succeed,after,63); strncpy(ap2.fail,ports.fail,63);
        char arg_a[64],arg_b[64];
        emit_expr(em,arg,ap2,arg_a,arg_b);
        Ldef(em,a); Jmp(em,arg_a);
        Ldef(em,b); Jmp(em,arg_b);
        Ldef(em,after);
        int arg_id=-1; sscanf(arg_a,"icon_%d_a",&arg_id);
        if(arg->kind==ICN_STR){
            /* rdi already points at string data (set by emit_str via lea) */
            E(em,"    call    icn_write_str\n");
        } else {
            if(arg_id>=0) E(em,"    mov     rdi, [rel icon_%d_val]\n",arg_id);
            E(em,"    call    icn_write_int\n");
        }
        Jmp(em,ports.succeed);
        return;
    }

    /* --- user procedure call --- */
    if(is_user_proc(fname)){
        /* Evaluate args left-to-right; push each onto icn_stack */
        /* Chain: arg0 → arg1 → ... → do_call */
        char do_call[64]; snprintf(do_call,sizeof do_call,"icon_%d_docall",id);
        char cur_succ[64]; strncpy(cur_succ,do_call,63);

        /* We need to emit args and push them. Use a sequential chain. */
        /* Emit all args, collect their α/β/val-id */
        char (*arg_alphas)[64] = nargs>0 ? malloc(nargs*64) : NULL;
        char (*arg_betas) [64] = nargs>0 ? malloc(nargs*64) : NULL;
        int  *arg_ids           = nargs>0 ? malloc(nargs*sizeof(int)) : NULL;

        /* Emit args in reverse order so we can chain succeed ports forward */
        /* Actually emit forward, each arg's succeed = push + next arg */
        /* Use a simpler approach: emit each arg with succeed=push_relay */
        char prev_succ[64]; strncpy(prev_succ,do_call,63);

        /* Emit args right-to-left so rightmost is pushed first (stack convention) */
        for(int i=nargs-1;i>=0;i--){
            char push_relay[64]; snprintf(push_relay,sizeof push_relay,"icon_%d_push%d",id,i);
            IcnPorts ap3; strncpy(ap3.succeed,push_relay,63); strncpy(ap3.fail,ports.fail,63);
            emit_expr(em,n->children[i+1],ap3,arg_alphas[i],arg_betas[i]);
            int aid=-1; sscanf(arg_alphas[i],"icon_%d_a",&aid);
            arg_ids[i]=aid;
            /* push relay: load val, push */
            Ldef(em,push_relay);
            if(aid>=0) E(em,"    mov     rdi, [rel icon_%d_val]\n",aid);
            E(em,"    call    icn_push\n");
            Jmp(em,prev_succ);
            strncpy(prev_succ,arg_alphas[i],63);
        }

        /* α: start first arg */
        Ldef(em,a);
        if(nargs>0) Jmp(em,prev_succ); /* prev_succ now = first arg's α */
        else Jmp(em,do_call);

        /* β: not resumable for now */
        Ldef(em,b); Jmp(em,ports.fail);

        /* do_call: args on stack, call proc */
        char after_call[64]; snprintf(after_call,sizeof after_call,"icon_%d_after",id);
        Ldef(em,do_call);
        E(em,"    call    icn_%s\n",fname);
        /* Check icn_failed */
        E(em,"    movzx   rax, byte [rel icn_failed]\n");
        E(em,"    test    rax, rax\n");
        E(em,"    jnz     %s\n",ports.fail);
        /* Load return value into a val bss slot */
        char ret_val[64]; label_val(id,ret_val,sizeof ret_val); bss_declare(ret_val);
        E(em,"    mov     rax, [rel icn_retval]\n");
        E(em,"    mov     [rel %s], rax\n",ret_val);
        Jmp(em,ports.succeed);

        if(arg_alphas) free(arg_alphas);
        if(arg_betas)  free(arg_betas);
        if(arg_ids)    free(arg_ids);
        return;
    }

    /* Unknown call — just fail */
    Ldef(em,a); Jmp(em,ports.fail);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * ICN_ALT — value alternation E1 | E2
 * α → E1.α; E1.ω → E2.α; E2.ω → node.ω
 * β → E1.β (simple: resume left first, then right)
 * ======================================================================= */
static void emit_alt(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                     char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    strncpy(oa,a,63); strncpy(ob,b,63);

    char e1a[64],e1b[64],e2a[64],e2b[64];
    IcnPorts e2p; strncpy(e2p.succeed,ports.succeed,63); strncpy(e2p.fail,ports.fail,63);
    emit_expr(em,n->children[1],e2p,e2a,e2b);

    IcnPorts e1p; strncpy(e1p.succeed,ports.succeed,63); strncpy(e1p.fail,e2a,63);
    emit_expr(em,n->children[0],e1p,e1a,e1b);

    Ldef(em,a); Jmp(em,e1a);
    Ldef(em,b); Jmp(em,e1b);
}

/* =========================================================================
 * Binary arithmetic — funcs-set wiring (§4.3)
 * ======================================================================= */
static void emit_binop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],compute[64],lbfwd[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    snprintf(compute,sizeof compute,"icon_%d_compute",id);
    snprintf(lbfwd,  sizeof lbfwd,  "icon_%d_lb",id);
    char rv[64]; label_val(id,rv,sizeof rv); bss_declare(rv);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==ICN_ADD?"ADD":n->kind==ICN_SUB?"SUB":n->kind==ICN_MUL?"MUL":n->kind==ICN_DIV?"DIV":"MOD";
    E(em,"    ; %s  id=%d\n",op,id);

    IcnPorts rp; strncpy(rp.succeed,compute,63); strncpy(rp.fail,lbfwd,63);
    char ra[64],rb[64]; emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.succeed,ra,63); strncpy(lp.fail,ports.fail,63);
    char la[64],lb[64]; emit_expr(em,n->children[0],lp,la,lb);

    Ldef(em,lbfwd); Jmp(em,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);

    int rid=-1,lid=-1; sscanf(ra,"icon_%d_a",&rid); sscanf(la,"icon_%d_a",&lid);
    Ldef(em,compute);
    if(rid>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",rid);
    if(lid>=0) E(em,"    mov     rcx, [rel icon_%d_val]\n",lid);
    switch(n->kind){
        case ICN_ADD: E(em,"    add     rcx, rax\n"); break;
        case ICN_SUB: E(em,"    sub     rcx, rax\n"); break;
        case ICN_MUL: E(em,"    imul    rcx, rax\n"); break;
        case ICN_DIV: E(em,"    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rax\n"); break;
        case ICN_MOD: E(em,"    xchg    rax, rcx\n    cqo\n    idiv    rcx\n    mov     rcx, rdx\n"); break;
        default: break;
    }
    E(em,"    mov     [rel %s], rcx\n",rv);
    Jmp(em,ports.succeed);
}

/* =========================================================================
 * Relational operators — goal-directed retry
 * ======================================================================= */
static void emit_relop(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],chk[64],lbfwd[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    snprintf(chk,  sizeof chk,  "icon_%d_check",id);
    snprintf(lbfwd,sizeof lbfwd,"icon_%d_lb",id);
    char rv[64]; label_val(id,rv,sizeof rv); bss_declare(rv);
    strncpy(oa,a,63); strncpy(ob,b,63);
    const char *op=n->kind==ICN_LT?"LT":n->kind==ICN_LE?"LE":n->kind==ICN_GT?"GT":n->kind==ICN_GE?"GE":n->kind==ICN_EQ?"EQ":"NE";
    E(em,"    ; %s  id=%d\n",op,id);

    IcnPorts rp; strncpy(rp.succeed,chk,63); strncpy(rp.fail,lbfwd,63);
    char ra[64],rb[64]; emit_expr(em,n->children[1],rp,ra,rb);
    IcnPorts lp; strncpy(lp.succeed,ra,63); strncpy(lp.fail,ports.fail,63);
    char la[64],lb[64]; emit_expr(em,n->children[0],lp,la,lb);

    Ldef(em,lbfwd); Jmp(em,lb);
    Ldef(em,a); Jmp(em,la);
    Ldef(em,b); Jmp(em,rb);

    int rid=-1,lid=-1; sscanf(ra,"icon_%d_a",&rid); sscanf(la,"icon_%d_a",&lid);
    Ldef(em,chk);
    if(lid>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",lid);
    if(rid>=0) E(em,"    mov     rcx, [rel icon_%d_val]\n",rid);
    E(em,"    cmp     rax, rcx\n");
    const char *jfail=n->kind==ICN_LT?"jge":n->kind==ICN_LE?"jg":n->kind==ICN_GT?"jle":n->kind==ICN_GE?"jl":n->kind==ICN_EQ?"jne":"je";
    E(em,"    %s      %s\n",jfail,rb);
    if(rid>=0) E(em,"    mov     [rel %s], rcx\n",rv);
    Jmp(em,ports.succeed);
}

/* =========================================================================
 * ICN_TO — range generator inline counter (§4.4)
 * ======================================================================= */
static void emit_to(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                    char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    icn_label_code(id,code,sizeof code);
    snprintf(init,sizeof init,"icon_%d_init",id);
    snprintf(e1bf,sizeof e1bf,"icon_%d_e1b",id);
    snprintf(e2bf,sizeof e2bf,"icon_%d_e2b",id);
    char I[64],v[64]; label_I(id,I,sizeof I); label_val(id,v,sizeof v);
    bss_declare(I); bss_declare(v);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; TO  id=%d\n",id);

    IcnPorts e2p; strncpy(e2p.succeed,init,63); strncpy(e2p.fail,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(em,n->children[1],e2p,e2a,e2b);
    IcnPorts e1p; strncpy(e1p.succeed,e2a,63); strncpy(e1p.fail,ports.fail,63);
    char e1a[64],e1b[64]; emit_expr(em,n->children[0],e1p,e1a,e1b);

    Ldef(em,e1bf); Jmp(em,e1b);
    Ldef(em,e2bf); Jmp(em,e2b);
    Ldef(em,a);    Jmp(em,e1a);
    Ldef(em,b);    E(em,"    inc     qword [rel %s]\n",I); Jmp(em,code);

    int e1id=-1,e2id=-1; sscanf(e1a,"icon_%d_a",&e1id); sscanf(e2a,"icon_%d_a",&e2id);
    Ldef(em,init);
    if(e1id>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",e1id);
    E(em,"    mov     [rel %s], rax\n",I);
    Jmp(em,code);
    Ldef(em,code);
    E(em,"    mov     rax, [rel %s]\n",I);
    if(e2id>=0) E(em,"    cmp     rax, [rel icon_%d_val]\n",e2id);
    E(em,"    jg      %s\n",e2bf);
    E(em,"    mov     [rel %s], rax\n",v);
    Jmp(em,ports.succeed);
}

/* =========================================================================
 * ICN_TO_BY
 * ======================================================================= */
static void emit_to_by(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],code[64],init[64],e1bf[64],e2bf[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    icn_label_code(id,code,sizeof code);
    snprintf(init,sizeof init,"icon_%d_init",id);
    snprintf(e1bf,sizeof e1bf,"icon_%d_e1b",id);
    snprintf(e2bf,sizeof e2bf,"icon_%d_e2b",id);
    char I[64],v[64]; label_I(id,I,sizeof I); label_val(id,v,sizeof v);
    bss_declare(I); bss_declare(v);
    strncpy(oa,a,63); strncpy(ob,b,63);

    IcnPorts e3p; strncpy(e3p.succeed,init,63); strncpy(e3p.fail,e2bf,63);
    char e3a[64],e3b[64]; emit_expr(em,n->children[2],e3p,e3a,e3b);
    IcnPorts e2p; strncpy(e2p.succeed,e3a,63); strncpy(e2p.fail,e1bf,63);
    char e2a[64],e2b[64]; emit_expr(em,n->children[1],e2p,e2a,e2b);
    IcnPorts e1p; strncpy(e1p.succeed,e2a,63); strncpy(e1p.fail,ports.fail,63);
    char e1a[64],e1b[64]; emit_expr(em,n->children[0],e1p,e1a,e1b);

    Ldef(em,e1bf); Jmp(em,e1b);
    Ldef(em,e2bf); Jmp(em,e2b);
    Ldef(em,a);    Jmp(em,e1a);
    int e1id=-1,e2id=-1,e3id=-1;
    sscanf(e1a,"icon_%d_a",&e1id); sscanf(e2a,"icon_%d_a",&e2id); sscanf(e3a,"icon_%d_a",&e3id);
    Ldef(em,b);
    if(e3id>=0) E(em,"    mov     rcx, [rel icon_%d_val]\n",e3id);
    E(em,"    add     [rel %s], rcx\n",I); Jmp(em,code);
    Ldef(em,init);
    if(e1id>=0) E(em,"    mov     rax, [rel icon_%d_val]\n",e1id);
    E(em,"    mov     [rel %s], rax\n",I); Jmp(em,code);
    Ldef(em,code);
    E(em,"    mov     rax, [rel %s]\n",I);
    if(e2id>=0) E(em,"    cmp     rax, [rel icon_%d_val]\n",e2id);
    E(em,"    jg      %s\n",e2bf);
    E(em,"    mov     [rel %s], rax\n",v);
    Jmp(em,ports.succeed);
}

/* =========================================================================
 * ICN_EVERY
 * ======================================================================= */
static void emit_every(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                       char *oa, char *ob) {
    int id=icn_new_id(em); char a[64],b[64],gbfwd[64];
    icn_label_alpha(id,a,sizeof a); icn_label_beta(id,b,sizeof b);
    snprintf(gbfwd,sizeof gbfwd,"icon_%d_genb",id);
    strncpy(oa,a,63); strncpy(ob,b,63);
    E(em,"    ; EVERY  id=%d\n",id);

    IcnNode *gen=n->children[0];
    IcnNode *body=(n->nchildren>1)?n->children[1]:NULL;
    char ga[64],gb[64];

    if(body){
        char bstart[64]; snprintf(bstart,sizeof bstart,"icon_%d_body",id);
        IcnPorts bp; strncpy(bp.succeed,gbfwd,63); strncpy(bp.fail,gbfwd,63);
        char ba[64],bb[64]; emit_expr(em,body,bp,ba,bb);
        IcnPorts gp; strncpy(gp.succeed,bstart,63); strncpy(gp.fail,ports.fail,63);
        emit_expr(em,gen,gp,ga,gb);
        Ldef(em,bstart); Jmp(em,ba);
    } else {
        IcnPorts gp; strncpy(gp.succeed,gbfwd,63); strncpy(gp.fail,ports.fail,63);
        emit_expr(em,gen,gp,ga,gb);
    }
    Ldef(em,gbfwd); Jmp(em,gb);
    Ldef(em,a); Jmp(em,ga);
    Ldef(em,b); Jmp(em,ports.fail);
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
static void emit_expr(IcnEmitter *em, IcnNode *n, IcnPorts ports,
                      char *oa, char *ob) {
    if(!n){ emit_fail_node(em,n,ports,oa,ob); return; }
    switch(n->kind){
        case ICN_INT:    emit_int      (em,n,ports,oa,ob); break;
        case ICN_STR:    emit_str      (em,n,ports,oa,ob); break;
        case ICN_VAR:    emit_var      (em,n,ports,oa,ob); break;
        case ICN_ASSIGN: emit_assign   (em,n,ports,oa,ob); break;
        case ICN_RETURN: emit_return   (em,n,ports,oa,ob); break;
        case ICN_FAIL:   emit_fail_node(em,n,ports,oa,ob); break;
        case ICN_IF:     emit_if       (em,n,ports,oa,ob); break;
        case ICN_ALT:    emit_alt      (em,n,ports,oa,ob); break;
        case ICN_ADD: case ICN_SUB: case ICN_MUL: case ICN_DIV: case ICN_MOD:
                         emit_binop    (em,n,ports,oa,ob); break;
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
                         emit_relop    (em,n,ports,oa,ob); break;
        case ICN_TO:     emit_to       (em,n,ports,oa,ob); break;
        case ICN_TO_BY:  emit_to_by    (em,n,ports,oa,ob); break;
        case ICN_EVERY:  emit_every    (em,n,ports,oa,ob); break;
        case ICN_CALL:   emit_call     (em,n,ports,oa,ob); break;
        default:{
            int id=icn_new_id(em); char a2[64],b2[64];
            icn_label_alpha(id,a2,sizeof a2); icn_label_beta(id,b2,sizeof b2);
            strncpy(oa,a2,63); strncpy(ob,b2,63);
            E(em,"    ; UNIMPL %s id=%d\n",icn_kind_name(n->kind),id);
            Ldef(em,a2); Jmp(em,ports.fail);
            Ldef(em,b2); Jmp(em,ports.fail);
        }
    }
}

/* =========================================================================
 * icn_emit_file — full file emission
 * ======================================================================= */
void icn_emit_file(IcnEmitter *em, IcnNode **nodes, int count) {
    bss_count=0; rodata_count=0; str_counter=0; user_proc_count=0;

    /* Pass 1: register all user procs */
    for(int pi=0;pi<count;pi++){
        IcnNode *proc=nodes[pi];
        if(!proc||proc->kind!=ICN_PROC||proc->nchildren<1) continue;
        const char *pname=proc->children[0]->val.sval;
        if(strcmp(pname,"main")==0) continue;
        register_user_proc(pname, (int)proc->val.ival);
    }

    /* Emit to temp buffer */
    FILE *tmp=tmpfile(); FILE *real=em->out; em->out=tmp; em->node_id=0;

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

        /* Setup local env */
        locals_reset();
        strncpy(cur_ret_label, is_main?"icn_main_done":proc_ret, 63);
        strncpy(cur_fail_label, proc_done, 63);

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
        /* Frame size: all locals (params + declared locals) */
        int frame_size=(cur_nlocals>0)?8*(cur_nlocals+1):0;
        if(frame_size%16!=0) frame_size=(frame_size+15)&~15;

        /* Chain statements in reverse — skip ICN_GLOBAL (local decl) nodes */
        char **alphas=calloc(nstmts,sizeof(char*));
        for(int i=0;i<nstmts;i++) alphas[i]=malloc(64);
        char next_a[64]; strncpy(next_a,proc_done,63);

        for(int i=nstmts-1;i>=0;i--){
            IcnNode *stmt=proc->children[body_start+i];
            if(!stmt||stmt->kind==ICN_GLOBAL){ strncpy(alphas[i],next_a,63); continue; }
            IcnPorts sp; strncpy(sp.succeed,next_a,63); strncpy(sp.fail,next_a,63);
            char sa[64],sb[64]; emit_expr(em,stmt,sp,sa,sb);
            strncpy(alphas[i],sa,63); strncpy(next_a,sa,63);
        }

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

        /* Return label (for non-main: restore frame, ret) */
        if(!is_main){
            Ldef(em,proc_ret);
            if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
            E(em,"    pop     rbp\n    ret\n");
        }
        Ldef(em,proc_done);
        if(frame_size>0) E(em,"    add     rsp, %d\n",frame_size);
        E(em,"    pop     rbp\n    ret\n");

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
    for(int i=0;i<bss_count;i++) E(em,"    %s: resq 1\n",bss_entries[i].name);
    E(em,"    icn_failed: resb 1\n\n");

    E(em,"section .text\n    global _start\n    extern icn_write_int\n    extern icn_write_str\n");
    E(em,"    extern icn_push\n    extern icn_pop\n\n");
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
