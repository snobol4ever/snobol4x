/*
 * icon_ast.c — IcnNode constructors and diagnostics
 */

#include "icon_ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

IcnNode *icn_node_new(IcnKind kind, int line, int nchildren, ...) {
    IcnNode *n = calloc(1, sizeof(IcnNode));
    n->kind = kind;
    n->line = line;
    n->nchildren = nchildren;
    if (nchildren > 0) {
        n->children = malloc(nchildren * sizeof(IcnNode*));
        va_list ap;
        va_start(ap, nchildren);
        for (int i = 0; i < nchildren; i++)
            n->children[i] = va_arg(ap, IcnNode*);
        va_end(ap);
    }
    return n;
}

IcnNode *icn_leaf_int(int line, long ival) {
    IcnNode *n = calloc(1, sizeof(IcnNode));
    n->kind = ICN_INT; n->line = line; n->val.ival = ival;
    return n;
}

IcnNode *icn_leaf_real(int line, double fval) {
    IcnNode *n = calloc(1, sizeof(IcnNode));
    n->kind = ICN_REAL; n->line = line; n->val.fval = fval;
    return n;
}

IcnNode *icn_leaf_str(IcnKind kind, int line, const char *s, size_t len) {
    IcnNode *n = calloc(1, sizeof(IcnNode));
    n->kind = kind; n->line = line;
    n->val.sval = malloc(len + 1);
    memcpy(n->val.sval, s, len);
    n->val.sval[len] = '\0';
    return n;
}

IcnNode *icn_leaf_var(int line, const char *name) {
    return icn_leaf_str(ICN_VAR, line, name, strlen(name));
}

void icn_node_free(IcnNode *n) {
    if (!n) return;
    for (int i = 0; i < n->nchildren; i++)
        icn_node_free(n->children[i]);
    free(n->children);
    if (n->kind == ICN_STR || n->kind == ICN_CSET || n->kind == ICN_VAR)
        free(n->val.sval);
    free(n);
}

const char *icn_kind_name(IcnKind kind) {
    switch (kind) {
        case ICN_INT:      return "INT";
        case ICN_REAL:     return "REAL";
        case ICN_STR:      return "STR";
        case ICN_CSET:     return "CSET";
        case ICN_VAR:      return "VAR";
        case ICN_ADD:      return "ADD";
        case ICN_SUB:      return "SUB";
        case ICN_MUL:      return "MUL";
        case ICN_DIV:      return "DIV";
        case ICN_MOD:      return "MOD";
        case ICN_POW:      return "POW";
        case ICN_NEG:      return "NEG";
        case ICN_LT:       return "LT";
        case ICN_LE:       return "LE";
        case ICN_GT:       return "GT";
        case ICN_GE:       return "GE";
        case ICN_EQ:       return "EQ";
        case ICN_NE:       return "NE";
        case ICN_SLT:      return "SLT";
        case ICN_SLE:      return "SLE";
        case ICN_SGT:      return "SGT";
        case ICN_SGE:      return "SGE";
        case ICN_SEQ:      return "SEQ";
        case ICN_SNE:      return "SNE";
        case ICN_CONCAT:   return "CONCAT";
        case ICN_LCONCAT:  return "LCONCAT";
        case ICN_TO:       return "TO";
        case ICN_TO_BY:    return "TO_BY";
        case ICN_ALT:      return "ALT";
        case ICN_AND:      return "AND";
        case ICN_BANG:     return "BANG";
        case ICN_SIZE:     return "SIZE";
        case ICN_LIMIT:    return "LIMIT";
        case ICN_NOT:      return "NOT";
        case ICN_NONNULL:  return "NONNULL";
        case ICN_NULL:     return "NULL";
        case ICN_SEQ_EXPR: return "SEQ_EXPR";
        case ICN_EVERY:    return "EVERY";
        case ICN_WHILE:    return "WHILE";
        case ICN_UNTIL:    return "UNTIL";
        case ICN_REPEAT:   return "REPEAT";
        case ICN_IF:       return "IF";
        case ICN_CASE:     return "CASE";
        case ICN_ASSIGN:   return "ASSIGN";
        case ICN_AUGOP:    return "AUGOP";
        case ICN_SWAP:     return "SWAP";
        case ICN_SCAN:     return "SCAN";
        case ICN_SCAN_AUGOP: return "SCAN_AUGOP";
        case ICN_CALL:     return "CALL";
        case ICN_RETURN:   return "RETURN";
        case ICN_SUSPEND:  return "SUSPEND";
        case ICN_FAIL:     return "FAIL";
        case ICN_BREAK:    return "BREAK";
        case ICN_NEXT:     return "NEXT";
        case ICN_PROC:     return "PROC";
        case ICN_FIELD:    return "FIELD";
        case ICN_SUBSCRIPT:return "SUBSCRIPT";
        case ICN_SECTION:  return "SECTION";
        case ICN_MAKELIST: return "MAKELIST";
        case ICN_RECORD:   return "RECORD";
        case ICN_GLOBAL:   return "GLOBAL";
        case ICN_INITIAL:  return "INITIAL";
        default:           return "???";
    }
}

/* Append one child to an existing node (used to flatten n-ary nodes). */
void icn_node_append(IcnNode *parent, IcnNode *child) {
    parent->children = realloc(parent->children,
                               (parent->nchildren + 1) * sizeof(IcnNode *));
    parent->children[parent->nchildren++] = child;
}

void icn_node_dump(IcnNode *n, int indent) {
    if (!n) { fprintf(stderr, "%*s(null)\n", indent*2, ""); return; }
    switch (n->kind) {
        case ICN_INT:
            fprintf(stderr, "%*s(INT %ld)\n", indent*2, "", n->val.ival); break;
        case ICN_REAL:
            fprintf(stderr, "%*s(REAL %g)\n", indent*2, "", n->val.fval); break;
        case ICN_STR:
            fprintf(stderr, "%*s(STR \"%s\")\n", indent*2, "", n->val.sval); break;
        case ICN_CSET:
            fprintf(stderr, "%*s(CSET '%s')\n", indent*2, "", n->val.sval); break;
        case ICN_VAR:
            fprintf(stderr, "%*s(VAR %s)\n", indent*2, "", n->val.sval); break;
        default:
            fprintf(stderr, "%*s(%s\n", indent*2, "", icn_kind_name(n->kind));
            for (int i = 0; i < n->nchildren; i++)
                icn_node_dump(n->children[i], indent+1);
            fprintf(stderr, "%*s)\n", indent*2, ""); break;
    }
}
