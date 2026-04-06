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
    return p;
}

void sm_prog_free(SM_Program *p)
{
    if (!p) return;
    free(p->instrs);
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
    p->instrs[idx].a[0].s = s;
    return idx;
}

int sm_emit_i(SM_Program *p, sm_opcode_t op, int64_t i)
{
    int idx = _grow(p);
    p->instrs[idx].op      = op;
    p->instrs[idx].a[0].i  = i;
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
    p->instrs[idx].a[0].s  = s;
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

static const char *opnames[SM_OPCODE_COUNT] = {
    "SM_LABEL","SM_JUMP","SM_JUMP_S","SM_JUMP_F","SM_HALT",
    "SM_PUSH_LIT_S","SM_PUSH_LIT_I","SM_PUSH_LIT_F","SM_PUSH_NULL",
    "SM_PUSH_VAR","SM_STORE_VAR","SM_POP",
    "SM_ADD","SM_SUB","SM_MUL","SM_DIV","SM_EXP","SM_CONCAT","SM_NEG",
    "SM_PAT_LIT","SM_PAT_ANY","SM_PAT_NOTANY","SM_PAT_SPAN","SM_PAT_BREAK",
    "SM_PAT_LEN","SM_PAT_POS","SM_PAT_RPOS","SM_PAT_TAB","SM_PAT_RTAB",
    "SM_PAT_ARB","SM_PAT_REM","SM_PAT_BAL","SM_PAT_FENCE","SM_PAT_ABORT",
    "SM_PAT_FAIL","SM_PAT_SUCCEED","SM_PAT_EPS","SM_PAT_ALT","SM_PAT_CAT",
    "SM_PAT_DEREF","SM_PAT_CAPTURE",
    "SM_EXEC_STMT",
    "SM_CALL","SM_RETURN","SM_FRETURN","SM_DEFINE",
    "SM_JUMP_INDIR","SM_SELBRA",
    "SM_STATE_PUSH","SM_STATE_POP",
    "SM_INCR","SM_DECR","SM_LCOMP","SM_RCOMP","SM_TRIM","SM_ACOMP",
    "SM_SPCINT","SM_SPREAL",
};

const char *sm_opcode_name(sm_opcode_t op)
{
    if (op >= 0 && op < SM_OPCODE_COUNT) return opnames[op];
    return "SM_UNKNOWN";
}
