/* mock_engine.c — minimal recursive interpreter for dynamic pattern nodes.
 *
 * The compiled Byrd box path (sno2c -trampoline output) emits labeled-goto C
 * for all statically known patterns.  But beauty.sno constructs a small number
 * of pattern trees at runtime (e.g. `nl | ';'` terminators in pat_Command)
 * that reach engine_match_ex via match_pattern_at.
 *
 * This interpreter handles those cases.  It is NOT the full Byrd Box signal
 * protocol from engine.c — no PROCEED/SUCCEED/CONCEDE/RECEDE stacks.  It is
 * a simple recursive descent matcher sufficient for the node types beauty.sno
 * actually constructs at runtime.
 *
 * Node types covered:
 *   T_LITERAL, T_PI, T_SIGMA, T_EPSILON, T_POS, T_RPOS, T_LEN,
 *   T_TAB, T_RTAB, T_REM, T_SUCCEED, T_FAIL, T_ANY, T_NOTANY,
 *   T_SPAN, T_BREAK, T_VARREF
 *
 * Returns new cursor position (>= 0) on success, -1 on failure.
 * engine_match_ex wraps this into a MatchResult.
 */
#include "engine.h"
#include <string.h>
#include <stdlib.h>

/* Forward declaration */
static int match_node(const Pattern *p, const char *s, int slen,
                      int cursor, const EngineOpts *opts, int scan_start);

/* charset helpers */
static int in_chars(char c, const char *chars) {
    if (!chars) return 0;
    return strchr(chars, c) != NULL;
}

static int match_node(const Pattern *p, const char *s, int slen,
                      int cursor, const EngineOpts *opts, int scan_start)
{
    if (!p) return cursor;  /* NULL = epsilon */

    switch (p->type) {

    case T_EPSILON:
        return cursor;

    case T_SUCCEED:
        return cursor;

    case T_FAIL:
        return -1;

    case T_LITERAL: {
        int len = p->s_len > 0 ? p->s_len : (p->s ? (int)strlen(p->s) : 0);
        if (cursor + len > slen) return -1;
        if (len == 0) return cursor;
        if (memcmp(s + cursor, p->s, len) != 0) return -1;
        return cursor + len;
    }

    case T_ANY: {
        if (cursor >= slen) return -1;
        if (p->chars && !in_chars(s[cursor], p->chars)) return -1;
        return cursor + 1;
    }

    case T_NOTANY: {
        if (cursor >= slen) return -1;
        if (p->chars && in_chars(s[cursor], p->chars)) return -1;
        return cursor + 1;
    }

    case T_SPAN: {
        if (cursor >= slen) return -1;  /* must match at least one */
        int i = cursor;
        while (i < slen && in_chars(s[i], p->chars)) i++;
        if (i == cursor) return -1;
        return i;
    }

    case T_BREAK: {
        int i = cursor;
        while (i < slen && !in_chars(s[i], p->chars)) i++;
        return i;  /* zero-width match allowed */
    }

    case T_LEN: {
        int end = cursor + p->n;
        if (end > slen) return -1;
        return end;
    }

    case T_POS: {
        int abs_pos = scan_start + p->n;
        /* cursor is relative to subject passed to engine_match_ex;
         * scan_start anchors absolute position within the original string */
        if (cursor != p->n) return -1;
        (void)abs_pos;
        return cursor;
    }

    case T_RPOS: {
        if (cursor != slen - p->n) return -1;
        return cursor;
    }

    case T_TAB: {
        if (p->n < cursor) return -1;
        if (p->n > slen)   return -1;
        return p->n;
    }

    case T_RTAB: {
        int target = slen - p->n;
        if (target < cursor) return -1;
        return target;
    }

    case T_REM:
        return slen;

    case T_PI: {
        /* Alternation — try each child in order */
        for (int i = 0; i < p->n && i < MAX_CHILDREN; i++) {
            int r = match_node(p->children[i], s, slen, cursor, opts, scan_start);
            if (r >= 0) return r;
        }
        return -1;
    }

    case T_SIGMA: {
        /* Sequence — thread cursor through all children */
        int cur = cursor;
        for (int i = 0; i < p->n && i < MAX_CHILDREN; i++) {
            cur = match_node(p->children[i], s, slen, cur, opts, scan_start);
            if (cur < 0) return -1;
        }
        return cur;
    }

    case T_VARREF: {
        /* Deferred variable reference — resolve via opts->var_fn if available */
        if (opts && opts->var_fn && p->s) {
            Pattern *resolved = opts->var_fn(p->s, opts->var_data);
            if (resolved)
                return match_node(resolved, s, slen, cursor, opts, scan_start);
        }
        return -1;
    }

    case T_FUNC: {
        /* Zero-width side-effect call: fire callback, then succeed at same cursor.
         * Used for nInc, nPush, nPop, Reduce etc. Returns -1 only if callback
         * explicitly signals failure (returns (void*)-1 cast). */
        if (p->func) {
            void *r = p->func(p->func_data);
            if (r == (void *)(intptr_t)-1) return -1;
        }
        return cursor;
    }

    case T_CAPTURE: {
        /* Wrap child: record start/end in opts->cap_fn after child succeeds. */
        if (!p->children[0]) return cursor;
        int start = cursor;
        int end = match_node(p->children[0], s, slen, cursor, opts, scan_start);
        if (end < 0) return -1;
        if (opts && opts->cap_fn)
            opts->cap_fn(p->n, scan_start + start, scan_start + end, opts->cap_data);
        return end;
    }

    default:
        /* Unhandled node type — fail safely */
        return -1;
    }
}

MatchResult engine_match_ex(Pattern *root, const char *subject, int subject_len,
                             const EngineOpts *opts)
{
    MatchResult r = {0, 0, 0};
    int scan_start = (opts && opts->scan_start > 0) ? opts->scan_start : 0;
    int end = match_node(root, subject, subject_len, 0, opts, scan_start);
    if (end >= 0) {
        r.matched = 1;
        r.start   = 0;
        r.end     = end;
    }
    return r;
}

MatchResult engine_match(Pattern *root, const char *subject, int subject_len)
{
    return engine_match_ex(root, subject, subject_len, NULL);
}
