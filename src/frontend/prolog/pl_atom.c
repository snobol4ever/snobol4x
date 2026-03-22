/*
 * pl_atom.c — atom interning table for the Prolog frontend
 *
 * Uses a simple open-addressing hash table for O(1) average lookup,
 * plus a flat id→name array for O(1) reverse lookup.
 */

#include "pl_atom.h"
#include "term.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Well-known atom IDs (definitions — declared extern in term.h)
 * ---------------------------------------------------------------------- */
int ATOM_DOT  = -1;
int ATOM_NIL  = -1;
int ATOM_TRUE = -1;
int ATOM_FAIL = -1;
int ATOM_CUT  = -1;

/* -------------------------------------------------------------------------
 * Internal storage
 * ---------------------------------------------------------------------- */
#define ATOM_INIT_CAP  256

static char  **atom_names = NULL;   /* id -> heap-allocated name string */
static int     atom_len   = 0;      /* number of interned atoms          */
static int     atom_cap   = 0;      /* capacity of atom_names[]          */

/* Hash table: maps string -> id.  Open addressing, linear probe. */
#define HT_INIT_SIZE  512           /* must be power of two              */

typedef struct { char *key; int id; } HEntry;
static HEntry *ht      = NULL;
static int     ht_size = 0;
static int     ht_used = 0;

static unsigned int ht_hash(const char *s) {
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

/* Grow hash table to new_size (must be power of two). */
static void ht_grow(int new_size) {
    HEntry *old = ht;
    int     old_size = ht_size;
    ht = calloc(new_size, sizeof(HEntry));
    ht_size = new_size;
    ht_used = 0;
    for (int i = 0; i < old_size; i++) {
        if (!old[i].key) continue;
        unsigned int h = ht_hash(old[i].key) & (ht_size - 1);
        while (ht[h].key) h = (h + 1) & (ht_size - 1);
        ht[h] = old[i];
        ht_used++;
    }
    free(old);
}

/* -------------------------------------------------------------------------
 * pl_atom_intern
 * ---------------------------------------------------------------------- */
int pl_atom_intern(const char *name) {
    if (!name) name = "";

    /* Lazy init */
    if (!ht) {
        ht_size = HT_INIT_SIZE;
        ht = calloc(ht_size, sizeof(HEntry));
    }
    if (!atom_names) {
        atom_cap  = ATOM_INIT_CAP;
        atom_names = malloc(atom_cap * sizeof(char *));
    }

    /* Lookup */
    unsigned int h = ht_hash(name) & (ht_size - 1);
    while (ht[h].key) {
        if (strcmp(ht[h].key, name) == 0) return ht[h].id;
        h = (h + 1) & (ht_size - 1);
    }

    /* Insert */
    if (ht_used * 2 >= ht_size) {
        ht_grow(ht_size * 2);
        /* Recompute probe position after rehash */
        h = ht_hash(name) & (ht_size - 1);
        while (ht[h].key) h = (h + 1) & (ht_size - 1);
    }

    if (atom_len >= atom_cap) {
        atom_cap *= 2;
        atom_names = realloc(atom_names, atom_cap * sizeof(char *));
    }

    char *copy = strdup(name);
    int   id   = atom_len++;
    atom_names[id] = copy;
    ht[h].key = copy;
    ht[h].id  = id;
    ht_used++;
    return id;
}

/* -------------------------------------------------------------------------
 * pl_atom_name
 * ---------------------------------------------------------------------- */
const char *pl_atom_name(int id) {
    if (id < 0 || id >= atom_len) return NULL;
    return atom_names[id];
}

/* -------------------------------------------------------------------------
 * pl_atom_count
 * ---------------------------------------------------------------------- */
int pl_atom_count(void) { return atom_len; }

/* -------------------------------------------------------------------------
 * pl_atom_init — intern well-known atoms and populate globals
 * ---------------------------------------------------------------------- */
void pl_atom_init(void) {
    ATOM_DOT  = pl_atom_intern(".");
    ATOM_NIL  = pl_atom_intern("[]");
    ATOM_TRUE = pl_atom_intern("true");
    ATOM_FAIL = pl_atom_intern("fail");
    ATOM_CUT  = pl_atom_intern("!");
}

/* -------------------------------------------------------------------------
 * Term constructors (live here because they need atom_* internals only
 * for the compound args allocation; no atom lookups required)
 * ---------------------------------------------------------------------- */
#include <stddef.h>

Term *term_new_atom(int atom_id) {
    Term *t = calloc(1, sizeof(Term));
    t->tag     = TT_ATOM;
    t->atom_id = atom_id;
    return t;
}

Term *term_new_var(int var_slot) {
    Term *t = calloc(1, sizeof(Term));
    t->tag        = TT_VAR;
    t->var_slot   = var_slot;
    t->saved_slot = var_slot;   /* preserved across bind() / trail_unwind() */
    return t;
}

Term *term_new_compound(int functor, int arity, Term **args) {
    Term *t = calloc(1, sizeof(Term));
    t->tag              = TT_COMPOUND;
    t->compound.functor = functor;
    t->compound.arity   = arity;
    if (arity > 0 && args) {
        t->compound.args = malloc(arity * sizeof(Term *));
        memcpy(t->compound.args, args, arity * sizeof(Term *));
    } else {
        t->compound.args = NULL;
    }
    return t;
}

Term *term_new_int(long ival) {
    Term *t = calloc(1, sizeof(Term));
    t->tag  = TT_INT;
    t->ival = ival;
    return t;
}

Term *term_new_float(double fval) {
    Term *t = calloc(1, sizeof(Term));
    t->tag  = TT_FLOAT;
    t->fval = fval;
    return t;
}
