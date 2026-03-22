/*
 * snocone_lex.c -- Snocone lexer implementation
 *
 * Direct port of snobol4jvm/src/SNOBOL4clojure/snocone.clj.
 * Logic mirrors the Clojure functions one-to-one:
 *   strip_comment()      <- strip-comment
 *   is_continuation()    <- continuation?
 *   split_semicolons()   <- split-semicolon
 *   scan_number()        <- scan-number
 *   tokenize_segment()   <- tokenize-segment
 *   snocone_lex()             <- tokenize  (public entry point)
 */

#include "snocone_lex.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Operator table -- longest-match (4-char first, then 3, 2, 1)
 * Mirrors Clojure ops-by-length sorted by descending length.
 * ------------------------------------------------------------------------- */
typedef struct { const char *text; SnoconeKind kind; } OpEntry;

static const OpEntry OP_TABLE[] = {
    /* 4-char */
    { ":!=:", SNOCONE_STR_NE    },
    { ":<=:", SNOCONE_STR_LE    },
    { ":>=:", SNOCONE_STR_GE    },
    { ":==:", SNOCONE_STR_EQ    },
    /* 3-char */
    { ":!:",  SNOCONE_STR_DIFFER },
    { ":<:",  SNOCONE_STR_LT    },
    { ":>:",  SNOCONE_STR_GT    },
    /* 2-char */
    { "::",   SNOCONE_STR_IDENT },
    { "||",   SNOCONE_OR        },
    { "&&",   SNOCONE_CONCAT    },
    { "==",   SNOCONE_EQ        },
    { "!=",   SNOCONE_NE        },
    { "<=",   SNOCONE_LE        },
    { ">=",   SNOCONE_GE        },
    { "**",   SNOCONE_CARET     },   /* SNOBOL4 ** same as ^ */
    /* 1-char */
    { "=",    SNOCONE_ASSIGN    },
    { "?",    SNOCONE_QUESTION  },
    { "|",    SNOCONE_PIPE      },
    { "+",    SNOCONE_PLUS      },
    { "-",    SNOCONE_MINUS     },
    { "/",    SNOCONE_SLASH     },
    { "*",    SNOCONE_STAR      },
    { "%",    SNOCONE_PERCENT   },
    { "^",    SNOCONE_CARET     },
    { ".",    SNOCONE_PERIOD    },
    { "$",    SNOCONE_DOLLAR    },
    { "&",    SNOCONE_AMPERSAND },
    { "@",    SNOCONE_AT        },
    { "~",    SNOCONE_TILDE     },
    { "<",    SNOCONE_LT        },
    { ">",    SNOCONE_GT        },
    { "(",    SNOCONE_LPAREN    },
    { ")",    SNOCONE_RPAREN    },
    { "{",    SNOCONE_LBRACE    },
    { "}",    SNOCONE_RBRACE    },
    { "[",    SNOCONE_LBRACKET  },
    { "]",    SNOCONE_RBRACKET  },
    { ",",    SNOCONE_COMMA     },
    { ";",    SNOCONE_SEMICOLON },
    { ":",    SNOCONE_COLON     },
    { NULL,   SNOCONE_UNKNOWN   }
};

/* ---------------------------------------------------------------------------
 * Keyword table -- mirrors Clojure keywords map
 * ------------------------------------------------------------------------- */
typedef struct { const char *word; SnoconeKind kind; } KwEntry;

static const KwEntry KW_TABLE[] = {
    { "if",        SNOCONE_KW_IF        },
    { "then",      SNOCONE_KW_THEN      },
    { "else",      SNOCONE_KW_ELSE      },
    { "while",     SNOCONE_KW_WHILE     },
    { "do",        SNOCONE_KW_DO        },
    { "for",       SNOCONE_KW_FOR       },
    { "return",    SNOCONE_KW_RETURN    },
    { "freturn",   SNOCONE_KW_FRETURN   },
    { "nreturn",   SNOCONE_KW_NRETURN   },
    { "go",        SNOCONE_KW_GO        },
    { "to",        SNOCONE_KW_TO        },
    { "procedure", SNOCONE_KW_PROCEDURE },
    { "struct",    SNOCONE_KW_STRUCT    },
    { NULL,        SNOCONE_UNKNOWN      }
};

/* ---------------------------------------------------------------------------
 * Continuation characters: @ $ % ^ & * ( - + = [ < > | ~ , ? :
 * Mirrors Clojure continuation-chars set.
 * ------------------------------------------------------------------------- */
static int is_cont_char(char c)
{
    return c == '@' || c == '$' || c == '%' || c == '^' || c == '&' ||
           c == '*' || c == '(' || c == '-' || c == '+' || c == '=' ||
           c == '[' || c == '<' || c == '>' || c == '|' || c == '~' ||
           c == ',' || c == '?' || c == ':';
}

/* ---------------------------------------------------------------------------
 * Dynamic token buffer
 * ------------------------------------------------------------------------- */
typedef struct {
    SnoconeToken *data;
    int      len;
    int      cap;
} TokBuf;

static void tb_init(TokBuf *b)
{
    b->cap  = 64;
    b->len  = 0;
    b->data = malloc(b->cap * sizeof(SnoconeToken));
}

static void tb_push(TokBuf *b, SnoconeKind kind, const char *text, int tlen, int line)
{
    if (b->len == b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap * sizeof(SnoconeToken));
    }
    char *t = malloc(tlen + 1);
    memcpy(t, text, tlen);
    t[tlen] = '\0';
    b->data[b->len].kind = kind;
    b->data[b->len].text = t;
    b->data[b->len].line = line;
    b->len++;
}

/* ---------------------------------------------------------------------------
 * strip_comment -- return length of line before any unquoted '#'
 * Mirrors Clojure strip-comment (respects ' and " strings).
 * ------------------------------------------------------------------------- */
static int strip_comment(const char *line, int len)
{
    int in_single = 0, in_double = 0;
    for (int i = 0; i < len; i++) {
        char c = line[i];
        if (c == '\'' && !in_double) { in_single = !in_single; continue; }
        if (c == '"'  && !in_single) { in_double = !in_double; continue; }
        if (c == '#'  && !in_single && !in_double) return i;
    }
    return len;
}

/* ---------------------------------------------------------------------------
 * is_continuation -- true when last non-ws char of stripped line is a cont char
 * Mirrors Clojure continuation?
 * ------------------------------------------------------------------------- */
static int is_continuation(const char *line, int stripped_len)
{
    int last = -1;
    for (int i = 0; i < stripped_len; i++)
        if (!isspace((unsigned char)line[i])) last = i;
    return last >= 0 && is_cont_char(line[last]);
}

/* ---------------------------------------------------------------------------
 * tokenize_segment -- tokenize one logical statement segment into TokBuf
 * Mirrors Clojure tokenize-segment.
 * ------------------------------------------------------------------------- */
static void tokenize_segment(const char *seg, int seg_len, int line_no, TokBuf *acc)
{
    int pos = 0;
    while (pos < seg_len) {
        char c = seg[pos];

        /* Whitespace -- skip */
        if (isspace((unsigned char)c)) { pos++; continue; }

        /* String literal: ' or " -- scan to matching close quote */
        if (c == '\'' || c == '"') {
            char quote = c;
            int start = pos;
            pos++;
            while (pos < seg_len && seg[pos] != quote) pos++;
            if (pos < seg_len) pos++;   /* consume closing quote */
            tb_push(acc, SNOCONE_STRING, seg + start, pos - start, line_no);
            continue;
        }

        /* Number: digit, or leading-dot float (.5) */
        if (isdigit((unsigned char)c) ||
            (c == '.' && pos+1 < seg_len && isdigit((unsigned char)seg[pos+1])))
        {
            int start    = pos;
            int is_real  = (c == '.');
            if (is_real) pos++;

            /* consume integer digits */
            while (pos < seg_len && isdigit((unsigned char)seg[pos])) pos++;

            /* decimal part: not already real, see '.' followed by digit */
            if (!is_real && pos < seg_len && seg[pos] == '.' &&
                pos+1 < seg_len && isdigit((unsigned char)seg[pos+1]))
            {
                is_real = 1;
                pos++;
                while (pos < seg_len && isdigit((unsigned char)seg[pos])) pos++;
            }

            /* exponent: eEdD [+-] digits */
            if (pos < seg_len && (seg[pos]=='e'||seg[pos]=='E'||
                                  seg[pos]=='d'||seg[pos]=='D'))
            {
                is_real = 1;
                pos++;
                if (pos < seg_len && (seg[pos]=='+' || seg[pos]=='-')) pos++;
                while (pos < seg_len && isdigit((unsigned char)seg[pos])) pos++;
            }

            tb_push(acc, is_real ? SNOCONE_REAL : SNOCONE_INTEGER,
                    seg + start, pos - start, line_no);
            continue;
        }

        /* Identifier / keyword: [A-Za-z_][A-Za-z0-9_]* */
        if (isalpha((unsigned char)c) || c == '_') {
            int start = pos;
            pos++;
            while (pos < seg_len &&
                   (isalnum((unsigned char)seg[pos]) || seg[pos] == '_'))
                pos++;
            int wlen = pos - start;

            /* keyword lookup */
            SnoconeKind kind = SNOCONE_IDENT;
            for (int k = 0; KW_TABLE[k].word; k++) {
                if ((int)strlen(KW_TABLE[k].word) == wlen &&
                    memcmp(KW_TABLE[k].word, seg + start, wlen) == 0)
                {
                    kind = KW_TABLE[k].kind;
                    break;
                }
            }
            tb_push(acc, kind, seg + start, wlen, line_no);
            continue;
        }

        /* Operators -- longest match (table already sorted desc by length) */
        {
            int matched = 0;
            for (int k = 0; OP_TABLE[k].text; k++) {
                int olen = (int)strlen(OP_TABLE[k].text);
                if (pos + olen <= seg_len &&
                    memcmp(OP_TABLE[k].text, seg + pos, olen) == 0)
                {
                    tb_push(acc, OP_TABLE[k].kind, seg + pos, olen, line_no);
                    pos += olen;
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                tb_push(acc, SNOCONE_UNKNOWN, seg + pos, 1, line_no);
                pos++;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * tokenize_logical_line -- split on unquoted ';', tokenize each segment,
 * append SNOCONE_NEWLINE after each non-blank segment.
 * Mirrors Clojure split-semicolon + inner reduce in tokenize.
 * ------------------------------------------------------------------------- */
static void tokenize_logical_line(const char *joined, int jlen,
                                  int stmt_line, TokBuf *acc)
{
    /* split on unquoted semicolons */
    int seg_start = 0;
    int in_single = 0, in_double = 0;

    for (int i = 0; i <= jlen; i++) {
        char c = (i < jlen) ? joined[i] : ';';  /* sentinel forces flush */
        if (c == '\'' && !in_double) { in_single = !in_single; continue; }
        if (c == '"'  && !in_single) { in_double = !in_double; continue; }

        if (c == ';' && !in_single && !in_double) {
            /* trim whitespace from segment */
            int s = seg_start, e = i;
            while (s < e && isspace((unsigned char)joined[s])) s++;
            while (e > s && isspace((unsigned char)joined[e-1])) e--;
            if (s < e) {
                int before = acc->len;
                tokenize_segment(joined + s, e - s, stmt_line, acc);
                if (acc->len > before) {
                    /* append NEWLINE after tokens from this segment */
                    tb_push(acc, SNOCONE_NEWLINE, "\n", 1, stmt_line);
                }
            }
            seg_start = i + 1;
            in_single = in_double = 0;
        }
    }
}

/* ---------------------------------------------------------------------------
 * snocone_lex -- public entry point
 * Mirrors Clojure tokenize: handles CRLF, continuation lines, #-comments.
 * ------------------------------------------------------------------------- */
ScTokenArray snocone_lex(const char *source)
{
    /* Split source into physical lines (handle CRLF) */
    /* Count lines first */
    int nlines = 1;
    for (const char *p = source; *p; p++)
        if (*p == '\n') nlines++;

    /* Build line pointer + length array */
    const char **line_ptr = malloc(nlines * sizeof(char *));
    int         *line_len = malloc(nlines * sizeof(int));
    int          n        = 0;
    const char  *start    = source;
    for (const char *p = source; ; p++) {
        if (*p == '\n' || *p == '\0') {
            int raw_len = (int)(p - start);
            /* strip trailing CR */
            int len = (raw_len > 0 && start[raw_len-1] == '\r') ?
                      raw_len - 1 : raw_len;
            line_ptr[n] = start;
            line_len[n] = len;
            n++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }

    TokBuf acc;
    tb_init(&acc);

    /* Accumulate physical lines into logical statements.
     * logical_buf: concatenation of stripped physical lines for current stmt.
     * logical_start_line: 1-based line of first physical line in this stmt. */
    char *logical_buf       = malloc(65536);
    int   logical_buf_cap   = 65536;
    int   logical_len       = 0;
    int   logical_start_line = 1;

    for (int i = 0; i < n; i++) {
        int   line_no   = i + 1;
        const char *raw = line_ptr[i];
        int   raw_len   = line_len[i];

        /* strip comment */
        int stripped_len = strip_comment(raw, raw_len);

        /* start of new logical stmt? */
        if (logical_len == 0) logical_start_line = line_no;

        /* append stripped physical line to logical buffer */
        if (logical_len + stripped_len + 1 > logical_buf_cap) {
            logical_buf_cap = (logical_len + stripped_len + 1) * 2;
            logical_buf = realloc(logical_buf, logical_buf_cap);
        }
        memcpy(logical_buf + logical_len, raw, stripped_len);
        logical_len += stripped_len;

        /* check continuation */
        if (is_continuation(logical_buf, logical_len) && i + 1 < n) {
            /* this line continues -- accumulate more */
            continue;
        }

        /* end of logical statement -- tokenize */
        tokenize_logical_line(logical_buf, logical_len,
                              logical_start_line, &acc);
        logical_len = 0;
    }

    /* flush any remaining logical line */
    if (logical_len > 0)
        tokenize_logical_line(logical_buf, logical_len,
                              logical_start_line, &acc);

    free(logical_buf);
    free(line_ptr);
    free(line_len);

    /* append EOF */
    tb_push(&acc, SNOCONE_EOF, "", 0, n);

    ScTokenArray result;
    result.tokens = acc.data;
    result.count  = acc.len;
    return result;
}

/* ---------------------------------------------------------------------------
 * sc_tokens_free
 * ------------------------------------------------------------------------- */
void sc_tokens_free(ScTokenArray *arr)
{
    for (int i = 0; i < arr->count; i++)
        free(arr->tokens[i].text);
    free(arr->tokens);
    arr->tokens = NULL;
    arr->count  = 0;
}

/* ---------------------------------------------------------------------------
 * sc_kind_name -- for debugging / test output
 * ------------------------------------------------------------------------- */
const char *sc_kind_name(SnoconeKind kind)
{
    switch (kind) {
    case SNOCONE_INTEGER:     return "INTEGER";
    case SNOCONE_REAL:        return "REAL";
    case SNOCONE_STRING:      return "STRING";
    case SNOCONE_IDENT:       return "IDENT";
    case SNOCONE_KW_IF:       return "KW_IF";
    case SNOCONE_KW_THEN:     return "KW_THEN";
    case SNOCONE_KW_ELSE:     return "KW_ELSE";
    case SNOCONE_KW_WHILE:    return "KW_WHILE";
    case SNOCONE_KW_DO:       return "KW_DO";
    case SNOCONE_KW_FOR:      return "KW_FOR";
    case SNOCONE_KW_RETURN:   return "KW_RETURN";
    case SNOCONE_KW_FRETURN:  return "KW_FRETURN";
    case SNOCONE_KW_NRETURN:  return "KW_NRETURN";
    case SNOCONE_KW_GO:       return "KW_GO";
    case SNOCONE_KW_TO:       return "KW_TO";
    case SNOCONE_KW_PROCEDURE:return "KW_PROCEDURE";
    case SNOCONE_KW_STRUCT:   return "KW_STRUCT";
    case SNOCONE_LPAREN:      return "LPAREN";
    case SNOCONE_RPAREN:      return "RPAREN";
    case SNOCONE_LBRACE:      return "LBRACE";
    case SNOCONE_RBRACE:      return "RBRACE";
    case SNOCONE_LBRACKET:    return "LBRACKET";
    case SNOCONE_RBRACKET:    return "RBRACKET";
    case SNOCONE_COMMA:       return "COMMA";
    case SNOCONE_SEMICOLON:   return "SEMICOLON";
    case SNOCONE_COLON:       return "COLON";
    case SNOCONE_ASSIGN:      return "ASSIGN";
    case SNOCONE_QUESTION:    return "QUESTION";
    case SNOCONE_PIPE:        return "PIPE";
    case SNOCONE_OR:          return "OR";
    case SNOCONE_CONCAT:      return "CONCAT";
    case SNOCONE_EQ:          return "EQ";
    case SNOCONE_NE:          return "NE";
    case SNOCONE_LT:          return "LT";
    case SNOCONE_GT:          return "GT";
    case SNOCONE_LE:          return "LE";
    case SNOCONE_GE:          return "GE";
    case SNOCONE_STR_IDENT:   return "STR_IDENT";
    case SNOCONE_STR_DIFFER:  return "STR_DIFFER";
    case SNOCONE_STR_LT:      return "STR_LT";
    case SNOCONE_STR_GT:      return "STR_GT";
    case SNOCONE_STR_LE:      return "STR_LE";
    case SNOCONE_STR_GE:      return "STR_GE";
    case SNOCONE_STR_EQ:      return "STR_EQ";
    case SNOCONE_STR_NE:      return "STR_NE";
    case SNOCONE_PLUS:        return "PLUS";
    case SNOCONE_MINUS:       return "MINUS";
    case SNOCONE_SLASH:       return "SLASH";
    case SNOCONE_STAR:        return "STAR";
    case SNOCONE_PERCENT:     return "PERCENT";
    case SNOCONE_CARET:       return "CARET";
    case SNOCONE_PERIOD:      return "PERIOD";
    case SNOCONE_DOLLAR:      return "DOLLAR";
    case SNOCONE_AT:          return "AT";
    case SNOCONE_AMPERSAND:   return "AMPERSAND";
    case SNOCONE_TILDE:       return "TILDE";
    case SNOCONE_NEWLINE:     return "NEWLINE";
    case SNOCONE_EOF:         return "EOF";
    case SNOCONE_UNKNOWN:     return "UNKNOWN";
    default:             return "???";
    }
}
