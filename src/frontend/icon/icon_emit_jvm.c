/*
 * icon_emit_jvm.c — Tiny-ICON Byrd Box → JVM Jasmin emitter (Sprint IJ-1)
 *
 * Consumes the same IcnNode* AST as icon_emit.c.
 * Emits Jasmin assembler (.j) text assembled by jasmin.jar into .class files.
 *
 * Pipeline:
 *   icon_driver -jvm foo.icn -o foo.j
 *   java -jar jasmin.jar foo.j -d outdir/
 *   java -cp outdir/ FooClass
 *
 * Oracles:
 *   icon_emit.c        — Byrd-box wiring logic per ICN node (49KB, authoritative)
 *   emit_byrd_jvm.c    — Jasmin output format, helpers (187KB, copy verbatim where useful)
 *   jcon-master/tran/  — JCON gen_bc.icn / irgen.icn — blueprint
 *
 * Design:
 *   icon_emit_jvm.c = icon_emit.c logic + Jasmin output format
 *   Where icon_emit.c emits:  E(em, "    jmp  %s\n", lbl)
 *   We emit:                  J("    goto %s\n", lbl)
 *
 * Value representation:
 *   Icon integers → JVM long (lload/lstore, local slots N and N+1)
 *   Success/failure → static byte field icn_failed (0=ok, 1=failed)
 *   Suspend state  → static byte icn_suspended + int icn_suspend_id
 *   Return value   → static long icn_retval
 *
 * Label conventions (parallel to ASM):
 *   α icn_N_α   β icn_N_β   γ (inherited IjPorts.γ)   ω (inherited IjPorts.ω)
 *   Node γ:  (inherited, passed as string)
 *   Node ω:  (inherited, passed as string)
 *   Extra:   icn_N_code, icn_N_init, icn_N_lstore, etc.
 *
 * Local variable slots:
 *   Each local/param occupies 2 JVM slots (long is wide).
 *   slot_base(i) = 2*i
 *
 * Suspend/resume encoding:
 *   Each ICN_SUSPEND site gets a unique integer ID (1-based).
 *   icn_suspend_id static int field records which site suspended.
 *   Generator proc β entry does tableswitch on icn_suspend_id.
 *
 * Sprint IJ-1 scope: ICN_INT, ICN_VAR, ICN_ASSIGN, ICN_ADD/SUB/MUL/DIV/MOD,
 *   ICN_LT/LE/GT/GE/EQ/NE, ICN_TO, ICN_EVERY, ICN_CALL(write), ICN_IF,
 *   ICN_PROC, ICN_RETURN, ICN_FAIL, ICN_SUSPEND, ICN_ALT, ICN_WHILE
 */

#define _POSIX_C_SOURCE 200809L
#include "icon_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* =========================================================================
 * Output helpers
 * ======================================================================= */
static FILE *jout;

static void J(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(jout, fmt, ap); va_end(ap);
}
static void JI(const char *instr, const char *ops) {
    if (ops && ops[0]) J("    %s %s\n", instr, ops);
    else                J("    %s\n",   instr);
}
static void JL(const char *label) {
    J("%s:\n", label);
}
static void JGoto(const char *lbl) {
    J("    goto %s\n", lbl);
}
static void JC(const char *comment) {
    J(";  %s\n", comment);
}

/* =========================================================================
 * Class name
 * ======================================================================= */
static char ij_classname[256];

void ij_set_classname(const char *filename) {
    const char *base = strrchr(filename, '/');
    base = base ? base+1 : filename;
    int i = 0;
    for (const char *p = base; *p && *p != '.' && i < 254; p++, i++) {
        ij_classname[i] = isalnum((unsigned char)*p) ? *p : '_';
    }
    if (i == 0) { strcpy(ij_classname, "IconProg"); return; }
    /* Capitalize first letter for valid Java class name */
    ij_classname[0] = toupper((unsigned char)ij_classname[0]);
    ij_classname[i] = '\0';
}

/* Build "ClassName/methodSig" into a static buffer for JI() calls */
static const char *ij_classname_buf(const char *sig) {
    static char buf[512];
    snprintf(buf, sizeof buf, "%s/%s", ij_classname, sig);
    return buf;
}

/* Build "ClassName/fieldName descriptor" for getstatic/putstatic */
static const char *ij_classname_buf_fld(const char *fld, const char *desc) {
    static char buf2[512];
    snprintf(buf2, sizeof buf2, "%s/%s %s", ij_classname, fld, desc);
    return buf2;
}

/* =========================================================================
 * Node ID counter and label generation
 * ======================================================================= */
static int ij_node_id = 0;

static int ij_new_id(void) { return ij_node_id++; }

static void lbl_α  (int id, char *b, size_t s) { snprintf(b,s,"icn_%d_α",   id); }
static void lbl_β  (int id, char *b, size_t s) { snprintf(b,s,"icn_%d_β",   id); }
static void lbl_code(int id, char *b, size_t s) { snprintf(b,s,"icn_%d_code",id); }

/* =========================================================================
 * IcnPorts — inherited success/fail labels (same struct as icon_emit.h)
 * ======================================================================= */
typedef struct { char γ[64]; char ω[64]; } IjPorts;

/* =========================================================================
 * User procedure registry (mirrors icon_emit.c)
 * ======================================================================= */
#define MAX_USER_PROCS 64
static char ij_user_procs[MAX_USER_PROCS][64];
static int  ij_user_nparams[MAX_USER_PROCS];
static int  ij_user_is_gen[MAX_USER_PROCS];
static int  ij_user_returns_str[MAX_USER_PROCS];  /* 1 if proc returns String */
static int  ij_user_count = 0;

static void ij_register_proc(const char *name, int nparams, int is_gen) {
    for (int i=0;i<ij_user_count;i++) if(!strcmp(ij_user_procs[i],name)) return;
    if (ij_user_count < MAX_USER_PROCS) {
        strncpy(ij_user_procs[ij_user_count], name, 63);
        ij_user_nparams[ij_user_count] = nparams;
        ij_user_is_gen[ij_user_count]  = is_gen;
        ij_user_returns_str[ij_user_count] = 0;  /* set later by pre-pass */
        ij_user_count++;
    }
}
static void ij_mark_proc_returns_str(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) { ij_user_returns_str[i]=1; return; }
}
static int ij_proc_returns_str(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) return ij_user_returns_str[i];
    return 0;
}
static int ij_is_user_proc(const char *name) {
    for (int i=0;i<ij_user_count;i++) if(!strcmp(ij_user_procs[i],name)) return 1;
    return 0;
}
static int ij_is_gen_proc(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) return ij_user_is_gen[i];
    return 0;
}
static int ij_nparams_for(const char *name) {
    for (int i=0;i<ij_user_count;i++)
        if(!strcmp(ij_user_procs[i],name)) return ij_user_nparams[i];
    return 0;
}
/* -------------------------------------------------------------------------
 * Record type registry
 * Maps record type name → array of field names, nfields
 * Used by ICN_CALL (constructor) and ICN_FIELD (getfield/putfield).
 * ------------------------------------------------------------------------- */
#define MAX_RECORD_TYPES 32
#define MAX_RECORD_FIELDS 16
static char ij_rec_names[MAX_RECORD_TYPES][64];
static char ij_rec_fields[MAX_RECORD_TYPES][MAX_RECORD_FIELDS][64];
static int  ij_rec_nfields[MAX_RECORD_TYPES];
static int  ij_nrec = 0;

static void ij_register_record(const char *name, IcnNode *rn) {
    for (int i = 0; i < ij_nrec; i++) if (!strcmp(ij_rec_names[i], name)) return;
    if (ij_nrec >= MAX_RECORD_TYPES) return;
    strncpy(ij_rec_names[ij_nrec], name, 63);
    int nf = rn->nchildren < MAX_RECORD_FIELDS ? rn->nchildren : MAX_RECORD_FIELDS;
    ij_rec_nfields[ij_nrec] = nf;
    for (int i = 0; i < nf; i++)
        strncpy(ij_rec_fields[ij_nrec][i], rn->children[i]->val.sval, 63);
    ij_nrec++;
}
static int ij_is_record_type(const char *name) {
    for (int i = 0; i < ij_nrec; i++) if (!strcmp(ij_rec_names[i], name)) return 1;
    return 0;
}
static int ij_record_nfields(const char *name) {
    for (int i = 0; i < ij_nrec; i++) if (!strcmp(ij_rec_names[i], name)) return ij_rec_nfields[i];
    return 0;
}
/* Returns field name at index idx for named record type, or NULL */
static const char *ij_record_field(const char *name, int idx) {
    for (int i = 0; i < ij_nrec; i++)
        if (!strcmp(ij_rec_names[i], name)) {
            if (idx < ij_rec_nfields[i]) return ij_rec_fields[i][idx];
        }
    return NULL;
}

static int ij_has_suspend(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_SUSPEND) return 1;
    for (int i=0;i<n->nchildren;i++)
        if (ij_has_suspend(n->children[i])) return 1;
    return 0;
}

/* =========================================================================
 * Per-procedure local variable table
 * ======================================================================= */
#define MAX_LOCALS 32
typedef struct { char name[32]; int slot; } IjLocal;
static IjLocal ij_locals[MAX_LOCALS];
static int     ij_nlocals = 0;
static int     ij_nparams = 0;
static int     ij_nstrefs = 0; /* count of logical slots used for astore (String refs) */
static int     ij_nint_scratch = 0; /* count of int/ref scratch slots above long-pair region */
/* Allocate a raw JVM slot for int/ref scratch — lives at 2*MAX_LOCALS + n,
 * entirely above the slot_jvm() long-pair region, so istore/astore never
 * alias lstore targets.  Returns the raw JVM slot index. */
static int ij_alloc_int_scratch(void) {
    /* Start well above the long-pair ceiling (2*MAX_LOCALS + generous padding)
     * so int/ref scratch slots never alias any slot_jvm(n) = 2*n target. */
    return 2 * MAX_LOCALS + 20 + ij_nint_scratch++;
}
static char    ij_ret_label[64]  = "";   /* label for return */
static char    ij_fail_label[64] = "";   /* label for proc fail */
static char    ij_sret_label[64] = "";   /* label for suspend-yield (frame kept) */
/* Suspend sites in current procedure */
static int     ij_suspend_ids[64];
static int     ij_suspend_count = 0;
static int     ij_next_suspend_id = 1;

/* =========================================================================
 * Loop label stack — for break/next
 * Each loop emitter pushes before emitting body, pops after.
 * break  → ij_loop_exit[top]    (= ports.ω of enclosing loop)
 * next   → ij_loop_next[top]    (= loop restart: top-of-body / cond label)
 * ======================================================================= */
#define IJ_LOOP_STACK_MAX 32
static char ij_loop_exit[IJ_LOOP_STACK_MAX][64];  /* ports.ω for break */
static char ij_loop_next[IJ_LOOP_STACK_MAX][64];  /* restart label for next */
static int  ij_loop_depth = 0;

static void ij_loop_push(const char *exit_lbl, const char *next_lbl) {
    if (ij_loop_depth < IJ_LOOP_STACK_MAX) {
        strncpy(ij_loop_exit[ij_loop_depth], exit_lbl, 63);
        strncpy(ij_loop_next[ij_loop_depth], next_lbl, 63);
        ij_loop_depth++;
    }
}
static void ij_loop_pop(void) {
    if (ij_loop_depth > 0) ij_loop_depth--;
}
static const char *ij_loop_break_target(void) {
    if (ij_loop_depth > 0) return ij_loop_exit[ij_loop_depth-1];
    return "icn_main_done";  /* fallback: top-level fail */
}
static const char *ij_loop_next_target(void) {
    if (ij_loop_depth > 0) return ij_loop_next[ij_loop_depth-1];
    return "icn_main_done";
}

static void ij_locals_reset(void) {
    ij_nlocals = 0; ij_nparams = 0; ij_nstrefs = 0; ij_nint_scratch = 0;
    ij_suspend_count = 0;
    ij_loop_depth = 0;
}
static int ij_locals_find(const char *name) {
    for (int i=0;i<ij_nlocals;i++)
        if(!strcmp(ij_locals[i].name,name)) return ij_locals[i].slot;
    return -1;
}
static int ij_locals_add(const char *name) {
    int slot = ij_nlocals;
    if (ij_nlocals < MAX_LOCALS) {
        strncpy(ij_locals[ij_nlocals].name, name, 31);
        ij_locals[ij_nlocals++].slot = slot;
    }
    return slot;
}
static int ij_locals_alloc_tmp(void) {
    int slot = ij_nlocals;
    if (ij_nlocals < MAX_LOCALS) {
        ij_locals[ij_nlocals].name[0] = '\0';
        ij_locals[ij_nlocals++].slot = slot;
    }
    return slot;
}

/* Each named local uses 2 JVM slots (long).
 * JVM slot index = 2 * logical_slot */
static int slot_jvm(int logical) { return 2 * logical; }

/* Total JVM locals needed for .limit locals directive */
static int ij_jvm_locals_count(void) {
    int top = 2 * MAX_LOCALS + 20 + ij_nint_scratch;
    return top + 4;
}

/* Current procedure name — used to namespace per-proc static var fields */
static char ij_cur_proc[64] = "";

/* Static field name for a named local/param in current proc */
static void ij_var_field(const char *varname, char *out, size_t outsz) {
    snprintf(out, outsz, "icn_pv_%s_%s", ij_cur_proc, varname);
}

/* =========================================================================
 * Static fields emitted at class level
 * ======================================================================= */
#define MAX_STATICS 512
static char ij_statics[MAX_STATICS][64];
static char ij_static_types[MAX_STATICS];  /* 'J'=long, 'I'=int */
static int  ij_nstatics = 0;

static void ij_declare_static_typed(const char *name, char type) {
    for (int i=0;i<ij_nstatics;i++) {
        if(!strcmp(ij_statics[i],name)) {
            /* Allow upgrade to Object type if previously declared as J (long) */
            if (type == 'O' && ij_static_types[i] == 'J')
                ij_static_types[i] = 'O';
            /* Allow upgrade to record-list if previously declared as plain list */
            if (type == 'R' && ij_static_types[i] == 'L')
                ij_static_types[i] = 'R';
            return;
        }
    }
    if (ij_nstatics < MAX_STATICS) {
        strncpy(ij_statics[ij_nstatics], name, 63);
        ij_static_types[ij_nstatics] = type;
        ij_nstatics++;
    }
}
static void ij_declare_static(const char *name) {
    ij_declare_static_typed(name, 'J');
}
static void ij_declare_static_int(const char *name) {
    ij_declare_static_typed(name, 'I');
}
static void ij_declare_static_str(const char *name) {
    ij_declare_static_typed(name, 'A');  /* 'A' = Ljava/lang/String; */
}

/* =========================================================================
 * Table default-value field map.
 * Maps table-variable field name → dflt Object static field name.
 * Populated by the table(dflt) emitter; consumed by ij_emit_subscript.
 * ij_pending_tdflt: set by table() emitter so assign store can register map.
 * ======================================================================= */
#define MAX_TBL_DFLT 64
static char ij_tdflt_tbl[MAX_TBL_DFLT][80];   /* table var field name   */
static char ij_tdflt_fld[MAX_TBL_DFLT][80];   /* dflt Object field name */
static int  ij_ntdflt = 0;
static char ij_pending_tdflt[80] = "";         /* set by table() emitter */

static void ij_register_tdflt(const char *tbl_field, const char *dflt_field) {
    for (int i = 0; i < ij_ntdflt; i++)
        if (!strcmp(ij_tdflt_tbl[i], tbl_field)) {
            strncpy(ij_tdflt_fld[i], dflt_field, 79); return;
        }
    if (ij_ntdflt < MAX_TBL_DFLT) {
        strncpy(ij_tdflt_tbl[ij_ntdflt], tbl_field, 79);
        strncpy(ij_tdflt_fld[ij_ntdflt], dflt_field, 79);
        ij_ntdflt++;
    }
}

/* Returns the dflt field for tbl_field, or NULL if not registered. */
static const char *ij_lookup_tdflt(const char *tbl_field) {
    for (int i = 0; i < ij_ntdflt; i++)
        if (!strcmp(ij_tdflt_tbl[i], tbl_field)) return ij_tdflt_fld[i];
    return NULL;
}

/* =========================================================================
 * Global variable name registry — separate from statics.
 * Pass 0 registers names here WITHOUT pre-declaring a field type.
 * Type is inferred naturally on first assignment (same as locals).
 * ij_is_global() tells var/assign emit to use icn_gvar_* fields.
 * ======================================================================= */
#define MAX_GLOBALS 64
static char ij_global_names[MAX_GLOBALS][64];
static int  ij_nglobals = 0;

static void ij_register_global(const char *name) {
    for (int i = 0; i < ij_nglobals; i++)
        if (!strcmp(ij_global_names[i], name)) return;
    if (ij_nglobals < MAX_GLOBALS)
        strncpy(ij_global_names[ij_nglobals++], name, 63);
}

static int ij_is_global(const char *name) {
    for (int i = 0; i < ij_nglobals; i++)
        if (!strcmp(ij_global_names[i], name)) return 1;
    return 0;
}

/* String pool for rodata */
#define MAX_STRINGS 256
static char ij_strings[MAX_STRINGS][512];
static int  ij_string_ids[MAX_STRINGS];
static int  ij_nstrings = 0;

static int ij_intern_string(const char *s) {
    for (int i=0;i<ij_nstrings;i++) if(!strcmp(ij_strings[i],s)) return ij_string_ids[i];
    int id = ij_nstrings;
    if (ij_nstrings < MAX_STRINGS) {
        strncpy(ij_strings[ij_nstrings], s, 511);
        ij_string_ids[ij_nstrings] = id;
        ij_nstrings++;
    }
    return id;
}

/* =========================================================================
 * Forward declaration
 * ======================================================================= */
static void ij_emit_expr(IcnNode *n, IjPorts ports,
                         char *out_α, char *out_β);

/* =========================================================================
 * Emit helpers for static field load/store
 * ======================================================================= */
static void ij_get_long(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s J", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_long(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s J", ij_classname, fname);
    JI("putstatic", buf);
}
static void ij_get_byte(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s B", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_byte(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s B", ij_classname, fname);
    JI("putstatic", buf);
}
static void ij_get_int_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s I", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_int_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s I", ij_classname, fname);
    JI("putstatic", buf);
}

/* String-typed static field helpers */
static void ij_get_str_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/String;", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_str_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/String;", ij_classname, fname);
    JI("putstatic", buf);
}

/* Double-typed (D) static field helpers — for ICN_POW, real to-by */
static void ij_declare_static_real(const char *name) {
    ij_declare_static_typed(name, 'D');
}
static void ij_get_real_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s D", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_real_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s D", ij_classname, fname);
    JI("putstatic", buf);
}

/* Forward declaration needed for ij_expr_is_string */
static int ij_expr_is_string(IcnNode *n);
static int ij_expr_is_list(IcnNode *n);
static int ij_expr_is_table(IcnNode *n);
static int ij_expr_is_record_list(IcnNode *n);
/* Returns the type tag for a declared static field, or 0 if not found */
static char ij_field_type_tag(const char *name) {
    for (int i = 0; i < ij_nstatics; i++)
        if (!strcmp(ij_statics[i], name)) return ij_static_types[i];
    return 0;
}

static int ij_expr_is_record(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_CALL && n->nchildren >= 1 && n->children[0]->kind == ICN_VAR)
        return ij_is_record_type(n->children[0]->val.sval);
    /* !<record-list> yields a record Object */
    if (n->kind == ICN_BANG && n->nchildren >= 1)
        return ij_expr_is_record_list(n->children[0]);
    /* VAR whose static field is Object-typed (assigned from a record constructor) */
    if (n->kind == ICN_VAR) {
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        if (ij_field_type_tag(fld) == 'O') return 1;
        /* global var */
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        if (ij_field_type_tag(gname) == 'O') return 1;
    }
    return 0;
}
/* Returns 1 if the list expression contains Object (record) elements rather than
 * boxed Longs.  True for:
 *   - MAKELIST whose first element is a record constructor
 *   - sortf(L, f) — always a record list
 *   - sort(L)     — if L itself is a record list
 *   - a VAR whose field tag is 'L' and whose backing MAKELIST had record elements
 *     (detected by checking the static field icn_N_elem_0 type tag == 'O')
 */
static int ij_expr_is_record_list(IcnNode *n) {
    if (!n) return 0;
    /* Direct list literal: [rec(...), ...] */
    if (n->kind == ICN_MAKELIST) {
        if (n->nchildren > 0 && ij_expr_is_record(n->children[0])) return 1;
        return 0;
    }
    /* sortf always returns a record list */
    if (n->kind == ICN_CALL && n->nchildren >= 1) {
        IcnNode *fn = n->children[0];
        if (fn && fn->kind == ICN_VAR) {
            if (strcmp(fn->val.sval, "sortf") == 0) return 1;
            /* sort(L) — record list if L is a record list */
            if (strcmp(fn->val.sval, "sort") == 0 && n->nchildren >= 2)
                return ij_expr_is_record_list(n->children[1]);
        }
    }
    /* Assignment: propagate from rhs */
    if (n->kind == ICN_ASSIGN && n->nchildren >= 2)
        return ij_expr_is_record_list(n->children[1]);
    /* VAR: check if any elem field for this var is Object-typed.
     * The makelist emitter stores elements in icn_N_elem_0 as Object fields;
     * we can't recover N from just the var name, so fall back to checking
     * whether the list static field has a sibling _elem_0 that is 'O'-typed.
     * Simpler heuristic: check the static-field registry for any field named
     * icn_*_elem_0 that is Object-typed — this is coarse but correct for the
     * single-list-per-scope case common in tests.
     * A precise approach: tag list fields at declare time. Use the coarse one for now. */
    if (n->kind == ICN_VAR) {
        /* Fast path: var tagged 'R' (record-list) at assign time */
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        if (ij_field_type_tag(fld) == 'R') return 1;
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        if (ij_field_type_tag(gname) == 'R') return 1;
        /* Coarse fallback: any elem_0 Object in scope (single-list-per-scope) */
        for (int i = 0; i < ij_nstatics; i++) {
            if (ij_static_types[i] == 'O' &&
                strstr(ij_statics[i], "_elem_0") != NULL) return 1;
        }
    }
    return 0;
}
/* Returns record type name if expr is a record constructor, else NULL */
static const char *ij_expr_record_type(IcnNode *n) {
    if (!n || n->kind != ICN_CALL || n->nchildren < 1) return NULL;
    if (n->children[0]->kind != ICN_VAR) return NULL;
    const char *nm = n->children[0]->val.sval;
    return ij_is_record_type(nm) ? nm : NULL;
}

/* Push 0 to icn_failed (success) */
static void ij_set_ok(void) {
    JI("iconst_0", ""); ij_put_byte("icn_failed");
}
/* Push 1 to icn_failed (failure) */
static void ij_set_fail(void) {
    JI("iconst_1", ""); ij_put_byte("icn_failed");
}
/* Test icn_failed — if nonzero jump to lbl */
static void ij_jmp_if_failed(const char *lbl) {
    ij_get_byte("icn_failed");
    J("    ifne %s\n", lbl);
}
/* Test icn_failed — if zero jump to lbl */
/* ij_jmp_if_ok removed — was unused */

/* =========================================================================
 * ICN_INT
 * α: push long literal, goto succeed
 * β: goto fail (one-shot)
 * ======================================================================= */
static void ij_emit_int(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("INT"); JL(a);
    J("    ldc2_w %ld\n", n->val.ival);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_REAL
 * α: push double via ldc2_w, goto succeed
 * β: goto fail (one-shot literal)
 * Doubles are stored in 'D' static fields; write() uses println(D).
 * ======================================================================= */
static void ij_declare_static_dbl(const char *name) {
    ij_declare_static_typed(name, 'D');
}
static void ij_get_dbl(const char *fld) {
    J("    getstatic %s/%s D\n", ij_classname, fld);
}
static void ij_put_dbl(const char *fld) {
    J("    putstatic %s/%s D\n", ij_classname, fld);
}

/* ArrayList-typed static field helpers ('L' type tag) */
static void ij_declare_static_list(const char *name) {
    ij_declare_static_typed(name, 'L');
}
/* Record-list helpers ('R' type tag — ArrayList of record Objects) */
static void ij_declare_static_reclist(const char *name) {
    ij_declare_static_typed(name, 'R');
}
static void ij_get_list_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/util/ArrayList;", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_list_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/util/ArrayList;", ij_classname, fname);
    JI("putstatic", buf);
}
/* Object-typed static field helpers ('O' type tag) — for boxed element temps */
static void ij_declare_static_obj(const char *name) {
    ij_declare_static_typed(name, 'O');
}
static void ij_get_obj_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/Object;", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_obj_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/lang/Object;", ij_classname, fname);
    JI("putstatic", buf);
}
/* HashMap-typed static field helpers ('T' type tag) — Icon tables */
static void ij_declare_static_table(const char *name) {
    ij_declare_static_typed(name, 'T');
}
static void ij_get_table_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/util/HashMap;", ij_classname, fname);
    JI("getstatic", buf);
}
static void ij_put_table_field(const char *fname) {
    char buf[384]; snprintf(buf, sizeof buf, "%s/%s Ljava/util/HashMap;", ij_classname, fname);
    JI("putstatic", buf);
}

static void ij_emit_real(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("REAL"); JL(a);
    /* Emit as double constant. Jasmin requires 'd' suffix for ldc2_w double literals
     * (without it Jasmin treats the value as float and widens, losing precision). */
    /* Ensure a decimal point is present so Jasmin treats it as double, not int.
     * %g may produce "2" for 2.0; append ".0" if no decimal or exponent present. */
    {
        char dbuf[64];
        snprintf(dbuf, sizeof dbuf, "%g", n->val.fval);
        int has_dot = 0;
        for (int ci = 0; dbuf[ci]; ci++)
            if (dbuf[ci] == '.' || dbuf[ci] == 'e' || dbuf[ci] == 'E' || dbuf[ci] == 'n') { has_dot = 1; break; }
        if (!has_dot) strncat(dbuf, ".0", sizeof dbuf - strlen(dbuf) - 1);
        J("    ldc2_w %sd\n", dbuf);
    }
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* Returns 1 if this node produces a double (real) value */
static int ij_expr_is_real(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_REAL) return 1;
    /* ICN_NEG: real if child is real */
    if (n->kind == ICN_NEG && n->nchildren >= 1)
        return ij_expr_is_real(n->children[0]);
    /* ICN_POW always returns double (Math.pow returns D) */
    if (n->kind == ICN_POW) return 1;
    /* Binop is real if either operand is real */
    if (n->kind == ICN_ADD || n->kind == ICN_SUB || n->kind == ICN_MUL ||
        n->kind == ICN_DIV || n->kind == ICN_MOD) {
        if (n->nchildren >= 2)
            return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    }
    /* Relops return the right-hand value on success: real if either operand is real */
    if (n->kind == ICN_LT || n->kind == ICN_LE || n->kind == ICN_GT ||
        n->kind == ICN_GE || n->kind == ICN_EQ || n->kind == ICN_NE) {
        if (n->nchildren >= 2)
            return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    }
    /* ICN_ALT: real if first alternative is real (all alternatives must be same type) */
    if (n->kind == ICN_ALT && n->nchildren >= 1)
        return ij_expr_is_real(n->children[0]);
    /* real() builtin call always returns double */
    if (n->kind == ICN_CALL && n->nchildren >= 1) {
        IcnNode *fn = n->children[0];
        if (fn && fn->kind == ICN_VAR && strcmp(fn->val.sval, "real") == 0) return 1;
        /* sqrt always returns double */
        if (fn && fn->kind == ICN_VAR && strcmp(fn->val.sval, "sqrt") == 0) return 1;
        /* abs/max/min are real if any arg is real */
        if (fn && fn->kind == ICN_VAR) {
            const char *fn_name = fn->val.sval;
            if ((strcmp(fn_name,"abs")==0 || strcmp(fn_name,"max")==0 || strcmp(fn_name,"min")==0)
                && n->nchildren >= 2) {
                for (int ci = 1; ci < n->nchildren; ci++)
                    if (ij_expr_is_real(n->children[ci])) return 1;
            }
        }
    }
    /* ICN_TO_BY: real if any of start/end/step is real */
    if (n->kind == ICN_TO_BY && n->nchildren >= 3)
        return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1])
               || ij_expr_is_real(n->children[2]);
    /* ICN_TO: real if either bound is real */
    if (n->kind == ICN_TO && n->nchildren >= 2)
        return ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    if (n->kind == ICN_VAR) {
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'D') return 1;
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'D') return 1;
    }
    return 0;
}

/* =========================================================================
 * ICN_STR
 * α: push String ref via ldc, goto succeed
 * β: goto fail
 * ======================================================================= */
static void ij_emit_str(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    (void)ij_intern_string(n->val.sval);
    JC("STR"); JL(a);
    /* Push string — ldc with quoted string */
    J("    ldc \"%s\"\n", n->val.sval);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_VAR
 * α: load from local slot (or static field for globals), goto succeed
 * β: goto fail (variables are one-shot)
 *
 * For long locals: lload slot_jvm(logical_slot)
 * For globals: getstatic classname/icn_gvar_NAME J
 * ======================================================================= */
static void ij_emit_var(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JC("VAR"); JL(a);

    /* &subject keyword — push current scan subject (String) */
    if (strcmp(n->val.sval, "&subject") == 0) {
        ij_declare_static_str("icn_subject");
        ij_get_str_field("icn_subject");
        JGoto(ports.γ);
        JL(b); JGoto(ports.ω);
        return;
    }

    int slot = ij_locals_find(n->val.sval);
    if (slot >= 0) {
        /* Named local/param: use per-proc static field (suspend-safe) */
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        /* Determine type: look up in statics table */
        int is_str = 0, is_dbl = 0, is_list = 0, is_table = 0, is_obj = 0;
        for (int i = 0; i < ij_nstatics; i++) {
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'A') { is_str  = 1; break; }
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'D') { is_dbl  = 1; break; }
            if (!strcmp(ij_statics[i], fld) && (ij_static_types[i] == 'L' || ij_static_types[i] == 'R')) { is_list = 1; break; }
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'T') { is_table= 1; break; }
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'O') { is_obj  = 1; break; }
        }
        if (is_str) {
            ij_declare_static_str(fld);
            ij_get_str_field(fld);
        } else if (is_list) {
            ij_declare_static_list(fld);
            ij_get_list_field(fld);
        } else if (is_table) {
            ij_declare_static_table(fld);
            ij_get_table_field(fld);
        } else if (is_dbl) {
            ij_declare_static_dbl(fld);
            ij_get_dbl(fld);
        } else if (is_obj) {
            /* Record variable — push 0L as placeholder; ICN_FIELD relay will reload Object */
            ij_declare_static_obj(fld);
            JI("lconst_0","");
        } else {
            ij_declare_static(fld);
            ij_get_long(fld);
        }
    } else {
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        /* Check global type */
        int is_str = 0, is_dbl = 0, is_list = 0, is_table = 0, is_obj = 0;
        for (int i = 0; i < ij_nstatics; i++) {
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'A') { is_str  = 1; break; }
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'D') { is_dbl  = 1; break; }
            if (!strcmp(ij_statics[i], gname) && (ij_static_types[i] == 'L' || ij_static_types[i] == 'R')) { is_list = 1; break; }
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'T') { is_table= 1; break; }
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'O') { is_obj  = 1; break; }
        }
        if (is_str)        { ij_declare_static_str(gname);   ij_get_str_field(gname); }
        else if (is_list)  { ij_declare_static_list(gname);  ij_get_list_field(gname); }
        else if (is_table) { ij_declare_static_table(gname); ij_get_table_field(gname); }
        else if (is_dbl)   { ij_declare_static_dbl(gname);   ij_get_dbl(gname); }
        else if (is_obj)   { ij_declare_static_obj(gname);   JI("lconst_0",""); }
        else              { ij_declare_static(gname);      ij_get_long(gname); }
    }
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_ASSIGN — E1 := E2
 * Evaluate RHS, store into LHS variable slot
 * ======================================================================= */
static void ij_emit_assign(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 2) {
        /* degenerate */
        int id = ij_new_id(); char a[64], b[64];
        lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
        strncpy(oα,a,63); strncpy(oβ,b,63);
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }

    /* -----------------------------------------------------------------------
     * EARLY EXIT: p.field := v   (record field assignment)
     * Flow: eval v → relay → box → load record obj → putfield → v_long → γ
     * --------------------------------------------------------------------- */
    {
        IcnNode *lhs = n->children[0];
        IcnNode *rhs = n->children[1];
        if (lhs && lhs->kind == ICN_FIELD && lhs->nchildren >= 2) {
            IcnNode *rec_expr  = lhs->children[0];
            IcnNode *field_var = lhs->children[1];
            const char *fname  = field_var->val.sval;
            /* Determine record type */
            const char *rtype = NULL;
            for (int ri = 0; ri < ij_nrec && !rtype; ri++)
                for (int fi = 0; fi < ij_rec_nfields[ri]; fi++)
                    if (!strcmp(ij_rec_fields[ri][fi], fname)) { rtype = ij_rec_names[ri]; break; }
            if (rtype && rec_expr && rec_expr->kind == ICN_VAR) {
                int sid = ij_new_id();
                char a[64], b[64]; lbl_α(sid,a,sizeof a); lbl_β(sid,b,sizeof b);
                strncpy(oα,a,63); strncpy(oβ,b,63);
                char v_relay[64]; snprintf(v_relay, sizeof v_relay, "icn_%d_fa_vr", sid);
                char val_long[80]; snprintf(val_long, sizeof val_long, "icn_%d_fa_vl", sid);
                char val_obj[80];  snprintf(val_obj,  sizeof val_obj,  "icn_%d_fa_vo", sid);
                ij_declare_static(val_long);
                ij_declare_static_obj(val_obj);
                IjPorts vp; strncpy(vp.γ, v_relay, 63); strncpy(vp.ω, ports.ω, 63);
                char va[64], vb[64]; ij_emit_expr(rhs, vp, va, vb);
                JL(a); JGoto(va);
                JL(b); JGoto(vb);
                JL(v_relay);
                /* Save long value */
                JI("dup2",""); ij_put_long(val_long);
                /* Box to Object */
                JI("invokestatic","java/lang/Long/valueOf(J)Ljava/lang/Long;");
                ij_put_obj_field(val_obj);
                /* Load record object */
                char recfld[128];
                int slot = ij_locals_find(rec_expr->val.sval);
                if (slot >= 0) ij_var_field(rec_expr->val.sval, recfld, sizeof recfld);
                else           snprintf(recfld, sizeof recfld, "icn_gvar_%s", rec_expr->val.sval);
                J("    getstatic %s/%s Ljava/lang/Object;\n", ij_classname, recfld);
                J("    checkcast %s$%s\n", ij_classname, rtype);
                /* Load boxed value */
                ij_get_obj_field(val_obj);
                /* putfield */
                J("    putfield %s$%s/%s Ljava/lang/Object;\n", ij_classname, rtype, fname);
                /* result = original long value */
                ij_get_long(val_long);
                JGoto(ports.γ);
                return;
            }
        }
    }

    /* -----------------------------------------------------------------------
     * EARLY EXIT: t[k] := v   (subscript-LHS table assignment)
     * Handle this BEFORE any generic emit to avoid mid-relay stack confusion.
     * Flow: eval v → v_relay → box+save; eval k → k_relay → str-convert+save;
     *       load T; load k_str; load v_obj; HashMap.put; pop; load v_long → γ
     * --------------------------------------------------------------------- */
    {
        IcnNode *lhs = n->children[0];
        IcnNode *rhs = n->children[1];
        if (lhs && lhs->kind == ICN_SUBSCRIPT && lhs->nchildren >= 2
                && ij_expr_is_table(lhs->children[0])) {
            IcnNode *tvar  = lhs->children[0];
            IcnNode *kexpr = lhs->children[1];
            int sid = ij_new_id();
            char a[64], b[64]; lbl_α(sid,a,sizeof a); lbl_β(sid,b,sizeof b);
            strncpy(oα,a,63); strncpy(oβ,b,63);

            char v_relay[64]; snprintf(v_relay, sizeof v_relay, "icn_%d_tsa_vr", sid);
            char k_relay[64]; snprintf(k_relay, sizeof k_relay, "icn_%d_tsa_kr", sid);
            char do_put[64];  snprintf(do_put,  sizeof do_put,  "icn_%d_tsa_put",sid);
            char val_long_fld[80]; snprintf(val_long_fld, sizeof val_long_fld, "icn_%d_tsa_vl", sid);
            char val_obj_fld[80];  snprintf(val_obj_fld,  sizeof val_obj_fld,  "icn_%d_tsa_vo", sid);
            char k_str_fld[80];    snprintf(k_str_fld,    sizeof k_str_fld,    "icn_%d_tsa_ks", sid);
            ij_declare_static(val_long_fld);
            ij_declare_static_obj(val_obj_fld);
            ij_declare_static_str(k_str_fld);

            /* Emit RHS v */
            IjPorts vp; strncpy(vp.γ, v_relay, 63); strncpy(vp.ω, ports.ω, 63);
            char va[64], vb[64]; ij_emit_expr(rhs, vp, va, vb);

            /* Emit key k */
            IjPorts kp; strncpy(kp.γ, k_relay, 63); strncpy(kp.ω, ports.ω, 63);
            char ka[64], kb[64]; ij_emit_expr(kexpr, kp, ka, kb);

            /* α/β entry points */
            JL(a); JGoto(va);
            JL(b); JGoto(ports.ω);

            /* v_relay: RHS long on stack → save long + box to obj */
            JL(v_relay);
            JI("dup2",""); ij_put_long(val_long_fld);
            JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
            ij_put_obj_field(val_obj_fld);
            /* Now evaluate key */
            JGoto(ka);

            /* k_relay: key long on stack → convert to String key */
            JL(k_relay);
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
            ij_put_str_field(k_str_fld);
            JGoto(do_put);

            /* do_put: load T, k_str, v_obj; call put; pop result */
            JL(do_put);
            {
                char taf[128]; ij_var_field(tvar->val.sval, taf, sizeof taf);
                ij_get_table_field(taf);
            }
            ij_get_str_field(k_str_fld);
            ij_get_obj_field(val_obj_fld);
            JI("invokevirtual",
               "java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
            JI("pop","");
            /* result = v (long) */
            ij_get_long(val_long_fld);
            JGoto(ports.γ);
            return;
        }
    }

    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char store[64]; snprintf(store, sizeof store, "icn_%d_store", id);

    IjPorts rp; strncpy(rp.γ, store, 63); strncpy(rp.ω, ports.ω, 63);
    char ra[64], rb[64];
    ij_emit_expr(n->children[1], rp, ra, rb);

    JL(a); JGoto(ra);
    JL(b); JGoto(rb);
    JL(store);

    IcnNode *lhs = n->children[0];
    IcnNode *rhs = n->children[1];
    int is_str  = ij_expr_is_string(rhs);
    int is_list = !is_str && ij_expr_is_list(rhs);
    int is_tbl  = !is_str && !is_list && ij_expr_is_table(rhs);
    int is_dbl  = !is_str && !is_list && !is_tbl && ij_expr_is_real(rhs);
    int is_rec  = !is_str && !is_list && !is_tbl && !is_dbl && ij_expr_is_record(rhs);
    /* Bang of a record-list yields the Object directly on the stack (no 0L placeholder) */
    int is_rec_direct = is_rec && rhs->kind == ICN_BANG && ij_expr_is_record_list(rhs->children[0]);
    int is_reclist = is_list && ij_expr_is_record_list(rhs);

    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str)       { ij_declare_static_str(fld);   ij_put_str_field(fld); }
            else if (is_list) {
                if (is_reclist) ij_declare_static_reclist(fld);
                else            ij_declare_static_list(fld);
                ij_put_list_field(fld);
            }
            else if (is_tbl)  {
                ij_declare_static_table(fld); ij_put_table_field(fld);
                if (ij_pending_tdflt[0]) {
                    /* Copy dflt to var-based dflt field: {fld}_dflt */
                    char vdflt[144]; snprintf(vdflt, sizeof vdflt, "%s_dflt", fld);
                    ij_declare_static_obj(vdflt);
                    ij_get_obj_field(ij_pending_tdflt);
                    ij_put_obj_field(vdflt);
                    ij_pending_tdflt[0] = '\0';
                }
            }
            else if (is_dbl)  { ij_declare_static_dbl(fld);   ij_put_dbl(fld); }
            else if (is_rec)  {
                ij_declare_static_obj(fld);
                if (is_rec_direct) {
                    /* Object is already on the stack (bang of record-list) */
                    ij_put_obj_field(fld);
                } else {
                    /* Constructor: stack has 0L placeholder, record in icn_retval_obj */
                    JI("pop2","");
                    J("    getstatic %s/icn_retval_obj Ljava/lang/Object;\n", ij_classname);
                    ij_put_obj_field(fld);
                }
                /* lconst_0 pushed by push-back section below */
            }
            else              { ij_declare_static(fld);       ij_put_long(fld); }
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str)       { ij_declare_static_str(gname);   ij_put_str_field(gname); }
            else if (is_list) {
                if (is_reclist) ij_declare_static_reclist(gname);
                else            ij_declare_static_list(gname);
                ij_put_list_field(gname);
            }
            else if (is_tbl)  {
                ij_declare_static_table(gname); ij_put_table_field(gname);
                if (ij_pending_tdflt[0]) {
                    char vdflt[144]; snprintf(vdflt, sizeof vdflt, "%s_dflt", gname);
                    ij_declare_static_obj(vdflt);
                    ij_get_obj_field(ij_pending_tdflt);
                    ij_put_obj_field(vdflt);
                    ij_pending_tdflt[0] = '\0';
                }
            }
            else if (is_dbl)  { ij_declare_static_dbl(gname);   ij_put_dbl(gname); }
            else if (is_rec)  {
                ij_declare_static_obj(gname);
                if (is_rec_direct) {
                    ij_put_obj_field(gname);
                } else {
                    JI("pop2","");
                    J("    getstatic %s/icn_retval_obj Ljava/lang/Object;\n", ij_classname);
                    ij_put_obj_field(gname);
                }
                /* lconst_0 pushed by push-back section below */
            }
            else              { ij_declare_static(gname);       ij_put_long(gname); }
        }
    } else {
        /* discard — pop appropriate type */
        if (is_str || is_list || is_tbl) JI("pop", "");
        else                             JI("pop2", "");
    }
    /* Push back value for expression result (Icon := returns the value) */
    if (lhs && lhs->kind == ICN_VAR) {
        int slot = ij_locals_find(lhs->val.sval);
        if (slot >= 0) {
            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
            if (is_str)       ij_get_str_field(fld);
            else if (is_list) ij_get_list_field(fld);
            else if (is_tbl)  ij_get_table_field(fld);
            else if (is_dbl)  ij_get_dbl(fld);
            else if (is_rec)  JI("lconst_0","");
            else              ij_get_long(fld);
        } else {
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
            if (is_str)       ij_get_str_field(gname);
            else if (is_list) ij_get_list_field(gname);
            else if (is_tbl)  ij_get_table_field(gname);
            else if (is_dbl)  ij_get_dbl(gname);
            else if (is_rec)  JI("lconst_0","");
            else              ij_get_long(gname);
        }
    } else {
        if (is_str || is_list || is_tbl) JI("aconst_null", "");
        else if (is_dbl)                 JI("dconst_0", "");
        else                             JI("lconst_0", "");
    }
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_RETURN
 * Store value into icn_retval, set icn_failed=0, jump to ret label
 * ======================================================================= */
static void ij_emit_return(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)ports;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren > 0) {
        char after[64]; snprintf(after, sizeof after, "icn_%d_ret_store", id);
        char on_fail[64]; snprintf(on_fail, sizeof on_fail, "icn_%d_ret_fail", id);
        IjPorts vp; strncpy(vp.γ, after, 63); strncpy(vp.ω, on_fail, 63);
        char va[64], vb[64];
        ij_emit_expr(n->children[0], vp, va, vb);
        JL(a); JGoto(va);
        JL(b); JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
        /* on_fail: expr failed — no value on stack, jump to ret label */
        JL(on_fail);
        ij_set_fail();
        JGoto(ij_fail_label[0] ? ij_fail_label : "icn_dead");
        /* after: expr succeeded — long or String ref on stack */
        JL(after);
        if (ij_expr_is_string(n->children[0])) {
            ij_put_str_field("icn_retval_str");
            J("    lconst_0\n"); ij_put_long("icn_retval"); /* clear long slot */
        } else {
            ij_put_long("icn_retval");
        }
        ij_set_ok();
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
    } else {
        JL(a);
        JI("lconst_0", ""); ij_put_long("icn_retval");
        ij_set_ok();
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
        JL(b);
        JGoto(ij_ret_label[0] ? ij_ret_label : "icn_dead");
    }
}

/* =========================================================================
 * ICN_FAIL
 * ======================================================================= */
static void ij_emit_fail_node(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)n;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    JL(a);
    ij_set_fail();
    JGoto(ij_fail_label[0] ? ij_fail_label : ports.ω);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_SUSPEND — co-routine yield (user-defined generator)
 *
 * Encoding: each suspend site gets unique int ID.
 * α: evaluate E → store in icn_retval, set icn_suspended=1,
 *    set icn_suspend_id=N, set icn_failed=0, goto sret_label (yield)
 * β: indirect — the proc β entry does tableswitch → resume_here
 * resume_here: run optional body, then loop back to E's β
 * ======================================================================= */
static void ij_emit_suspend(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    (void)ports;
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Assign a unique suspend ID for tableswitch */
    int susp_id = ij_next_suspend_id++;
    if (ij_suspend_count < 64) ij_suspend_ids[ij_suspend_count++] = id;  /* Bug2 fix: store node id, not susp_id */

    char resume_here[64]; snprintf(resume_here, sizeof resume_here, "icn_%d_resume", id);
    char after_val[64];   snprintf(after_val,   sizeof after_val,   "icn_%d_yield",  id);

    IcnNode *val_node  = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren > 1) ? n->children[1] : NULL;

    char va[64], vb[64];
    if (val_node) {
        IjPorts vp;
        strncpy(vp.γ, after_val, 63);
        strncpy(vp.ω, ij_fail_label[0] ? ij_fail_label : "icn_dead", 63);
        ij_emit_expr(val_node, vp, va, vb);
    } else {
        snprintf(va, 80, "icn_%d_noval", id);
        snprintf(vb, 80, "icn_%d_nvlb", id);
        JL(va); JI("lconst_0",""); JGoto(after_val);
        JL(vb); JGoto(ij_fail_label[0] ? ij_fail_label : "icn_dead");
    }

    /* α: enter value evaluation */
    JL(a); JGoto(va);
    /* β: resume via tableswitch in proc β — just a target label here */
    JL(b); JGoto(resume_here);   /* proc β will dispatch here via tableswitch */

    /* after_val: value (long) on JVM stack — yield to caller */
    JL(after_val);
    ij_put_long("icn_retval");
    ij_set_ok();
    /* Mark suspended with this site's ID */
    JI("iconst_1",""); ij_put_byte("icn_suspended");
    J("    sipush %d\n", susp_id); ij_put_int_field("icn_suspend_id");
    /* Yield — jump to sret (bare return from method, frame reclaimed) */
    JGoto(ij_sret_label[0] ? ij_sret_label : "icn_dead");

    /* resume_here: execution resumes here after proc β dispatches */
    if (body_node) {
        /* Fix IJ-6 Bug2: body assignment leaves a long result on the JVM operand
         * stack.  ports.γ is the while-loop top (icn_0_top), also reached from the
         * relop with an EMPTY stack.  Entering it with a stale long gives:
         *   "Inconsistent stack height 0 != 2"
         * Route body γ and ω through a drain label: pop2 then loop back. */
        char body_done[64]; snprintf(body_done, sizeof body_done, "icn_%d_bdone", id);
        char ba[64], bb[64];
        IjPorts bp;
        strncpy(bp.γ, body_done, 63);
        strncpy(bp.ω, ports.γ, 63);  /* body fail: empty stack → jump direct, no pop */
        ij_emit_expr(body_node, bp, ba, bb);
        JL(body_done); JI("pop2", ""); JGoto(ports.γ);
        JL(resume_here); JGoto(ba);
    } else {
        JL(resume_here); JGoto(ports.γ);
    }
}

/* =========================================================================
/* =========================================================================
 * ICN_CASE — case E of { V1: R1  V2: R2 ... [default: RD] }
 * children: [0]=dispatch, [1]=v1,[2]=r1, [3]=v2,[4]=r2, ..., [last]=default_result
 * if (nc-1) is even → no default (odd clause pairs); if (nc-1) is odd → has default.
 * Semantics: eval dispatch once; compare to each Vi in order; first match → eval Ri → γ;
 * no match → eval default → γ (or fail → ω if no default).
 * ======================================================================= */
static void ij_emit_case(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 1) { JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return; }

    IcnNode *disp = n->children[0];
    int nc = n->nchildren;
    /* nc-1 remaining children: pairs (val,res) + optional default */
    int npairs = (nc - 1) / 2;
    int has_default = ((nc - 1) % 2 == 1);

    /* Dispatch value storage */
    char disp_fld[80]; snprintf(disp_fld, sizeof disp_fld, "icn_%d_case_disp", id);
    int disp_is_str = ij_expr_is_string(disp);
    int disp_is_dbl = !disp_is_str && ij_expr_is_real(disp);
    if (disp_is_str)      ij_declare_static_str(disp_fld);
    else if (disp_is_dbl) ij_declare_static_real(disp_fld);
    else                  ij_declare_static(disp_fld);

    /* Eval dispatch */
    char disp_relay[64]; snprintf(disp_relay, sizeof disp_relay, "icn_%d_case_dr", id);
    IjPorts dp; strncpy(dp.γ, disp_relay, 63); strncpy(dp.ω, ports.ω, 63);
    char da[64], db[64]; ij_emit_expr(disp, dp, da, db);
    JL(a); JGoto(da);
    JL(b); JGoto(db);
    JL(disp_relay);
    if (disp_is_str)      ij_put_str_field(disp_fld);
    else if (disp_is_dbl) ij_put_dbl(disp_fld);
    else                  ij_put_long(disp_fld);

    /* For each clause: eval Vi, compare to dispatch, branch to Ri or next */
    for (int ci = 0; ci < npairs; ci++) {
        IcnNode *vnode = n->children[1 + ci*2];
        IcnNode *rnode = n->children[2 + ci*2];
        char vcmp[64];  snprintf(vcmp,  sizeof vcmp,  "icn_%d_cv%d",  id, ci);
        char vmatch[64];snprintf(vmatch,sizeof vmatch,"icn_%d_cm%d",  id, ci);
        char vnext[64]; snprintf(vnext, sizeof vnext, "icn_%d_cn%d",  id, ci);

        /* Eval clause value */
        char vrelay[64]; snprintf(vrelay, sizeof vrelay, "icn_%d_cvr%d", id, ci);
        IjPorts vp; strncpy(vp.γ, vrelay, 63); strncpy(vp.ω, vnext, 63);
        char va[64], vb[64]; ij_emit_expr(vnode, vp, va, vb);
        JL(vcmp); JGoto(va);
        JL(vrelay);
        /* Compare: if disp_is_str: String.equals; else lcmp */
        if (disp_is_str) {
            /* stack: String (clause val) */
            ij_get_str_field(disp_fld);
            JI("invokevirtual", "java/lang/String/equals(Ljava/lang/Object;)Z");
            J("    ifne %s\n", vmatch);
        } else {
            /* stack: long (clause val); compare to stored disp */
            ij_get_long(disp_fld);
            JI("lcmp", "");
            J("    ifeq %s\n", vmatch);
        }
        JGoto(vnext);

        /* Result branch: emit result expression first, then wire vmatch → ra */
        {
            char rrelay[64]; snprintf(rrelay, sizeof rrelay, "icn_%d_crr%d", id, ci);
            IjPorts rp; strncpy(rp.γ, rrelay, 63); strncpy(rp.ω, ports.ω, 63);
            char ra[64], rb[64]; ij_emit_expr(rnode, rp, ra, rb);
            JL(vmatch); JGoto(ra);
            JL(rrelay); JGoto(ports.γ);
        }

        JL(vnext);
        /* Falls through to next clause vcmp, or exits on last clause */
        if (ci == npairs - 1) {
            if (has_default) {
                /* Will jump to dstart after default expr is emitted below */
                /* Store dstart name for use after the loop */
                char dstart_tmp[64]; snprintf(dstart_tmp, sizeof dstart_tmp, "icn_%d_cdef", id);
                JGoto(dstart_tmp);
            } else {
                JGoto(ports.ω);
            }
        }
    }

    /* Default or fail */
    if (has_default) {
        IcnNode *dnode = n->children[nc-1];
        char drelay[64]; snprintf(drelay, sizeof drelay, "icn_%d_cdefr", id);
        char dstart[64]; snprintf(dstart, sizeof dstart, "icn_%d_cdef",  id);
        IjPorts defp; strncpy(defp.γ, drelay, 63); strncpy(defp.ω, ports.ω, 63);
        char dea[64], deb[64]; ij_emit_expr(dnode, defp, dea, deb);
        JL(dstart); JGoto(dea);
        JL(drelay); JGoto(ports.γ);
    }
}

/* =========================================================================
 * ICN_IF — if cond then E2 [else E3]
 * ======================================================================= */
static void ij_emit_if(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond  = n->children[0];
    IcnNode *thenb = (n->nchildren > 1) ? n->children[1] : NULL;
    IcnNode *elseb = (n->nchildren > 2) ? n->children[2] : NULL;

    char cond_then[64]; snprintf(cond_then, sizeof cond_then, "icn_%d_then", id);
    char cond_else[64]; snprintf(cond_else, sizeof cond_else, "icn_%d_else", id);

    char then_a[64]="", then_b[64]="", else_a[64]="";
    if (thenb) {
        IjPorts tp; strncpy(tp.γ,ports.γ,63); strncpy(tp.ω,ports.ω,63);
        ij_emit_expr(thenb, tp, then_a, then_b);
    } else { strncpy(then_a,ports.γ,63); }

    if (elseb) {
        IjPorts ep; strncpy(ep.γ,ports.γ,63); strncpy(ep.ω,ports.ω,63);
        char ea[64], eb[64];
        ij_emit_expr(elseb, ep, ea, eb);
        strncpy(else_a, ea, 63);
    } else { strncpy(else_a, ports.ω, 63); }

    IjPorts cp; strncpy(cp.γ,cond_then,63); strncpy(cp.ω,cond_else,63);
    char ca[64], cb[64];
    ij_emit_expr(cond, cp, ca, cb);

    /* cond_then: condition succeeded — value on stack, discard it.
     * Use pop for String (1 slot), pop2 for long/double (2 slots). */
    JL(cond_then);
    if (ij_expr_is_string(cond)) JI("pop","");
    else                         JI("pop2","");
    JGoto(thenb ? then_a : ports.γ);

    JL(cond_else);
    JGoto(else_a);

    JL(a); JGoto(ca);
    JL(b); JGoto(cb);
}

/* =========================================================================
 * ICN_CALL — write() built-in or user procedure
 * ======================================================================= */
static void ij_emit_call(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 1) { ij_emit_fail_node(NULL,ports,oα,oβ); return; }
    IcnNode *fn = n->children[0];
    int nargs = n->nchildren - 1;
    const char *fname = (fn->kind == ICN_VAR) ? fn->val.sval : "unknown";
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* --- record constructor: RecordType(v1, v2, ...) --- */
    if (ij_is_record_type(fname)) {
        int nf = ij_record_nfields(fname);
        /* Relay labels per arg */
        char **relays = malloc((nargs+1) * sizeof(char*));
        char **arg_flds = malloc(nargs * sizeof(char*));
        for (int i = 0; i <= nargs; i++) {
            relays[i] = malloc(64);
            snprintf(relays[i], 64, "icn_%d_rc_r%d", id, i);
        }
        for (int i = 0; i < nargs; i++) {
            arg_flds[i] = malloc(80);
            snprintf(arg_flds[i], 80, "icn_%d_rc_a%d", id, i);
            ij_declare_static_obj(arg_flds[i]);
        }
        /* Static Object field to hold the new record object */
        char rec_obj_fld[80]; snprintf(rec_obj_fld, sizeof rec_obj_fld, "icn_%d_rc_obj", id);
        ij_declare_static_obj(rec_obj_fld);

        /* Emit each arg expression, chained through relays */
        char (*arg_a)[64] = malloc(nargs * sizeof(*arg_a));
        char (*arg_b)[64] = malloc(nargs * sizeof(*arg_b));
        for (int i = 0; i < nargs; i++) {
            const char *next_relay = (i+1 < nargs) ? relays[i+1] : relays[nargs];
            IjPorts ap; strncpy(ap.γ, (i < nargs-1) ? relays[i+1] : relays[nargs], 63);
            strncpy(ap.ω, ports.ω, 63);
            /* override gamma to relay through constructor assembly */
            snprintf(ap.γ, 64, "icn_%d_rc_r%d", id, i+1);
            ij_emit_expr(n->children[1+i], ap, arg_a[i], arg_b[i]);
            (void)next_relay;
        }

        JL(a); if (nargs > 0) JGoto(arg_a[0]); else JGoto(relays[0]);
        JL(b); JGoto(ports.ω);

        /* build label — jumped to after all args boxed */
        char build_lbl[64]; snprintf(build_lbl, sizeof build_lbl, "icn_%d_rc_build", id);

        /* arg relay chain: box each arg into its Object static */
        for (int i = 0; i < nargs; i++) {
            JL(relays[i+1]);  /* relay[i+1] is gamma of arg[i] */
            JI("invokestatic","java/lang/Long/valueOf(J)Ljava/lang/Long;");
            ij_put_obj_field(arg_flds[i]);
            if (i+1 < nargs) JGoto(arg_a[i+1]);
            else              JGoto(build_lbl);  /* all args done → build */
        }

        /* Build the record object */
        if (nargs == 0) JL(relays[0]);
        JL(build_lbl);
        J("    new %s$%s\n", ij_classname, fname);
        JI("dup","");
        J("    invokespecial %s$%s/<init>()V\n", ij_classname, fname);
        /* putfield for each arg */
        for (int i = 0; i < nargs && i < nf; i++) {
            const char *fldname = ij_record_field(fname, i);
            if (!fldname) continue;
            JI("dup","");
            ij_get_obj_field(arg_flds[i]);
            J("    putfield %s$%s/%s Ljava/lang/Object;\n", ij_classname, fname, fldname);
        }
        /* Store object reference into rec_obj_fld (as Object) */
        ij_put_obj_field(rec_obj_fld);
        /* Also store into icn_retval_obj so ICN_ASSIGN can retrieve it */
        ij_get_obj_field(rec_obj_fld);
        J("    putstatic %s/icn_retval_obj Ljava/lang/Object;\n", ij_classname);

        /* Result: push 0L as numeric placeholder, then γ */
        JI("lconst_0","");
        char done_lbl[64]; snprintf(done_lbl, sizeof done_lbl, "icn_%d_rc_done", id);
        JGoto(ports.γ);

        for (int i = 0; i <= nargs; i++) free(relays[i]);
        for (int i = 0; i < nargs; i++) free(arg_flds[i]);
        free(relays); free(arg_flds); free(arg_a); free(arg_b);
        return;
    }

    /* --- built-in write --- */
    if (strcmp(fname, "write") == 0) {
        if (nargs == 0) {
            JL(a);
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            JI("ldc", "\"\"");
            JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
            JGoto(ports.γ);
            JL(b); JGoto(ports.ω);
            return;
        }
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_call", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(arg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        /* Value on stack: String ref or long depending on arg type */
        if (ij_expr_is_string(arg)) {
            /* String ref on stack — store to scratch, push stream, load, println */
            int scratch = ij_alloc_int_scratch();
            J("    astore %d\n", scratch);
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    aload %d\n", scratch);
            JI("invokevirtual", "java/io/PrintStream/println(Ljava/lang/String;)V");
            /* write() returns its argument — reload String for caller */
            J("    aload %d\n", scratch);
        } else if (ij_expr_is_real(arg)) {
            /* Double on stack — print as real */
            int scratch = ij_locals_alloc_tmp();
            J("    dstore %d\n", slot_jvm(scratch));
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    dload %d\n", slot_jvm(scratch));
            JI("invokevirtual", "java/io/PrintStream/println(D)V");
            J("    dload %d\n", slot_jvm(scratch));
        } else {
            /* Long on stack — print as integer */
            int scratch = ij_locals_alloc_tmp();
            J("    lstore %d\n", slot_jvm(scratch));
            JI("getstatic", "java/lang/System/out Ljava/io/PrintStream;");
            J("    lload %d\n", slot_jvm(scratch));
            JI("invokevirtual", "java/io/PrintStream/println(J)V");
            /* Reload: write() returns its argument */
            J("    lload %d\n", slot_jvm(scratch));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- built-in read() --- read one line from stdin; fail on EOF ---
     * Returns a String value on γ, fails (→ ω) on EOF.
     * Uses a static BufferedReader field icn_stdin_reader (Object slot).
     * Lazy-initialised on first call via a static int flag icn_stdin_init.
     * JVM: invokestatic icn_builtin_read_line()Ljava/lang/String; → null on EOF */
    if (strcmp(fname, "read") == 0 && nargs == 0 && !ij_is_user_proc(fname)) {
        ij_declare_static_obj("icn_stdin_reader");
        ij_declare_static_int("icn_stdin_init");
        char init_lbl[64], call_lbl[64], null_lbl[64];
        snprintf(init_lbl, sizeof init_lbl, "icn_%d_rd_init",  id);
        snprintf(call_lbl, sizeof call_lbl, "icn_%d_rd_call",  id);
        snprintf(null_lbl, sizeof null_lbl, "icn_%d_rd_null",  id);

        /* α: lazily construct BufferedReader wrapping System.in */
        JL(a);
        ij_get_int_field("icn_stdin_init");
        J("    ifne %s\n", call_lbl);
        /* First call: build reader */
        JI("new", "java/io/BufferedReader");
        JI("dup", "");
        JI("new", "java/io/InputStreamReader");
        JI("dup", "");
        JI("getstatic", "java/lang/System/in Ljava/io/InputStream;");
        JI("invokespecial", "java/io/InputStreamReader/<init>(Ljava/io/InputStream;)V");
        JI("invokespecial", "java/io/BufferedReader/<init>(Ljava/io/Reader;)V");
        /* store reader */
        { char buf[384]; snprintf(buf,sizeof buf,"%s/icn_stdin_reader Ljava/lang/Object;",ij_classname);
          JI("putstatic", buf); }
        JI("iconst_1", ""); ij_put_int_field("icn_stdin_init");
        JL(call_lbl);
        /* load reader, cast, call readLine() */
        { char buf[384]; snprintf(buf,sizeof buf,"%s/icn_stdin_reader Ljava/lang/Object;",ij_classname);
          JI("getstatic", buf); }
        JI("checkcast", "java/io/BufferedReader");
        JI("invokevirtual", "java/io/BufferedReader/readLine()Ljava/lang/String;");
        /* null → EOF → ω */
        JI("dup", "");
        J("    ifnull %s\n", null_lbl);
        JGoto(ports.γ);         /* String ref on stack */
        JL(null_lbl);
        JI("pop", "");          /* discard null */
        JGoto(ports.ω);
        JL(b); JGoto(ports.ω);
        return;
    }

    /* --- built-in reads(n) --- read n bytes from stdin; fail on EOF ---
     * Reads exactly n chars via BufferedReader.read(char[],0,n).
     * Returns String on γ, fails on EOF / 0 bytes read. */
    if (strcmp(fname, "reads") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        ij_declare_static_obj("icn_stdin_reader");
        ij_declare_static_int("icn_stdin_init");
        IcnNode *arg = n->children[1];
        char after[64], init_lbl[64], call_lbl[64], eof_lbl[64];
        snprintf(after,    sizeof after,    "icn_%d_rds_after", id);
        snprintf(init_lbl, sizeof init_lbl, "icn_%d_rds_init",  id);
        snprintf(call_lbl, sizeof call_lbl, "icn_%d_rds_call",  id);
        snprintf(eof_lbl,  sizeof eof_lbl,  "icn_%d_rds_eof",   id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);

        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        /* arg (long) on stack → cast to int */
        JI("l2i", "");
        int n_slot = ij_alloc_int_scratch();
        J("    istore %d\n", n_slot);
        /* lazy-init reader */
        ij_get_int_field("icn_stdin_init");
        J("    ifne %s\n", call_lbl);
        JI("new", "java/io/BufferedReader");
        JI("dup", "");
        JI("new", "java/io/InputStreamReader");
        JI("dup", "");
        JI("getstatic", "java/lang/System/in Ljava/io/InputStream;");
        JI("invokespecial", "java/io/InputStreamReader/<init>(Ljava/io/InputStream;)V");
        JI("invokespecial", "java/io/BufferedReader/<init>(Ljava/io/Reader;)V");
        { char buf[384]; snprintf(buf,sizeof buf,"%s/icn_stdin_reader Ljava/lang/Object;",ij_classname);
          JI("putstatic", buf); }
        JI("iconst_1", ""); ij_put_int_field("icn_stdin_init");
        JL(call_lbl);
        /* allocate char[] of size n */
        J("    iload %d\n", n_slot);
        JI("newarray", "char");
        int arr_slot = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(arr_slot));
        /* load reader */
        { char buf[384]; snprintf(buf,sizeof buf,"%s/icn_stdin_reader Ljava/lang/Object;",ij_classname);
          JI("getstatic", buf); }
        JI("checkcast", "java/io/BufferedReader");
        J("    aload %d\n", slot_jvm(arr_slot));
        JI("iconst_0", "");
        J("    iload %d\n", n_slot);
        JI("invokevirtual", "java/io/BufferedReader/read([CII)I");
        /* result: -1 = EOF, 0 = nothing read → ω; else build String */
        JI("dup", "");
        J("    ifle %s\n", eof_lbl);
        /* nread on stack */
        int nread_slot = ij_alloc_int_scratch();
        J("    istore %d\n", nread_slot);
        JI("new", "java/lang/String");
        JI("dup", "");
        J("    aload %d\n", slot_jvm(arr_slot));
        JI("iconst_0", "");
        J("    iload %d\n", nread_slot);
        JI("invokespecial", "java/lang/String/<init>([CII)V");
        JGoto(ports.γ);
        JL(eof_lbl);
        JI("pop", "");
        JGoto(ports.ω);
        return;
    }

    /* --- built-in integer(x) --- convert real or string to long */
    if (strcmp(fname, "integer") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_int_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_real(arg))        JI("d2l", "");   /* double → long */
        else if (ij_expr_is_string(arg)) {
            /* String → Long.parseLong */
            int scratch = ij_alloc_int_scratch();
            J("    astore %d\n", scratch);
            J("    aload %d\n",  scratch);
            JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        }
        /* else already long — no conversion needed */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in real(x) --- convert long or string to double */
    if (strcmp(fname, "real") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_real_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            int scratch = ij_alloc_int_scratch();
            J("    astore %d\n", scratch);
            J("    aload %d\n",  scratch);
            JI("invokestatic", "java/lang/Double/parseDouble(Ljava/lang/String;)D");
        } else if (!ij_expr_is_real(arg)) {
            JI("l2d", "");  /* long → double */
        }
        /* else already double — no conversion */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in string(x) --- convert numeric to String */
    if (strcmp(fname, "string") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_str_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            /* already a String — no conversion */
        } else if (ij_expr_is_real(arg)) {
            JI("invokestatic", "java/lang/Double/toString(D)Ljava/lang/String;");
        } else {
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        }
        JGoto(ports.γ);
        return;
    }

    /* --- built-in any(cs) ---
     * Only fires if 'any' is NOT a user-defined procedure in this file. */
    if (strcmp(fname, "any") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        JC("any: cs String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        /* stack: cs_String */
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        /* invoke helper: icn_builtin_any(cs, subj, pos) → long newpos or -1 */
        JI("invokestatic", ij_classname_buf("icn_builtin_any(Ljava/lang/String;Ljava/lang/String;I)J"));
        /* result: long on stack — lconst_m1 check */
        J("    dup2\n");                         /* [res res] */
        J("    lconst_1\n"); J("    lneg\n");    /* [res res -1L] */
        J("    lcmp\n");                         /* [res cmp] */
        J("    ifne %s\n", chk);                /* cmp!=0 → ok (not -1) */
        J("    pop2\n");                         /* discard -1L result */
        JGoto(ports.ω);
        JL(chk);
        JC("any ok: advance icn_pos, push newpos");
        /* newpos is on stack as long = (icn_pos+2); update icn_pos = newpos-1 (0-based) */
        J("    dup2\n");
        J("    lconst_1\n"); J("    lsub\n");    /* newpos-1 = new 0-based pos */
        J("    l2i\n");
        ij_put_int_field("icn_pos");             /* store updated pos */
        /* newpos long remains on stack → ports.γ */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in many(cs) --- only if not user-defined */
    if (strcmp(fname, "many") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(arg_b);
        JL(after);
        JC("many: cs String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_many(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n");
        J("    lconst_1\n"); J("    lneg\n");
        J("    lcmp\n");
        J("    ifne %s\n", chk);
        J("    pop2\n");
        JGoto(ports.ω);
        JL(chk);
        JC("many ok: advance icn_pos");
        J("    dup2\n");
        J("    lconst_1\n"); J("    lsub\n");
        J("    l2i\n");
        ij_put_int_field("icn_pos");
        JGoto(ports.γ);
        return;
    }

    /* --- built-in upto(cs) --- only if not user-defined */
    if (strcmp(fname, "upto") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *csarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_after", id);
        char step[64];  snprintf(step,  sizeof step,  "icn_%d_step",  id);
        char chk[64];   snprintf(chk,   sizeof chk,   "icn_%d_chk",  id);
        /* Allocate a static String field to hold the cset across resume */
        char cs_fld[64]; snprintf(cs_fld, sizeof cs_fld, "icn_upto_cs_%d", id);
        ij_declare_static_str(cs_fld);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(csarg, ap, arg_a, arg_b);
        /* α: evaluate cs, store, then enter step */
        JL(a); JGoto(arg_a);
        JL(after);
        JC("upto α: save cs, enter step");
        ij_put_str_field(cs_fld);         /* consume cs String from stack */
        JGoto(step);
        /* β: cs already saved, re-enter step (pos already advanced) */
        JL(b); JGoto(step);
        /* step: scan forward until char in cs or end */
        JL(step);
        JC("upto step");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field(cs_fld);
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_upto_step(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n");
        J("    lconst_1\n"); J("    lneg\n");
        J("    lcmp\n");
        J("    ifne %s\n", chk);
        J("    pop2\n");
        JGoto(ports.ω);
        JL(chk);
        JC("upto: advance icn_pos past match, yield pos");
        /* result long = matched position (1-based); advance icn_pos to pos (0-based = result) */
        J("    dup2\n");
        J("    l2i\n");
        ij_put_int_field("icn_pos");      /* icn_pos = result (0-based next scan start) */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in find(s1, s2) --- generator: every occurrence of s1 in s2
     * α: eval s1, eval s2, store both, reset pos=0, goto check
     * β: advance pos (pos was 1-based result; next search starts at pos), goto check
     * check: icn_builtin_find(s1,s2,pos) → -1L → ω; else store pos=result, push result → γ */
    if (strcmp(fname, "find") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *s1arg = n->children[1];
        IcnNode *s2arg = n->children[2];
        char after1[64], after2[64], chk[64], ok[64];
        snprintf(after1, sizeof after1, "icn_%d_fa1",  id);
        snprintf(after2, sizeof after2, "icn_%d_fa2",  id);
        snprintf(chk,    sizeof chk,    "icn_%d_fchk", id);
        snprintf(ok,     sizeof ok,     "icn_%d_fok",  id);
        char s1f[64], s2f[64], pf[64];
        snprintf(s1f, sizeof s1f, "icn_find_s1_%d",  id);
        snprintf(s2f, sizeof s2f, "icn_find_s2_%d",  id);
        snprintf(pf,  sizeof pf,  "icn_find_pos_%d", id);
        ij_declare_static_str(s1f);
        ij_declare_static_str(s2f);
        ij_declare_static_int(pf);
        IjPorts ap1; strncpy(ap1.γ, after1, 63); strncpy(ap1.ω, ports.ω, 63);
        char a1[64], b1[64];
        ij_emit_expr(s1arg, ap1, a1, b1);
        IjPorts ap2; strncpy(ap2.γ, after2, 63); strncpy(ap2.ω, ports.ω, 63);
        char a2[64], b2[64];
        ij_emit_expr(s2arg, ap2, a2, b2);
        /* α */
        JL(a); JGoto(a1);
        JL(after1); ij_put_str_field(s1f); JGoto(a2);
        JL(after2); ij_put_str_field(s2f);
        J("    iconst_0\n"); ij_put_int_field(pf);
        JGoto(chk);
        /* β: last result was 1-based idx; next search from that same 0-based pos (idx itself) */
        JL(b);
        ij_get_int_field(pf);            /* pf holds 1-based last result */
        ij_put_int_field(pf);            /* no change — s2.indexOf scans from pf (0-based=pf-1+1=pf) */
        JGoto(chk);
        /* check */
        JL(chk);
        ij_get_str_field(s1f);
        ij_get_str_field(s2f);
        ij_get_int_field(pf);
        JI("invokestatic", ij_classname_buf("icn_builtin_find(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n    lconst_1\n    lneg\n    lcmp\n    ifne %s\n", ok);
        J("    pop2\n"); JGoto(ports.ω);
        JL(ok);
        JC("find ok: store result as new pos, push result → γ");
        J("    dup2\n    l2i\n"); ij_put_int_field(pf);
        JGoto(ports.γ);
        return;
    }

    /* --- built-in match(s) — one-shot: match s at current icn_pos
     * Calls icn_builtin_match(s, subject, pos) → new 1-based pos or -1L.
     * On success: advance icn_pos = result-1 (0-based), push result → γ. */
    if (strcmp(fname, "match") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_ma", id);
        snprintf(ok,    sizeof ok,    "icn_%d_mo", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(sarg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("match: s String on stack");
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_match(Ljava/lang/String;Ljava/lang/String;I)J"));
        J("    dup2\n    lconst_1\n    lneg\n    lcmp\n    ifne %s\n", ok);
        J("    pop2\n"); JGoto(ports.ω);
        JL(ok);
        JC("match ok: icn_pos = result-1 (0-based), push result → γ");
        J("    dup2\n    l2i\n    iconst_1\n    isub\n");
        ij_put_int_field("icn_pos");
        JGoto(ports.γ);
        return;
    }

    /* --- built-in tab(n) — one-shot: substring from icn_pos to n-1 (0-based), returns String
     * Calls icn_builtin_tab_str(n_int, subj, pos) → String or null on failure.
     * On success: icn_pos = n-1, push String → γ. */
    if (strcmp(fname, "tab") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *narg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_ta", id);
        snprintf(ok,    sizeof ok,    "icn_%d_to", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(narg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("tab: n (long) on stack");
        J("    l2i\n");   /* convert long n to int */
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_tab_str(ILjava/lang/String;I)Ljava/lang/String;"));
        J("    dup\n    ifnonnull %s\n", ok);
        J("    pop\n"); JGoto(ports.ω);
        JL(ok);
        JC("tab ok: advance icn_pos = n-1, push String → γ");
        /* Helper already updated icn_pos — just push String */
        JGoto(ports.γ);
        return;
    }

    /* --- built-in move(n) — one-shot: substring of length n from icn_pos, returns String
     * Calls icn_builtin_move_str(n_int, subj, pos) → String or null on failure.
     * On success: icn_pos = pos+n, push String → γ. */
    if (strcmp(fname, "move") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *narg = n->children[1];
        char after[64], ok[64];
        snprintf(after, sizeof after, "icn_%d_mva", id);
        snprintf(ok,    sizeof ok,    "icn_%d_mvo", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char arg_a[64], arg_b[64];
        ij_emit_expr(narg, ap, arg_a, arg_b);
        JL(a); JGoto(arg_a);
        JL(b); JGoto(ports.ω);
        JL(after);
        JC("move: n (long) on stack");
        J("    l2i\n");   /* convert long n to int */
        ij_declare_static_str("icn_subject");
        ij_declare_static_int("icn_pos");
        ij_get_str_field("icn_subject");
        ij_get_int_field("icn_pos");
        JI("invokestatic", ij_classname_buf("icn_builtin_move_str(ILjava/lang/String;I)Ljava/lang/String;"));
        J("    dup\n    ifnonnull %s\n", ok);
        J("    pop\n"); JGoto(ports.ω);
        JL(ok);
        JC("move ok: icn_pos advanced by helper, push String → γ");
        JGoto(ports.γ);
        return;
    }

    /* === LIST BUILTINS ===
     * push(L, v) — prepend v to front of list (index 0); returns L
     * put(L, v)  — append v to back of list; returns L
     * get(L)/pop(L) — remove+return first element; fail if empty
     * pull(L)    — remove+return last element; fail if empty
     * list(n, x) — create list of n copies of x
     * All guarded: !ij_is_user_proc(fname)
     */

    /* Helper: determine element unbox opcode from the list variable's elem type.
     * We use a simple heuristic: if v arg is string → checkcast String (no unbox);
     * if real → checkcast Double + doubleValue()D + d2l for long return;
     * else → checkcast Long + longValue()J. */
#define LIST_UNBOX_LONG(label_ok) \
    do { \
        J("    dup\n    ifnonnull %s\n", label_ok); \
        J("    pop\n"); JGoto(ports.ω); \
        JL(label_ok); \
        JI("checkcast", "java/lang/Long"); \
        JI("invokevirtual", "java/lang/Long/longValue()J"); \
    } while(0)

    if ((strcmp(fname, "push") == 0 || strcmp(fname, "put") == 0)
            && nargs >= 2 && !ij_is_user_proc(fname)) {
        /* push(L, v): add v to front (index 0) of L; put(L,v): add to back */
        int is_push = (strcmp(fname, "push") == 0);
        IcnNode *larg = n->children[1];   /* list */
        IcnNode *varg = n->children[2];   /* value */
        char lrelay[64], vrelay[64];
        snprintf(lrelay, sizeof lrelay, "icn_%d_lr", id);
        snprintf(vrelay, sizeof vrelay, "icn_%d_vr", id);
        /* elem temp field */
        char elem_fld[80]; snprintf(elem_fld, sizeof elem_fld, "icn_%d_pushval", id);
        ij_declare_static_obj(elem_fld);
        /* list temp field */
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_pushlist", id);
        ij_declare_static_list(list_fld);

        int is_str_v = ij_expr_is_string(varg);
        int is_dbl_v = !is_str_v && ij_expr_is_real(varg);

        IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);
        char la[64], lb[64]; ij_emit_expr(larg, lp, la, lb);
        IjPorts vp; strncpy(vp.γ, vrelay, 63); strncpy(vp.ω, ports.ω, 63);
        char va[64], vb[64]; ij_emit_expr(varg, vp, va, vb);

        JL(a); JGoto(la);
        JL(b); JGoto(ports.ω);

        /* lrelay: ArrayList ref on stack → store, eval v */
        JL(lrelay);
        ij_put_list_field(list_fld);
        JGoto(va);

        /* vrelay: value on stack → box → store elem → call add */
        JL(vrelay);
        if (is_str_v) {
            /* String — already Object */
        } else if (is_dbl_v) {
            JI("invokestatic", "java/lang/Double/valueOf(D)Ljava/lang/Double;");
        } else {
            JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
        }
        ij_put_obj_field(elem_fld);
        ij_get_list_field(list_fld);
        if (is_push) {
            JI("iconst_0", "");
            ij_get_obj_field(elem_fld);
            JI("invokevirtual", "java/util/ArrayList/add(ILjava/lang/Object;)V");
        } else {
            ij_get_obj_field(elem_fld);
            JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
            JI("pop", "");
        }
        /* return L (the list) */
        ij_get_list_field(list_fld);
        JGoto(ports.γ);
        return;
    }

    if ((strcmp(fname, "get") == 0 || strcmp(fname, "pop") == 0)
            && nargs >= 1 && !ij_is_user_proc(fname)) {
        /* get(L)/pop(L): remove+return first element; fail if empty */
        IcnNode *larg = n->children[1];
        char lrelay[64]; snprintf(lrelay, sizeof lrelay, "icn_%d_lr", id);
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_glist", id);
        char ok[64]; snprintf(ok, sizeof ok, "icn_%d_gok", id);
        ij_declare_static_list(list_fld);
        IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);
        char la[64], lb[64]; ij_emit_expr(larg, lp, la, lb);
        JL(a); JGoto(la);
        JL(b); JGoto(ports.ω);
        JL(lrelay);
        ij_put_list_field(list_fld);
        /* size check */
        ij_get_list_field(list_fld);
        JI("invokevirtual", "java/util/ArrayList/size()I");
        J("    ifeq %s\n", ports.ω);   /* empty → fail */
        /* remove(0) → Object */
        ij_get_list_field(list_fld);
        JI("iconst_0", "");
        JI("invokevirtual", "java/util/ArrayList/remove(I)Ljava/lang/Object;");
        /* unbox as long */
        LIST_UNBOX_LONG(ok);
        JGoto(ports.γ);
        return;
    }

    if (strcmp(fname, "pull") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        /* pull(L): remove+return last element; fail if empty */
        IcnNode *larg = n->children[1];
        char lrelay[64]; snprintf(lrelay, sizeof lrelay, "icn_%d_lr", id);
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_plist", id);
        char sz_tmp[80]; snprintf(sz_tmp, sizeof sz_tmp, "icn_%d_psz", id);
        char ok[64]; snprintf(ok, sizeof ok, "icn_%d_pok", id);
        ij_declare_static_list(list_fld);
        ij_declare_static_int(sz_tmp);
        IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);
        char la[64], lb[64]; ij_emit_expr(larg, lp, la, lb);
        JL(a); JGoto(la);
        JL(b); JGoto(ports.ω);
        JL(lrelay);
        ij_put_list_field(list_fld);
        /* size check — use a pop-then-fail trampoline so stack is clean at ports.ω */
        char pull_fail[64]; snprintf(pull_fail, sizeof pull_fail, "icn_%d_pfail", id);
        ij_get_list_field(list_fld);
        JI("invokevirtual", "java/util/ArrayList/size()I");
        J("    dup\n    ifeq %s\n", pull_fail); /* empty → pop + fail */
        /* sz is on stack; compute sz-1 */
        JI("iconst_1", "");
        JI("isub", "");
        ij_put_int_field(sz_tmp);
        /* remove(sz-1) → Object */
        ij_get_list_field(list_fld);
        ij_get_int_field(sz_tmp);
        JI("invokevirtual", "java/util/ArrayList/remove(I)Ljava/lang/Object;");
        /* unbox as long */
        LIST_UNBOX_LONG(ok);
        JGoto(ports.γ);
        /* pull_fail: pop the dup'd size int, then fail */
        JL(pull_fail); JI("pop", ""); JGoto(ports.ω);
        return;
    }

    if (strcmp(fname, "list") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        /* list(n, x): create ArrayList of n copies of x */
        IcnNode *narg = n->children[1];
        IcnNode *xarg = n->children[2];
        char nrelay[64], xrelay[64];
        snprintf(nrelay, sizeof nrelay, "icn_%d_nlr", id);
        snprintf(xrelay, sizeof xrelay, "icn_%d_xlr", id);
        char n_fld[80];    snprintf(n_fld,    sizeof n_fld,    "icn_%d_ln", id);
        char x_fld[80];    snprintf(x_fld,    sizeof x_fld,    "icn_%d_lx", id);
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_ll", id);
        char cnt_fld[80];  snprintf(cnt_fld,  sizeof cnt_fld,  "icn_%d_lc", id);
        char loop[64];     snprintf(loop,     sizeof loop,     "icn_%d_llp", id);
        char done[64];     snprintf(done,     sizeof done,     "icn_%d_lld", id);
        ij_declare_static(n_fld);       /* long: count */
        ij_declare_static_obj(x_fld);  /* Object: boxed value */
        ij_declare_static_list(list_fld);
        ij_declare_static_int(cnt_fld);

        int is_str_x = ij_expr_is_string(xarg);
        int is_dbl_x = !is_str_x && ij_expr_is_real(xarg);

        IjPorts np2; strncpy(np2.γ, nrelay, 63); strncpy(np2.ω, ports.ω, 63);
        char na[64], nb[64]; ij_emit_expr(narg, np2, na, nb);
        IjPorts xp; strncpy(xp.γ, xrelay, 63); strncpy(xp.ω, ports.ω, 63);
        char xa[64], xb[64]; ij_emit_expr(xarg, xp, xa, xb);

        JL(a); JGoto(na);
        JL(b); JGoto(ports.ω);

        /* nrelay: n (long) on stack */
        JL(nrelay);
        ij_put_long(n_fld);
        JGoto(xa);

        /* xrelay: x value on stack → box → store */
        JL(xrelay);
        if (is_str_x) {
            /* String — no boxing */
        } else if (is_dbl_x) {
            JI("invokestatic", "java/lang/Double/valueOf(D)Ljava/lang/Double;");
        } else {
            JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
        }
        ij_put_obj_field(x_fld);
        /* Build new ArrayList */
        JI("new", "java/util/ArrayList");
        JI("dup", "");
        JI("invokespecial", "java/util/ArrayList/<init>()V");
        ij_put_list_field(list_fld);
        /* n as int → counter */
        ij_get_long(n_fld);
        JI("l2i", "");
        ij_put_int_field(cnt_fld);
        /* loop: while cnt > 0: add x; cnt-- */
        JL(loop);
        ij_get_int_field(cnt_fld);
        J("    ifeq %s\n", done);
        ij_get_list_field(list_fld);
        ij_get_obj_field(x_fld);
        JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
        JI("pop", "");
        ij_get_int_field(cnt_fld);
        JI("iconst_1", ""); JI("isub", "");
        ij_put_int_field(cnt_fld);
        JGoto(loop);
        JL(done);
        ij_get_list_field(list_fld);
        JGoto(ports.γ);
        return;
    }

    /* === END LIST BUILTINS === */

    /* === TABLE BUILTINS ===
     * table(dflt)      — create new HashMap with default value dflt
     * insert(T,k,v)    — T.put(k,v); returns T
     * delete(T,k)      — T.remove(k); returns T
     * member(T,k)      — succeeds (returning k) if k in T, else fails
     * key(T)           — generator: yields each key in T
     *
     * Key representation: all keys stored as String (ldc / Long.toString).
     * Default value stored in a parallel per-table Object static _dflt field.
     * Table subscript t[k] handled in ij_emit_subscript (extended below).
     */

    if (strcmp(fname, "table") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        /* table(dflt): create new HashMap; store default in _dflt static */
        IcnNode *darg = n->children[1];
        char drelay[64]; snprintf(drelay, sizeof drelay, "icn_%d_tdr", id);
        char tbl_fld[80];  snprintf(tbl_fld,  sizeof tbl_fld,  "icn_%d_tbl",  id);
        char dflt_fld[80]; snprintf(dflt_fld, sizeof dflt_fld, "icn_%d_tdflt", id);
        ij_declare_static_table(tbl_fld);
        ij_declare_static_obj(dflt_fld);

        IjPorts dp; strncpy(dp.γ, drelay, 63); strncpy(dp.ω, ports.ω, 63);
        char da[64], db[64]; ij_emit_expr(darg, dp, da, db);

        JL(a); JGoto(da);
        JL(b); JGoto(ports.ω);

        /* drelay: default value (long) on stack → box → store as Object default */
        JL(drelay);
        JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
        ij_put_obj_field(dflt_fld);
        /* create new HashMap */
        JI("new", "java/util/HashMap");
        JI("dup", "");
        JI("invokespecial", "java/util/HashMap/<init>()V");
        ij_put_table_field(tbl_fld);
        /* Record pending dflt so ij_emit_assign can register var→dflt map */
        strncpy(ij_pending_tdflt, dflt_fld, 79);
        ij_get_table_field(tbl_fld);
        JGoto(ports.γ);
        return;
    }

    if (strcmp(fname, "insert") == 0 && nargs >= 3 && !ij_is_user_proc(fname)) {
        IcnNode *targ = n->children[1];
        IcnNode *karg = n->children[2];
        IcnNode *varg = n->children[3];
        char trelay[64], krelay[64], vrelay[64];
        snprintf(trelay, sizeof trelay, "icn_%d_itr2", id);
        snprintf(krelay, sizeof krelay, "icn_%d_ikr2", id);
        snprintf(vrelay, sizeof vrelay, "icn_%d_ivr2", id);
        char t_fld[80]; snprintf(t_fld, sizeof t_fld, "icn_%d_itbl2", id);
        char k_fld[80]; snprintf(k_fld, sizeof k_fld, "icn_%d_ikey2", id);
        char v_fld[80]; snprintf(v_fld, sizeof v_fld, "icn_%d_ival2", id);
        ij_declare_static_table(t_fld);
        ij_declare_static_str(k_fld);
        ij_declare_static_obj(v_fld);

        IjPorts tp2; strncpy(tp2.γ, trelay, 63); strncpy(tp2.ω, ports.ω, 63);
        char ta2[64], tb2[64]; ij_emit_expr(targ, tp2, ta2, tb2);
        IjPorts kp2; strncpy(kp2.γ, krelay, 63); strncpy(kp2.ω, ports.ω, 63);
        char ka2[64], kb2[64]; ij_emit_expr(karg, kp2, ka2, kb2);
        IjPorts vp3; strncpy(vp3.γ, vrelay, 63); strncpy(vp3.ω, ports.ω, 63);
        char va3[64], vb3[64]; ij_emit_expr(varg, vp3, va3, vb3);

        JL(a); JGoto(ta2);
        JL(b); JGoto(ports.ω);
        JL(trelay); ij_put_table_field(t_fld); JGoto(ka2);
        JL(krelay);
        JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        ij_put_str_field(k_fld);
        JGoto(va3);
        JL(vrelay);
        JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
        ij_put_obj_field(v_fld);
        ij_get_table_field(t_fld);
        ij_get_str_field(k_fld);
        ij_get_obj_field(v_fld);
        JI("invokevirtual", "java/util/HashMap/put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
        JI("pop", "");
        ij_get_table_field(t_fld);
        JGoto(ports.γ);
        return;
    }

    if (strcmp(fname, "delete") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        /* delete(T, k): T.remove(key_str); return T */
        IcnNode *targ = n->children[1];
        IcnNode *karg = n->children[2];
        char trelay[64], krelay[64];
        snprintf(trelay, sizeof trelay, "icn_%d_dtr", id);
        snprintf(krelay, sizeof krelay, "icn_%d_dkr", id);
        char t_fld[80]; snprintf(t_fld, sizeof t_fld, "icn_%d_dtbl", id);
        char k_fld[80]; snprintf(k_fld, sizeof k_fld, "icn_%d_dkey", id);
        ij_declare_static_table(t_fld);
        ij_declare_static_str(k_fld);

        IjPorts tp3; strncpy(tp3.γ, trelay, 63); strncpy(tp3.ω, ports.ω, 63);
        char ta3[64], tb3[64]; ij_emit_expr(targ, tp3, ta3, tb3);
        IjPorts kp3; strncpy(kp3.γ, krelay, 63); strncpy(kp3.ω, ports.ω, 63);
        char ka3[64], kb3[64]; ij_emit_expr(karg, kp3, ka3, kb3);

        JL(a); JGoto(ta3);
        JL(b); JGoto(ports.ω);
        JL(trelay); ij_put_table_field(t_fld); JGoto(ka3);
        JL(krelay);
        JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        ij_put_str_field(k_fld);
        ij_get_table_field(t_fld);
        ij_get_str_field(k_fld);
        JI("invokevirtual", "java/util/HashMap/remove(Ljava/lang/Object;)Ljava/lang/Object;");
        JI("pop", "");
        ij_get_table_field(t_fld);
        JGoto(ports.γ);
        return;
    }

    if (strcmp(fname, "member") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        /* member(T, k): succeed returning k (long) if k is a key; else fail */
        IcnNode *targ = n->children[1];
        IcnNode *karg = n->children[2];
        char trelay[64], krelay[64];
        snprintf(trelay, sizeof trelay, "icn_%d_mtr", id);
        snprintf(krelay, sizeof krelay, "icn_%d_mkr", id);
        char t_fld[80]; snprintf(t_fld, sizeof t_fld, "icn_%d_mtbl", id);
        char k_fld[80]; snprintf(k_fld, sizeof k_fld, "icn_%d_mkey", id);
        char kv_fld[80]; snprintf(kv_fld, sizeof kv_fld, "icn_%d_mkeyv", id);
        ij_declare_static_table(t_fld);
        ij_declare_static_str(k_fld);
        ij_declare_static(kv_fld);  /* long: the key value */

        IjPorts tp4; strncpy(tp4.γ, trelay, 63); strncpy(tp4.ω, ports.ω, 63);
        char ta4[64], tb4[64]; ij_emit_expr(targ, tp4, ta4, tb4);
        IjPorts kp4; strncpy(kp4.γ, krelay, 63); strncpy(kp4.ω, ports.ω, 63);
        char ka4[64], kb4[64]; ij_emit_expr(karg, kp4, ka4, kb4);

        JL(a); JGoto(ta4);
        JL(b); JGoto(ports.ω);
        JL(trelay); ij_put_table_field(t_fld); JGoto(ka4);
        JL(krelay);
        JI("dup2", "");            /* dup long key value for later return */
        ij_put_long(kv_fld);      /* save key as long */
        JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        ij_put_str_field(k_fld);
        ij_get_table_field(t_fld);
        ij_get_str_field(k_fld);
        JI("invokevirtual", "java/util/HashMap/containsKey(Ljava/lang/Object;)Z");
        J("    ifeq %s\n", ports.ω);   /* false → fail */
        ij_get_long(kv_fld);            /* success: return the key as long */
        JGoto(ports.γ);
        return;
    }

    if (strcmp(fname, "key") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        /* key(T): generator — yields each key (as long) from T.
         * Strategy: snapshot keySet().toArray() into Object[] static at start;
         * use int index static to iterate. One-shot keys (no deletion during iteration).
         */
        IcnNode *targ = n->children[1];
        char trelay[64]; snprintf(trelay, sizeof trelay, "icn_%d_ktr", id);
        char t_fld[80];   snprintf(t_fld,   sizeof t_fld,   "icn_%d_ktbl",  id);
        char arr_fld[80]; snprintf(arr_fld,  sizeof arr_fld, "icn_%d_karr",  id);
        char idx_fld[80];  snprintf(idx_fld,   sizeof idx_fld,  "icn_%d_kidx",  id);
        char kinit_fld[80];snprintf(kinit_fld, sizeof kinit_fld,"icn_%d_kinit", id);
        char check[64];    snprintf(check,     sizeof check,    "icn_%d_kchk",  id);
        ij_declare_static_table(t_fld);
        ij_declare_static_obj(arr_fld);   /* Object[] stored as Object ref */
        ij_declare_static_int(idx_fld);
        ij_declare_static_int(kinit_fld); /* 0=fresh, 1=already init'd */

        IjPorts tp5; strncpy(tp5.γ, trelay, 63); strncpy(tp5.ω, ports.ω, 63);
        char ta5[64], tb5[64]; ij_emit_expr(targ, tp5, ta5, tb5);

        /* α: if already initialized, skip re-snapshot and jump straight to kchk */
        JL(a);
        ij_get_int_field(kinit_fld);
        JI("ifne", check);           /* kinit != 0 → already init'd, skip to check */
        JGoto(ta5);
        /* ktr: eval T result arrives here; snapshot keys and mark init'd */
        JL(trelay);
        ij_put_table_field(t_fld);
        /* T.keySet().toArray() → Object[] */
        ij_get_table_field(t_fld);
        JI("invokevirtual", "java/util/HashMap/keySet()Ljava/util/Set;");
        JI("invokeinterface", "java/util/Set/toArray()[Ljava/lang/Object; 1");
        ij_put_obj_field(arr_fld);
        /* idx = 0 */
        JI("iconst_0", "");
        ij_put_int_field(idx_fld);
        /* mark initialized */
        JI("iconst_1", "");
        ij_put_int_field(kinit_fld);
        JGoto(check);

        /* β: increment index, fall into check */
        JL(b);
        ij_get_int_field(idx_fld);
        JI("iconst_1", ""); JI("iadd", "");
        ij_put_int_field(idx_fld);

        /* check: if idx >= arr.length → fail; else load arr[idx], parse long, → γ */
        JL(check);
        ij_get_obj_field(arr_fld);
        JI("checkcast", "[Ljava/lang/Object;");
        JI("arraylength", "");
        ij_get_int_field(idx_fld);
        JI("if_icmple", ports.ω);         /* arr.length <= idx → fail */
        /* load arr[idx] as String key */
        ij_get_obj_field(arr_fld);
        JI("checkcast", "[Ljava/lang/Object;");
        ij_get_int_field(idx_fld);
        JI("aaload", "");
        JI("checkcast", "java/lang/String");
        JI("invokestatic", "java/lang/Long/parseLong(Ljava/lang/String;)J");
        JGoto(ports.γ);
        return;
    }

    /* === END TABLE BUILTINS === */

    /* === M-IJ-BUILTINS-STR === */

    /* --- repl(s, n) --- repeat string s n times */
    if (strcmp(fname, "repl") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        IcnNode *narg = n->children[2];
        char mid[64]; snprintf(mid, sizeof mid, "icn_%d_repl_mid", id);
        char after[64]; snprintf(after, sizeof after, "icn_%d_repl_after", id);
        IjPorts ap; strncpy(ap.γ, mid, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        IjPorts bp; strncpy(bp.γ, after, 63); strncpy(bp.ω, ports.ω, 63);
        char na[64], nb[64]; ij_emit_expr(narg, bp, na, nb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(mid);
        /* stack: String s */
        int scratch_s = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_s));
        JGoto(na);
        JL(after);
        /* stack: long n — convert to int */
        JI("l2i", "");
        int scratch_n = ij_locals_alloc_tmp();
        J("    istore %d\n", slot_jvm(scratch_n));
        /* StringBuilder sb = new StringBuilder() */
        JI("new", "java/lang/StringBuilder");
        JI("dup", "");
        JI("invokespecial", "java/lang/StringBuilder/<init>()V");
        int scratch_sb = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_sb));
        /* loop i=0; i<n; i++ */
        char loop[64], done[64];
        snprintf(loop, sizeof loop, "icn_%d_repl_loop", id);
        snprintf(done, sizeof done, "icn_%d_repl_done", id);
        int scratch_i = ij_locals_alloc_tmp();
        J("    iconst_0\n");
        J("    istore %d\n", slot_jvm(scratch_i));
        JL(loop);
        J("    iload %d\n", slot_jvm(scratch_i));
        J("    iload %d\n", slot_jvm(scratch_n));
        J("    if_icmpge %s\n", done);
        J("    aload %d\n", slot_jvm(scratch_sb));
        J("    aload %d\n", slot_jvm(scratch_s));
        JI("invokevirtual", "java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;");
        JI("pop", "");
        J("    iinc %d 1\n", slot_jvm(scratch_i));
        JGoto(loop);
        JL(done);
        J("    aload %d\n", slot_jvm(scratch_sb));
        JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
        JGoto(ports.γ);
        return;
    }

    /* --- reverse(s) --- */
    if (strcmp(fname, "reverse") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_rev_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(after);
        /* stack: String — wrap in StringBuilder, reverse, toString */
        JI("new", "java/lang/StringBuilder");
        JI("dup_x1", "");
        JI("swap", "");
        JI("invokespecial", "java/lang/StringBuilder/<init>(Ljava/lang/String;)V");
        JI("invokevirtual", "java/lang/StringBuilder/reverse()Ljava/lang/StringBuilder;");
        JI("invokevirtual", "java/lang/StringBuilder/toString()Ljava/lang/String;");
        JGoto(ports.γ);
        return;
    }

    /* --- left(s, n [, pad]) --- pad/truncate to width n, left-aligned */
    if (strcmp(fname, "left") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        IcnNode *narg = n->children[2];
        /* pad arg optional — use space if absent */
        char mid[64]; snprintf(mid, sizeof mid, "icn_%d_left_mid", id);
        char after[64]; snprintf(after, sizeof after, "icn_%d_left_after", id);
        IjPorts ap; strncpy(ap.γ, mid, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        IjPorts bp; strncpy(bp.γ, after, 63); strncpy(bp.ω, ports.ω, 63);
        char na[64], nb[64]; ij_emit_expr(narg, bp, na, nb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(mid);
        int scratch_s = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_s));
        JGoto(na);
        JL(after);
        JI("l2i", "");
        int scratch_n = ij_locals_alloc_tmp();
        J("    istore %d\n", slot_jvm(scratch_n));
        /* call static helper */
        J("    aload %d\n", slot_jvm(scratch_s));
        J("    iload %d\n", slot_jvm(scratch_n));
        if (nargs >= 3) {
            char mid2[64]; snprintf(mid2, sizeof mid2, "icn_%d_left_mid2", id);
            IjPorts cp; strncpy(cp.γ, mid2, 63); strncpy(cp.ω, ports.ω, 63);
            char ca[64], cb[64]; ij_emit_expr(n->children[3], cp, ca, cb);
            JGoto(ca);
            JL(mid2);
            /* pad string on stack */
            JI("invokestatic", ij_classname_buf("icn_builtin_left(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        } else {
            JI("ldc", "\" \"");
            JI("invokestatic", ij_classname_buf("icn_builtin_left(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- right(s, n [, pad]) --- pad/truncate to width n, right-aligned */
    if (strcmp(fname, "right") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        IcnNode *narg = n->children[2];
        char mid[64]; snprintf(mid, sizeof mid, "icn_%d_right_mid", id);
        char after[64]; snprintf(after, sizeof after, "icn_%d_right_after", id);
        IjPorts ap; strncpy(ap.γ, mid, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        IjPorts bp; strncpy(bp.γ, after, 63); strncpy(bp.ω, ports.ω, 63);
        char na[64], nb[64]; ij_emit_expr(narg, bp, na, nb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(mid);
        int scratch_s = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_s));
        JGoto(na);
        JL(after);
        JI("l2i", "");
        int scratch_n = ij_locals_alloc_tmp();
        J("    istore %d\n", slot_jvm(scratch_n));
        J("    aload %d\n", slot_jvm(scratch_s));
        J("    iload %d\n", slot_jvm(scratch_n));
        if (nargs >= 3) {
            char mid2[64]; snprintf(mid2, sizeof mid2, "icn_%d_right_mid2", id);
            IjPorts cp; strncpy(cp.γ, mid2, 63); strncpy(cp.ω, ports.ω, 63);
            char ca[64], cb[64]; ij_emit_expr(n->children[3], cp, ca, cb);
            JGoto(ca);
            JL(mid2);
            JI("invokestatic", ij_classname_buf("icn_builtin_right(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        } else {
            JI("ldc", "\" \"");
            JI("invokestatic", ij_classname_buf("icn_builtin_right(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- center(s, n [, pad]) --- */
    if (strcmp(fname, "center") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        IcnNode *narg = n->children[2];
        char mid[64]; snprintf(mid, sizeof mid, "icn_%d_ctr_mid", id);
        char after[64]; snprintf(after, sizeof after, "icn_%d_ctr_after", id);
        IjPorts ap; strncpy(ap.γ, mid, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        IjPorts bp; strncpy(bp.γ, after, 63); strncpy(bp.ω, ports.ω, 63);
        char na[64], nb[64]; ij_emit_expr(narg, bp, na, nb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(mid);
        int scratch_s = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_s));
        JGoto(na);
        JL(after);
        JI("l2i", "");
        int scratch_n = ij_locals_alloc_tmp();
        J("    istore %d\n", slot_jvm(scratch_n));
        J("    aload %d\n", slot_jvm(scratch_s));
        J("    iload %d\n", slot_jvm(scratch_n));
        if (nargs >= 3) {
            char mid2[64]; snprintf(mid2, sizeof mid2, "icn_%d_ctr_mid2", id);
            IjPorts cp; strncpy(cp.γ, mid2, 63); strncpy(cp.ω, ports.ω, 63);
            char ca[64], cb[64]; ij_emit_expr(n->children[3], cp, ca, cb);
            JGoto(ca);
            JL(mid2);
            JI("invokestatic", ij_classname_buf("icn_builtin_center(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        } else {
            JI("ldc", "\" \"");
            JI("invokestatic", ij_classname_buf("icn_builtin_center(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;"));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- trim(s [, cs]) --- remove trailing chars in cs (default whitespace) */
    if (strcmp(fname, "trim") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_trim_after", id);
        if (nargs >= 2) {
            char mid[64]; snprintf(mid, sizeof mid, "icn_%d_trim_mid", id);
            IjPorts ap; strncpy(ap.γ, mid, 63); strncpy(ap.ω, ports.ω, 63);
            char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
            IjPorts bp; strncpy(bp.γ, after, 63); strncpy(bp.ω, ports.ω, 63);
            char ca[64], cb[64]; ij_emit_expr(n->children[2], bp, ca, cb);
            JL(a); JGoto(sa);
            JL(b); JGoto(sb);
            JL(mid);
            int scratch_s = ij_locals_alloc_tmp();
            J("    astore %d\n", slot_jvm(scratch_s));
            JGoto(ca);
            JL(after);
            J("    aload %d\n", slot_jvm(scratch_s));
            JI("swap", "");  /* stack: cs, s → s, cs */
            JI("invokestatic", ij_classname_buf("icn_builtin_trim(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"));
        } else {
            IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
            char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
            JL(a); JGoto(sa);
            JL(b); JGoto(sb);
            JL(after);
            JI("ldc", "\" \\t\\n\\r\"");
            JI("invokestatic", ij_classname_buf("icn_builtin_trim(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"));
        }
        JGoto(ports.γ);
        return;
    }

    /* --- map(s, src, dst) --- translate chars in s: src[i]→dst[i] */
    if (strcmp(fname, "map") == 0 && nargs >= 3 && !ij_is_user_proc(fname)) {
        IcnNode *sarg   = n->children[1];
        IcnNode *srcarg = n->children[2];
        IcnNode *dstarg = n->children[3];
        char mid1[64]; snprintf(mid1, sizeof mid1, "icn_%d_map_mid1", id);
        char mid2[64]; snprintf(mid2, sizeof mid2, "icn_%d_map_mid2", id);
        char after[64]; snprintf(after, sizeof after, "icn_%d_map_after", id);
        IjPorts ap; strncpy(ap.γ, mid1, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        IjPorts bp; strncpy(bp.γ, mid2, 63); strncpy(bp.ω, ports.ω, 63);
        char srca[64], srcb[64]; ij_emit_expr(srcarg, bp, srca, srcb);
        IjPorts cp; strncpy(cp.γ, after, 63); strncpy(cp.ω, ports.ω, 63);
        char dsta[64], dstb[64]; ij_emit_expr(dstarg, cp, dsta, dstb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(mid1);
        int scratch_s = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_s));
        JGoto(srca);
        JL(mid2);
        int scratch_src = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_src));
        JGoto(dsta);
        JL(after);
        /* stack: dst String */
        int scratch_dst = ij_locals_alloc_tmp();
        J("    astore %d\n", slot_jvm(scratch_dst));
        J("    aload %d\n", slot_jvm(scratch_s));
        J("    aload %d\n", slot_jvm(scratch_src));
        J("    aload %d\n", slot_jvm(scratch_dst));
        JI("invokestatic", ij_classname_buf("icn_builtin_map(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"));
        JGoto(ports.γ);
        return;
    }

    /* --- char(i) --- integer → single-char String */
    if (strcmp(fname, "char") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *iarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_char_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char ia[64], ib[64]; ij_emit_expr(iarg, ap, ia, ib);
        JL(a); JGoto(ia);
        JL(b); JGoto(ib);
        JL(after);
        /* stack: long → int → char → String.valueOf(char) */
        JI("l2i", "");
        JI("i2c", "");
        JI("invokestatic", "java/lang/String/valueOf(C)Ljava/lang/String;");
        JGoto(ports.γ);
        return;
    }

    /* --- ord(s) --- first char of string → long */
    if (strcmp(fname, "ord") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *sarg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_ord_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(sarg, ap, sa, sb);
        JL(a); JGoto(sa);
        JL(b); JGoto(sb);
        JL(after);
        /* stack: String → charAt(0) → int → long */
        JI("iconst_0", "");
        JI("invokevirtual", "java/lang/String/charAt(I)C");
        JI("i2l", "");
        JGoto(ports.γ);
        return;
    }

    /* === M-IJ-BUILTINS-TYPE === */

    /* --- type(x) --- returns "integer", "real", or "string" (compile-time constant) */
    if (strcmp(fname, "type") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_type_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        /* pop the evaluated value (size-aware), then push compile-time type name */
        if (ij_expr_is_string(arg)) {
            JI("pop", "");
            JI("ldc", "\"string\"");
        } else if (ij_expr_is_real(arg)) {
            JI("pop2", "");
            JI("ldc", "\"real\"");
        } else {
            JI("pop2", "");
            JI("ldc", "\"integer\"");
        }
        JGoto(ports.γ);
        return;
    }

    /* --- copy(x) --- shallow copy; strings/integers/reals are immutable → identity */
    if (strcmp(fname, "copy") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_copy_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        /* value already on stack — identity copy for immutable types */
        JGoto(ports.γ);
        return;
    }

    /* --- image(x) --- string representation of any value */
    if (strcmp(fname, "image") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_image_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            /* already a String — no-op */
        } else if (ij_expr_is_real(arg)) {
            JI("invokestatic", "java/lang/Double/toString(D)Ljava/lang/String;");
        } else {
            JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        }
        JGoto(ports.γ);
        return;
    }

    /* --- numeric(x) --- convert string to integer; fail if not numeric */
    if (strcmp(fname, "numeric") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_num_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_string(arg)) {
            /* call icn_builtin_numeric(String) → long; Long.MIN_VALUE sentinel = fail */
            JI("invokestatic", ij_classname_buf("icn_builtin_numeric(Ljava/lang/String;)J"));
            J("    dup2\n");
            J("    ldc2_w -9223372036854775808\n");
            J("    lcmp\n");
            char ok[64]; snprintf(ok, sizeof ok, "icn_%d_num_ok", id);
            J("    ifne %s\n", ok);
            J("    pop2\n");
            JGoto(ports.ω);
            JL(ok);
        }
        /* else already numeric — pass through */
        JGoto(ports.γ);
        return;
    }

    /* === M-IJ-BUILTINS-MISC === */

    /* --- abs(x) --- absolute value; works for integer (long) and real (double) */
    if (strcmp(fname, "abs") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_abs_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (ij_expr_is_real(arg)) {
            JI("invokestatic", "java/lang/Math/abs(D)D");
        } else {
            JI("invokestatic", "java/lang/Math/abs(J)J");
        }
        JGoto(ports.γ);
        return;
    }

    /* --- max(x, y, ...) --- maximum of 2+ args */
    if (strcmp(fname, "max") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        int use_real = 0;
        for (int i = 0; i < nargs; i++) use_real |= ij_expr_is_real(n->children[1+i]);
        char tmp_fld[64]; snprintf(tmp_fld, sizeof tmp_fld, "icn_%d_max_tmp", id);
        if (use_real) ij_declare_static_dbl(tmp_fld);
        else          ij_declare_static(tmp_fld);
        char (*arg_a)[64] = malloc(nargs * sizeof(*arg_a));
        char (*arg_b)[64] = malloc(nargs * sizeof(*arg_b));
        char (*relay)[64] = malloc((nargs+1) * sizeof(*relay));
        for (int i = 0; i <= nargs; i++) snprintf(relay[i], 64, "icn_%d_max_r%d", id, i);
        for (int i = 0; i < nargs; i++) {
            IjPorts pi; strncpy(pi.γ, relay[i+1], 63); strncpy(pi.ω, ports.ω, 63);
            ij_emit_expr(n->children[1+i], pi, arg_a[i], arg_b[i]);
        }
        JL(a); JGoto(arg_a[0]);
        JL(b); JGoto(arg_b[0]);
        JL(relay[1]); /* arg0 TOS — store as running max */
        if (use_real) ij_put_dbl(tmp_fld); else ij_put_long(tmp_fld);
        for (int i = 1; i < nargs; i++) {
            JGoto(arg_a[i]);
            JL(relay[i+1]); /* arg_i TOS; load tmp below it, call Math.max(tmp, arg_i) */
            if (use_real) { ij_get_dbl(tmp_fld); JI("invokestatic","java/lang/Math/max(DD)D"); ij_put_dbl(tmp_fld); }
            else          { ij_get_long(tmp_fld); JI("invokestatic","java/lang/Math/max(JJ)J"); ij_put_long(tmp_fld); }
        }
        if (use_real) ij_get_dbl(tmp_fld); else ij_get_long(tmp_fld);
        free(arg_a); free(arg_b); free(relay);
        JGoto(ports.γ);
        return;
    }

    /* --- min(x, y, ...) --- minimum of 2+ args */
    if (strcmp(fname, "min") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        int use_real = 0;
        for (int i = 0; i < nargs; i++) use_real |= ij_expr_is_real(n->children[1+i]);
        char tmp_fld[64]; snprintf(tmp_fld, sizeof tmp_fld, "icn_%d_min_tmp", id);
        if (use_real) ij_declare_static_dbl(tmp_fld);
        else          ij_declare_static(tmp_fld);
        char (*arg_a)[64] = malloc(nargs * sizeof(*arg_a));
        char (*arg_b)[64] = malloc(nargs * sizeof(*arg_b));
        char (*relay)[64] = malloc((nargs+1) * sizeof(*relay));
        for (int i = 0; i <= nargs; i++) snprintf(relay[i], 64, "icn_%d_min_r%d", id, i);
        for (int i = 0; i < nargs; i++) {
            IjPorts pi; strncpy(pi.γ, relay[i+1], 63); strncpy(pi.ω, ports.ω, 63);
            ij_emit_expr(n->children[1+i], pi, arg_a[i], arg_b[i]);
        }
        JL(a); JGoto(arg_a[0]);
        JL(b); JGoto(arg_b[0]);
        JL(relay[1]);
        if (use_real) ij_put_dbl(tmp_fld); else ij_put_long(tmp_fld);
        for (int i = 1; i < nargs; i++) {
            JGoto(arg_a[i]);
            JL(relay[i+1]);
            if (use_real) { ij_get_dbl(tmp_fld); JI("invokestatic","java/lang/Math/min(DD)D"); ij_put_dbl(tmp_fld); }
            else          { ij_get_long(tmp_fld); JI("invokestatic","java/lang/Math/min(JJ)J"); ij_put_long(tmp_fld); }
        }
        if (use_real) ij_get_dbl(tmp_fld); else ij_get_long(tmp_fld);
        free(arg_a); free(arg_b); free(relay);
        JGoto(ports.γ);
        return;
    }

    /* --- sqrt(x) --- square root; result is real */
    if (strcmp(fname, "sqrt") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *arg = n->children[1];
        char after[64]; snprintf(after, sizeof after, "icn_%d_sqrt_after", id);
        IjPorts ap; strncpy(ap.γ, after, 63); strncpy(ap.ω, ports.ω, 63);
        char aa[64], ab[64]; ij_emit_expr(arg, ap, aa, ab);
        JL(a); JGoto(aa);
        JL(b); JGoto(ab);
        JL(after);
        if (!ij_expr_is_real(arg)) JI("l2d", "");  /* promote integer to double */
        JI("invokestatic", "java/lang/Math/sqrt(D)D");
        JGoto(ports.γ);
        return;
    }

    /* --- seq(i [,j]) --- infinite generator: i, i+j, i+2j, ...
     * α (start): evaluate args, store start+step, push start value → γ
     * β (resume): cur += step, push cur → γ
     * seq never fails on its own (infinite); limit operator controls termination */
    if (strcmp(fname, "seq") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        IcnNode *startarg = n->children[1];
        char cur_fld[64];  snprintf(cur_fld,  sizeof cur_fld,  "icn_%d_seq_cur",  id);
        char step_fld[64]; snprintf(step_fld, sizeof step_fld, "icn_%d_seq_step", id);
        ij_declare_static(cur_fld);
        ij_declare_static(step_fld);

        char after_s[64]; snprintf(after_s, sizeof after_s, "icn_%d_seq_as", id);
        char after_t[64]; snprintf(after_t, sizeof after_t, "icn_%d_seq_at", id);
        char produce[64]; snprintf(produce, sizeof produce, "icn_%d_seq_prod", id);

        IjPorts sp; strncpy(sp.γ, after_s, 63); strncpy(sp.ω, ports.ω, 63);
        char sa[64], sb[64]; ij_emit_expr(startarg, sp, sa, sb);

        /* α: evaluate start (and step if present), init cur, produce first value */
        JL(a); JGoto(sa);
        JL(b);  /* β: resume — increment cur, produce */
        ij_get_long(cur_fld);
        ij_get_long(step_fld);
        J("    ladd\n");
        ij_put_long(cur_fld);
        JGoto(produce);

        JL(after_s);  /* start arg evaluated, long on stack */
        ij_put_long(cur_fld);
        if (nargs >= 2) {
            IjPorts tp; strncpy(tp.γ, after_t, 63); strncpy(tp.ω, ports.ω, 63);
            char ta[64], tb[64]; ij_emit_expr(n->children[2], tp, ta, tb);
            JGoto(ta);
            JL(after_t);
            ij_put_long(step_fld);
        } else {
            J("    lconst_1\n");
            ij_put_long(step_fld);
        }
        JL(produce);
        ij_get_long(cur_fld);
        JGoto(ports.γ);
        return;
    }

    /* === M-IJ-SORT === */

    /* sort(L) — sort list of longs ascending, return new ArrayList.
     * One-shot: α evals L, stores, calls icn_builtin_sort, result list → γ.
     * β → ω. */
    if (strcmp(fname, "sort") == 0 && nargs >= 1 && !ij_is_user_proc(fname)) {
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_sort_list", id);
        char relay[64];    snprintf(relay,    sizeof relay,    "icn_%d_sort_rel",  id);
        ij_declare_static_list(list_fld);

        IjPorts ep; strncpy(ep.γ, relay, 63); strncpy(ep.ω, ports.ω, 63);
        char ea[64], eb[64]; ij_emit_expr(n->children[1], ep, ea, eb);

        JL(a); JGoto(ea);
        JL(b); JGoto(ports.ω);

        JL(relay);
        ij_put_list_field(list_fld);
        ij_get_list_field(list_fld);
        JI("invokestatic", ij_classname_buf("icn_builtin_sort(Ljava/util/ArrayList;)Ljava/util/ArrayList;"));
        JGoto(ports.γ);
        return;
    }

    /* sortf(L, field) — sort list of records by 1-based field index, return new ArrayList.
     * One-shot: α evals L then field index, calls icn_builtin_sortf, result → γ.
     * β → ω. */
    if (strcmp(fname, "sortf") == 0 && nargs >= 2 && !ij_is_user_proc(fname)) {
        char list_fld[80];  snprintf(list_fld,  sizeof list_fld,  "icn_%d_sortf_list", id);
        char field_fld[80]; snprintf(field_fld, sizeof field_fld, "icn_%d_sortf_fld",  id);
        char relay_l[64];   snprintf(relay_l,   sizeof relay_l,   "icn_%d_sortf_rl",   id);
        char relay_f[64];   snprintf(relay_f,   sizeof relay_f,   "icn_%d_sortf_rf",   id);
        ij_declare_static_list(list_fld);
        ij_declare_static(field_fld);

        IjPorts ep; strncpy(ep.γ, relay_l, 63); strncpy(ep.ω, ports.ω, 63);
        char ea[64], eb[64]; ij_emit_expr(n->children[1], ep, ea, eb);

        IjPorts fp; strncpy(fp.γ, relay_f, 63); strncpy(fp.ω, ports.ω, 63);
        char fa[64], fb[64]; ij_emit_expr(n->children[2], fp, fa, fb);

        JL(a); JGoto(ea);
        JL(b); JGoto(ports.ω);

        JL(relay_l);
        ij_put_list_field(list_fld);
        JGoto(fa);

        JL(relay_f);
        ij_put_long(field_fld);

        ij_get_list_field(list_fld);
        ij_get_long(field_fld);
        J("    l2i\n");
        JI("invokestatic", ij_classname_buf("icn_builtin_sortf(Ljava/util/ArrayList;I)Ljava/util/ArrayList;"));
        JGoto(ports.γ);
        return;
    }

    /* === END M-IJ-SORT === */

    /* === END M-IJ-BUILTINS-MISC === */

    /* === END M-IJ-BUILTINS-TYPE === */

    /* === END M-IJ-BUILTINS-STR === */

    if (ij_is_user_proc(fname)) {
        int is_gen = ij_is_gen_proc(fname);
        int np = ij_nparams_for(fname);
        char do_call[64]; snprintf(do_call, sizeof do_call, "icn_%d_docall", id);

        /* Evaluate args left-to-right, store into static arg slots */
        /* We use static fields icn_arg_0, icn_arg_1, ... for simplicity */
        char prev_succ[64]; strncpy(prev_succ, do_call, 63);
        char (*arg_alphas)[64] = nargs > 0 ? malloc(nargs * 64) : NULL;
        char (*arg_betas) [64] = nargs > 0 ? malloc(nargs * 64) : NULL;

        for (int i = nargs-1; i >= 0; i--) {
            char push_relay[64]; snprintf(push_relay, sizeof push_relay, "icn_%d_arg%d", id, i);
            IjPorts ap; strncpy(ap.γ, push_relay, 63); strncpy(ap.ω, ports.ω, 63);
            ij_emit_expr(n->children[i+1], ap, arg_alphas[i], arg_betas[i]);
            char argfield[64];    snprintf(argfield,    sizeof argfield,    "icn_arg_%d",     i);
            char argobjfield[64]; snprintf(argobjfield, sizeof argobjfield, "icn_arg_obj_%d", i);
            int arg_is_rec = ij_expr_is_record(n->children[i+1]);
            int arg_is_str = !arg_is_rec && ij_expr_is_string(n->children[i+1]);
            int arg_is_dbl = !arg_is_rec && !arg_is_str && ij_expr_is_real(n->children[i+1]);
            char argstrfield[64]; snprintf(argstrfield, sizeof argstrfield, "icn_arg_str_%d", i);
            JL(push_relay);
            if (arg_is_rec) {
                /* record arg: value is in icn_retval_obj; pop the lconst_0 placeholder,
                 * then copy icn_retval_obj into icn_arg_obj_N and sentinel icn_arg_N */
                JI("pop2", "");
                ij_declare_static_obj(argobjfield);
                J("    getstatic %s/icn_retval_obj Ljava/lang/Object;\n", ij_classname);
                ij_put_obj_field(argobjfield);
                ij_declare_static(argfield);
                JI("lconst_0", ""); ij_put_long(argfield);
            } else if (arg_is_str) {
                /* string arg: store in icn_arg_str_N */
                ij_declare_static_str(argstrfield);
                ij_put_str_field(argstrfield);
                ij_declare_static(argfield);
                JI("lconst_0", ""); ij_put_long(argfield); /* sentinel */
            } else {
                ij_declare_static(argfield);
                ij_put_long(argfield);
            }
            JGoto(prev_succ);
            strncpy(prev_succ, arg_alphas[i], 63);
        }

        JL(a);
        if (nargs > 0) JGoto(prev_succ);
        else           JGoto(do_call);

        /* β: for generators, resume if suspended; else fail */
        JL(b);
        if (is_gen) {
            char b_resume[64]; snprintf(b_resume, sizeof b_resume, "icn_%d_b_resume", id);
            char b_fail[64];   snprintf(b_fail,   sizeof b_fail,   "icn_%d_b_fail",   id);
            ij_get_byte("icn_suspended");
            J("    ifeq %s\n", b_fail);
            /* Resume: clear suspend_id AFTER we're done — entry dispatch uses icn_suspend_id!=0 */
            /* (icn_suspended stays set until proc itself clears on fresh call) */
            /* Just invoke — proc entry dispatches via icn_suspend_id */
            char sig[384]; snprintf(sig, sizeof sig, "%s/icn_%s()V", ij_classname, fname);
            JI("invokestatic", sig);
            /* Check result */
            char after_resume[64]; snprintf(after_resume, sizeof after_resume, "icn_%d_after_resume", id);
            ij_jmp_if_failed(after_resume);
            if (ij_proc_returns_str(fname)) { ij_get_str_field("icn_retval_str"); }
            else                            { ij_get_long("icn_retval"); }
            JGoto(ports.γ);
            JL(after_resume); JGoto(ports.ω);
            JL(b_fail); JGoto(ports.ω);
        } else {
            JGoto(ports.ω);
        }

        /* do_call: push args from static fields, invoke proc */
        JL(do_call);
        /* Load args into static arg fields — already done above.
         * Proc pops from icn_arg_0..N-1. */
        {
            char sig[384]; snprintf(sig, sizeof sig, "%s/icn_%s()V", ij_classname, fname);
            JI("invokestatic", sig);
        }
        /* Check icn_failed */
        char after_call[64]; snprintf(after_call, sizeof after_call, "icn_%d_after_call", id);
        ij_jmp_if_failed(after_call);
        /* Check if suspended (generator yielded) or returned */
        if (ij_proc_returns_str(fname)) { ij_get_str_field("icn_retval_str"); }
        else                            { ij_get_long("icn_retval"); }
        JGoto(ports.γ);
        JL(after_call); JGoto(ports.ω);

        if (arg_alphas) free(arg_alphas);
        if (arg_betas)  free(arg_betas);
        (void)np;
        return;
    }

    /* Unknown — fail */
    JL(a); JGoto(ports.ω);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_ALT — E1 | E2 | ... | En  value alternation (n-ary flat)
 *
 * Naïve wiring (old, BUGGY for β-resume):
 *   α → E1.α; E1.ω → E2.α; ... ; En.ω → node.ω
 *   β → E1.β   ← always restarts from E1, loops infinitely in every
 *
 * Correct wiring — indirect-goto gate (JCON ir_a_Alt + JCON-ANALYSIS §'| value alternation'):
 *   Per-site static int field icn_N_alt_gate tracks which Ei last succeeded.
 *   α:    gate=0, goto E1.α
 *   Ei.γ: gate=i (1-based), goto ports.γ     (MoveLabel + succeed)
 *   Ei.ω: goto E(i+1).α  (En.ω → ports.ω)
 *   β:    iload gate → tableswitch → E1.β / E2.β / ... / En.β
 *
 * This matches JCON irgen.icn ir_MoveLabel + ir_IndirectGoto (§4.5).
 * ======================================================================= */
static void ij_emit_alt(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    int nc = n->nchildren;
    char (*ca)[64] = malloc(nc*64);
    char (*cb)[64] = malloc(nc*64);
    /* Per-alternative γ relay labels: we intercept Ei.γ to set the gate */
    char (*cg)[64] = malloc(nc*64);

    /* Gate field name — unique per alt site */
    char gate_fld[64]; snprintf(gate_fld, sizeof gate_fld, "icn_%d_alt_gate", id);
    ij_declare_static_int(gate_fld);

    /* Emit children right-to-left so each child's α/β/γ/ω are known
     * when we wire the chain. Each child's ports.γ → our relay cg[i],
     * ports.ω → next child's α (or ports.ω for last child). */
    for (int i = nc-1; i >= 0; i--) {
        /* relay label: child[i] success lands here → set gate, goto ports.γ */
        snprintf(cg[i], 64, "icn_%d_alt_g%d", id, i);
        IjPorts ep;
        strncpy(ep.γ, cg[i], 63);
        strncpy(ep.ω, (i == nc-1) ? ports.ω : ca[i+1], 63);
        ij_emit_expr(n->children[i], ep, ca[i], cb[i]);
    }

    /* Emit relay labels: cg[i] → pop child value, push lconst_0 sentinel,
     * set gate, goto ports.γ with J on stack.
     * Contract: ICN_ALT always delivers exactly one long (lconst_0) on the
     * stack at ports.γ.  AND relay trampolines issue putstatic :J for ALT
     * children — same as for any long-typed child. */
    /* Determine if all alternatives have the same type for pass-through */
    int alt_is_str = 1, alt_is_dbl = 1;
    for (int i = 0; i < nc; i++) {
        if (!ij_expr_is_string(n->children[i])) alt_is_str = 0;
        if (!ij_expr_is_real(n->children[i]))   alt_is_dbl  = 0;
    }
    /* For uniform-typed ALT (all str, all dbl, or all long): pass value through.
     * For mixed: discard and use lconst_0 sentinel (legacy behaviour). */
    int alt_uniform = alt_is_str || alt_is_dbl || (!alt_is_str && !alt_is_dbl);
    /* alt_val_fld: temp static to hold the yielded value while we set gate */
    char alt_val_fld[80]; snprintf(alt_val_fld, sizeof alt_val_fld, "icn_%d_alt_val", id);
    if (!alt_is_str) {
        if (alt_is_dbl) ij_declare_static_real(alt_val_fld);
        else            ij_declare_static(alt_val_fld);
    }

    for (int i = 0; i < nc; i++) {
        JL(cg[i]);
        if (alt_is_str) {
            /* String on stack — store to scratch astore, set gate, reload */
            int scratch = ij_alloc_int_scratch();
            J("    astore %d\n", scratch);
            J("    sipush %d\n", i+1);
            ij_put_int_field(gate_fld);
            J("    aload %d\n", scratch);
        } else if (alt_is_dbl) {
            /* Double on stack — store to temp dbl field, set gate, reload */
            ij_put_dbl(alt_val_fld);
            J("    sipush %d\n", i+1);
            ij_put_int_field(gate_fld);
            ij_get_dbl(alt_val_fld);
        } else {
            /* Long (or mixed) — store to temp long field, set gate, reload */
            if (ij_expr_is_string(n->children[i])) { JI("pop",""); JI("lconst_0",""); }
            else if (ij_expr_is_real(n->children[i])) {
                JI("invokestatic","java/lang/Double/doubleToRawLongBits(D)J");
            }
            ij_put_long(alt_val_fld);
            J("    sipush %d\n", i+1);
            ij_put_int_field(gate_fld);
            ij_get_long(alt_val_fld);
        }
        JGoto(ports.γ);
    }

    /* α: gate=0 (mark "in α, no prior success"), goto E1.α */
    JL(a);
    JI("iconst_0",""); ij_put_int_field(gate_fld);
    JGoto(ca[0]);

    /* β: indirect-goto via gate → tableswitch → each Ei.β
     * gate values: 0 = never succeeded (shouldn't happen in bounded ctx, go to ω)
     *              1..nc = Ei last succeeded → resume Ei.β                         */
    JL(b);
    ij_get_int_field(gate_fld);
    J("    tableswitch 1 %d\n", nc);
    for (int i = 0; i < nc; i++) {
        J("        %s\n", cb[i]);
    }
    J("        default: %s\n", ports.ω);

    free(ca); free(cb); free(cg);
}

/* =========================================================================
 * ICN_POW — E1 ^ E2  (exponentiation, always returns double)
 * Wiring: standard funcs-set (§4.3): E1 then E2, compute, → γ.
 * Both operands promoted to double. Result = Math.pow(D,D) → double.
 * One-shot (β → ω) — pow is not a generator.
 * ======================================================================= */
static void ij_emit_pow(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char lrelay[64], rrelay[64], lstore[64], rstore[64];
    snprintf(lrelay, sizeof lrelay, "icn_%d_pow_lr", id);
    snprintf(rrelay, sizeof rrelay, "icn_%d_pow_rr", id);
    snprintf(lstore, sizeof lstore, "icn_%d_pow_lv", id);
    snprintf(rstore, sizeof rstore, "icn_%d_pow_rv", id);
    ij_declare_static_real(lstore);  /* D: left operand value */
    ij_declare_static_real(rstore);  /* D: right operand value */

    IcnNode *lchild = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *rchild = (n->nchildren > 1) ? n->children[1] : NULL;

    IjPorts rp; strncpy(rp.γ, rrelay, 63); strncpy(rp.ω, ports.ω, 63);
    IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);

    char ra[64], rb[64]; ij_emit_expr(rchild, rp, ra, rb);
    char la[64], lb[64]; ij_emit_expr(lchild, lp, la, lb);

    JC("POW -- E1 ^ E2 via Math.pow(D,D)");

    /* α: eval left */
    JL(a); JGoto(la);

    /* left γ: promote to double, store, eval right */
    JL(lrelay);
    if (!ij_expr_is_real(lchild)) { JI("l2d",""); }  /* long → double */
    ij_put_real_field(lstore);
    JGoto(ra);

    /* right γ: promote, store, then load left, load right, call Math.pow */
    JL(rrelay);
    if (!ij_expr_is_real(rchild)) { JI("l2d",""); }  /* long → double */
    ij_put_real_field(rstore);
    /* Now load in order: left (base), right (exponent) */
    ij_get_real_field(lstore);
    ij_get_real_field(rstore);
    JI("invokestatic","java/lang/Math/pow(DD)D");
    /* Result is D on stack → γ */
    JGoto(ports.γ);

    /* β: one-shot → ω */
    JL(b); JGoto(ports.ω);
}

 /* ==========================================================================
 * Binary arithmetic — funcs-set wiring (Proebsting §4.3)
 * JVM discipline: operand stack EMPTY at every label boundary.
 * Values passed across labels go through static fields (lc, rc, bf).
 * ========================================================================= */
static void ij_emit_binop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char compute[64]; snprintf(compute, sizeof compute, "icn_%d_compute", id);
    char lbfwd[64];   snprintf(lbfwd,   sizeof lbfwd,   "icn_%d_lb",     id);
    char lstore[64];  snprintf(lstore,  sizeof lstore,  "icn_%d_lstore", id);
    /* Determine if this is a double (real) operation */
    int is_dbl = ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]);
    /* Use static fields for relay staging so relay labels are safe to reach on
     * an empty stack (e.g. from ICN_EVERY β-tableswitch).  JVM lstore/lload
     * require the verifier to see a long on the stack at every predecessor. */
    char lc_field[80]; snprintf(lc_field, sizeof lc_field, "icn_%d_binop_lc", id);
    char rc_field[80]; snprintf(rc_field, sizeof rc_field, "icn_%d_binop_rc", id);
    char bf_field[80]; snprintf(bf_field, sizeof bf_field, "icn_%d_binop_bf", id);
    if (is_dbl) { ij_declare_static_dbl(lc_field); ij_declare_static_dbl(rc_field); }
    else        { ij_declare_static(lc_field);      ij_declare_static(rc_field);     }
    ij_declare_static_int(bf_field);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay, 63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    IcnNode *lchild = n->children[0];
    int left_is_value = (lchild->kind == ICN_VAR || lchild->kind == ICN_INT ||
                         lchild->kind == ICN_REAL ||
                         lchild->kind == ICN_STR || lchild->kind == ICN_CALL);

    /* left_relay: value on stack → promote if needed → store to lc_field */
    JL(left_relay);
    if (is_dbl && !ij_expr_is_real(n->children[0])) JI("l2d", "");
    if (is_dbl) ij_put_dbl(lc_field); else ij_put_long(lc_field);
    JGoto(lstore);
    /* right_relay: value on stack → promote if needed → store to rc_field */
    JL(right_relay);
    if (is_dbl && !ij_expr_is_real(n->children[1])) JI("l2d", "");
    if (is_dbl) ij_put_dbl(rc_field); else ij_put_long(rc_field);
    JGoto(compute);

    JL(lbfwd); JGoto(lb);
    JL(a); JI("iconst_0",""); ij_put_int_field(bf_field); JGoto(la);
    JL(b);
    if (left_is_value) { JI("iconst_1",""); ij_put_int_field(bf_field); JGoto(la); }
    else { JGoto(rb); }

    JL(lstore);
    ij_get_int_field(bf_field);
    J("    ifeq %s\n", ra);
    JGoto(rb);

    /* compute: lc_field=left, rc_field=right — stack empty */
    JL(compute);
    if (is_dbl) { ij_get_dbl(lc_field); ij_get_dbl(rc_field); }
    else        { ij_get_long(lc_field); ij_get_long(rc_field); }
    if (is_dbl) {
        switch (n->kind) {
            case ICN_ADD: JI("dadd",""); break;
            case ICN_SUB: JI("dsub",""); break;
            case ICN_MUL: JI("dmul",""); break;
            case ICN_DIV: JI("ddiv",""); break;
            case ICN_MOD: JI("drem",""); break;
            default: break;
        }
    } else {
        switch (n->kind) {
            case ICN_ADD: JI("ladd",""); break;
            case ICN_SUB: JI("lsub",""); break;
            case ICN_MUL: JI("lmul",""); break;
            case ICN_DIV: JI("ldiv",""); break;
            case ICN_MOD: JI("lrem",""); break;
            default: break;
        }
    }
    JGoto(ports.γ);
}


/* =========================================================================
 * Relational operators — goal-directed retry (Proebsting §4.3)
 * JVM discipline: stack empty at all label boundaries.
 * lc_field = left cache, rc_field = right staging.
 * ======================================================================= */
static void ij_emit_relop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char chk[64];    snprintf(chk,    sizeof chk,    "icn_%d_check",  id);
    char lbfwd[64];  snprintf(lbfwd,  sizeof lbfwd,  "icn_%d_lb",     id);
    char lstore[64]; snprintf(lstore, sizeof lstore, "icn_%d_lstore", id);

    /* Determine operand type: string > double > long */
    int is_str = ij_expr_is_string(n->children[0]) || ij_expr_is_string(n->children[1]);
    int is_dbl = !is_str &&
                 (ij_expr_is_real(n->children[0]) || ij_expr_is_real(n->children[1]));

    /* Use static fields for relay staging (not JVM locals) so that relay labels
     * are safe to reach on an empty stack — e.g. from an ICN_EVERY β-tableswitch
     * branch that dispatches through the same label region.  JVM locals require
     * the verifier to see a long on the stack at every predecessor; putstatic/
     * getstatic do not, because the field is already live on all paths. */
    char lc_field[80]; snprintf(lc_field, sizeof lc_field, "icn_%d_relop_lc", id);
    char rc_field[80]; snprintf(rc_field, sizeof rc_field, "icn_%d_relop_rc", id);
    if (is_str) { ij_declare_static_str(lc_field); ij_declare_static_str(rc_field); }
    else if (is_dbl) { ij_declare_static_dbl(lc_field); ij_declare_static_dbl(rc_field); }
    else        { ij_declare_static(lc_field);      ij_declare_static(rc_field);     }
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    /* left_relay: value on stack → store to lc_field */
    JL(left_relay);
    if (is_str)      ij_put_str_field(lc_field);
    else if (is_dbl) { if (!ij_expr_is_real(n->children[0])) JI("l2d","");
                       ij_put_dbl(lc_field); }
    else             ij_put_long(lc_field);
    JGoto(lstore);
    /* right_relay: value on stack → store to rc_field */
    JL(right_relay);
    if (is_str)      ij_put_str_field(rc_field);
    else if (is_dbl) { if (!ij_expr_is_real(n->children[1])) JI("l2d","");
                       ij_put_dbl(rc_field); }
    else             ij_put_long(rc_field);
    JGoto(chk);

    JL(lbfwd); JGoto(lb);
    JL(a); JGoto(la);
    JL(b); JGoto(rb);

    /* lstore: lc_field has left → go to right.α */
    JL(lstore); JGoto(ra);

    /* chk: lc_field=left, rc_field=right — stack empty */
    JL(chk);
    const char *jfail;
    if (is_str) {
        /* String comparison: lc.compareTo(rc) → int; 0=eq, <0=lt, >0=gt */
        ij_get_str_field(lc_field);
        ij_get_str_field(rc_field);
        JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
        switch (n->kind) {
            case ICN_LT: jfail = "ifge"; break;
            case ICN_LE: jfail = "ifgt"; break;
            case ICN_GT: jfail = "ifle"; break;
            case ICN_GE: jfail = "iflt"; break;
            case ICN_EQ: jfail = "ifne"; break;
            case ICN_NE: jfail = "ifeq"; break;
            default:     jfail = "ifne"; break;
        }
        J("    %s %s\n", jfail, rb);
        /* success: reload right as String result, convert to lconst_0 token */
        ij_get_str_field(rc_field);
        JGoto(ports.γ);
        return;
    } else if (is_dbl) {
        ij_get_dbl(lc_field); ij_get_dbl(rc_field);
        /* dcmpl: pushes -1 if either NaN, or left < right; 0 if equal; 1 if left > right.
         * Use dcmpl for </<= (NaN → -1 → treated as less, safe fail for > case).
         * Use dcmpg for >/>= (NaN → +1 → treated as greater, safe fail for < case).
         * For = and ~=, dcmpl works (NaN yields -1 or 1, never 0 → correct). */
        switch (n->kind) {
            case ICN_LT: JI("dcmpl",""); jfail = "ifge"; break;
            case ICN_LE: JI("dcmpl",""); jfail = "ifgt"; break;
            case ICN_GT: JI("dcmpg",""); jfail = "ifle"; break;
            case ICN_GE: JI("dcmpg",""); jfail = "iflt"; break;
            case ICN_EQ: JI("dcmpl",""); jfail = "ifne"; break;
            case ICN_NE: JI("dcmpl",""); jfail = "ifeq"; break;
            default:     JI("dcmpl",""); jfail = "ifne"; break;
        }
    } else {
        ij_get_long(lc_field); ij_get_long(rc_field);
        /* Integer path: lcmp leaves int on stack */
        JI("lcmp","");
        switch (n->kind) {
            case ICN_LT: jfail = "ifge"; break;
            case ICN_LE: jfail = "ifgt"; break;
            case ICN_GT: jfail = "ifle"; break;
            case ICN_GE: jfail = "iflt"; break;
            case ICN_EQ: jfail = "ifne"; break;
            case ICN_NE: jfail = "ifeq"; break;
            default:     jfail = "ifne"; break;
        }
    }
    J("    %s %s\n", jfail, rb);
    /* success: reload right as result value */
    if (is_dbl) ij_get_dbl(rc_field); else ij_get_long(rc_field);
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_TO — range generator inline counter (§4.4)
 * ======================================================================= */
static void ij_emit_to(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64], code[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b); lbl_code(id,code,sizeof code);
    char init[64]; snprintf(init,sizeof init,"icn_%d_init",id);
    char e1bf[64]; snprintf(e1bf,sizeof e1bf,"icn_%d_e1b",id);
    char e2bf[64]; snprintf(e2bf,sizeof e2bf,"icn_%d_e2b",id);
    /* Static fields for all cross-label values */
    char I_field[64];     snprintf(I_field,     sizeof I_field,     "icn_%d_I",     id);
    char bound_field[64]; snprintf(bound_field, sizeof bound_field, "icn_%d_bound", id);
    char e1cur_field[64]; snprintf(e1cur_field, sizeof e1cur_field, "icn_%d_e1cur", id);
    char e2seen_field[64];snprintf(e2seen_field,sizeof e2seen_field,"icn_%d_e2seen",id);
    /* Staging field: child exprs store their value here before jumping to init/code */
    char e1val_field[64]; snprintf(e1val_field, sizeof e1val_field, "icn_%d_e1val", id);
    char e2val_field[64]; snprintf(e2val_field, sizeof e2val_field, "icn_%d_e2val", id);
    ij_declare_static(I_field);
    ij_declare_static(bound_field);
    ij_declare_static(e1cur_field);
    ij_declare_static_int(e2seen_field);
    ij_declare_static(e1val_field);
    ij_declare_static(e2val_field);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Relay labels: drain stack before crossing to init */
    char e1_relay[64]; snprintf(e1_relay, sizeof e1_relay, "icn_%d_e1relay", id);
    char e2_relay[64]; snprintf(e2_relay, sizeof e2_relay, "icn_%d_e2relay", id);

    IjPorts e2p; strncpy(e2p.γ, e2_relay, 63); strncpy(e2p.ω, e1bf, 63);
    char e2a[64],e2b[64]; ij_emit_expr(n->children[1], e2p, e2a, e2b);
    IjPorts e1p; strncpy(e1p.γ, e1_relay, 63); strncpy(e1p.ω, ports.ω, 63);
    char e1a[64],e1b[64]; ij_emit_expr(n->children[0], e1p, e1a, e1b);

    /* e1_relay: E1 long on stack → store to e1val, jump to e2a */
    JL(e1_relay);
    ij_put_long(e1val_field);
    JGoto(e2a);

    /* e2_relay: E2 long on stack → store to e2val, jump to init */
    JL(e2_relay);
    ij_put_long(e2val_field);
    JGoto(init);

    JL(e1bf);
    JI("iconst_0",""); ij_put_int_field(e2seen_field);
    JGoto(e1b);
    JL(e2bf); JGoto(e2b);

    JL(a);
    JI("iconst_0",""); ij_put_int_field(e2seen_field);
    JGoto(e1a);
    /* β: increment I, loop to code — stack empty here */
    JL(b);
    ij_get_long(I_field);
    JI("lconst_1","");
    JI("ladd","");
    ij_put_long(I_field);
    JGoto(code);

    /* init: E1 in e1val_field, E2 in e2val_field — stack empty */
    JL(init);
    ij_get_long(e2val_field);
    ij_put_long(bound_field);
    char init_e2adv[64]; snprintf(init_e2adv, sizeof init_e2adv, "icn_%d_init_e2adv", id);
    ij_get_int_field(e2seen_field);
    J("    ifne %s\n", init_e2adv);
    /* First time: read E1 value */
    ij_get_long(e1val_field);
    ij_put_long(e1cur_field);
    ij_get_long(e1cur_field);
    ij_put_long(I_field);
    JI("iconst_1",""); ij_put_int_field(e2seen_field);
    JGoto(code);
    JL(init_e2adv);
    ij_get_long(e1cur_field);
    ij_put_long(I_field);
    JGoto(code);

    /* code: I <= bound? — stack empty */
    JL(code);
    ij_get_long(I_field);
    ij_get_long(bound_field);
    JI("lcmp","");
    J("    ifgt %s\n", e2bf);
    /* Push I as the generated value — consumer must drain before next label */
    ij_get_long(I_field);
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_EVERY
 * ======================================================================= */
static void ij_emit_every(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char gbfwd[64]; snprintf(gbfwd, sizeof gbfwd, "icn_%d_genb", id);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *gen  = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;
    char ga[64], gb[64];

    if (body) {
        /* bstart: generator yielded a value — drain it, then run body */
        char bstart[64]; snprintf(bstart, sizeof bstart, "icn_%d_body", id);
        IjPorts bp; strncpy(bp.γ,gbfwd,63); strncpy(bp.ω,gbfwd,63);
        char ba[64], bb[64];
        ij_loop_push(ports.ω, ga);   /* break→exit every, next→pump generator */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        IjPorts gp; strncpy(gp.γ,bstart,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
        JL(bstart); JI(ij_expr_is_string(gen) ? "pop" : "pop2",""); JGoto(ba);
    } else {
        IjPorts gp; strncpy(gp.γ,gbfwd,63); strncpy(gp.ω,ports.ω,63);
        ij_emit_expr(gen, gp, ga, gb);
    }
    /* gbfwd: generator yielded — drain value, kick beta to get next */
    JL(gbfwd); JI(ij_expr_is_string(gen) ? "pop" : "pop2",""); JGoto(gb);
    JL(a); JGoto(ga);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_WHILE
 * ======================================================================= */
static void ij_emit_while(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;

    char cond_ok[64];  snprintf(cond_ok,  sizeof cond_ok,  "icn_%d_condok", id);
    char loop_top[64]; snprintf(loop_top, sizeof loop_top, "icn_%d_top",    id);

    IjPorts cp; strncpy(cp.γ,cond_ok,63); strncpy(cp.ω,ports.ω,63);
    char ca[64], cb[64]; ij_emit_expr(cond, cp, ca, cb);

    JL(cond_ok);
    if (ij_expr_is_string(cond)) { JI("pop",""); } else { JI("pop2",""); }  /* discard condition value */
    if (body) {
        char ba[64], bb[64];
        char body_start[64]; snprintf(body_start, sizeof body_start, "icn_%d_wbstart", id);
        char body_drain[64]; snprintf(body_drain, sizeof body_drain, "icn_%d_wbdrain", id);
        /* Jump explicitly to body start (avoid fall-through into mid-body code) */
        JGoto(body_start);
        /* body.γ → drain (pop body result), body.ω → loop_top (no value) */
        IjPorts bp; strncpy(bp.γ, body_drain, 63); strncpy(bp.ω, loop_top, 63);
        ij_loop_push(ports.ω, ca);   /* break→exit, next→re-eval cond */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        /* body_start label anchors the explicit jump from cond_ok */
        JL(body_start); JGoto(ba);
        /* drain body return value then loop */
        JL(body_drain);
        if (ij_expr_is_string(body)) { JI("pop",""); }
        else if (ij_expr_is_real(body)) { JI("pop2",""); }
        else { JI("pop2",""); }
        JL(loop_top); JGoto(ca);
    } else {
        JGoto(ca);
    }
    JL(a); JGoto(ca);
    JL(b); JGoto(ports.ω);
}

static void ij_emit_until(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    /* until E do body end — loop while E FAILS; exit when E SUCCEEDS.
     * Byrd-box dual of while: cond.γ → ports.ω (cond succeeded → stop)
     *                         cond.ω → body.α  (cond failed → run body) */
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *cond = n->children[0];
    IcnNode *body = (n->nchildren > 1) ? n->children[1] : NULL;

    char cond_ok[64];   snprintf(cond_ok,   sizeof cond_ok,   "icn_%d_condok",  id);
    char cond_fail[64]; snprintf(cond_fail, sizeof cond_fail, "icn_%d_cfail",   id);
    char loop_top[64];  snprintf(loop_top,  sizeof loop_top,  "icn_%d_top",     id);

    /* cond.γ → cond_ok (discard value, then exit); cond.ω → cond_fail → body */
    IjPorts cp; strncpy(cp.γ, cond_ok, 63); strncpy(cp.ω, cond_fail, 63);
    char ca[64], cb[64]; ij_emit_expr(cond, cp, ca, cb);

    /* cond succeeded → discard value, jump to ports.ω */
    JL(cond_ok);
    if (!ij_expr_is_string(cond)) { JI("pop2",""); } else { JI("pop",""); }
    JGoto(ports.ω);

    /* cond failed → run body */
    JL(cond_fail);
    if (body) {
        char ba[64], bb[64];
        char body_ok[64]; snprintf(body_ok, sizeof body_ok, "icn_%d_bok", id);
        IjPorts bp; strncpy(bp.γ, body_ok, 63); strncpy(bp.ω, loop_top, 63);
        ij_loop_push(ports.ω, ca);   /* break→exit, next→re-eval cond */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        JGoto(ba);
        /* body succeeded: drain value, loop */
        JL(body_ok);
        if (!ij_expr_is_string(body)) { JI("pop2",""); } else { JI("pop",""); }
        JL(loop_top); JGoto(ca);
    } else {
        JL(loop_top); JGoto(ca);
    }
    JL(a); JGoto(ca);
    JL(b); JGoto(ports.ω);
}

static void ij_emit_repeat(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    /* repeat body end — run body forever; break exits via ports.ω.
     * body.γ and body.ω both loop back to body.α. */
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *body = (n->nchildren > 0) ? n->children[0] : NULL;

    char loop_top[64]; snprintf(loop_top, sizeof loop_top, "icn_%d_top", id);

    if (body) {
        char ba[64], bb[64];
        char body_ok[64]; snprintf(body_ok, sizeof body_ok, "icn_%d_bok", id);
        IjPorts bp; strncpy(bp.γ, body_ok, 63); strncpy(bp.ω, loop_top, 63);
        ij_loop_push(ports.ω, loop_top);  /* break→exit, next→loop_top */
        ij_emit_expr(body, bp, ba, bb);
        ij_loop_pop();
        JL(a); JL(loop_top); JGoto(ba);
        /* body succeeded: drain value, loop again */
        JL(body_ok);
        if (!ij_expr_is_string(body)) { JI("pop2",""); } else { JI("pop",""); }
        JGoto(loop_top);
    } else {
        JL(a); JL(loop_top); JGoto(loop_top);  /* infinite empty loop */
    }
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * String type predicate and concat emitter
 * ======================================================================= */
static int ij_expr_is_string(IcnNode *n) {
    if (!n) return 0;
    switch (n->kind) {
        case ICN_STR:    return 1;
        case ICN_CSET:   return 1;  /* cset literal is a String */
        case ICN_CONCAT: return 1;
        case ICN_LCONCAT:return 1;  /* Tiny-ICON: ||| treated as string concat */
        case ICN_BANG:
            /* !E yields single-char Strings for string operands,
             * but longs for list operands */
            if (n->nchildren >= 1) return ij_expr_is_list(n->children[0]) ? 0 : 1;
            return 1;
        case ICN_AUGOP:  /* ||:= (TK_AUGCONCAT=35) yields String; arithmetic augops yield long */
            return (n->val.ival == 35) ? 1 : 0;
        case ICN_CALL: {
            if (n->nchildren >= 1) {
                IcnNode *fn = n->children[0];
                if (fn && fn->kind == ICN_VAR) {
                    const char *fn_name = fn->val.sval;
                    /* write(str_arg) returns its argument */
                    if (strcmp(fn_name, "write") == 0 && n->nchildren >= 2)
                        return ij_expr_is_string(n->children[1]);
                    /* tab/move return String substrings */
                    if (strcmp(fn_name, "tab") == 0)    return 1;
                    if (strcmp(fn_name, "move") == 0)   return 1;
                    /* string() conversion returns String */
                    if (strcmp(fn_name, "string") == 0) return 1;
                    /* read()/reads() return String (or fail) */
                    if (strcmp(fn_name, "read")  == 0) return 1;
                    if (strcmp(fn_name, "reads") == 0) return 1;
                    /* M-IJ-BUILTINS-STR: all return String */
                    if (strcmp(fn_name, "repl")    == 0) return 1;
                    if (strcmp(fn_name, "reverse") == 0) return 1;
                    if (strcmp(fn_name, "left")    == 0) return 1;
                    if (strcmp(fn_name, "right")   == 0) return 1;
                    if (strcmp(fn_name, "center")  == 0) return 1;
                    if (strcmp(fn_name, "trim")    == 0) return 1;
                    if (strcmp(fn_name, "map")     == 0) return 1;
                    if (strcmp(fn_name, "char")    == 0) return 1;
                    /* M-IJ-BUILTINS-TYPE: string-returning */
                    if (strcmp(fn_name, "type")    == 0) return 1;
                    if (strcmp(fn_name, "image")   == 0) return 1;
                    if (strcmp(fn_name, "copy")    == 0 && n->nchildren >= 2
                        && ij_expr_is_string(n->children[1])) return 1;
                    /* ord() returns long — not listed here */
                    /* match returns a long position, not a String */
                    /* user-defined proc — check proc table */
                    if (ij_proc_returns_str(fn_name)) return 1;
                }
            }
            return 0;
        }
        case ICN_ASSIGN: {
            /* Assignment returns the RHS value */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_SCAN: {
            /* Scan result type = body result type */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_IF: {
            /* if/then/else: result type = then-branch type (conservative: check then) */
            if (n->nchildren >= 2) return ij_expr_is_string(n->children[1]);
            return 0;
        }
        case ICN_CASE: {
            /* case: result type = first result branch type (children[2] if exists) */
            if (n->nchildren >= 3) return ij_expr_is_string(n->children[2]);
            return 0;
        }
        case ICN_ALT: {
            /* ALT is string-typed if all alternatives are strings */
            if (n->nchildren == 0) return 0;
            for (int i = 0; i < n->nchildren; i++)
                if (!ij_expr_is_string(n->children[i])) return 0;
            return 1;
        }
        case ICN_AND: {
            /* AND chain result type = last child's type */
            if (n->nchildren >= 1)
                return ij_expr_is_string(n->children[n->nchildren - 1]);
            return 0;
        }
        case ICN_LIMIT: {
            /* E \ N yields same type as E */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[0]);
            return 0;
        }
        case ICN_SWAP: {
            /* :=: returns new lhs value — same type as children */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[0]);
            return 0;
        }
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
            /* Numeric relops yield long; string relops yield the rc String value */
            if (n->nchildren >= 2)
                return ij_expr_is_string(n->children[0]) ||
                       ij_expr_is_string(n->children[1]);
            return 0;
        case ICN_SUBSCRIPT:
            /* t[k] on a table yields a long, not a String */
            if (n->nchildren >= 1 && ij_expr_is_table(n->children[0])) return 0;
            return 1;  /* s[i] always yields a single-char String */
        case ICN_SECTION:
            return 1;  /* s[i:j] always yields a String */
        case ICN_SEQ_EXPR: {
            /* Result type is that of the last child */
            if (n->nchildren >= 1) return ij_expr_is_string(n->children[n->nchildren-1]);
            return 0;
        }
        case ICN_VAR: {
            /* &subject keyword is always a String */
            if (strcmp(n->val.sval, "&subject") == 0) return 1;
            /* Check if var's static field is typed 'A' (String) */
            char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
            for (int i = 0; i < ij_nstatics; i++)
                if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'A') return 1;
            /* Also check global var */
            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
            for (int i = 0; i < ij_nstatics; i++)
                if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'A') return 1;
            return 0;
        }
        default: return 0;
    }
}

/* =========================================================================
 * List type predicate — returns 1 if node is known to produce an ArrayList
 * ======================================================================= */
static int ij_expr_is_list(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_MAKELIST) return 1;
    if (n->kind == ICN_ASSIGN) {
        /* Assignment returns the RHS value */
        if (n->nchildren >= 2) return ij_expr_is_list(n->children[1]);
        return 0;
    }
    if (n->kind == ICN_CALL) {
        /* push(L,v) and put(L,v) return the list; list(n,x) returns a new list */
        if (n->nchildren >= 1) {
            IcnNode *fn = n->children[0];
            if (fn && fn->kind == ICN_VAR) {
                const char *fname = fn->val.sval;
                if (strcmp(fname, "push") == 0 || strcmp(fname, "put") == 0 ||
                    strcmp(fname, "list") == 0 ||
                    strcmp(fname, "sort") == 0 || strcmp(fname, "sortf") == 0) return 1;
            }
        }
        return 0;
    }
    if (n->kind == ICN_VAR) {
        /* Check local/global var static type tag 'L' */
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], fld) && (ij_static_types[i] == 'L' || ij_static_types[i] == 'R')) return 1;
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], gname) && (ij_static_types[i] == 'L' || ij_static_types[i] == 'R')) return 1;
    }
    return 0;
}

static int ij_expr_is_table(IcnNode *n) {
    if (!n) return 0;
    if (n->kind == ICN_ASSIGN) {
        if (n->nchildren >= 2) return ij_expr_is_table(n->children[1]);
        return 0;
    }
    if (n->kind == ICN_CALL) {
        if (n->nchildren >= 1) {
            IcnNode *fn = n->children[0];
            if (fn && fn->kind == ICN_VAR) {
                const char *fname = fn->val.sval;
                if (strcmp(fname, "table") == 0) return 1;
                if (strcmp(fname, "insert") == 0) return 1;
                if (strcmp(fname, "delete") == 0) return 1;
            }
        }
        return 0;
    }
    if (n->kind == ICN_VAR) {
        char fld[128]; ij_var_field(n->val.sval, fld, sizeof fld);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], fld) && ij_static_types[i] == 'T') return 1;
        char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", n->val.sval);
        for (int i = 0; i < ij_nstatics; i++)
            if (!strcmp(ij_statics[i], gname) && ij_static_types[i] == 'T') return 1;
    }
    return 0;
}

/* ICN_CONCAT — E1 || E2  (string concatenation)
 * Byrd-box wiring: same funcs-set pattern as binop (Proebsting §4.3).
 * Left/right values are String refs stored in String-typed static fields.
 * Compute: invokevirtual String.concat → result String ref on stack → ports.γ
 *
 * Stack discipline: EMPTY at every label. String refs passed via static fields.
 */
static void ij_emit_concat(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    char compute[64]; snprintf(compute, sizeof compute, "icn_%d_compute", id);
    char lbfwd[64];   snprintf(lbfwd,   sizeof lbfwd,   "icn_%d_lb",     id);
    char lstore[64];  snprintf(lstore,  sizeof lstore,  "icn_%d_lstore", id);
    char lc_fld[64];  snprintf(lc_fld,  sizeof lc_fld,  "icn_%d_lc",     id);
    char rc_fld[64];  snprintf(rc_fld,  sizeof rc_fld,  "icn_%d_rc",     id);
    /* bf: track whether we are on fresh (0) or resume (1) path */
    int bf_slot = ij_alloc_int_scratch();
    ij_declare_static_str(lc_fld);
    ij_declare_static_str(rc_fld);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char right_relay[64]; snprintf(right_relay, sizeof right_relay, "icn_%d_rrelay", id);
    char left_relay[64];  snprintf(left_relay,  sizeof left_relay,  "icn_%d_lrelay", id);

    IjPorts rp; strncpy(rp.γ, right_relay, 63); strncpy(rp.ω, lbfwd, 63);
    char ra[64], rb[64]; ij_emit_expr(n->children[1], rp, ra, rb);
    IjPorts lp; strncpy(lp.γ, left_relay,  63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(n->children[0], lp, la, lb);

    IcnNode *lchild = n->children[0];
    /* left_is_value: true only for one-shot string producers (literals, vars, one-shot calls).
     * ICN_ALT is a generator — its β must be used to advance it, not restart from α. */
    int left_is_value = ij_expr_is_string(lchild) && lchild->kind != ICN_ALT;

    /* left_relay: String ref on stack → astore into lc_fld → lstore */
    JL(left_relay); ij_put_str_field(lc_fld); JGoto(lstore);
    /* right_relay: String ref on stack → astore into rc_fld → compute */
    JL(right_relay); ij_put_str_field(rc_fld); JGoto(compute);

    JL(lbfwd); JGoto(lb);
    JL(a); JI("iconst_0",""); J("    istore %d\n", bf_slot); JGoto(la);
    JL(b);
    if (left_is_value) { JI("iconst_1",""); J("    istore %d\n", bf_slot); JGoto(la); }
    else { JGoto(rb); }

    JL(lstore);
    J("    iload %d\n", bf_slot);
    J("    ifeq %s\n", ra);
    JGoto(rb);

    /* compute: lc_fld = left String, rc_fld = right String */
    JL(compute);
    ij_get_str_field(lc_fld);
    ij_get_str_field(rc_fld);
    JI("invokevirtual", "java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_SCAN — E ? body
 *
 * Byrd-box wiring (four-port, per JCON ir_a_Scan / JCON-ANALYSIS §E ? body):
 *
 *   α  → expr.α
 *   expr.γ (new subject on stack as String):
 *          save old icn_subject → old_subject_N
 *          save old icn_pos    → old_pos_N
 *          putstatic icn_subject  (consumes new String from stack)
 *          iconst_0 → putstatic icn_pos
 *          → body.α
 *   expr.ω → ports.ω  (expr failed → whole scan fails)
 *   body.γ → restore old_subject / old_pos → ports.γ
 *   body.ω → restore old_subject / old_pos → expr.β
 *             (expr is one-shot string → expr.β → ports.ω)
 *   β      → restore old_subject / old_pos → body.β
 *
 * Save slots: per-scan static fields old_subject_N (String) and old_pos_N (I).
 * ======================================================================= */
static void ij_emit_scan(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Per-scan save-restore static fields */
    char old_subj[64], old_pos[64];
    snprintf(old_subj, sizeof old_subj, "icn_scan_oldsubj_%d", id);
    snprintf(old_pos,  sizeof old_pos,  "icn_scan_oldpos_%d",  id);
    ij_declare_static_str(old_subj);
    ij_declare_static_int(old_pos);

    /* Ensure global subject/pos fields are declared */
    ij_declare_static_str("icn_subject");
    ij_declare_static_int("icn_pos");

    /* Child nodes */
    IcnNode *expr_node = (n->nchildren >= 1) ? n->children[0] : NULL;
    IcnNode *body_node = (n->nchildren >= 2) ? n->children[1] : NULL;

    /* Label names for intermediate wiring */
    char setup[64], body_fail_restore[64], body_ok_restore[64], beta_restore[64];
    snprintf(setup,             sizeof setup,             "icn_%d_scan_setup",    id);
    snprintf(body_fail_restore, sizeof body_fail_restore, "icn_%d_scan_bfail",    id);
    snprintf(body_ok_restore,   sizeof body_ok_restore,   "icn_%d_scan_bok",      id);
    snprintf(beta_restore,      sizeof beta_restore,       "icn_%d_scan_beta",     id);

    /* Wire expr: success → setup, fail → ports.ω */
    IjPorts ep;
    strncpy(ep.γ, setup,    63);
    strncpy(ep.ω, ports.ω,  63);
    char ea[64], eb[64];
    ij_emit_expr(expr_node, ep, ea, eb);

    /* Wire body: success → body_ok_restore, fail → body_fail_restore */
    IjPorts bp;
    strncpy(bp.γ, body_ok_restore,   63);
    strncpy(bp.ω, body_fail_restore,  63);
    char ba[64], bb[64];
    ij_emit_expr(body_node, bp, ba, bb);

    /* α → expr.α */
    JC("SCAN"); JL(a); JGoto(ea);

    /* β → restore → body.β */
    JL(b); JGoto(beta_restore);

    /* setup: expr.γ — new subject String is on JVM stack
     *   save old subject/pos, install new subject, reset pos, → body.α */
    JL(setup);
    JC("SCAN save+install");
    /* Stack has new subject String — save it in a temp by storing first,
     * then saving old subject, restoring new.  Use dup to keep on stack. */
    /* Strategy: dup the new subject, put into icn_subject, then save old
     * subject was already overwritten — so: save old first, then install.
     * But old is in icn_subject already and new is on stack.  Use a temp
     * approach: swap via old_subj slot. */
    /* Correct sequence:
     *   [stack: new_str]
     *   getstatic icn_subject → save to old_subj  (pops nothing, pushes old)
     *   putstatic old_subj                         (consumes old)
     *   getstatic icn_pos → save to old_pos
     *   putstatic old_pos
     *   [stack: new_str still there from expr.γ]
     *   putstatic icn_subject                      (consumes new_str)
     *   iconst_0 → putstatic icn_pos
     * PROBLEM: getstatic doesn't consume the new_str — but new_str is
     * already on the stack when we arrive here.  We must save icn_subject
     * BEFORE consuming new_str.  Use dup: */
    /* Actually at expr.γ the String ref is on the JVM operand stack.
     * We do NOT want to consume it yet.  So: */
    /*   dup                        ; [new new]
     *   → we don't need dup; instead: save old, then install new.
     * The new_str is on stack.  getstatic pushes old on top:
     *   [new old_subj]
     * That's 2 items on stack.  We need to putstatic old_subj first,
     * but that pops old_subj leaving [new].  Perfect: */
    ij_get_str_field("icn_subject");         /* push old subject */
    ij_put_str_field(old_subj);              /* save old subject; stack: [new_str] */
    ij_get_int_field("icn_pos");             /* push old pos */
    ij_put_int_field(old_pos);               /* save old pos;    stack: [new_str] */
    ij_put_str_field("icn_subject");         /* install new subject; stack: [] */
    J("    iconst_0\n");
    ij_put_int_field("icn_pos");             /* reset pos to 0 */
    JGoto(ba);                               /* → body.α */

    /* body.γ — body succeeded: restore subject/pos, → ports.γ */
    JL(body_ok_restore);
    JC("SCAN restore (body ok)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(ports.γ);

    /* body.ω — body failed: restore subject/pos, → expr.β (retry = fail) */
    JL(body_fail_restore);
    JC("SCAN restore (body fail)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(eb);   /* expr.β — one-shot string → expr.ω → ports.ω */

    /* β: restore subject/pos, → body.β */
    JL(beta_restore);
    JC("SCAN restore (outer beta)");
    ij_get_str_field(old_subj); ij_put_str_field("icn_subject");
    ij_get_int_field(old_pos);  ij_put_int_field("icn_pos");
    JGoto(bb);   /* body.β */
}

/* Re-implement ICN_NOT cleanly with succeed trampoline */
/* =========================================================================
 * ICN_INITIAL — initial stmt
 * Runs the child statement exactly once, on the first call to this procedure.
 * Implementation: per-proc static boolean field icn_init_PROCNAME (I, default 0).
 * α: load flag; if non-zero → skip (already ran) → ports.γ with lconst_0.
 *    else: set flag=1, emit child stmt, → ports.γ.
 * Always succeeds (initial block failure is ignored — Icon spec).
 * β → ports.ω (one-shot; if called again after first, just succeeds silently).
 * ======================================================================= */
static void ij_emit_initial(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Per-proc init flag: icn_init_PROCNAME */
    char flag_fld[80]; snprintf(flag_fld, sizeof flag_fld, "icn_init_%s", ij_cur_proc);
    ij_declare_static_int(flag_fld);

    char skip[64], run[64];
    snprintf(skip, sizeof skip, "icn_%d_init_skip", id);
    snprintf(run,  sizeof run,  "icn_%d_init_run",  id);

    JL(a);
    ij_get_int_field(flag_fld);
    J("    ifne %s\n", skip);        /* flag != 0 → already ran, skip */
    /* First call: set flag, run body */
    JI("iconst_1", ""); ij_put_int_field(flag_fld);
    if (n->nchildren >= 1 && n->children[0]) {
        /* Wire body's γ to a drain label so we pop the body's result before
         * pushing lconst_0.  Both the run-path (body succeeded) and the
         * body-fail path (body.ω → run) converge at the drain label.
         * This keeps stack height at ports.γ = 2 (one long slot) on all paths. */
        char body_drain[64];
        snprintf(body_drain, sizeof body_drain, "icn_%d_init_drain", id);
        IjPorts cp;
        strncpy(cp.γ, body_drain, 63);  /* body success → drain */
        strncpy(cp.ω, run,        63);  /* body fail    → run (no value on stack) */
        char ca[64], cb[64]; ij_emit_expr(n->children[0], cp, ca, cb);
        JGoto(ca);
        JL(body_drain);
        /* Drain the value the body left on the stack */
        int bstr = ij_expr_is_string(n->children[0]);
        int bdbl = !bstr && ij_expr_is_real(n->children[0]);
        if (bstr) JI("pop", ""); else (void)bdbl, JI("pop2", "");
        /* Fall through to run */
        JL(run);
    }
    JI("lconst_0", ""); JGoto(ports.γ);
    JL(skip);
    JI("lconst_0", ""); JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

static void ij_emit_not(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    char child_ok[64];  snprintf(child_ok,  sizeof child_ok,  "icn_%d_cok",  id);
    char succeed[64];   snprintf(succeed,   sizeof succeed,   "icn_%d_succ", id);
    IjPorts cp;
    strncpy(cp.γ, child_ok, 63);  /* child success → not fails */
    strncpy(cp.ω, succeed,  63);  /* child fail    → not succeeds */
    char ca[64], cb[64];
    ij_emit_expr(child, cp, ca, cb);
    JC("NOT"); JL(a); JGoto(ca);
    JL(b); JGoto(cb);
    JL(child_ok);
    if (ij_expr_is_string(child)) { JI("pop",""); } else { JI("pop2",""); }
    JGoto(ports.ω);
    JL(succeed);
    JI("lconst_0","");   /* push dummy long 0 as result */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_NONNULL — \E: succeed if E succeeds, produce E value unchanged.
 * Semantics: transparent pass-through on success; fail on failure.
 * α: emit child; child.γ → ports.γ (value already on stack); child.ω → ports.ω
 * β: child.β (re-entry for generators)
 * ======================================================================= */
static void ij_emit_nonnull(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    IjPorts cp;
    strncpy(cp.γ, ports.γ, 63);  /* child success → caller success (value intact) */
    strncpy(cp.ω, ports.ω, 63);  /* child fail    → caller fail */
    char ca[64], cb[64];
    ij_emit_expr(child, cp, ca, cb);
    JC("NONNULL"); JL(a); JGoto(ca);
    JL(b); JGoto(cb);
}

/* =========================================================================
 * ICN_NULL — /E: succeed if E fails, produce &null (0L).
 * Semantics: inverted — succeeds iff E fails; result is &null.
 * α: emit child; child.γ → drain value, goto ports.ω (E succeeded → /E fails)
 *                child.ω → push lconst_0, goto ports.γ (E failed → /E succeeds)
 * β: child.β
 * ======================================================================= */
static void ij_emit_null(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    char child_ok[64]; snprintf(child_ok, sizeof child_ok, "icn_%d_cok", id);
    char succeed[64];  snprintf(succeed,  sizeof succeed,  "icn_%d_succ", id);
    IjPorts cp;
    strncpy(cp.γ, child_ok, 63);  /* child success → drain → /E fails */
    strncpy(cp.ω, succeed,  63);  /* child fail    → /E succeeds */
    char ca[64], cb[64];
    ij_emit_expr(child, cp, ca, cb);
    JC("NULL"); JL(a); JGoto(ca);
    JL(b); JGoto(cb);
    JL(child_ok);
    /* drain child value off stack */
    if (child && ij_expr_is_string(child)) { JI("pop",""); } else { JI("pop2",""); }
    JGoto(ports.ω);  /* /E fails because E succeeded */
    JL(succeed);
    JI("lconst_0","");  /* &null */
    JGoto(ports.γ);  /* /E succeeds */
}

/* =========================================================================
 * ICN_NEG — unary minus: -E
 * ======================================================================= */
static void ij_emit_neg(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    IcnNode *child = (n->nchildren >= 1) ? n->children[0] : NULL;
    char negate[64]; snprintf(negate, sizeof negate, "icn_%d_neg", id);
    IjPorts cp; strncpy(cp.γ, negate, 63); strncpy(cp.ω, ports.ω, 63);
    char ca[64], cb[64]; ij_emit_expr(child, cp, ca, cb);
    JC("NEG"); JL(a); JGoto(ca); JL(b); JGoto(cb);
    JL(negate);
    if (ij_expr_is_real(child)) JI("dneg",""); else JI("lneg","");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_TO_BY — E1 to E2 by E3  (stepped range generator)
 * step>0: yield i while i<=end; step<0: yield i while i>=end.
 *
 * Forward-only jump structure (no backward branches -> no StackMapTable):
 *
 *   a:     eval E1 -> I_f; eval E2 -> end_f; eval E3 -> step_f; goto check
 *   b:     I_f += step_f; goto check
 *   check: (reached by forward jumps from both a and b)
 *          step>0: if I_f > end_f -> ports.w; else push I_f -> ports.g
 *          step<0: if I_f < end_f -> ports.w; else push I_f -> ports.g
 *          step=0: -> ports.w
 * ======================================================================= */
static void ij_emit_to_by(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *e1 = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *e2 = (n->nchildren > 1) ? n->children[1] : NULL;
    IcnNode *e3 = (n->nchildren > 2) ? n->children[2] : NULL;

    /* Determine if any operand is real → use double fields throughout */
    int is_dbl = ij_expr_is_real(e1) || ij_expr_is_real(e2) || ij_expr_is_real(e3);

    /* Static fields — D for real, J for integer */
    char I_f[64], end_f[64], step_f[64];
    snprintf(I_f,    sizeof I_f,    "icn_%d_toby_i",   id);
    snprintf(end_f,  sizeof end_f,  "icn_%d_toby_end", id);
    snprintf(step_f, sizeof step_f, "icn_%d_toby_stp", id);
    if (is_dbl) {
        ij_declare_static_real(I_f);
        ij_declare_static_real(end_f);
        ij_declare_static_real(step_f);
    } else {
        ij_declare_static(I_f);
        ij_declare_static(end_f);
        ij_declare_static(step_f);
    }

/* Helpers: get/put the counter field and push zero for comparison */
#define TB_GET_I()    (is_dbl ? ij_get_real_field(I_f)    : ij_get_long(I_f))
#define TB_PUT_I()    (is_dbl ? ij_put_real_field(I_f)    : ij_put_long(I_f))
#define TB_GET_END()  (is_dbl ? ij_get_real_field(end_f)  : ij_get_long(end_f))
#define TB_GET_STP()  (is_dbl ? ij_get_real_field(step_f) : ij_get_long(step_f))
#define TB_PUT_END()  (is_dbl ? ij_put_real_field(end_f)  : ij_put_long(end_f))
#define TB_PUT_STP()  (is_dbl ? ij_put_real_field(step_f) : ij_put_long(step_f))
#define TB_ZERO()     (is_dbl ? JI("dconst_0","")         : JI("lconst_0",""))
#define TB_ADD()      (is_dbl ? JI("dadd","")             : JI("ladd",""))
#define TB_CMP()      (is_dbl ? JI("dcmpl","")            : JI("lcmp",""))
#define TB_CMP_GT()   (is_dbl ? JI("dcmpg","")            : JI("lcmp",""))
/* Promote operand to double if needed (only in dbl mode) */
#define TB_PROMOTE(expr) if (is_dbl && !ij_expr_is_real(expr)) { JI("l2d",""); }

    /* Labels */
    char r1[64], r2[64], r3[64], check[64], chkp[64], chkn[64];
    snprintf(r1,    sizeof r1,    "icn_%d_tb_r1",    id);
    snprintf(r2,    sizeof r2,    "icn_%d_tb_r2",    id);
    snprintf(r3,    sizeof r3,    "icn_%d_tb_r3\n",  id);  /* trailing \n trick: unused */
    snprintf(r3,    sizeof r3,    "icn_%d_tb_r3",    id);
    snprintf(check, sizeof check, "icn_%d_tb_check", id);
    snprintf(chkp,  sizeof chkp,  "icn_%d_tb_ckp",  id);
    snprintf(chkn,  sizeof chkn,  "icn_%d_tb_ckn\n",id);
    snprintf(chkn,  sizeof chkn,  "icn_%d_tb_ckn",  id);

    IjPorts p1; strncpy(p1.γ,r1,63); strncpy(p1.ω,ports.ω,63);
    IjPorts p2; strncpy(p2.γ,r2,63); strncpy(p2.ω,ports.ω,63);
    IjPorts p3; strncpy(p3.γ,r3,63); strncpy(p3.ω,ports.ω,63);

    char a1[64],b1[64],a2[64],b2[64],a3[64],b3[64];
    ij_emit_expr(e1, p1, a1, b1);
    ij_emit_expr(e2, p2, a2, b2);
    ij_emit_expr(e3, p3, a3, b3);

    JC("TO_BY -- forward-only, supports integer and real");

    /* α: eval E1 → r1 */
    JL(a);  JGoto(a1);
    JL(r1); TB_PROMOTE(e1); TB_PUT_I();    JGoto(a2);
    JL(r2); TB_PROMOTE(e2); TB_PUT_END();  JGoto(a3);
    JL(r3); TB_PROMOTE(e3); TB_PUT_STP(); JGoto(check);

    /* β: I += step, → check */
    JL(b);
    TB_GET_I(); TB_GET_STP(); TB_ADD(); TB_PUT_I();
    JGoto(check);

    /* check: forward-only — inspect step sign, then bounds check */
    JL(check);
    TB_GET_STP(); TB_ZERO(); TB_CMP();
    J("    ifgt %s\n", chkp);
    TB_GET_STP(); TB_ZERO(); TB_CMP();
    J("    iflt %s\n", chkn);
    JGoto(ports.ω);   /* step == 0 → always fail */

    JL(chkp);   /* positive step: yield while I <= end */
    TB_GET_I(); TB_GET_END(); TB_CMP_GT();
    J("    ifgt %s\n", ports.ω);
    TB_GET_I(); JGoto(ports.γ);

    JL(chkn);   /* negative step: yield while I >= end */
    TB_GET_I(); TB_GET_END(); TB_CMP();
    J("    iflt %s\n", ports.ω);
    TB_GET_I(); JGoto(ports.γ);

#undef TB_GET_I
#undef TB_PUT_I
#undef TB_GET_END
#undef TB_GET_STP
#undef TB_PUT_END
#undef TB_PUT_STP
#undef TB_ZERO
#undef TB_ADD
#undef TB_CMP
#undef TB_CMP_GT
#undef TB_PROMOTE

    (void)b1; (void)b2; (void)b3;
}

/* =========================================================================
 * String relational ops: ICN_SEQ (==), ICN_SNE (~==), ICN_SLT (<<),
 * ICN_SLE (<<=), ICN_SGT (>>), ICN_SGE (>>=)
 * Use String.compareTo() and compare result to 0.
 * Both operands are String refs on the stack.
 * ======================================================================= */
static void ij_emit_strrelop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char lrelay[64], rrelay[64], chk[64], lstore_lbl[64];
    snprintf(lrelay,    sizeof lrelay,    "icn_%d_lrelay", id);
    snprintf(rrelay,    sizeof rrelay,    "icn_%d_rrelay", id);
    snprintf(chk,       sizeof chk,       "icn_%d_chk",    id);
    snprintf(lstore_lbl,sizeof lstore_lbl,"icn_%d_lstore", id);
    IcnNode *lhs = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *rhs = (n->nchildren > 1) ? n->children[1] : NULL;
    IjPorts lp; strncpy(lp.γ, lrelay, 63); strncpy(lp.ω, ports.ω, 63);
    IjPorts rp; strncpy(rp.γ, rrelay, 63); strncpy(rp.ω, ports.ω, 63);
    char la[64], lb2[64], ra[64], rb2[64];
    ij_emit_expr(lhs, lp, la, lb2);
    ij_emit_expr(rhs, rp, ra, rb2);
    /* Allocate String scratch AFTER child emits so slots land above all
     * long-pair slots.  Use int scratch region (raw JVM slots above 2*MAX_LOCALS)
     * so they never alias lstore/lload targets. */
    int ls_jvm = ij_alloc_int_scratch();
    int rs_jvm = ij_alloc_int_scratch();
    ij_nstrefs++;  /* track for zero-init — these get iconst_0/istore */
    JC("STRRELOP"); JL(a); JGoto(la);
    JL(b); JGoto(lb2);
    JL(lrelay); J("    astore %d\n", ls_jvm); JGoto(lstore_lbl);
    JL(lstore_lbl); JGoto(ra);
    JL(rrelay); J("    astore %d\n", rs_jvm); JGoto(chk);
    JL(chk);
    J("    aload %d\n", ls_jvm);
    J("    aload %d\n", rs_jvm);
    JI("invokevirtual", "java/lang/String/compareTo(Ljava/lang/String;)I");
    const char *jfail;
    switch (n->kind) {
        case ICN_SEQ: jfail = "ifne"; break;
        case ICN_SNE: jfail = "ifeq"; break;
        case ICN_SLT: jfail = "ifge"; break;
        case ICN_SLE: jfail = "ifgt"; break;
        case ICN_SGT: jfail = "ifle"; break;
        case ICN_SGE: jfail = "iflt"; break;
        default:      jfail = "ifne"; break;
    }
    J("    %s %s\n", jfail, rb2);
    /* success: reload right String as result, convert to long (dummy 0) */
    /* String relops return the right operand in Icon — but we need a long
     * for consistent stack. Push 0L as dummy result. */
    JI("lconst_0","");
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_BREAK — exit enclosing loop; optional value (ignored in our impl)
 * break jumps directly to the enclosing loop's ports.ω (exit path).
 * ======================================================================= */
static void ij_emit_break(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    const char *exit_lbl = ij_loop_break_target();
    JL(a); JGoto(exit_lbl);
    JL(b); JGoto(exit_lbl);  /* β also exits — break is one-shot */
    (void)ports; (void)n;
}

/* =========================================================================
 * ICN_NEXT — restart enclosing loop body (skip rest of current iteration)
 * next jumps to the loop's restart label (loop_top / cond re-eval).
 * ======================================================================= */
static void ij_emit_next(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    const char *next_lbl = ij_loop_next_target();
    JL(a); JGoto(next_lbl);
    JL(b); JGoto(next_lbl);
    (void)ports; (void)n;
}

/* =========================================================================
 * ICN_AUGOP — augmented assignment: lhs op:= rhs
 *   Semantics: lhs := lhs op rhs  (lhs must be a variable)
 *   node->val.ival encodes the augop token kind (TK_AUGPLUS etc.)
 *   We: load lhs, eval rhs, apply op, store back, push result → ports.γ
 * ======================================================================= */
static void ij_emit_augop(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (n->nchildren < 2) {
        int id = ij_new_id(); char a[64], b[64];
        lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
        strncpy(oα,a,63); strncpy(oβ,b,63);
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }

    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *lhs = n->children[0];
    IcnNode *rhs = n->children[1];
    long aug_kind = n->val.ival;

    /* Eval rhs; on rhs fail → ports.ω */
    char rhs_ok[64]; snprintf(rhs_ok, sizeof rhs_ok, "icn_%d_rhsok", id);
    IjPorts rp; strncpy(rp.γ, rhs_ok, 63); strncpy(rp.ω, ports.ω, 63);
    char ra[64], rb[64]; ij_emit_expr(rhs, rp, ra, rb);

    JL(a); JGoto(ra);
    JL(b); JGoto(rb);

    JL(rhs_ok);
    /* rhs value is on stack — String ref for ||:=, long for arithmetic ops.
     * Load lhs current value, apply op, store result back, push result. */
    if (lhs && lhs->kind == ICN_VAR) {
        char fld[128];
        int is_local = (ij_locals_find(lhs->val.sval) >= 0);
        if (is_local) {
            ij_var_field(lhs->val.sval, fld, sizeof fld);
        } else {
            snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
        }

        if ((int)aug_kind == 35) {
            /* TK_AUGCONCAT=35: String ||:= String
             * Stack at rhs_ok: String ref (rhs). */
            char tmp_str_fld[128]; snprintf(tmp_str_fld, sizeof tmp_str_fld, "icn_%d_augtemp_s", id);
            ij_declare_static_str(tmp_str_fld);
            ij_declare_static_str(fld);             /* ensure lhs field is str-typed */
            ij_put_str_field(tmp_str_fld);          /* save rhs String */
            ij_get_str_field(fld);                  /* load current lhs String */
            ij_get_str_field(tmp_str_fld);          /* reload rhs String */
            JI("invokevirtual", "java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;");
            JI("dup","");                           /* one copy stays on stack for γ */
            ij_put_str_field(fld);                  /* store result back to lhs */
            JGoto(ports.γ);
        } else {
            /* Arithmetic augop: rhs is long on stack. */
            char tmp_fld[128]; snprintf(tmp_fld, sizeof tmp_fld, "icn_%d_augtemp", id);
            ij_declare_static(tmp_fld);
            ij_put_long(tmp_fld);              /* save rhs */

            ij_declare_static(fld);
            ij_get_long(fld);                  /* load current lhs value */
            ij_get_long(tmp_fld);              /* reload rhs */

            /* Apply arithmetic op. Actual TK_AUG* values (from icon_lex.h enum):
             * TK_AUGPLUS=30 TK_AUGMINUS=31 TK_AUGSTAR=32 TK_AUGSLASH=33 TK_AUGMOD=34 */
            switch ((int)aug_kind) {
                case 30: JI("ladd",""); break;  /* TK_AUGPLUS   */
                case 31: JI("lsub",""); break;  /* TK_AUGMINUS  */
                case 32: JI("lmul",""); break;  /* TK_AUGSTAR   */
                case 33: JI("ldiv",""); break;  /* TK_AUGSLASH  */
                case 34: JI("lrem",""); break;  /* TK_AUGMOD    */
                default: JI("ladd",""); break;
            }

            /* dup result: one copy stored back to lhs, one stays on stack as expr result */
            JI("dup2","");
            ij_put_long(fld);
            JGoto(ports.γ);
        }
    } else {
        /* Non-var lhs — just discard and push rhs as result (rare/error case) */
        JGoto(ports.γ);
    }
}

/* =========================================================================
 * ICN_BANG — !E  string character generator
 * Byrd-box: α evals E once into static str field, resets pos=0, then falls
 * through to check.  check: if pos >= length → ω.  Else substring(pos,pos+1)
 * on stack → γ; pos incremented before goto γ.  β → check (resume).
 * Result type: String (single char).  ij_expr_is_string returns 1 for BANG.
 * ======================================================================= */
static void ij_emit_bang(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id();
    char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    IcnNode *child = (n->nchildren > 0) ? n->children[0] : NULL;

    /* --- LIST bang branch: !L iterates ArrayList by index --- */
    if (child && ij_expr_is_list(child)) {
        char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_bang_list", id);
        char idx_fld[80];  snprintf(idx_fld,  sizeof idx_fld,  "icn_%d_bang_idx",  id);
        char store[64];    snprintf(store,    sizeof store,    "icn_%d_bang_st",   id);
        char chk[64];      snprintf(chk,      sizeof chk,      "icn_%d_bang_chk",  id);
        char ok[64];       snprintf(ok,       sizeof ok,       "icn_%d_bang_ok",   id);
        ij_declare_static_list(list_fld);
        ij_declare_static_int(idx_fld);

        /* child γ → store relay; β → chk (resume, no re-eval) */
        IjPorts ep; strncpy(ep.γ, store, 63); strncpy(ep.ω, ports.ω, 63);
        char ea[64], eb[64]; ij_emit_expr(child, ep, ea, eb);

        /* α: eval child list once */
        JL(a); JGoto(ea);
        /* β: resume — list already stored, just re-check */
        JL(b); JGoto(chk);

        /* store: ArrayList ref on stack → store list, reset idx=0, fall to chk */
        JL(store);
        ij_put_list_field(list_fld);
        JI("iconst_0", ""); ij_put_int_field(idx_fld);
        /* fall through to chk */

        /* chk: stack empty, list in field */
        JL(chk);
        ij_get_list_field(list_fld);
        JI("invokevirtual", "java/util/ArrayList/size()I");
        ij_get_int_field(idx_fld);
        J("    if_icmple %s\n", ports.ω); /* size <= idx → exhausted */
        /* get(idx) → Object; unbox as Long for scalar lists, leave as Object for record lists */
        ij_get_list_field(list_fld);
        ij_get_int_field(idx_fld);
        JI("invokevirtual", "java/util/ArrayList/get(I)Ljava/lang/Object;");
        JL(ok);
        if (!ij_expr_is_record_list(child)) {
            JI("checkcast", "java/lang/Long");
            JI("invokevirtual", "java/lang/Long/longValue()J");
        }
        /* increment idx */
        ij_get_int_field(idx_fld);
        JI("iconst_1", ""); JI("iadd", "");
        ij_put_int_field(idx_fld);
        JGoto(ports.γ);
        return;
    }

    /* --- STRING bang branch (original) --- */

    char str_fld[128]; snprintf(str_fld, sizeof str_fld, "icn_%d_bang_str", id);
    char pos_fld[128]; snprintf(pos_fld, sizeof pos_fld, "icn_%d_bang_pos", id);
    char check[64];    snprintf(check,   sizeof check,   "icn_%d_bang_chk", id);
    char advance[64];  snprintf(advance, sizeof advance,  "icn_%d_bang_adv", id);

    ij_declare_static_str(str_fld);
    ij_declare_static_int(pos_fld);

    /* child expr */
    IjPorts ep; strncpy(ep.γ, advance, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64]; ij_emit_expr(n->children[0], ep, ea, eb);

    /* α: eval child, store string, reset pos, goto check */
    JL(a); JGoto(ea);
    /* β: resume — just re-enter check without re-evaluating E */
    JL(b); JGoto(check);

    /* advance: child produced String on stack → store into str_fld, reset pos */
    JL(advance);
    ij_put_str_field(str_fld);
    JI("iconst_0",""); ij_put_int_field(pos_fld);
    /* fall through to check */

    /* check: if pos >= length(str) → ω; else extract char, increment pos, → γ */
    JL(check);
    ij_get_str_field(str_fld);
    JI("invokevirtual","java/lang/String/length()I");
    ij_get_int_field(pos_fld);
    /* stack: [length, pos] — if pos >= length → ω */
    J("    if_icmple %s\n", ports.ω);  /* if length <= pos → ω  (i.e. pos >= length) */

    /* extract substring(pos, pos+1) */
    ij_get_str_field(str_fld);
    ij_get_int_field(pos_fld);
    ij_get_int_field(pos_fld);
    JI("iconst_1","");
    JI("iadd","");
    JI("invokevirtual","java/lang/String/substring(II)Ljava/lang/String;");
    /* increment pos */
    ij_get_int_field(pos_fld);
    JI("iconst_1","");
    JI("iadd","");
    ij_put_int_field(pos_fld);
    /* String result is on stack → γ */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_SIZE — *E  string size operator
 * One-shot: α evals child (must be String), calls String.length(), converts
 * int→long via i2l, pushes long → ports.γ.  β → ports.ω (one-shot, no retry).
 * ======================================================================= */
static void ij_emit_size(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);
    char relay[64]; snprintf(relay, sizeof relay, "icn_%d_relay", id);
    IcnNode *child = (n->nchildren > 0) ? n->children[0] : NULL;
    IjPorts cp; strncpy(cp.γ, relay, 63); strncpy(cp.ω, ports.ω, 63);
    char ca[64], cb[64]; ij_emit_expr(child, cp, ca, cb);
    JL(a); JGoto(ca);
    JL(b); JGoto(cb);
    JL(relay);
    if (child && ij_expr_is_list(child)) {
        /* ArrayList ref on stack → size()I → i2l → long */
        JI("invokevirtual", "java/util/ArrayList/size()I");
    } else {
        /* String ref on stack */
        JI("invokevirtual", "java/lang/String/length()I");
    }
    JI("i2l", "");         /* int → long for uniform numeric stack type */
    JGoto(ports.γ);
}

/* =========================================================================
 * ICN_MAKELIST — [e1, e2, ...] list constructor
 * Builds a java.util.ArrayList.  Each child is evaluated one-shot:
 *   - long values boxed via Long.valueOf(J)
 *   - double values boxed via Double.valueOf(D)
 *   - String refs left as-is (already Object)
 * Result: ArrayList ref on stack at ports.γ.
 * ======================================================================= */
static void ij_emit_makelist(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    char list_fld[80]; snprintf(list_fld, sizeof list_fld, "icn_%d_mklist", id);
    ij_declare_static_list(list_fld);

    JL(a);
    JI("new", "java/util/ArrayList");
    JI("dup", "");
    JI("invokespecial", "java/util/ArrayList/<init>()V");
    ij_put_list_field(list_fld);

    for (int i = 0; i < n->nchildren; i++) {
        char relay[64]; snprintf(relay, sizeof relay, "icn_%d_mke_%d", id, i);
        char elem_fld[80]; snprintf(elem_fld, sizeof elem_fld, "icn_%d_elem_%d", id, i);
        ij_declare_static_obj(elem_fld);

        IjPorts cp; strncpy(cp.γ, relay, 63); strncpy(cp.ω, ports.ω, 63);
        char ca[64], cb[64]; ij_emit_expr(n->children[i], cp, ca, cb);
        JGoto(ca);
        JL(relay);
        if (ij_expr_is_string(n->children[i])) {
            /* String is already Object — no boxing needed */
        } else if (ij_expr_is_real(n->children[i])) {
            JI("invokestatic", "java/lang/Double/valueOf(D)Ljava/lang/Double;");
        } else if (ij_expr_is_record(n->children[i])) {
            /* Constructor left 0L on stack; actual record is in icn_retval_obj */
            JI("pop2", "");
            J("    getstatic %s/icn_retval_obj Ljava/lang/Object;\n", ij_classname);
        } else {
            JI("invokestatic", "java/lang/Long/valueOf(J)Ljava/lang/Long;");
        }
        ij_put_obj_field(elem_fld);
        ij_get_list_field(list_fld);
        ij_get_obj_field(elem_fld);
        JI("invokevirtual", "java/util/ArrayList/add(Ljava/lang/Object;)Z");
        JI("pop", "");
    }

    ij_get_list_field(list_fld);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * ICN_LIMIT — E \ N  (limitation operator, JCON-ANALYSIS §"E \ N")
 *
 * Semantics: yield at most N values from E.
 *   - N is evaluated once (bounded) at α → stored in limit_max (long)
 *   - counter starts 0 at α
 *   - each time E yields: if counter >= max → exhaust (goto E.β → ports.ω)
 *                          else counter++; propagate value → ports.γ
 *   - β: if counter >= max → ports.ω; else counter++; → E.β
 *
 * Per-site static fields:
 *   icn_N_limit_count  J  — how many values yielded so far (long)
 *   icn_N_limit_max    J  — N value, evaluated once
 *
 * Children: [0]=E (the generator), [1]=N (the bound)
 * ======================================================================= */
/* =========================================================================
 * ICN_FIELD — E.name  (record field access / lvalue)
 *
 * E.name is a one-shot expression: eval E (must succeed once), then
 * getfield name from the resulting Object.  On resume → fail.
 * Fields are stored as Object (boxed Long or String).
 *
 * Children: [0] = record expression, [1] = ICN_VAR with field name
 *
 * Read path (α):
 *   eval E → relay → checkcast RecordType → getfield → unbox → γ
 * β → ω (one-shot)
 *
 * Assignment (p.x := v) is handled in ICN_ASSIGN by detecting ICN_FIELD lhs.
 * ======================================================================= */
static void ij_emit_field(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 2) { JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return; }

    IcnNode *rec_expr  = n->children[0];
    IcnNode *field_var = n->children[1];   /* ICN_VAR — field name */
    const char *fname  = field_var->val.sval;

    /* Determine record type from expression (must be a variable) */
    const char *rtype = NULL;
    if (rec_expr && rec_expr->kind == ICN_VAR) {
        /* Walk record registry: find a type that has this field */
        for (int ri = 0; ri < ij_nrec; ri++) {
            for (int fi = 0; fi < ij_rec_nfields[ri]; fi++) {
                if (!strcmp(ij_rec_fields[ri][fi], fname)) {
                    rtype = ij_rec_names[ri]; break;
                }
            }
            if (rtype) break;
        }
    }

    char obj_fld[80]; snprintf(obj_fld, sizeof obj_fld, "icn_%d_fld_obj", id);
    ij_declare_static_obj(obj_fld);

    char e_relay[64]; snprintf(e_relay, sizeof e_relay, "icn_%d_fld_er", id);
    IjPorts ep; strncpy(ep.γ, e_relay, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64]; ij_emit_expr(rec_expr, ep, ea, eb);

    JL(a); JGoto(ea);
    JL(b); JGoto(ports.ω);

    JL(e_relay);
    /* Stack: long (Object reference stored as long via icn_retval pathway)
     * Actually record objects are stored in Object statics — pop the long,
     * reload from the Object static of the record variable. */
    /* Pop the long placeholder off stack */
    JI("pop2", "");
    /* Load the record object from its static field */
    if (rec_expr && rec_expr->kind == ICN_VAR) {
        char vfld[128];
        int slot = ij_locals_find(rec_expr->val.sval);
        if (slot >= 0) {
            ij_var_field(rec_expr->val.sval, vfld, sizeof vfld);
        } else {
            snprintf(vfld, sizeof vfld, "icn_gvar_%s", rec_expr->val.sval);
        }
        J("    getstatic %s/%s Ljava/lang/Object;\n", ij_classname, vfld);
    }
    /* checkcast to record type and getfield */
    if (rtype) {
        J("    checkcast %s$%s\n", ij_classname, rtype);
        J("    getfield %s$%s/%s Ljava/lang/Object;\n", ij_classname, rtype, fname);
    } else {
        /* fallback: just fail if we can't resolve type */
        JI("pop", "");
        JGoto(ports.ω);
        JL(b); JGoto(ports.ω);
        return;
    }
    /* Unbox: if Long → longValue, else treat as string → push 0L as placeholder */
    J("    dup\n");
    J("    instanceof java/lang/Long\n");
    char is_long[64], not_long[64], done[64];
    snprintf(is_long,  sizeof is_long,  "icn_%d_fld_il", id);
    snprintf(not_long, sizeof not_long, "icn_%d_fld_nl", id);
    snprintf(done,     sizeof done,     "icn_%d_fld_dn", id);
    J("    ifne %s\n", is_long);
    /* String path: store as string static, push 0 as numeric value, goto γ */
    JL(not_long);
    J("    checkcast java/lang/String\n");
    ij_put_str_field(obj_fld);   /* reuse obj_fld as str temporarily */
    JI("lconst_0", "");
    JGoto(ports.γ);
    JL(is_long);
    J("    checkcast java/lang/Long\n");
    J("    invokevirtual java/lang/Long/longValue()J\n");
    JGoto(ports.γ);
}

/* =========================================================================
 * ij_emit_record_class — emit a Jasmin inner-class for a record type
 *
 * Emits a nested class  OuterClass$RecordName  with:
 *   - One `public Object` field per record field
 *   - A no-arg <init> that calls super
 * ======================================================================= */
static void ij_emit_record_class(const char *rec_name, int nfields,
                                 const char (*fields)[64], FILE *out) {
    FILE *save = jout; jout = out;
    J("; --- record %s ---\n", rec_name);
    J(".class public %s$%s\n", ij_classname, rec_name);
    J(".super java/lang/Object\n");
    for (int i = 0; i < nfields; i++)
        J(".field public %s Ljava/lang/Object;\n", fields[i]);
    J("\n.method public <init>()V\n");
    J("    .limit stack 1\n    .limit locals 1\n");
    J("    aload_0\n");
    J("    invokespecial java/lang/Object/<init>()V\n");
    J("    return\n");
    J(".end method\n\n");
    jout = save;
}

static void ij_emit_limit(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    /* Per-site statics */
    char cnt_fld[64], max_fld[64];
    snprintf(cnt_fld, sizeof cnt_fld, "icn_%d_limit_count", id);
    snprintf(max_fld, sizeof max_fld, "icn_%d_limit_max",   id);
    ij_declare_static(cnt_fld);   /* long */
    ij_declare_static(max_fld);   /* long */

    IcnNode *expr  = (n->nchildren > 0) ? n->children[0] : NULL;
    IcnNode *bound = (n->nchildren > 1) ? n->children[1] : NULL;

    /* Relay labels */
    char n_relay[64]; snprintf(n_relay, sizeof n_relay, "icn_%d_limit_nrelay", id);
    char e_gamma[64]; snprintf(e_gamma, sizeof e_gamma, "icn_%d_limit_egamma", id);
    char chk_ex[64];  snprintf(chk_ex,  sizeof chk_ex,  "icn_%d_limit_chkex",  id);

    /* Emit N (bounded — one-shot, eval once) */
    IjPorts np; strncpy(np.γ, n_relay, 63); strncpy(np.ω, ports.ω, 63);
    char na[64], nb[64]; ij_emit_expr(bound, np, na, nb);

    /* Emit E — γ → e_gamma (we intercept), ω → ports.ω */
    IjPorts ep; strncpy(ep.γ, e_gamma, 63); strncpy(ep.ω, ports.ω, 63);
    char ea[64], eb[64]; ij_emit_expr(expr, ep, ea, eb);

    /* n_relay: N value (long) on stack → store max, reset count=0, goto E.α */
    JL(n_relay);
    ij_put_long(max_fld);
    JI("lconst_0", ""); ij_put_long(cnt_fld);
    JGoto(ea);

    /* e_gamma: E yielded a value (long or String) on stack.
     * Check counter: if count >= max → pop value, goto E.β (exhaust).
     * Else: count++, propagate value to ports.γ.
     * We must not disturb the value on the stack during the check, so we
     * use a relay: store value in a temp static first, check, then reload. */
    char val_fld[64]; snprintf(val_fld, sizeof val_fld, "icn_%d_limit_val", id);
    int expr_is_str = ij_expr_is_string(expr);
    if (expr_is_str) {
        ij_declare_static_str(val_fld);
    } else {
        ij_declare_static(val_fld);
    }
    JL(e_gamma);
    /* store value */
    if (expr_is_str) { ij_put_str_field(val_fld); }
    else             { ij_put_long(val_fld); }

    /* check: count >= max? */
    JL(chk_ex);
    ij_get_long(cnt_fld);
    ij_get_long(max_fld);
    JI("lcmp", "");
    J("    ifge %s\n", eb);       /* count >= max → exhaust via E.β */

    /* count++ */
    ij_get_long(cnt_fld);
    JI("lconst_1", ""); JI("ladd", "");
    ij_put_long(cnt_fld);

    /* reload value and succeed */
    if (expr_is_str) { ij_get_str_field(val_fld); }
    else             { ij_get_long(val_fld); }
    JGoto(ports.γ);

    /* α: eval N first (to get max), then will fall through to E.α via n_relay */
    JL(a); JGoto(na);

    /* β: check counter; if exhausted → ports.ω; else resume E.β
     * Do NOT increment here — counter only increments when a value is actually
     * yielded (in e_gamma path).  β just checks and re-drives the inner generator. */
    JL(b);
    ij_get_long(cnt_fld);
    ij_get_long(max_fld);
    JI("lcmp", "");
    J("    ifge %s\n", ports.ω);  /* count >= max: already gave all allowed values */
    JGoto(eb);
}

/* =========================================================================
 * ICN_SUBSCRIPT — s[i]  (string character subscript, 1-based)
 *
 * Semantics: s[i] returns the single character at 1-based position i as a
 * String.  Fails if i < 1 or i > length(s).
 * Negative indices count from the end: s[-1] = last char.
 *
 * Implementation (one-shot — β → ω):
 *   eval s → store in per-site static (String)
 *   eval i → l2i → compute 0-based offset (handle negative)
 *   bounds check: if offset < 0 || offset >= length → ω
 *   substring(offset, offset+1) → γ (String)
 *
 * Children: [0]=string expr, [1]=index expr (long, 1-based)
 * Result type: String (single character)
 * ======================================================================= */
static void ij_emit_subscript(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 2) {
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }

    IcnNode *str_child = n->children[0];
    IcnNode *idx_child = n->children[1];

    /* -----------------------------------------------------------------------
     * TABLE SUBSCRIPT: t[k]
     * If the LHS is a table variable, do HashMap.get(key_str).
     * If key not present: return the table's default value.
     * Default is stored in a parallel _dflt Object static.
     * Assignment t[k] := v is handled by a separate path in ICN_ASSIGN (below).
     * One-shot (β → ω).
     * ----------------------------------------------------------------------- */
    if (ij_expr_is_table(str_child)) {
        char t_fld[80];    snprintf(t_fld,    sizeof t_fld,    "icn_%d_ts_tbl",   id);
        char k_fld[80];    snprintf(k_fld,    sizeof k_fld,    "icn_%d_ts_key",   id);
        char kv_fld[80];   snprintf(kv_fld,   sizeof kv_fld,   "icn_%d_ts_kval",  id);
        char trelay[64];   snprintf(trelay,   sizeof trelay,   "icn_%d_ts_tr",    id);
        char krelay[64];   snprintf(krelay,   sizeof krelay,   "icn_%d_ts_kr",    id);
        char got_val[64];  snprintf(got_val,  sizeof got_val,  "icn_%d_ts_got",   id);
        ij_declare_static_table(t_fld);
        ij_declare_static_str(k_fld);
        ij_declare_static(kv_fld);

        IjPorts tp; strncpy(tp.γ, trelay, 63); strncpy(tp.ω, ports.ω, 63);
        char ta[64], tb[64]; ij_emit_expr(str_child, tp, ta, tb);
        IjPorts kp; strncpy(kp.γ, krelay, 63); strncpy(kp.ω, ports.ω, 63);
        char ka[64], kb[64]; ij_emit_expr(idx_child, kp, ka, kb);

        JL(a); JGoto(ta);
        JL(b); JGoto(kb);  /* resume key generator (or idx expression) on β */

        JL(trelay); ij_put_table_field(t_fld); JGoto(ka);
        JL(krelay);
        /* save raw long key */
        JI("dup2",""); ij_put_long(kv_fld);
        /* convert to String key */
        JI("invokestatic", "java/lang/Long/toString(J)Ljava/lang/String;");
        ij_put_str_field(k_fld);
        /* HashMap.get(key) */
        ij_get_table_field(t_fld);
        ij_get_str_field(k_fld);
        JI("invokevirtual", "java/util/HashMap/get(Ljava/lang/Object;)Ljava/lang/Object;");
        /* if null: return default (stored as Long in {varfld}_dflt); else unbox Long */
        JI("dup", "");
        J("    ifnonnull %s\n", got_val);
        /* null branch: load {varfld}_dflt (pre-declared by pre-pass if table(x) was used) */
        JI("pop", "");
        if (str_child && str_child->kind == ICN_VAR) {
            char tvfld[128];
            int slot = ij_locals_find(str_child->val.sval);
            if (slot >= 0) {
                ij_var_field(str_child->val.sval, tvfld, sizeof tvfld);
            } else {
                snprintf(tvfld, sizeof tvfld, "icn_gvar_%s", str_child->val.sval);
            }
            char vdflt[144]; snprintf(vdflt, sizeof vdflt, "%s_dflt", tvfld);
            /* Check if declared in statics (pre-pass registered it) */
            int dflt_declared = 0;
            for (int _di = 0; _di < ij_nstatics; _di++) {
                if (!strcmp(ij_statics[_di], vdflt)) { dflt_declared = 1; break; }
            }
            if (dflt_declared) {
                ij_get_obj_field(vdflt);
                JI("checkcast", "java/lang/Long");
                JI("invokevirtual", "java/lang/Long/longValue()J");
            } else {
                JI("lconst_0", "");
            }
        } else {
            JI("lconst_0", "");
        }
        JGoto(ports.γ);
        JL(got_val);
        JI("checkcast", "java/lang/Long");
        JI("invokevirtual", "java/lang/Long/longValue()J");
        JGoto(ports.γ);
        return;
    }

    /* Per-site statics for cross-label value passing */
    char s_fld[64], i_fld[64];
    snprintf(s_fld, sizeof s_fld, "icn_%d_sub_s", id);
    snprintf(i_fld, sizeof i_fld, "icn_%d_sub_i", id);
    ij_declare_static_str(s_fld);
    ij_declare_static_int(i_fld);   /* 0-based int offset */

    /* Relay labels */
    char s_relay[64], i_relay[64], check[64];
    snprintf(s_relay, sizeof s_relay, "icn_%d_sub_srelay", id);
    snprintf(i_relay, sizeof i_relay, "icn_%d_sub_irelay", id);
    snprintf(check,   sizeof check,   "icn_%d_sub_check",  id);

    /* Emit index child (long) — fail→ω */
    IjPorts ip; strncpy(ip.γ, i_relay, 63); strncpy(ip.ω, ports.ω, 63);
    char ia[64], ib[64]; ij_emit_expr(idx_child, ip, ia, ib);

    /* Emit string child — fail→ω */
    IjPorts sp; strncpy(sp.γ, s_relay, 63); strncpy(sp.ω, ports.ω, 63);
    char sa[64], sb[64]; ij_emit_expr(str_child, sp, sa, sb);

    /* s_relay: String on stack → store, then eval index */
    JL(s_relay);
    ij_put_str_field(s_fld);
    JGoto(ia);

    /* i_relay: long index on stack → convert to 0-based int, store, goto check */
    JL(i_relay);
    JI("l2i", "");                       /* long → int (1-based Icon index) */
    /* Convert 1-based to 0-based, handling negative (count from end) */
    /* if i > 0: offset = i - 1
       if i < 0: offset = length + i   (e.g. s[-1] = length-1)
       if i == 0: fail */
    char pos_branch[64], neg_branch[64], zero_fail[64];
    snprintf(pos_branch, sizeof pos_branch, "icn_%d_sub_pos", id);
    snprintf(neg_branch, sizeof neg_branch, "icn_%d_sub_neg", id);
    snprintf(zero_fail,  sizeof zero_fail,  "icn_%d_sub_z",   id);
    /* Duplicate int on stack for the comparison (it's on stack once) */
    int idx_slot = ij_alloc_int_scratch();
    J("    istore %d\n", idx_slot);
    J("    iload %d\n",  idx_slot);
    J("    ifgt %s\n", pos_branch);
    J("    iload %d\n",  idx_slot);
    J("    iflt %s\n", neg_branch);
    /* i == 0 → fail */
    JL(zero_fail); JGoto(ports.ω);

    /* pos_branch: offset = i - 1 */
    JL(pos_branch);
    J("    iload %d\n", idx_slot);
    JI("iconst_1", ""); JI("isub", "");
    ij_put_int_field(i_fld);
    JGoto(check);

    /* neg_branch: offset = length + i */
    JL(neg_branch);
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    J("    iload %d\n", idx_slot);
    JI("iadd", "");
    ij_put_int_field(i_fld);
    JGoto(check);

    /* check: if offset < 0 || offset >= length → fail; else substring */
    JL(check);
    ij_get_int_field(i_fld);
    J("    iflt %s\n", ports.ω);          /* offset < 0 → fail */
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    ij_get_int_field(i_fld);
    JI("if_icmple", ports.ω);             /* length <= offset → fail */
    /* substring(offset, offset+1) */
    ij_get_str_field(s_fld);
    ij_get_int_field(i_fld);
    ij_get_int_field(i_fld);
    JI("iconst_1", ""); JI("iadd", "");
    JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
    JGoto(ports.γ);

    /* α: eval string first, then index */
    JL(a); JGoto(sa);
    /* β: retry index generator (allows every s[1 to N] to work).
     * String child is one-shot and its value is cached in s_fld, so we
     * just re-drive the index child's β to get the next index. */
    JL(b); JGoto(ib);
}


/* =========================================================================
 * ICN_SECTION — s[i:j]  (string section / substring)
 * 3 children: str, lo, hi  (all 1-based Icon positions)
 * JCON: ir_a_Sectionop — eval str → lo → hi, then call substring.
 * Convention: 1-based lo/hi → 0-based lo-1 and hi-1 for Java substring.
 * Positive i: offset = i-1.  Negative i: offset = length+i.  0 → fail.
 * One-shot: β → ω.  Result is a String.
 * ======================================================================= */
static void ij_emit_section(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 3) {
        JL(a); JGoto(ports.ω); JL(b); JGoto(ports.ω); return;
    }
    IcnNode *str_child = n->children[0];
    IcnNode *lo_child  = n->children[1];
    IcnNode *hi_child  = n->children[2];

    /* Per-site statics */
    char s_fld[64], lo_fld[64], hi_fld[64];
    snprintf(s_fld,  sizeof s_fld,  "icn_%d_sec_s",  id);
    snprintf(lo_fld, sizeof lo_fld, "icn_%d_sec_lo", id);
    snprintf(hi_fld, sizeof hi_fld, "icn_%d_sec_hi", id);
    ij_declare_static_str(s_fld);
    ij_declare_static_int(lo_fld);
    ij_declare_static_int(hi_fld);

    /* Relay labels */
    char s_relay[64], lo_relay[64], hi_relay[64], compute[64];
    snprintf(s_relay,  sizeof s_relay,  "icn_%d_sec_sr",  id);
    snprintf(lo_relay, sizeof lo_relay, "icn_%d_sec_lor", id);
    snprintf(hi_relay, sizeof hi_relay, "icn_%d_sec_hir", id);
    snprintf(compute,  sizeof compute,  "icn_%d_sec_cmp", id);

    /* Helper macro: convert 1-based Icon pos to 0-based Java index,
       store into a field; fail on 0.  Uses an istore slot. */
    /* Emit string child */
    IjPorts sp; strncpy(sp.γ, s_relay, 63); strncpy(sp.ω, ports.ω, 63);
    char sa[64], sb[64]; ij_emit_expr(str_child, sp, sa, sb);

    /* Emit lo child */
    IjPorts lp; strncpy(lp.γ, lo_relay, 63); strncpy(lp.ω, ports.ω, 63);
    char la[64], lb[64]; ij_emit_expr(lo_child, lp, la, lb);

    /* Emit hi child */
    IjPorts hp; strncpy(hp.γ, hi_relay, 63); strncpy(hp.ω, ports.ω, 63);
    char ha[64], hb[64]; ij_emit_expr(hi_child, hp, ha, hb);

    /* Inline helper: convert long on stack to 0-based int, store in field.
       Negative: length+i.  Zero: fail.  Positive: i-1. */
    /* lo_relay: long lo on stack */
    char lo_pos[64], lo_neg[64], lo_zero[64];
    snprintf(lo_pos,  sizeof lo_pos,  "icn_%d_sec_lop", id);
    snprintf(lo_neg,  sizeof lo_neg,  "icn_%d_sec_lon", id);
    snprintf(lo_zero, sizeof lo_zero, "icn_%d_sec_loz", id);
    int lo_slot = ij_alloc_int_scratch();

    JL(lo_relay);
    JI("l2i", "");
    J("    istore %d\n", lo_slot);
    J("    iload %d\n",  lo_slot);
    J("    ifgt %s\n", lo_pos);
    J("    iload %d\n",  lo_slot);
    J("    iflt %s\n", lo_neg);
    JL(lo_zero); JGoto(ports.ω);          /* lo == 0 → fail */
    JL(lo_pos);
    J("    iload %d\n", lo_slot);
    JI("iconst_1", ""); JI("isub", "");    /* lo - 1 */
    ij_put_int_field(lo_fld);
    JGoto(ha);
    JL(lo_neg);
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    J("    iload %d\n", lo_slot);
    JI("iadd", "");                        /* length + lo (lo is negative) */
    ij_put_int_field(lo_fld);
    JGoto(ha);

    /* hi_relay: long hi on stack */
    char hi_pos[64], hi_neg[64], hi_zero[64];
    snprintf(hi_pos,  sizeof hi_pos,  "icn_%d_sec_hip", id);
    snprintf(hi_neg,  sizeof hi_neg,  "icn_%d_sec_hin", id);
    snprintf(hi_zero, sizeof hi_zero, "icn_%d_sec_hiz", id);
    int hi_slot = ij_alloc_int_scratch();

    JL(hi_relay);
    JI("l2i", "");
    J("    istore %d\n", hi_slot);
    J("    iload %d\n",  hi_slot);
    J("    ifgt %s\n", hi_pos);
    J("    iload %d\n",  hi_slot);
    J("    iflt %s\n", hi_neg);
    JL(hi_zero); JGoto(ports.ω);          /* hi == 0 is valid: s[1:0] = "" but map to empty */
    JL(hi_pos);
    J("    iload %d\n", hi_slot);
    JI("iconst_1", ""); JI("isub", "");    /* hi - 1 */
    ij_put_int_field(hi_fld);
    JGoto(compute);
    JL(hi_neg);
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    J("    iload %d\n", hi_slot);
    JI("iadd", "");
    ij_put_int_field(hi_fld);
    JGoto(compute);

    /* compute: bounds check then substring(lo_0based, hi_0based) */
    JL(compute);
    /* if lo > hi → empty string (valid in Icon: s[3:2] = "") */
    /* if lo < 0 or hi < 0 → fail */
    ij_get_int_field(lo_fld);
    J("    iflt %s\n", ports.ω);
    ij_get_int_field(hi_fld);
    J("    iflt %s\n", ports.ω);
    /* if lo > length → fail */
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    ij_get_int_field(lo_fld);
    JI("if_icmplt", ports.ω);             /* length < lo → fail */
    /* clamp hi to length */
    int len_slot = ij_alloc_int_scratch();
    ij_get_str_field(s_fld);
    JI("invokevirtual", "java/lang/String/length()I");
    J("    istore %d\n", len_slot);
    ij_get_int_field(hi_fld);
    J("    iload %d\n", len_slot);
    char hi_ok[64]; snprintf(hi_ok, sizeof hi_ok, "icn_%d_sec_hiok", id);
    J("    if_icmple %s\n", hi_ok);
    /* hi > length: clamp */
    J("    iload %d\n", len_slot);
    ij_put_int_field(hi_fld);
    JL(hi_ok);
    /* substring(lo_0based, hi_0based) — Java substring end is exclusive,
       Icon hi is already the exclusive end after our -1 conversion */
    ij_get_str_field(s_fld);
    ij_get_int_field(lo_fld);
    ij_get_int_field(hi_fld);
    JI("invokevirtual", "java/lang/String/substring(II)Ljava/lang/String;");
    JGoto(ports.γ);

    /* α: eval str → lo → hi */
    JL(a);  JGoto(sa);
    JL(s_relay); ij_put_str_field(s_fld); JGoto(la);
    /* β: one-shot → ω */
    JL(b);  JGoto(ports.ω);
}

/* =========================================================================
 * ICN_SEQ_EXPR — (E1; E2; ... En)  expression sequence
 * Semantics: evaluate E1..E(n-1), discard results, then E_n — its
 * success/failure is the result of the whole expression.
 * Wiring: identical to ICN_AND (ir_conjunction) — drain intermediates,
 * last child's γ/ω flow to ports.γ/ω.  β → last child's β.
 * ======================================================================= */
static void ij_emit_seq_expr(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int nc = n->nchildren;
    if (nc == 0) { /* degenerate */
        int id = ij_new_id(); char a[64], b[64];
        lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
        strncpy(oα,a,63); strncpy(oβ,b,63);
        JL(a); JI("lconst_0",""); JGoto(ports.γ);
        JL(b); JGoto(ports.ω);
        return;
    }
    if (nc == 1) {
        ij_emit_expr(n->children[0], ports, oα, oβ);
        return;
    }
    /* n >= 2: same relay-label pattern as ICN_AND */
    int cid = ij_new_id(); char ca2[64], cb2[64];
    lbl_α(cid,ca2,sizeof ca2); lbl_β(cid,cb2,sizeof cb2);
    strncpy(oα,ca2,63); strncpy(oβ,cb2,63);
    char (*cca)[64] = malloc(nc * 64);
    char (*ccb)[64] = malloc(nc * 64);
    char (*relay_g)[64] = malloc(nc * 64);
    for (int i = 0; i < nc; i++) {
        snprintf(relay_g[i], 64, "icn_%d_seq_rg_%d", cid, i);
        cca[i][0] = '\0'; ccb[i][0] = '\0';
    }
    for (int i = 0; i < nc; i++) {
        IjPorts ep;
        strncpy(ep.γ, (i == nc-1) ? ports.γ : relay_g[i], 63);
        strncpy(ep.ω, (i == 0)    ? ports.ω  : ccb[i-1],  63);
        ij_emit_expr(n->children[i], ep, cca[i], ccb[i]);
    }
    /* relay trampolines: drain Ei.γ result, jump to E(i+1).α */
    for (int i = 0; i < nc-1; i++) {
        JL(relay_g[i]);
        int child_is_str = ij_expr_is_string(n->children[i]);
        JI(child_is_str ? "pop" : "pop2", "");
        JGoto(cca[i+1]);
    }
    JL(ca2); JGoto(cca[0]);
    JL(cb2); JGoto(ccb[nc-1]);
    free(cca); free(ccb); free(relay_g);
}

/* =========================================================================
 * ICN_SWAP — E1 :=: E2  (swap values of two variables)
 * Semantics: atomically exchange values of lhs and rhs variables.
 * Returns the new value of E1 (Icon :=: returns lhs after swap).
 * Implementation: read both, write both crossed, using per-site temp statics.
 * Only handles VAR := VAR (the common case); other forms fall to UNIMPL.
 * ======================================================================= */
static void ij_emit_swap(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    int id = ij_new_id(); char a[64], b[64];
    lbl_α(id,a,sizeof a); lbl_β(id,b,sizeof b);
    strncpy(oα,a,63); strncpy(oβ,b,63);

    if (n->nchildren < 2 ||
        !n->children[0] || n->children[0]->kind != ICN_VAR ||
        !n->children[1] || n->children[1]->kind != ICN_VAR) {
        /* Degenerate/unsupported form — fail */
        JL(a); JGoto(ports.ω);
        JL(b); JGoto(ports.ω);
        return;
    }

    IcnNode *lv = n->children[0];
    IcnNode *rv = n->children[1];

    /* Determine field names for both sides */
    char lfld[128], rfld[128];
    int lslot = ij_locals_find(lv->val.sval);
    int rslot = ij_locals_find(rv->val.sval);
    if (lslot >= 0) ij_var_field(lv->val.sval, lfld, sizeof lfld);
    else            snprintf(lfld, sizeof lfld, "icn_gvar_%s", lv->val.sval);
    if (rslot >= 0) ij_var_field(rv->val.sval, rfld, sizeof rfld);
    else            snprintf(rfld, sizeof rfld, "icn_gvar_%s", rv->val.sval);

    /* Determine types from statics table — default long */
    int l_str = 0, l_dbl = 0, r_str = 0, r_dbl = 0;
    for (int i = 0; i < ij_nstatics; i++) {
        if (!strcmp(ij_statics[i], lfld)) {
            l_str = (ij_static_types[i] == 'A');
            l_dbl = (ij_static_types[i] == 'D');
        }
        if (!strcmp(ij_statics[i], rfld)) {
            r_str = (ij_static_types[i] == 'A');
            r_dbl = (ij_static_types[i] == 'D');
        }
    }
    /* Treat both as same type (swap between mismatched types is unusual) */
    int is_str = l_str || r_str;
    int is_dbl = l_dbl || r_dbl;

    /* Declare tmp fields for the swap */
    char tmp1[64], tmp2[64];
    snprintf(tmp1, sizeof tmp1, "icn_%d_swap_t1", id);
    snprintf(tmp2, sizeof tmp2, "icn_%d_swap_t2", id);
    if (is_str)      { ij_declare_static_str(tmp1); ij_declare_static_str(tmp2); }
    else if (is_dbl) { ij_declare_static_dbl(tmp1); ij_declare_static_dbl(tmp2); }
    else             { ij_declare_static(tmp1);     ij_declare_static(tmp2); }

    JL(a);
    /* Read both values into tmps */
    if (is_str)      { ij_get_str_field(lfld); ij_put_str_field(tmp1);
                       ij_get_str_field(rfld); ij_put_str_field(tmp2); }
    else if (is_dbl) { ij_get_dbl(lfld); ij_put_dbl(tmp1);
                       ij_get_dbl(rfld); ij_put_dbl(tmp2); }
    else             { ij_get_long(lfld); ij_put_long(tmp1);
                       ij_get_long(rfld); ij_put_long(tmp2); }
    /* Write crossed */
    if (is_str)      { ij_get_str_field(tmp2); ij_put_str_field(lfld);
                       ij_get_str_field(tmp1); ij_put_str_field(rfld); }
    else if (is_dbl) { ij_get_dbl(tmp2); ij_put_dbl(lfld);
                       ij_get_dbl(tmp1); ij_put_dbl(rfld); }
    else             { ij_get_long(tmp2); ij_put_long(lfld);
                       ij_get_long(tmp1); ij_put_long(rfld); }
    /* Return new value of lhs (tmp2 = old rhs = new lhs) */
    if (is_str)      ij_get_str_field(lfld);
    else if (is_dbl) ij_get_dbl(lfld);
    else             ij_get_long(lfld);
    JGoto(ports.γ);
    JL(b); JGoto(ports.ω);
}

/* =========================================================================
 * Dispatch
 * ======================================================================= */
static void ij_emit_expr(IcnNode *n, IjPorts ports, char *oα, char *oβ) {
    if (!n) { ij_emit_fail_node(NULL,ports,oα,oβ); return; }
    switch (n->kind) {
        case ICN_INT:     ij_emit_int      (n,ports,oα,oβ); break;
        case ICN_REAL:    ij_emit_real     (n,ports,oα,oβ); break;
        case ICN_STR:     ij_emit_str      (n,ports,oα,oβ); break;
        case ICN_CSET:    ij_emit_str      (n,ports,oα,oβ); break; /* cset = typed String */
        case ICN_VAR:     ij_emit_var      (n,ports,oα,oβ); break;
        case ICN_ASSIGN:  ij_emit_assign   (n,ports,oα,oβ); break;
        case ICN_RETURN:  ij_emit_return   (n,ports,oα,oβ); break;
        case ICN_SUSPEND: ij_emit_suspend  (n,ports,oα,oβ); break;
        case ICN_FAIL:    ij_emit_fail_node(n,ports,oα,oβ); break;
        case ICN_IF:      ij_emit_if       (n,ports,oα,oβ); break;
        case ICN_CASE:    ij_emit_case     (n,ports,oα,oβ); break;
        case ICN_ALT:     ij_emit_alt      (n,ports,oα,oβ); break;
        case ICN_AND: {
            /* n-ary conjunction: E1 & E2 & ... & En
             * irgen.icn ir_conjunction: Ei.γ → E(i+1).α; Ei.ω → E(i-1).β; β → En.β
             *
             * Bug fix: must emit LEFT-TO-RIGHT so ccb[i-1] is known when wiring
             * E[i].ω.  But Ei.γ needs to point to E(i+1).α which isn't known yet.
             * Solution: pre-allocate a unique "relay" label for each child's γ,
             * emit left-to-right using those relay labels as γ targets, then emit
             * the relay → real-α trampolines after all children.
             */
            int nc = n->nchildren;
            int cid = ij_new_id(); char ca2[64], cb2[64];
            lbl_α(cid,ca2,sizeof ca2); lbl_β(cid,cb2,sizeof cb2);
            strncpy(oα,ca2,63); strncpy(oβ,cb2,63);
            char (*cca)[64] = malloc(nc * 64);
            char (*ccb)[64] = malloc(nc * 64);
            /* Pre-allocate relay labels for each γ port */
            char (*relay_g)[64] = malloc(nc * 64);
            for (int i = 0; i < nc; i++) {
                snprintf(relay_g[i], 64, "icn_%d_and_rg_%d", cid, i);
                cca[i][0] = '\0'; ccb[i][0] = '\0';
            }
            /* Emit children left-to-right */
            for (int i = 0; i < nc; i++) {
                IjPorts ep;
                /* γ: last child → ports.γ; otherwise relay_g[i] (trampoline) */
                strncpy(ep.γ, (i == nc-1) ? ports.γ : relay_g[i], 63);
                /* ω: first child → ports.ω; otherwise ccb[i-1] (already populated) */
                strncpy(ep.ω, (i == 0) ? ports.ω : ccb[i-1], 63);
                ij_emit_expr(n->children[i], ep, cca[i], ccb[i]);
            }
            /* Emit relay trampolines: relay_g[i] → drain result to static field → cca[i+1]
             * E[i].γ leaves a value on the JVM stack; E[i+1].α expects empty stack.
             * Use putstatic (static field drain) instead of pop/pop2 so relay labels
             * are safe to reach from any stack state (e.g. ICN_EVERY β-tableswitch).
             * ICN_ALT children deliver lconst_0 (J) at ports.γ — same as long-typed
             * non-ALT children, so putstatic :J handles them correctly. */
            for (int i = 0; i < nc-1; i++) {
                char drain_fld[80];
                snprintf(drain_fld, sizeof drain_fld, "icn_%d_and_drain_%d", cid, i);
                int child_is_str  = ij_expr_is_string(n->children[i]);
                int child_is_list = !child_is_str && ij_expr_is_list(n->children[i]);
                int child_is_tbl  = !child_is_str && !child_is_list
                                    && ij_expr_is_table(n->children[i]);
                int child_is_ref  = child_is_str || child_is_list || child_is_tbl;
                int child_is_dbl  = !child_is_ref && ij_expr_is_real(n->children[i]);
                if (child_is_ref)      ij_declare_static_str(drain_fld);
                else if (child_is_dbl) ij_declare_static_dbl(drain_fld);
                else                   ij_declare_static(drain_fld);
                JL(relay_g[i]);
                if (child_is_ref)      ij_put_str_field(drain_fld);
                else if (child_is_dbl) ij_put_dbl(drain_fld);
                else                   ij_put_long(drain_fld);
                JGoto(cca[i+1]);
            }
            JL(ca2); JGoto(cca[0]);
            JL(cb2); JGoto(ccb[nc-1]);
            free(cca); free(ccb); free(relay_g);
            break;
        }
        case ICN_ADD: case ICN_SUB: case ICN_MUL: case ICN_DIV: case ICN_MOD:
                          ij_emit_binop    (n,ports,oα,oβ); break;
        case ICN_POW:     ij_emit_pow      (n,ports,oα,oβ); break;
        case ICN_CONCAT:  ij_emit_concat   (n,ports,oα,oβ); break;
        case ICN_LCONCAT: ij_emit_concat   (n,ports,oα,oβ); break; /* Tiny-ICON: ||| = || */
        case ICN_SWAP:    ij_emit_swap      (n,ports,oα,oβ); break;
        case ICN_SUBSCRIPT: ij_emit_subscript(n,ports,oα,oβ); break;
        case ICN_SECTION:   ij_emit_section  (n,ports,oα,oβ); break;
        case ICN_MAKELIST:  ij_emit_makelist (n,ports,oα,oβ); break;
        case ICN_SEQ_EXPR: ij_emit_seq_expr (n,ports,oα,oβ); break;
        case ICN_LT: case ICN_LE: case ICN_GT: case ICN_GE: case ICN_EQ: case ICN_NE:
                          ij_emit_relop    (n,ports,oα,oβ); break;
        case ICN_TO:      ij_emit_to       (n,ports,oα,oβ); break;
        case ICN_TO_BY:   ij_emit_to_by    (n,ports,oα,oβ); break;
        case ICN_EVERY:   ij_emit_every    (n,ports,oα,oβ); break;
        case ICN_WHILE:   ij_emit_while    (n,ports,oα,oβ); break;
        case ICN_UNTIL:   ij_emit_until    (n,ports,oα,oβ); break;
        case ICN_REPEAT:  ij_emit_repeat   (n,ports,oα,oβ); break;
        case ICN_CALL:    ij_emit_call     (n,ports,oα,oβ); break;
        case ICN_SCAN:    ij_emit_scan     (n,ports,oα,oβ); break;
        case ICN_NOT:     ij_emit_not      (n,ports,oα,oβ); break;
        case ICN_NONNULL: ij_emit_nonnull  (n,ports,oα,oβ); break;
        case ICN_NULL:    ij_emit_null     (n,ports,oα,oβ); break;
        case ICN_INITIAL: ij_emit_initial  (n,ports,oα,oβ); break;
        case ICN_GLOBAL: {
            /* local 'global x,y;' decl inside proc body — declare icn_gvar_* fields,
               skip as a statement (no runtime effect beyond declaration). */
            for (int ci = 0; ci < n->nchildren; ci++) {
                IcnNode *v = n->children[ci];
                if (!v || v->kind != ICN_VAR) continue;
                char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", v->val.sval);
                ij_declare_static(gname);
            }
            int id2 = ij_new_id(); char a2[64], b2[64];
            lbl_α(id2,a2,sizeof a2); lbl_β(id2,b2,sizeof b2);
            strncpy(oα,a2,63); strncpy(oβ,b2,63);
            JL(a2); JI("lconst_0",""); JGoto(ports.γ);
            JL(b2); JGoto(ports.ω);
            break;
        }
        case ICN_NEG:     ij_emit_neg      (n,ports,oα,oβ); break;
        case ICN_BREAK:   ij_emit_break    (n,ports,oα,oβ); break;
        case ICN_NEXT:    ij_emit_next     (n,ports,oα,oβ); break;
        case ICN_AUGOP:   ij_emit_augop    (n,ports,oα,oβ); break;
        case ICN_BANG:    ij_emit_bang     (n,ports,oα,oβ); break;
        case ICN_SIZE:    ij_emit_size     (n,ports,oα,oβ); break;
        case ICN_LIMIT:   ij_emit_limit    (n,ports,oα,oβ); break;
        case ICN_FIELD:   ij_emit_field    (n,ports,oα,oβ); break;
        case ICN_SEQ: case ICN_SNE: case ICN_SLT:
        case ICN_SLE: case ICN_SGT: case ICN_SGE:
                          ij_emit_strrelop (n,ports,oα,oβ); break;
        default: {
            int id = ij_new_id(); char a2[64], b2[64];
            lbl_α(id,a2,sizeof a2); lbl_β(id,b2,sizeof b2);
            strncpy(oα,a2,63); strncpy(oβ,b2,63);
            J(";  UNIMPL %d id=%d\n", n->kind, id);
            JL(a2); JGoto(ports.ω);
            JL(b2); JGoto(ports.ω);
        }
    }
}

/* =========================================================================
 * Emit one procedure as a static method
 * ======================================================================= */
/* Pre-pass 3 (recursive): walk the entire AST and register VAR types for all
 * ICN_ASSIGN nodes, regardless of nesting depth.  This ensures that variables
 * assigned inside ICN_AND/ICN_ALT/ICN_WHILE sub-expressions are typed before
 * any drain-type query fires during emission.  Must be called after the locals
 * list is populated (so ij_locals_find works) and before ij_emit_expr. */
static void ij_prepass_types(IcnNode *n) {
    if (!n) return;
    if (n->kind == ICN_ASSIGN && n->nchildren >= 2) {
        IcnNode *lhs = n->children[0];
        IcnNode *rhs = n->children[1];
        if (lhs && lhs->kind == ICN_VAR) {
            int slot = ij_locals_find(lhs->val.sval);
            char fld[128];
            if (slot >= 0) ij_var_field(lhs->val.sval, fld, sizeof fld);
            else           snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
            if (ij_expr_is_string(rhs)) {
                ij_declare_static_str(fld);
            } else if (ij_expr_is_list(rhs)) {
                if (ij_expr_is_record_list(rhs))
                    ij_declare_static_reclist(fld);
                else
                    ij_declare_static_list(fld);
            } else if (ij_expr_is_table(rhs)) {
                ij_declare_static_table(fld);
            } else if (ij_expr_is_real(rhs)) {
                ij_declare_static_dbl(fld);
            }
            /* long-typed: declared on first use, no pre-declaration needed */
        }
    }
    for (int i = 0; i < n->nchildren; i++)
        ij_prepass_types(n->children[i]);
}

static void ij_emit_proc(IcnNode *proc, FILE *out_target) {
    FILE *save = jout;
    /* Emit proc body to a temp buffer first (so we know locals count) */
    FILE *tmp = tmpfile();
    jout = tmp;

    const char *pname = proc->children[0]->val.sval;
    int is_main = (strcmp(pname, "main") == 0);
    int np = (int)proc->val.ival;
    int is_gen = !is_main && ij_is_gen_proc(pname);
    int body_start = 1 + np;
    int nstmts = proc->nchildren - body_start;

    /* Setup local environment */
    ij_locals_reset();
    strncpy(ij_cur_proc, pname, 63);
    char proc_done[64];
    if (is_main) snprintf(proc_done, sizeof proc_done, "icn_main_done");
    else         snprintf(proc_done, sizeof proc_done, "icn_%s_done", pname);
    char proc_ret[64];  snprintf(proc_ret,  sizeof proc_ret,  "icn_%s_ret",  pname);
    char proc_sret[64]; snprintf(proc_sret, sizeof proc_sret, "icn_%s_sret", pname);

    strncpy(ij_ret_label,  is_main ? "icn_main_done" : proc_ret,  63);
    strncpy(ij_fail_label, proc_done, 63);
    strncpy(ij_sret_label, is_main ? "icn_main_done" : proc_sret, 63);
    ij_next_suspend_id = 1;

    /* Register params as locals 0..np-1 */
    ij_nparams = np;
    for (int i = 0; i < np; i++) {
        IcnNode *pv = proc->children[1 + i];
        if (pv && pv->kind == ICN_VAR) ij_locals_add(pv->val.sval);
    }
    /* Scan for local declarations in body */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *s = proc->children[body_start + si];
        if (s && s->kind == ICN_GLOBAL) {
            for (int ci = 0; ci < s->nchildren; ci++) {
                IcnNode *v = s->children[ci];
                /* Top-level globals (registered in ij_global_names) must NOT
                 * be added to ij_locals — they live as icn_gvar_* static fields
                 * shared across all procs.  Only proc-local var declarations
                 * that somehow have ICN_GLOBAL kind should be added. */
                if (v && v->kind == ICN_VAR && ij_locals_find(v->val.sval) < 0
                        && !ij_is_global(v->val.sval))
                    ij_locals_add(v->val.sval);
            }
        }
    }

    /* Pre-pass (forward): register string-typed and double-typed variable fields so
     * ij_expr_is_string / ij_expr_is_real work correctly when the reverse emit loop
     * checks statement result types.
     * Walk assignments left-to-right: declare the LHS var's static field type
     * before emit begins. */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *stmt = proc->children[body_start + si];
        if (!stmt || stmt->kind != ICN_ASSIGN || stmt->nchildren < 2) continue;
        IcnNode *lhs = stmt->children[0];
        IcnNode *rhs = stmt->children[1];
        if (!lhs || lhs->kind != ICN_VAR) continue;
        int slot = ij_locals_find(lhs->val.sval);
        char fld[128];
        if (slot >= 0) ij_var_field(lhs->val.sval, fld, sizeof fld);
        else           snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
        if (ij_expr_is_string(rhs)) {
            ij_declare_static_str(fld);
        } else if (ij_expr_is_list(rhs)) {
            if (ij_expr_is_record_list(rhs))
                ij_declare_static_reclist(fld);
            else
                ij_declare_static_list(fld);
        } else if (ij_expr_is_table(rhs)) {
            ij_declare_static_table(fld);
            /* Check if rhs is table(dflt) — if so, pre-declare {fld}_dflt */
            if (rhs->kind == ICN_CALL && rhs->nchildren >= 1) {
                IcnNode *fn = rhs->children[0];
                if (fn && fn->kind == ICN_VAR &&
                        strcmp(fn->val.sval, "table") == 0) {
                    char vdflt[144]; snprintf(vdflt, sizeof vdflt, "%s_dflt", fld);
                    ij_declare_static_obj(vdflt);
                }
            }
        } else if (ij_expr_is_real(rhs)) {
            ij_declare_static_dbl(fld);
        }
        /* long-typed vars are declared on first use — no pre-declaration needed */
    }

    /* Pre-pass 2: scan ICN_EVERY generators for `v := !reclist` assigns.
     * The every emitter emits body before gen, so the ICN_VAR for v in the body
     * would be emitted before the assign tags v as 'O'. Pre-tag here. */
    for (int si = 0; si < nstmts; si++) {
        IcnNode *stmt = proc->children[body_start + si];
        if (!stmt || stmt->kind != ICN_EVERY || stmt->nchildren < 1) continue;
        IcnNode *gen = stmt->children[0];
        if (!gen || gen->kind != ICN_ASSIGN || gen->nchildren < 2) continue;
        IcnNode *lhs = gen->children[0];
        IcnNode *rhs = gen->children[1];  /* should be ICN_BANG(!reclist) */
        if (!lhs || lhs->kind != ICN_VAR) continue;
        if (!rhs || rhs->kind != ICN_BANG || rhs->nchildren < 1) continue;
        if (!ij_expr_is_record_list(rhs->children[0])) continue;
        /* lhs var is assigned a record from a record-list bang — tag as Object */
        int slot = ij_locals_find(lhs->val.sval);
        char fld[128];
        if (slot >= 0) ij_var_field(lhs->val.sval, fld, sizeof fld);
        else           snprintf(fld, sizeof fld, "icn_gvar_%s", lhs->val.sval);
        ij_declare_static_obj(fld);
    }

    /* Pre-pass 3: recursive walk — catches assigns nested inside AND/ALT/WHILE. */
    for (int si = 0; si < nstmts; si++)
        ij_prepass_types(proc->children[body_start + si]);

    /* Chain statements.
     * Fix IJ-6 Bug3: every top-level statement (assignment etc.) leaves its result
     * value (a long) on the JVM operand stack when it jumps to its γ port.  The γ
     * port of statement[i] is the α port of statement[i+1], which is ALSO entered
     * from other paths with an empty stack → "Inconsistent stack height 0 != 2".
     * Solution: route each statement's γ through a private sdrain label that pops
     * the result before forwarding to the real next_a. */
    char **alphas = calloc(nstmts, sizeof(char*));
    for (int i = 0; i < nstmts; i++) alphas[i] = malloc(64);
    char next_a[64]; strncpy(next_a, proc_done, 63);

    for (int i = nstmts-1; i >= 0; i--) {
        IcnNode *stmt = proc->children[body_start + i];
        if (!stmt || stmt->kind == ICN_GLOBAL) { strncpy(alphas[i], next_a, 63); continue; }
        /* ICN_EVERY / ICN_WHILE / ICN_UNTIL / ICN_REPEAT never fire ports.γ with a value —
         * they always end via ports.ω (exhaustion/failure). Skip the drain to avoid
         * JVM VerifyError on dead pop2 with empty stack. */
        if (stmt->kind == ICN_EVERY || stmt->kind == ICN_WHILE ||
            stmt->kind == ICN_UNTIL || stmt->kind == ICN_REPEAT) {
            IjPorts sp; strncpy(sp.γ, next_a, 63); strncpy(sp.ω, next_a, 63);
            char sa[64], sb[64]; ij_emit_expr(stmt, sp, sa, sb);
            strncpy(alphas[i], sa, 63);
            strncpy(next_a, sa, 63);
            continue;
        }
        /* Determine if statement produces a 1-slot ref (String or ArrayList) or 2-slot (long/double) */
        int stmt_is_str  = ij_expr_is_string(stmt);
        int stmt_is_list = !stmt_is_str && ij_expr_is_list(stmt);
        int stmt_is_tbl  = !stmt_is_str && !stmt_is_list && ij_expr_is_table(stmt);
        int stmt_is_ref  = stmt_is_str || stmt_is_list || stmt_is_tbl;
        /* Build a drain label for this statement's success port */
        char sdrain[64]; snprintf(sdrain, sizeof sdrain, "icn_s%d_sdrain", i);
        IjPorts sp; strncpy(sp.γ, sdrain, 63); strncpy(sp.ω, next_a, 63);
        char sa[64], sb[64]; ij_emit_expr(stmt, sp, sa, sb);
        /* Emit the drain: pop result (1-slot String or 2-slot long) then fall through */
        J("%s:\n", sdrain);
        JI(stmt_is_ref ? "pop" : "pop2", "");
        JGoto(next_a);
        strncpy(alphas[i], sa, 63);
        strncpy(next_a, sa, 63);
    }

    /* Generator β tableswitch (if applicable) */
    int total_susp = ij_suspend_count;
    if (is_gen && total_susp > 0) {
        char beta_entry[64]; snprintf(beta_entry, sizeof beta_entry, "icn_%s_beta", pname);
        JL(beta_entry);
        ij_get_int_field("icn_suspend_id");
        J("    tableswitch 1 %d\n", total_susp);
        for (int k = 0; k < total_susp; k++) {
            /* Find the resume label for this suspend ID */
            J("        icn_%d_resume\n", ij_suspend_ids[k]); /* approximate: real IDs from emit */
        }
        J("        default: %s\n", proc_done);
    }

    /* proc_done: failure exit (for non-main); icn_main_done for main */
    JL(proc_done);
    if (!is_main) {
        /* Clear suspend state so subsequent calls to this proc start fresh */
        JI("iconst_0", ""); ij_put_byte("icn_suspended");
        JI("iconst_0", ""); ij_put_int_field("icn_suspend_id");
        ij_set_fail();
        JI("return","");
        /* proc_ret: success return */
        JL(proc_ret);
        ij_set_ok();
        JI("return","");
        /* proc_sret: suspend-yield return */
        JL(proc_sret);
        JI("return","");
    } else {
        JI("return","");
    }

    /* Read body buffer */
    long sz = ftell(tmp); rewind(tmp);
    char *body = malloc(sz + 1); fread(body, 1, sz, tmp); body[sz] = '\0';
    fclose(tmp);
    jout = out_target;

    /* Emit method header */
    int jvm_locals = ij_jvm_locals_count() + 10; /* generous padding */
    J(".method public static icn_%s()V\n", pname);
    J("    .limit stack 16\n");
    J("    .limit locals %d\n", jvm_locals);

    /* Zero-init ALL JVM local slots so the verifier sees consistent types at
     * every control-flow join point.
     * - Long-pair region [0 .. 2*ij_nlocals-1]: lconst_0/lstore (every 2 slots)
     * - Padding long-pairs up to 2*MAX_LOCALS: lconst_0/lstore
     * - Int/ref scratch region [2*MAX_LOCALS .. 2*MAX_LOCALS+ij_nint_scratch-1]:
     *   iconst_0/istore (single slots, never touched by lstore) */
    for (int s = 0; s < MAX_LOCALS; s++) {
        JI("lconst_0", "");
        J("    lstore %d\n", s * 2);
    }
    for (int i = 0; i < ij_nint_scratch + 4; i++) {
        JI("iconst_0", "");
        J("    istore %d\n", 2 * MAX_LOCALS + 20 + i);
    }

    /* For non-main procs, load params from static arg fields */
    /* NOTE: for generators, param load happens at fresh_entry (after zero-init).
     * For non-generators, load here since there is no zero-init block. */
    if (!is_main && np > 0 && !(is_gen && total_susp > 0)) {
        for (int i = 0; i < np; i++) {
            IcnNode *pv = proc->children[1 + i];
            const char *pname2 = (pv && pv->kind == ICN_VAR) ? pv->val.sval : "";
            char argfield[64];    snprintf(argfield,    sizeof argfield,    "icn_arg_%d",     i);
            char argobjfield[64]; snprintf(argobjfield, sizeof argobjfield, "icn_arg_obj_%d", i);
            char argstrfield[64]; snprintf(argstrfield, sizeof argstrfield, "icn_arg_str_%d", i);
            char fld[128]; ij_var_field(pname2, fld, sizeof fld);
            if (ij_field_type_tag(argobjfield) == 'O') {
                ij_declare_static_obj(fld);
                ij_get_obj_field(argobjfield);
                ij_put_obj_field(fld);
            } else if (ij_field_type_tag(argstrfield) == 'A') {
                ij_declare_static_str(fld);
                ij_get_str_field(argstrfield);
                ij_put_str_field(fld);
            } else {
                ij_declare_static(fld);
                ij_get_long(argfield);
                ij_put_long(fld);
            }
        }
    }

    /* Entry: generator dispatch — if icn_suspend_id != 0, we are resuming → beta entry */
    if (is_gen && total_susp > 0) {
        char beta_entry[64]; snprintf(beta_entry, sizeof beta_entry, "icn_%s_beta", pname);
        char fresh_entry[64]; snprintf(fresh_entry, sizeof fresh_entry, "icn_%s_fresh", pname);
        /* Zero-init all JVM local slots so verifier sees consistent types at all
         * control-flow join points (fresh path + all tableswitch resume targets). */
        for (int s = 0; s < ij_nlocals; s++) {
            JI("lconst_0", "");
            J("    lstore %d\n", slot_jvm(s));
        }
        J("    getstatic %s/icn_suspend_id I\n", ij_classname);
        J("    ifne %s\n", beta_entry);
        JL(fresh_entry);
        /* Fresh entry: load params from arg fields into per-proc static var fields.
         * This must happen AFTER zero-init (so zero-init doesn't clobber them)
         * and only on fresh calls (not resume — param values persist in static fields). */
        if (np > 0) {
            for (int i = 0; i < np; i++) {
                IcnNode *pv2 = proc->children[1 + i];
                const char *pvname = (pv2 && pv2->kind == ICN_VAR) ? pv2->val.sval : "";
                char argfield[64];    snprintf(argfield,    sizeof argfield,    "icn_arg_%d",     i);
                char argobjfield[64]; snprintf(argobjfield, sizeof argobjfield, "icn_arg_obj_%d", i);
                char argstrfield[64]; snprintf(argstrfield, sizeof argstrfield, "icn_arg_str_%d", i);
                char fld[128]; ij_var_field(pvname, fld, sizeof fld);
                if (ij_field_type_tag(argobjfield) == 'O') {
                    ij_declare_static_obj(fld);
                    ij_get_obj_field(argobjfield);
                    ij_put_obj_field(fld);
                } else if (ij_field_type_tag(argstrfield) == 'A') {
                    ij_declare_static_str(fld);
                    ij_get_str_field(argstrfield);
                    ij_put_str_field(fld);
                } else {
                    ij_declare_static(fld);
                    ij_get_long(argfield);
                    ij_put_long(fld);
                }
            }
        }
    }
    /* Entry: jump to first statement */
    if (nstmts > 0 && alphas[0][0]) JGoto(alphas[0]);
    else JGoto(proc_done);

    fputs(body, jout);
    free(body);

    J(".end method\n\n");

    for (int i = 0; i < nstmts; i++) free(alphas[i]);
    free(alphas);
    jout = save;
}

/* =========================================================================
 * ij_emit_file — entry point
 * ======================================================================= */
void ij_emit_file(IcnNode **nodes, int count, FILE *out, const char *filename, const char *outpath) {
    ij_node_id = 0;
    ij_user_count = 0;
    ij_nstatics = 0;
    ij_nstrings = 0;
    ij_ntdflt = 0;
    ij_nrec = 0;

    ij_set_classname(filename ? filename : "IconProg");

    /* Pass 0: register top-level global var names in ij_global_names so that
     * var/assign emit routes them to icn_gvar_* fields instead of ij_locals.
     * Do NOT pre-declare a typed field here — type is inferred on first
     * assignment, same as local vars.  ij_is_global(name) is the predicate. */
    ij_nglobals = 0;
    for (int pi = 0; pi < count; pi++) {
        IcnNode *nd = nodes[pi];
        if (!nd) continue;
        if (nd->kind == ICN_RECORD)
            ij_register_record(nd->val.sval, nd);
        if (nd->kind != ICN_GLOBAL) continue;
        for (int ci = 0; ci < nd->nchildren; ci++) {
            IcnNode *v = nd->children[ci];
            if (!v || v->kind != ICN_VAR) continue;
            ij_register_global(v->val.sval);
        }
    }

    /* Pass 1: register all user procs */
    for (int pi = 0; pi < count; pi++) {
        IcnNode *proc = nodes[pi];
        if (!proc || proc->kind != ICN_PROC || proc->nchildren < 1) continue;
        const char *pname = proc->children[0]->val.sval;
        if (strcmp(pname, "main") == 0) continue;
        int np = (int)proc->val.ival;
        int gen = 0;
        int body_start = 1 + np;
        for (int si = body_start; si < proc->nchildren; si++)
            if (ij_has_suspend(proc->children[si])) { gen = 1; break; }
        ij_register_proc(pname, np, gen);
        /* Mark string-returning procs: scan body for ICN_RETURN with string child */
        for (int si = body_start; si < proc->nchildren; si++) {
            IcnNode *stmt = proc->children[si];
            if (stmt && stmt->kind == ICN_RETURN &&
                stmt->nchildren > 0 && ij_expr_is_string(stmt->children[0])) {
                ij_mark_proc_returns_str(pname);
                break;
            }
        }
    }

    /* Pass 1b: pre-register variables assigned from record constructors as Object-typed.
     * This ensures ij_emit_var emits lconst_0 (not getstatic J) even for var reads
     * that are emitted before the assignment in the Byrd-box layout. */
    {
        /* Helper: walk an AST node looking for ASSIGN(VAR, CALL(RecordType,...)) */
        /* We do a simple recursive scan of all proc bodies */
        void scan_record_assigns(IcnNode *n, int is_proc_scope);
        /* Inline recursive lambda not in C — use a stack-based worklist */
        #define MAX_SCAN_STACK 512
        IcnNode *stack[MAX_SCAN_STACK]; int top = 0;
        for (int pi = 0; pi < count; pi++) {
            IcnNode *proc = nodes[pi];
            if (!proc || proc->kind != ICN_PROC) continue;
            /* Set up locals context for this proc */
            ij_locals_reset();
            strncpy(ij_cur_proc, proc->children[0]->val.sval, 63);
            int np = (int)proc->val.ival;
            for (int i = 1; i <= np; i++)
                if (proc->children[i] && proc->children[i]->kind == ICN_VAR)
                    ij_locals_add(proc->children[i]->val.sval);
            /* Push all body nodes */
            top = 0;
            for (int i = 1 + np; i < proc->nchildren; i++)
                if (top < MAX_SCAN_STACK) stack[top++] = proc->children[i];
            while (top > 0) {
                IcnNode *cur = stack[--top];
                if (!cur) continue;
                if (cur->kind == ICN_ASSIGN && cur->nchildren >= 2) {
                    IcnNode *lhs = cur->children[0];
                    IcnNode *rhs = cur->children[1];
                    if (lhs && lhs->kind == ICN_VAR && ij_expr_is_record(rhs)) {
                        /* Pre-declare this var as Object */
                        int slot = ij_locals_find(lhs->val.sval);
                        if (slot >= 0) {
                            char fld[128]; ij_var_field(lhs->val.sval, fld, sizeof fld);
                            ij_declare_static_obj(fld);
                        } else {
                            char gname[80]; snprintf(gname, sizeof gname, "icn_gvar_%s", lhs->val.sval);
                            ij_declare_static_obj(gname);
                        }
                    }
                }
                for (int i = 0; i < cur->nchildren && top < MAX_SCAN_STACK; i++)
                    stack[top++] = cur->children[i];
            }
        }
        #undef MAX_SCAN_STACK
    }

    /* Pass 1c: pre-register icn_arg_obj_N as Object for each record arg at user-proc
     * call sites.  This must run before Pass 2 so that callee param-load emit can
     * detect record params via ij_field_type_tag("icn_arg_obj_N") == 'O'.
     * Also pre-declares the callee's param var field as Object so ij_emit_var
     * emits getstatic Object (not J) even before the param-load code runs. */
    {
        #define MAX_SCAN_STACK2 512
        IcnNode *stack2[MAX_SCAN_STACK2]; int top2 = 0;
        for (int pi = 0; pi < count; pi++) {
            IcnNode *proc = nodes[pi];
            if (!proc || proc->kind != ICN_PROC) continue;
            int np2 = (int)proc->val.ival;
            top2 = 0;
            for (int i = 1 + np2; i < proc->nchildren; i++)
                if (top2 < MAX_SCAN_STACK2) stack2[top2++] = proc->children[i];
            while (top2 > 0) {
                IcnNode *cur = stack2[--top2];
                if (!cur) continue;
                if (cur->kind == ICN_CALL && cur->nchildren >= 1
                        && cur->children[0]->kind == ICN_VAR
                        && ij_is_user_proc(cur->children[0]->val.sval)) {
                    const char *callee = cur->children[0]->val.sval;
                    int na = cur->nchildren - 1;
                    /* Find callee proc node to get param names */
                    IcnNode *callee_proc = NULL;
                    for (int ci = 0; ci < count; ci++) {
                        IcnNode *cp = nodes[ci];
                        if (cp && cp->kind == ICN_PROC && cp->nchildren >= 1
                                && cp->children[0]->kind == ICN_VAR
                                && !strcmp(cp->children[0]->val.sval, callee)) {
                            callee_proc = cp; break;
                        }
                    }
                    for (int i = 0; i < na; i++) {
                        if (ij_expr_is_record(cur->children[i+1])) {
                            char argobjfield[64];
                            snprintf(argobjfield, sizeof argobjfield, "icn_arg_obj_%d", i);
                            ij_declare_static_obj(argobjfield);
                            /* Also pre-declare callee param var field as Object */
                            if (callee_proc) {
                                int cnp = (int)callee_proc->val.ival;
                                if (i < cnp) {
                                    IcnNode *pv = callee_proc->children[1 + i];
                                    if (pv && pv->kind == ICN_VAR) {
                                        char saved_proc[64];
                                        strncpy(saved_proc, ij_cur_proc, 63);
                                        strncpy(ij_cur_proc, callee, 63);
                                        char fld[128]; ij_var_field(pv->val.sval, fld, sizeof fld);
                                        ij_declare_static_obj(fld);
                                        strncpy(ij_cur_proc, saved_proc, 63);
                                    }
                                }
                            }
                        }
                    }
                }
                for (int i = 0; i < cur->nchildren && top2 < MAX_SCAN_STACK2; i++)
                    stack2[top2++] = cur->children[i];
            }
        }
        #undef MAX_SCAN_STACK2
    }

    /* Pass 1d: pre-register icn_arg_str_N as String for each string arg at user-proc
     * call sites, and pre-declare callee param var field as String, so the callee
     * prologue can detect string params via ij_field_type_tag("icn_arg_str_N")=='A'.
     * Run to fixpoint: each iteration may expose new string-typed vars that enable
     * further propagation (e.g. wrap(s) → suffix(s) where s is string). */
    {
        #define MAX_SCAN_STACK3 512
        /* Three passes covers depth-3 call chains; fixpoint for deeper chains */
        for (int pass1d = 0; pass1d < 3; pass1d++) {
        IcnNode *stack3[MAX_SCAN_STACK3]; int top3 = 0;
        for (int pi = 0; pi < count; pi++) {
            IcnNode *proc = nodes[pi];
            if (!proc || proc->kind != ICN_PROC) continue;
            int np3 = (int)proc->val.ival;
            /* Set ij_cur_proc so ij_var_field() resolves correctly */
            if (proc->children[0] && proc->children[0]->kind == ICN_VAR)
                strncpy(ij_cur_proc, proc->children[0]->val.sval, 63);
            top3 = 0;
            for (int i = 1 + np3; i < proc->nchildren; i++)
                if (top3 < MAX_SCAN_STACK3) stack3[top3++] = proc->children[i];
            while (top3 > 0) {
                IcnNode *cur = stack3[--top3];
                if (!cur) continue;
                if (cur->kind == ICN_CALL && cur->nchildren >= 1
                        && cur->children[0]->kind == ICN_VAR
                        && ij_is_user_proc(cur->children[0]->val.sval)) {
                    const char *callee = cur->children[0]->val.sval;
                    int na = cur->nchildren - 1;
                    IcnNode *callee_proc = NULL;
                    for (int ci = 0; ci < count; ci++) {
                        IcnNode *cp = nodes[ci];
                        if (cp && cp->kind == ICN_PROC && cp->nchildren >= 1
                                && cp->children[0]->kind == ICN_VAR
                                && !strcmp(cp->children[0]->val.sval, callee)) {
                            callee_proc = cp; break;
                        }
                    }
                    for (int i = 0; i < na; i++) {
                        if (ij_expr_is_string(cur->children[i+1])
                                && !ij_expr_is_record(cur->children[i+1])) {
                            char argstrfield[64];
                            snprintf(argstrfield, sizeof argstrfield, "icn_arg_str_%d", i);
                            ij_declare_static_str(argstrfield);
                            /* Pre-declare callee param var field as String */
                            if (callee_proc) {
                                int cnp = (int)callee_proc->val.ival;
                                if (i < cnp) {
                                    IcnNode *pv = callee_proc->children[1 + i];
                                    if (pv && pv->kind == ICN_VAR) {
                                        char saved_proc[64];
                                        strncpy(saved_proc, ij_cur_proc, 63);
                                        strncpy(ij_cur_proc, callee, 63);
                                        char fld[128]; ij_var_field(pv->val.sval, fld, sizeof fld);
                                        ij_declare_static_str(fld);
                                        strncpy(ij_cur_proc, saved_proc, 63);
                                    }
                                }
                            }
                        }
                    }
                }
                for (int i = 0; i < cur->nchildren && top3 < MAX_SCAN_STACK3; i++)
                    stack3[top3++] = cur->children[i];
            }
        }
        } /* end pass1d loop */
        #undef MAX_SCAN_STACK3
    }

    /* Pass 2: emit each proc to a buffer */
    FILE *body_buf = tmpfile();
    FILE *save = jout;
    for (int pi = 0; pi < count; pi++) {
        IcnNode *proc = nodes[pi];
        if (!proc || proc->kind != ICN_PROC || proc->nchildren < 1) continue;
        ij_emit_proc(proc, body_buf);
    }

    /* Read proc body */
    long bsz = ftell(body_buf); rewind(body_buf);
    char *procs_text = malloc(bsz + 1); fread(procs_text, 1, bsz, body_buf); procs_text[bsz] = '\0';
    fclose(body_buf);
    jout = out;

    /* Emit class header */
    J("; Auto-generated by icon_emit_jvm.c — Tiny-ICON Byrd Box JVM\n");
    J(".bytecode 45.0\n");   /* Java 6 — no StackMapTable required */
    J(".class public %s\n", ij_classname);
    J(".super java/lang/Object\n\n");

    /* Static fields: byte flags */
    J(".field public static icn_failed B\n");
    J(".field public static icn_suspended B\n");
    J(".field public static icn_suspend_id I\n");

    /* Long fields */
    J(".field public static icn_retval_obj Ljava/lang/Object;\n");
    J(".field public static icn_retval J\n");
    J(".field public static icn_retval_str Ljava/lang/String;\n");
    /* Per-user-proc arg static fields (already in statics array from emit_call) */
    for (int i = 0; i < ij_nstatics; i++) {
        char type = ij_static_types[i];
        if (type == 'A')
            J(".field public static %s Ljava/lang/String;\n", ij_statics[i]);
        else if (type == 'L' || type == 'R')
            J(".field public static %s Ljava/util/ArrayList;\n", ij_statics[i]);
        else if (type == 'T')
            J(".field public static %s Ljava/util/HashMap;\n", ij_statics[i]);
        else if (type == 'O')
            J(".field public static %s Ljava/lang/Object;\n", ij_statics[i]);
        else
            J(".field public static %s %c\n", ij_statics[i], type);
    }
    J("\n");

    /* Static initializer: set icn_subject = "" (not null), icn_pos = 0
     * Only emit if scan was used (icn_subject will be in statics). */
    int has_scan_fields = 0;
    for (int i = 0; i < ij_nstatics; i++)
        if (!strcmp(ij_statics[i], "icn_subject")) { has_scan_fields = 1; break; }
    if (has_scan_fields) {
        J(".method static <clinit>()V\n");
        J("    .limit stack 2\n    .limit locals 0\n");
        J("    ldc \"\"\n");
        J("    putstatic %s/icn_subject Ljava/lang/String;\n", ij_classname);
        J("    iconst_0\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    return\n");
        J(".end method\n\n");
    }

    /* main method: calls icn_main */
    J(".method public static main([Ljava/lang/String;)V\n");
    J("    .limit stack 4\n    .limit locals 1\n");
    J("    invokestatic %s/icn_main()V\n", ij_classname);
    J("    return\n");
    J(".end method\n\n");

    /* Emit built-in scan helper methods (only if cset/scan builtins are used).
     * icn_builtin_any(cs, subj, pos)      → long newpos (1-based) or -1L
     * icn_builtin_many(cs, subj, pos)     → long newpos (1-based) or -1L
     * icn_builtin_upto_step(cs, subj, pos)→ long matched-pos (1-based) or -1L
     *   upto_step scans forward from pos until it finds a char in cs,
     *   returns that 1-based position (caller sets icn_pos=result for next resume). */
    int need_scan_builtins = 0;
    for (int i = 0; i < ij_nstatics; i++)
        if (!strncmp(ij_statics[i], "icn_upto_cs_", 12)) { need_scan_builtins = 1; break; }
    for (int i = 0; i < ij_nstatics; i++)
        if (!strcmp(ij_statics[i], "icn_subject")) { need_scan_builtins = 1; break; }
    /* Also detect find usage (it uses icn_find_s1_N statics) */
    for (int i = 0; i < ij_nstatics; i++)
        if (!strncmp(ij_statics[i], "icn_find_s1_", 12)) { need_scan_builtins = 1; break; }
    if (need_scan_builtins) {
        /* icn_builtin_any(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_any(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    ; if pos >= subj.length() → return -1\n");
        J("    aload_1\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    iload_2\n");
        J("    if_icmpgt icn_any_inbounds\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_any_inbounds:\n");
        J("    ; cs.indexOf(subj.charAt(pos)) >= 0?\n");
        J("    aload_0\n");
        J("    aload_1\n    iload_2\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_any_match\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_any_match:\n");
        J("    ; return pos+2 (new 1-based pos after consuming one char)\n");
        J("    iload_2\n    iconst_2\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_many(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_many(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; check first char in cs — must match at least one\n");
        J("    aload_1\n");
        J("    invokevirtual java/lang/String/length()I\n");
        J("    iload_2\n");
        J("    if_icmpgt icn_many_first_ok\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_many_first_ok:\n");
        J("    aload_0\n    aload_1\n    iload_2\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_many_loop_init\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_many_loop_init:\n");
        J("    ; local 3 = pos (working)\n");
        J("    iload_2\n    istore_3\n");
        J("icn_many_loop:\n");
        J("    iinc 3 1\n");
        J("    ; if pos3 >= length → done\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpgt icn_many_cont\n");
        J("    iload_3\n    i2l\n    lconst_1\n    ladd\n    lreturn\n");
        J("icn_many_cont:\n");
        J("    aload_0\n    aload_1\n    iload_3\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_many_loop\n");
        J("    ; stopped — return pos3+1 (1-based)\n");
        J("    iload_3\n    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_upto_step(String cs, String subj, int pos) → long */
        J(".method public static icn_builtin_upto_step(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    iload_2\n    istore_3\n");
        J("icn_upto_scan:\n");
        J("    ; if pos3 >= length → -1\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpgt icn_upto_inbounds\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_upto_inbounds:\n");
        J("    aload_0\n    aload_1\n    iload_3\n");
        J("    invokevirtual java/lang/String/charAt(I)C\n");
        J("    invokevirtual java/lang/String/indexOf(I)I\n");
        J("    ifge icn_upto_found\n");
        J("    iinc 3 1\n    goto icn_upto_scan\n");
        J("icn_upto_found:\n");
        J("    ; return pos3+1 (1-based); caller sets icn_pos=result for next resume\n");
        J("    iload_3\n    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_find(String s1, String s2, int startpos) → long
         * Returns 1-based index of s1 in s2 starting from startpos (0-based), or -1L. */
        J(".method public static icn_builtin_find(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    aload_1\n    aload_0\n    iload_2\n");
        J("    invokevirtual java/lang/String/indexOf(Ljava/lang/String;I)I\n");
        J("    dup\n    ifge icn_find_ok\n");
        J("    pop\n    ldc2_w -1\n    lreturn\n");
        J("icn_find_ok:\n");
        J("    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_match(String s1, String subj, int pos) → long
         * If subj starts with s1 at pos, return pos+len(s1)+1 as 1-based new pos; else -1L. */
        J(".method public static icn_builtin_match(Ljava/lang/String;Ljava/lang/String;I)J\n");
        J("    .limit stack 4\n    .limit locals 3\n");
        J("    aload_1\n    aload_0\n    iload_2\n");
        J("    invokevirtual java/lang/String/startsWith(Ljava/lang/String;I)Z\n");
        J("    ifne icn_match_ok\n");
        J("    ldc2_w -1\n    lreturn\n");
        J("icn_match_ok:\n");
        J("    iload_2\n    aload_0\n    invokevirtual java/lang/String/length()I\n    iadd\n");
        J("    iconst_1\n    iadd\n    i2l\n    lreturn\n");
        J(".end method\n\n");

        /* icn_builtin_tab_str(int n, String subj, int pos) → String or null
         * n is 1-based target position.  end = n-1 (0-based).
         * Returns subj.substring(pos, end) and sets icn_pos = end; null on bounds failure. */
        J(".method public static icn_builtin_tab_str(ILjava/lang/String;I)Ljava/lang/String;\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; end = n-1 (0-based)\n");
        J("    iload_0\n    iconst_1\n    isub\n    istore_3\n");
        J("    ; fail if end < pos\n");
        J("    iload_3\n    iload_2\n    if_icmpge icn_tab_posok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_tab_posok:\n");
        J("    ; fail if end > subj.length()\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpge icn_tab_lenok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_tab_lenok:\n");
        J("    ; update icn_pos = end (static field)\n");
        J("    iload_3\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    ; return subj.substring(pos, end)\n");
        J("    aload_1\n    iload_2\n    iload_3\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");

        /* icn_builtin_move_str(int n, String subj, int pos) → String or null
         * Returns subj.substring(pos, pos+n) and sets icn_pos = pos+n; null if out of bounds. */
        J(".method public static icn_builtin_move_str(ILjava/lang/String;I)Ljava/lang/String;\n");
        J("    .limit stack 4\n    .limit locals 4\n");
        J("    ; end = pos+n\n");
        J("    iload_2\n    iload_0\n    iadd\n    istore_3\n");
        J("    ; fail if end > subj.length()\n");
        J("    aload_1\n    invokevirtual java/lang/String/length()I\n");
        J("    iload_3\n    if_icmpge icn_move_ok\n");
        J("    aconst_null\n    areturn\n");
        J("icn_move_ok:\n");
        J("    ; update icn_pos = end\n");
        J("    iload_3\n");
        J("    putstatic %s/icn_pos I\n", ij_classname);
        J("    ; return subj.substring(pos, end)\n");
        J("    aload_1\n    iload_2\n    iload_3\n");
        J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
        J("    areturn\n");
        J(".end method\n\n");
    }

    /* === Emit M-IJ-BUILTINS-STR helper methods (always emitted) === */

    /* icn_builtin_left(String s, int n, String pad) → String */
    J(".method public static icn_builtin_left(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 4\n    .limit locals 5\n");
    /* if s.length() >= n: return s.substring(0,n) */
    J("    aload_0\n    invokevirtual java/lang/String/length()I\n");
    J("    iload_1\n    if_icmplt icn_left_pad\n");
    J("    aload_0\n    iconst_0\n    iload_1\n");
    J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
    J("    areturn\n");
    J("icn_left_pad:\n");
    /* build: sb = new StringBuilder(s); while sb.length()<n: sb.append(pad.charAt(0)) */
    J("    new java/lang/StringBuilder\n    dup\n    aload_0\n");
    J("    invokespecial java/lang/StringBuilder/<init>(Ljava/lang/String;)V\n");
    J("    astore_3\n");
    J("icn_left_loop:\n");
    J("    aload_3\n    invokevirtual java/lang/StringBuilder/length()I\n");
    J("    iload_1\n    if_icmpge icn_left_done\n");
    J("    aload_3\n    aload_2\n    iconst_0\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n");
    J("    pop\n");
    J("    goto icn_left_loop\n");
    J("icn_left_done:\n");
    J("    aload_3\n    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* icn_builtin_right(String s, int n, String pad) → String */
    J(".method public static icn_builtin_right(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 4\n    .limit locals 5\n");
    J("    aload_0\n    invokevirtual java/lang/String/length()I\n");
    J("    iload_1\n    if_icmplt icn_right_pad\n");
    /* truncate: return s.substring(s.length()-n) */
    J("    aload_0\n    invokevirtual java/lang/String/length()I\n");
    J("    iload_1\n    isub\n");
    J("    aload_0\n    swap\n");
    J("    invokevirtual java/lang/String/substring(I)Ljava/lang/String;\n");
    J("    areturn\n");
    J("icn_right_pad:\n");
    /* pad left: build pad prefix then append s */
    J("    new java/lang/StringBuilder\n    dup\n");
    J("    invokespecial java/lang/StringBuilder/<init>()V\n");
    J("    astore_3\n");
    /* fill until length = n - s.length() */
    J("    iload_1\n    aload_0\n    invokevirtual java/lang/String/length()I\n    isub\n    istore 4\n");
    J("    iconst_0\n");  /* i=0 */
    J("icn_right_loop:\n");
    J("    dup\n    iload 4\n    if_icmpge icn_right_append\n");
    J("    aload_3\n    aload_2\n    iconst_0\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n    pop\n");
    J("    iconst_1\n    iadd\n    goto icn_right_loop\n");
    J("icn_right_append:\n    pop\n");
    J("    aload_3\n    aload_0\n");
    J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n    pop\n");
    J("    aload_3\n    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* icn_builtin_center(String s, int n, String pad) → String
     * pad_left = (n - s.length()) / 2  (integer division, truncate)
     * pad_right = n - s.length() - pad_left */
    J(".method public static icn_builtin_center(Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 5\n    .limit locals 7\n");
    J("    aload_0\n    invokevirtual java/lang/String/length()I\n");
    J("    iload_1\n    if_icmplt icn_ctr_pad\n");
    /* truncate */
    J("    aload_0\n    iconst_0\n    iload_1\n");
    J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n    areturn\n");
    J("icn_ctr_pad:\n");
    /* total_pad = n - s.length() */
    J("    iload_1\n    aload_0\n    invokevirtual java/lang/String/length()I\n    isub\n    istore_3\n");
    /* pad_left = total_pad / 2 */
    J("    iload_3\n    iconst_2\n    idiv\n    istore 4\n");
    /* pad_right = total_pad - pad_left */
    J("    iload_3\n    iload 4\n    isub\n    istore 5\n");
    J("    new java/lang/StringBuilder\n    dup\n");
    J("    invokespecial java/lang/StringBuilder/<init>()V\n    astore 6\n");
    /* emit pad_left pad chars */
    J("    iconst_0\n");
    J("icn_ctr_lloop:\n    dup\n    iload 4\n    if_icmpge icn_ctr_mid\n");
    J("    aload 6\n    aload_2\n    iconst_0\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n    pop\n");
    J("    iconst_1\n    iadd\n    goto icn_ctr_lloop\n");
    J("icn_ctr_mid:\n    pop\n");
    /* append s */
    J("    aload 6\n    aload_0\n");
    J("    invokevirtual java/lang/StringBuilder/append(Ljava/lang/String;)Ljava/lang/StringBuilder;\n    pop\n");
    /* emit pad_right pad chars */
    J("    iconst_0\n");
    J("icn_ctr_rloop:\n    dup\n    iload 5\n    if_icmpge icn_ctr_done\n");
    J("    aload 6\n    aload_2\n    iconst_0\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n    pop\n");
    J("    iconst_1\n    iadd\n    goto icn_ctr_rloop\n");
    J("icn_ctr_done:\n    pop\n");
    J("    aload 6\n    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* icn_builtin_trim(String s, String cs) → String  (remove trailing chars in cs) */
    J(".method public static icn_builtin_trim(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 4\n    .limit locals 3\n");
    /* end = s.length() */
    J("    aload_0\n    invokevirtual java/lang/String/length()I\n    istore_2\n");
    J("icn_trim_loop:\n");
    J("    iload_2\n    ifle icn_trim_done\n");
    /* check cs.indexOf(s.charAt(end-1)) >= 0 */
    J("    aload_1\n    aload_0\n    iload_2\n    iconst_1\n    isub\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/String/indexOf(I)I\n");
    J("    iflt icn_trim_done\n");
    J("    iinc 2 -1\n    goto icn_trim_loop\n");
    J("icn_trim_done:\n");
    J("    aload_0\n    iconst_0\n    iload_2\n");
    J("    invokevirtual java/lang/String/substring(II)Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* icn_builtin_map(String s, String src, String dst) → String */
    J(".method public static icn_builtin_map(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;\n");
    J("    .limit stack 5\n    .limit locals 7\n");
    J("    new java/lang/StringBuilder\n    dup\n");
    J("    invokespecial java/lang/StringBuilder/<init>()V\n    astore_3\n");
    J("    iconst_0\n    istore 4\n");  /* i = 0 */
    J("icn_map_loop:\n");
    J("    iload 4\n    aload_0\n    invokevirtual java/lang/String/length()I\n");
    J("    if_icmpge icn_map_done\n");
    J("    aload_0\n    iload 4\n    invokevirtual java/lang/String/charAt(I)C\n    istore 5\n");
    J("    aload_1\n    iload 5\n    invokevirtual java/lang/String/indexOf(I)I\n    istore 6\n");
    J("    iload 6\n    iflt icn_map_notfound\n");
    /* found: append dst.charAt(idx) */
    J("    iload 6\n    aload_2\n    invokevirtual java/lang/String/length()I\n");
    J("    if_icmpge icn_map_notfound\n");
    J("    aload_3\n    aload_2\n    iload 6\n");
    J("    invokevirtual java/lang/String/charAt(I)C\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n    pop\n");
    J("    goto icn_map_next\n");
    J("icn_map_notfound:\n");
    J("    aload_3\n    iload 5\n");
    J("    invokevirtual java/lang/StringBuilder/append(C)Ljava/lang/StringBuilder;\n    pop\n");
    J("icn_map_next:\n");
    J("    iinc 4 1\n    goto icn_map_loop\n");
    J("icn_map_done:\n");
    J("    aload_3\n    invokevirtual java/lang/StringBuilder/toString()Ljava/lang/String;\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* === M-IJ-BUILTINS-TYPE helpers === */

    /* icn_builtin_numeric(String s) → long
     * Tries Long.parseLong; returns Long.MIN_VALUE sentinel on failure. */
    J(".method public static icn_builtin_numeric(Ljava/lang/String;)J\n");
    J("    .limit stack 2\n    .limit locals 2\n");
    J("    .catch java/lang/NumberFormatException from icn_num_L0 to icn_num_L1 using icn_num_catch\n");
    J("icn_num_L0:\n");
    J("    aload_0\n");
    J("    invokestatic java/lang/Long/parseLong(Ljava/lang/String;)J\n");
    J("icn_num_L1:\n");
    J("    lreturn\n");
    J("icn_num_catch:\n");
    J("    pop\n");
    J("    ldc2_w -9223372036854775808\n");
    J("    lreturn\n");
    J(".end method\n\n");

    /* === END M-IJ-BUILTINS-TYPE helpers === */

    /* === M-IJ-SORT helpers === */

    /* icn_builtin_sort(ArrayList src) → ArrayList
     * Copies src into a new ArrayList, sorts ascending by Long value
     * using insertion sort (no Comparator class needed in Jasmin).
     * Stack discipline: local 0 = src, local 1 = result, local 2 = i (int),
     *   local 3 = j (int), local 4-5 = key (long). */
    J(".method public static icn_builtin_sort(Ljava/util/ArrayList;)Ljava/util/ArrayList;\n");
    J("    .limit stack 6\n    .limit locals 8\n");
    /* result = new ArrayList(src) */
    J("    new java/util/ArrayList\n");
    J("    dup\n");
    J("    aload_0\n");
    J("    invokespecial java/util/ArrayList/<init>(Ljava/util/Collection;)V\n");
    J("    astore_1\n");
    /* i = 1 */
    J("    iconst_1\n");
    J("    istore_2\n");
    /* outer loop: while i < result.size() */
    J("icn_sort_outer:\n");
    J("    aload_1\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    iload_2\n");
    J("    if_icmple icn_sort_done\n");
    /* key = ((Long)result.get(i)).longValue() */
    J("    aload_1\n");
    J("    iload_2\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast java/lang/Long\n");
    J("    invokevirtual java/lang/Long/longValue()J\n");
    J("    lstore 4\n");
    /* j = i - 1 */
    J("    iload_2\n");
    J("    iconst_1\n");
    J("    isub\n");
    J("    istore_3\n");
    /* inner loop: while j >= 0 && result.get(j) > key */
    J("icn_sort_inner:\n");
    J("    iload_3\n");
    J("    iflt icn_sort_insert\n");
    J("    aload_1\n");
    J("    iload_3\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    checkcast java/lang/Long\n");
    J("    invokevirtual java/lang/Long/longValue()J\n");
    J("    lload 4\n");
    J("    lcmp\n");
    J("    ifle icn_sort_insert\n");
    /* result.set(j+1, result.get(j)) */
    J("    aload_1\n");
    J("    iload_3\n");
    J("    iconst_1\n");
    J("    iadd\n");
    J("    aload_1\n");
    J("    iload_3\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    invokevirtual java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;\n");
    J("    pop\n");
    /* j-- */
    J("    iinc 3 -1\n");
    J("    goto icn_sort_inner\n");
    /* result.set(j+1, Long(key)) */
    J("icn_sort_insert:\n");
    J("    aload_1\n");
    J("    iload_3\n");
    J("    iconst_1\n");
    J("    iadd\n");
    J("    lload 4\n");
    J("    invokestatic java/lang/Long/valueOf(J)Ljava/lang/Long;\n");
    J("    invokevirtual java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;\n");
    J("    pop\n");
    /* i++ */
    J("    iinc 2 1\n");
    J("    goto icn_sort_outer\n");
    J("icn_sort_done:\n");
    J("    aload_1\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* icn_builtin_sortf(ArrayList src, int fieldIdx) → ArrayList
     * Sorts list of records by 1-based fieldIdx using insertion sort.
     * Records are Objects; fields are accessed by reflection (getDeclaredFields()[fieldIdx-1]).
     * local 0=src, 1=fieldIdx, 2=result, 3=i, 4=j, 5=keyObj, 6=tmpObj */
    J(".method public static icn_builtin_sortf(Ljava/util/ArrayList;I)Ljava/util/ArrayList;\n");
    J("    .limit stack 8\n    .limit locals 10\n");
    /* result = new ArrayList(src) */
    J("    new java/util/ArrayList\n");
    J("    dup\n");
    J("    aload_0\n");
    J("    invokespecial java/util/ArrayList/<init>(Ljava/util/Collection;)V\n");
    J("    astore_2\n");
    /* i = 1 */
    J("    iconst_1\n");
    J("    istore_3\n");
    /* outer: while i < result.size() */
    J("icn_sortf_outer:\n");
    J("    aload_2\n");
    J("    invokevirtual java/util/ArrayList/size()I\n");
    J("    iload_3\n");
    J("    if_icmple icn_sortf_done\n");
    /* keyObj = result.get(i) */
    J("    aload_2\n");
    J("    iload_3\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    astore 5\n");
    /* j = i-1 */
    J("    iload_3\n    iconst_1\n    isub\n    istore 4\n");
    /* inner: while j>=0 && cmpField(result[j], keyObj, fieldIdx) > 0 */
    J("icn_sortf_inner:\n");
    J("    iload 4\n");
    J("    iflt icn_sortf_insert\n");
    /* get field value of result[j] */
    J("    aload_2\n    iload 4\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    astore 6\n");
    /* reflect: result[j].getClass().getDeclaredFields()[fieldIdx-1].get(result[j]) */
    J("    aload 6\n");
    J("    invokevirtual java/lang/Object/getClass()Ljava/lang/Class;\n");
    J("    invokevirtual java/lang/Class/getDeclaredFields()[Ljava/lang/reflect/Field;\n");
    J("    iload_1\n    iconst_1\n    isub\n    aaload\n");
    J("    aload 6\n");
    J("    invokevirtual java/lang/reflect/Field/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("    astore 7\n");
    /* same for keyObj */
    J("    aload 5\n");
    J("    invokevirtual java/lang/Object/getClass()Ljava/lang/Class;\n");
    J("    invokevirtual java/lang/Class/getDeclaredFields()[Ljava/lang/reflect/Field;\n");
    J("    iload_1\n    iconst_1\n    isub\n    aaload\n");
    J("    aload 5\n");
    J("    invokevirtual java/lang/reflect/Field/get(Ljava/lang/Object;)Ljava/lang/Object;\n");
    J("    astore 8\n");
    /* compare as Long: if local7 (Long) > local8 (Long) → shift */
    J("    aload 7\n");
    J("    checkcast java/lang/Long\n");
    J("    aload 8\n");
    J("    checkcast java/lang/Long\n");
    J("    invokevirtual java/lang/Long/compareTo(Ljava/lang/Long;)I\n");
    J("    ifle icn_sortf_insert\n");
    /* result.set(j+1, result.get(j)) */
    J("    aload_2\n    iload 4\n    iconst_1\n    iadd\n");
    J("    aload_2\n    iload 4\n");
    J("    invokevirtual java/util/ArrayList/get(I)Ljava/lang/Object;\n");
    J("    invokevirtual java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;\n");
    J("    pop\n");
    J("    iinc 4 -1\n");
    J("    goto icn_sortf_inner\n");
    J("icn_sortf_insert:\n");
    J("    aload_2\n    iload 4\n    iconst_1\n    iadd\n    aload 5\n");
    J("    invokevirtual java/util/ArrayList/set(ILjava/lang/Object;)Ljava/lang/Object;\n");
    J("    pop\n");
    J("    iinc 3 1\n");
    J("    goto icn_sortf_outer\n");
    J("icn_sortf_done:\n");
    J("    aload_2\n");
    J("    areturn\n");
    J(".end method\n\n");

    /* === END M-IJ-BUILTINS-STR helpers === */

    /* Emit all procedure methods */
    fputs(procs_text, jout);
    free(procs_text);

    /* Emit record inner classes as separate .j files */
    for (int ri = 0; ri < ij_nrec; ri++) {
        /* Derive sibling filename: replace main .j basename with ClassName$RecordName.j */
        char rec_jpath[512] = "/tmp/icn_record_tmp.j";
        if (outpath) {
            /* Copy outpath, replace basename with ClassName$RecordName.j */
            const char *slash = strrchr(outpath, '/');
            size_t dirlen = slash ? (size_t)(slash - outpath + 1) : 0;
            snprintf(rec_jpath, sizeof rec_jpath, "%.*s%s$%s.j",
                     (int)dirlen, outpath, ij_classname, ij_rec_names[ri]);
        } else {
            snprintf(rec_jpath, sizeof rec_jpath, "/tmp/%s$%s.j",
                     ij_classname, ij_rec_names[ri]);
        }
        FILE *rf = fopen(rec_jpath, "w");
        if (rf) {
            ij_emit_record_class(ij_rec_names[ri], ij_rec_nfields[ri],
                                 (const char (*)[64])ij_rec_fields[ri], rf);
            fclose(rf);
        }
    }

    jout = save;
}
