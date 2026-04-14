/*
 * sm_prog.c — SM_Program builder (M-SCRIP-U2)
 */

#include "sm_prog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SM_Program *sm_prog_new(void)
{
    SM_Program *p = calloc(1, sizeof *p);
    p->cap    = 64;
    p->instrs = calloc((size_t)p->cap, sizeof(SM_Instr));
    /* IM-9: stno_labels[0] unused; pre-allocate 64 statement slots */
    p->stno_labels_cap = 64;
    p->stno_labels     = calloc((size_t)p->stno_labels_cap, sizeof(const char *));
    p->stno_count      = 0;
    return p;
}

void sm_prog_free(SM_Program *p)
{
    if (!p) return;
    free(p->instrs);
    free(p->stno_labels);
    free(p);
}

static int _grow(SM_Program *p)
{
    if (p->count >= p->cap) {
        p->cap *= 2;
        p->instrs = realloc(p->instrs, (size_t)p->cap * sizeof(SM_Instr));
    }
    int idx = p->count++;
    memset(&p->instrs[idx], 0, sizeof(SM_Instr));
    p->instrs[idx].op = (sm_opcode_t)0;
    return idx;
}

int sm_emit(SM_Program *p, sm_opcode_t op)
{
    int idx = _grow(p);
    p->instrs[idx].op = op;
    return idx;
}

int sm_emit_s(SM_Program *p, sm_opcode_t op, const char *s)
{
    int idx = _grow(p);
    p->instrs[idx].op   = op;
    p->instrs[idx].a[0].s = s ? strdup(s) : NULL;
    return idx;
}

int sm_emit_i(SM_Program *p, sm_opcode_t op, int64_t i)
{
    int idx = _grow(p);
    p->instrs[idx].op      = op;
    p->instrs[idx].a[0].i  = i;
    return idx;
}

int sm_emit_ptr(SM_Program *p, sm_opcode_t op, void *ptr)
{
    int idx = _grow(p);
    p->instrs[idx].op        = op;
    p->instrs[idx].a[0].ptr  = ptr;
    return idx;
}


int sm_emit_f(SM_Program *p, sm_opcode_t op, double f)
{
    int idx = _grow(p);
    p->instrs[idx].op      = op;
    p->instrs[idx].a[0].f  = f;
    return idx;
}

int sm_emit_si(SM_Program *p, sm_opcode_t op, const char *s, int64_t i)
{
    int idx = _grow(p);
    p->instrs[idx].op      = op;
    p->instrs[idx].a[0].s  = s ? strdup(s) : NULL;
    p->instrs[idx].a[1].i  = i;
    return idx;
}

int sm_emit_ii(SM_Program *p, sm_opcode_t op, int64_t i0, int64_t i1)
{
    int idx = _grow(p);
    p->instrs[idx].op      = op;
    p->instrs[idx].a[0].i  = i0;
    p->instrs[idx].a[1].i  = i1;
    return idx;
}

int sm_label(SM_Program *p)
{
    int target = p->count;   /* index of *next* instruction after this label */
    int idx = _grow(p);
    p->instrs[idx].op      = SM_LABEL;
    p->instrs[idx].a[0].i  = (int64_t)target;
    return target;
}

void sm_patch_jump(SM_Program *p, int jump_idx, int target_label)
{
    p->instrs[jump_idx].a[0].i = (int64_t)target_label;
}

/* IM-9: record source label for statement stno (1-based).
 * label may be NULL (unlabelled statement). String is not copied — caller
 * owns the lifetime (interned in STMT_t which lives for the program). */
void sm_stno_label_record(SM_Program *p, int stno, const char *label)
{
    if (stno <= 0) return;
    if (stno >= p->stno_labels_cap) {
        int newcap = p->stno_labels_cap * 2;
        while (newcap <= stno) newcap *= 2;
        p->stno_labels = realloc(p->stno_labels, (size_t)newcap * sizeof(const char *));
        for (int i = p->stno_labels_cap; i < newcap; i++) p->stno_labels[i] = NULL;
        p->stno_labels_cap = newcap;
    }
    p->stno_labels[stno] = label;
    if (stno > p->stno_count) p->stno_count = stno;
}

static const char *opnames[SM_OPCODE_COUNT] = {
    "SM_LABEL","SM_JUMP","SM_JUMP_S","SM_JUMP_F","SM_HALT",
    "SM_STNO",
    "SM_PUSH_LIT_S","SM_PUSH_LIT_I","SM_PUSH_LIT_F","SM_PUSH_NULL",
    "SM_PUSH_VAR","SM_PUSH_EXPR","SM_STORE_VAR","SM_POP",
    "SM_ADD","SM_SUB","SM_MUL","SM_DIV","SM_EXP","SM_CONCAT","SM_COERCE_NUM","SM_NEG",
    "SM_PAT_LIT","SM_PAT_ANY","SM_PAT_NOTANY","SM_PAT_SPAN","SM_PAT_BREAK",
    "SM_PAT_LEN","SM_PAT_POS","SM_PAT_RPOS","SM_PAT_TAB","SM_PAT_RTAB",
    "SM_PAT_ARB","SM_PAT_REM","SM_PAT_BAL","SM_PAT_FENCE","SM_PAT_ABORT",
    "SM_PAT_FAIL","SM_PAT_SUCCEED","SM_PAT_EPS","SM_PAT_ALT","SM_PAT_CAT",
    "SM_PAT_DEREF","SM_PAT_CAPTURE",
    "SM_EXEC_STMT",
    "SM_CALL","SM_RETURN","SM_FRETURN","SM_NRETURN","SM_DEFINE",
    "SM_JUMP_INDIR","SM_SELBRA",
    "SM_STATE_PUSH","SM_STATE_POP",
    "SM_INCR","SM_DECR","SM_LCOMP","SM_RCOMP","SM_TRIM","SM_ACOMP",
    "SM_SPCINT","SM_SPREAL",
    "SM_PAT_BOXVAL",
};

const char *sm_opcode_name(sm_opcode_t op)
{
    if (op >= 0 && op < SM_OPCODE_COUNT) return opnames[op];
    return "SM_UNKNOWN";
}

/* ── sm_prog_print — --dump-sm diagnostic ──────────────────────────────── */
#include <stdio.h>

void sm_prog_print(const SM_Program *p, FILE *out)
{
    if (!p) { fprintf(out, "(null SM_Program)\n"); return; }
    fprintf(out, "; SM_Program  count=%d\n", p->count);
    for (int i = 0; i < p->count; i++) {
        const SM_Instr *in = &p->instrs[i];
        const char *name = sm_opcode_name(in->op);
        fprintf(out, "%4d  %-20s", i, name);
        /* Print operands heuristically based on opcode */
        switch (in->op) {
            /* string operands */
            case SM_PUSH_LIT_S:
            case SM_PAT_LIT:
            case SM_PAT_ANY: case SM_PAT_NOTANY:
            case SM_PAT_SPAN: case SM_PAT_BREAK:
                fprintf(out, " s=\"%s\"", in->a[0].s ? in->a[0].s : "");
                break;
            /* int operand */
            case SM_PUSH_LIT_I:
            case SM_PAT_LEN: case SM_PAT_POS: case SM_PAT_RPOS:
            case SM_PAT_TAB: case SM_PAT_RTAB:
            case SM_INCR: case SM_DECR:
            case SM_LCOMP: case SM_RCOMP: case SM_TRIM:
                fprintf(out, " i=%lld", (long long)in->a[0].i);
                break;
            /* float operand */
            case SM_PUSH_LIT_F:
                fprintf(out, " f=%g", in->a[0].f);
                break;
            /* jump targets */
            case SM_JUMP:
            case SM_JUMP_S:
            case SM_JUMP_F:
                fprintf(out, " -> %lld", (long long)in->a[0].i);
                break;
            case SM_JUMP_INDIR:
                fprintf(out, " -> %lld", (long long)in->a[0].i);
                break;
            /* string + int */
            case SM_PUSH_VAR: case SM_STORE_VAR:
            case SM_PAT_CAPTURE: case SM_PAT_DEREF:
                if (in->a[0].s) fprintf(out, " s=\"%s\"", in->a[0].s);
                break;
            case SM_CALL:
                fprintf(out, " s=\"%s\" nargs=%lld",
                    in->a[0].s ? in->a[0].s : "?",
                    (long long)in->a[1].i);
                break;
            case SM_DEFINE:
                fprintf(out, " s=\"%s\"", in->a[0].s ? in->a[0].s : "?");
                break;
            case SM_EXEC_STMT:
                if (in->a[0].i) fprintf(out, " has_repl=%lld", (long long)in->a[1].i);
                if (in->a[0].s) fprintf(out, " subj=\"%s\"", in->a[0].s);
                break;
            case SM_STNO:
                fprintf(out, " stmt=%lld", (long long)in->a[0].i);
                break;
            default:
                break;
        }
        fprintf(out, "\n");
    }
}
