/*
 * pl_lex.c — Prolog lexer for snobol4x Prolog frontend
 *
 * Handles Edinburgh/ISO Prolog surface syntax:
 *   - % line comments
 *   - /* block comments  * /
 *   - Quoted atoms:  'foo bar'  ('' escape for embedded quote)
 *   - Double-quoted strings: "hello"  (\n \t \\ \" escapes)
 *   - Variables: uppercase or _ prefix, e.g. X, Foo, _Bar
 *   - Anonymous: bare _
 *   - Integers: decimal, 0'<char>, 0b<bin>, 0x<hex>
 *   - Floats: digits . digits (optional e/E exponent)
 *   - Operators: returned as TK_OP with text
 *   - Clause terminator: . followed by whitespace or EOF
 */

#include "pl_lex.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* =========================================================================
 * Helpers
 * ======================================================================= */

static char cur(Lexer *lx) {
    return lx->src[lx->pos];
}
static char peek1(Lexer *lx) {
    if (lx->src[lx->pos] == '\0') return '\0';
    return lx->src[lx->pos + 1];
}
static char advance(Lexer *lx) {
    char c = lx->src[lx->pos];
    if (c == '\n') lx->line++;
    if (c) lx->pos++;
    return c;
}

/* Append c to a heap buffer; buf/cap passed by pointer. */
static void buf_push(char **buf, int *len, int *cap, char c) {
    if (*len + 2 > *cap) {
        *cap = (*cap) ? (*cap) * 2 : 32;
        *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static Token make_tok(TkKind kind, char *text, int line) {
    Token t; t.kind = kind; t.text = text; t.ival = 0; t.fval = 0.0; t.line = line;
    return t;
}
static Token make_err(int line, const char *msg) {
    Token t; t.kind = TK_ERROR; t.text = strdup(msg); t.ival = 0; t.fval = 0.0; t.line = line;
    return t;
}

/* =========================================================================
 * Skip whitespace and comments
 * ======================================================================= */
static void skip_ws(Lexer *lx) {
    for (;;) {
        /* whitespace */
        while (cur(lx) && isspace((unsigned char)cur(lx))) advance(lx);
        /* % line comment */
        if (cur(lx) == '%') {
            while (cur(lx) && cur(lx) != '\n') advance(lx);
            continue;
        }
        /* /* block comment */
        if (cur(lx) == '/' && peek1(lx) == '*') {
            advance(lx); advance(lx);
            while (cur(lx)) {
                if (cur(lx) == '*' && peek1(lx) == '/') {
                    advance(lx); advance(lx); break;
                }
                advance(lx);
            }
            continue;
        }
        break;
    }
}

/* =========================================================================
 * Scan quoted atom  'text'
 * ======================================================================= */
static Token scan_quoted_atom(Lexer *lx) {
    int line = lx->line;
    advance(lx); /* consume opening ' */
    char *buf = NULL; int len = 0, cap = 0;
    for (;;) {
        char c = cur(lx);
        if (c == '\0') return make_err(line, "unterminated quoted atom");
        if (c == '\'') {
            advance(lx);
            if (cur(lx) == '\'') { /* '' escape */
                buf_push(&buf, &len, &cap, '\''); advance(lx);
            } else break;
        } else if (c == '\\') {
            advance(lx);
            char e = advance(lx);
            switch (e) {
                case 'n': buf_push(&buf, &len, &cap, '\n'); break;
                case 't': buf_push(&buf, &len, &cap, '\t'); break;
                case '\\': buf_push(&buf, &len, &cap, '\\'); break;
                case '\'': buf_push(&buf, &len, &cap, '\''); break;
                default:   buf_push(&buf, &len, &cap, '\\');
                           buf_push(&buf, &len, &cap, e); break;
            }
        } else {
            buf_push(&buf, &len, &cap, c); advance(lx);
        }
    }
    if (!buf) buf = strdup("");
    Token t = make_tok(TK_ATOM, buf, line);
    return t;
}

/* =========================================================================
 * Scan double-quoted string  "text"
 * ======================================================================= */
static Token scan_string(Lexer *lx) {
    int line = lx->line;
    advance(lx); /* consume opening " */
    char *buf = NULL; int len = 0, cap = 0;
    for (;;) {
        char c = cur(lx);
        if (c == '\0') return make_err(line, "unterminated string");
        if (c == '"') { advance(lx); break; }
        if (c == '\\') {
            advance(lx);
            char e = advance(lx);
            switch (e) {
                case 'n': buf_push(&buf, &len, &cap, '\n'); break;
                case 't': buf_push(&buf, &len, &cap, '\t'); break;
                case '\\': buf_push(&buf, &len, &cap, '\\'); break;
                case '"': buf_push(&buf, &len, &cap, '"'); break;
                default:  buf_push(&buf, &len, &cap, '\\');
                          buf_push(&buf, &len, &cap, e); break;
            }
        } else {
            buf_push(&buf, &len, &cap, c); advance(lx);
        }
    }
    if (!buf) buf = strdup("");
    Token t = make_tok(TK_STRING, buf, line);
    return t;
}

/* =========================================================================
 * Scan number (integer or float)
 * ======================================================================= */
static Token scan_number(Lexer *lx) {
    int line = lx->line;
    char *buf = NULL; int len = 0, cap = 0;
    int is_float = 0;

    /* 0' char-code, 0b binary, 0x hex */
    if (cur(lx) == '0' && peek1(lx) == '\'') {
        advance(lx); advance(lx);
        char c = advance(lx);
        Token t = make_tok(TK_INT, NULL, line);
        t.ival = (long)c; t.text = strdup("0'c");
        return t;
    }
    if (cur(lx) == '0' && (peek1(lx) == 'b' || peek1(lx) == 'x')) {
        buf_push(&buf, &len, &cap, advance(lx));
        buf_push(&buf, &len, &cap, advance(lx));
        while (isxdigit((unsigned char)cur(lx)) || cur(lx) == '_')
            if (cur(lx) != '_') buf_push(&buf, &len, &cap, advance(lx));
            else advance(lx);
        Token t = make_tok(TK_INT, buf, line);
        t.ival = strtol(buf+2, NULL, (buf[1]=='b'||buf[1]=='B') ? 2 : 16);
        return t;
    }

    while (isdigit((unsigned char)cur(lx)))
        buf_push(&buf, &len, &cap, advance(lx));

    /* float: digits '.' digits */
    if (cur(lx) == '.' && isdigit((unsigned char)peek1(lx))) {
        is_float = 1;
        buf_push(&buf, &len, &cap, advance(lx)); /* . */
        while (isdigit((unsigned char)cur(lx)))
            buf_push(&buf, &len, &cap, advance(lx));
        if (cur(lx) == 'e' || cur(lx) == 'E') {
            buf_push(&buf, &len, &cap, advance(lx));
            if (cur(lx) == '+' || cur(lx) == '-')
                buf_push(&buf, &len, &cap, advance(lx));
            while (isdigit((unsigned char)cur(lx)))
                buf_push(&buf, &len, &cap, advance(lx));
        }
    }

    if (!buf) buf = strdup("0");

    if (is_float) {
        Token t = make_tok(TK_FLOAT, buf, line);
        t.fval = atof(buf);
        return t;
    } else {
        Token t = make_tok(TK_INT, buf, line);
        t.ival = atol(buf);
        return t;
    }
}

/* =========================================================================
 * Scan atom or keyword (lowercase start or special graphic)
 * ======================================================================= */
static int is_atom_start(char c) {
    return islower((unsigned char)c) || c == '+' || c == '-' || c == '*' ||
           c == '/' || c == '\\' || c == '^' || c == '<' || c == '>' ||
           c == '=' || c == '~' || c == '?' || c == '@' || c == '#' ||
           c == '&';
}

static Token scan_word(Lexer *lx) {
    int line = lx->line;
    char *buf = NULL; int len = 0, cap = 0;
    while (isalnum((unsigned char)cur(lx)) || cur(lx) == '_')
        buf_push(&buf, &len, &cap, advance(lx));
    if (!buf) buf = strdup("");
    return make_tok(TK_ATOM, buf, line);
}

/* =========================================================================
 * Scan operator sequence (graphic chars)
 * ======================================================================= */
static int is_graphic(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '\\' ||
           c == '^' || c == '<' || c == '>' || c == '=' || c == '~' ||
           c == '?' || c == '@' || c == '#' || c == '&' || c == ':' ||
           c == '.' || c == '!';
}

static Token scan_graphic(Lexer *lx) {
    int line = lx->line;
    char *buf = NULL; int len = 0, cap = 0;
    while (is_graphic(cur(lx)) && cur(lx) != ',' && cur(lx) != '|')
        buf_push(&buf, &len, &cap, advance(lx));
    if (!buf) buf = strdup("");

    /* Classify known multi-char operators */
    if (strcmp(buf, ":-") == 0) { Token t = make_tok(TK_NECK, buf, line); return t; }
    if (strcmp(buf, "?-") == 0) { Token t = make_tok(TK_QUERY, buf, line); return t; }
    /* Everything else is TK_OP */
    return make_tok(TK_OP, buf, line);
}

/* =========================================================================
 * lexer_next — main scan function
 * ======================================================================= */
Token lexer_next(Lexer *lx) {
    if (lx->has_peek) {
        lx->has_peek = 0;
        return lx->peek;
    }

    skip_ws(lx);
    int line = lx->line;
    char c = cur(lx);

    if (c == '\0') return make_tok(TK_EOF, strdup(""), line);

    /* Quoted atom */
    if (c == '\'') return scan_quoted_atom(lx);

    /* Double-quoted string */
    if (c == '"') return scan_string(lx);

    /* Variable or anonymous */
    if (isupper((unsigned char)c)) {
        char *buf = NULL; int len = 0, cap = 0;
        while (isalnum((unsigned char)cur(lx)) || cur(lx) == '_')
            buf_push(&buf, &len, &cap, advance(lx));
        if (!buf) buf = strdup("");
        return make_tok(TK_VAR, buf, line);
    }
    if (c == '_') {
        advance(lx);
        if (!isalnum((unsigned char)cur(lx)) && cur(lx) != '_')
            return make_tok(TK_ANON, strdup("_"), line);
        /* _Foo — named var */
        char *buf = NULL; int len = 0, cap = 0;
        buf_push(&buf, &len, &cap, '_');
        while (isalnum((unsigned char)cur(lx)) || cur(lx) == '_')
            buf_push(&buf, &len, &cap, advance(lx));
        return make_tok(TK_VAR, buf, line);
    }

    /* Number */
    if (isdigit((unsigned char)c)) return scan_number(lx);

    /* Lowercase atom */
    if (islower((unsigned char)c)) return scan_word(lx);

    /* Punctuation */
    advance(lx);
    switch (c) {
        case '(': return make_tok(TK_LPAREN,   strdup("("), line);
        case ')': return make_tok(TK_RPAREN,   strdup(")"), line);
        case '[':
            if (cur(lx) == ']') { advance(lx); return make_tok(TK_ATOM, strdup("[]"), line); }
            return make_tok(TK_LBRACKET, strdup("["), line);
        case ']': return make_tok(TK_RBRACKET, strdup("]"), line);
        case '|': return make_tok(TK_PIPE,     strdup("|"), line);
        case ',': return make_tok(TK_COMMA,    strdup(","), line);
        case '!': return make_tok(TK_CUT,      strdup("!"), line);
        case ';': return make_tok(TK_SEMI,     strdup(";"), line);
        case '.':
            /* Clause terminator: . followed by whitespace or EOF */
            if (cur(lx) == '\0' || isspace((unsigned char)cur(lx)))
                return make_tok(TK_DOT, strdup("."), line);
            /* Otherwise a graphic operator starting with . */
            lx->pos--; /* put back the . */
            return scan_graphic(lx);
        default:
            /* Graphic operator */
            lx->pos--; /* put back */
            if (is_graphic(cur(lx))) return scan_graphic(lx);
            { char msg[32]; snprintf(msg,sizeof msg,"unexpected '%c'",c);
              return make_err(line, msg); }
    }
}

/* =========================================================================
 * lexer_peek / lexer_expect
 * ======================================================================= */
Token lexer_peek(Lexer *lx) {
    if (!lx->has_peek) {
        lx->peek     = lexer_next(lx);
        lx->has_peek = 1;
    }
    return lx->peek;
}

Token lexer_expect(Lexer *lx, TkKind kind, const char *context) {
    Token t = lexer_next(lx);
    if (t.kind != kind) {
        fprintf(stderr, "parse error at line %d in %s: expected %s, got '%s'\n",
                t.line, context, tk_name(kind), t.text ? t.text : "?");
        /* return error token so caller can propagate */
        Token err; err.kind = TK_ERROR; err.text = strdup("expected");
        err.ival = 0; err.fval = 0; err.line = t.line;
        return err;
    }
    return t;
}

void token_free(Token *t) { free(t->text); t->text = NULL; }

void lexer_init(Lexer *lx, const char *src) {
    lx->src      = src ? src : "";
    lx->pos      = 0;
    lx->line     = 1;
    lx->has_peek = 0;
    memset(&lx->peek, 0, sizeof lx->peek);
}

const char *tk_name(TkKind kind) {
    switch (kind) {
        case TK_EOF:      return "EOF";
        case TK_ATOM:     return "atom";
        case TK_VAR:      return "variable";
        case TK_ANON:     return "_";
        case TK_INT:      return "integer";
        case TK_FLOAT:    return "float";
        case TK_STRING:   return "string";
        case TK_LPAREN:   return "(";
        case TK_RPAREN:   return ")";
        case TK_LBRACKET: return "[";
        case TK_RBRACKET: return "]";
        case TK_PIPE:     return "|";
        case TK_COMMA:    return ",";
        case TK_DOT:      return ".";
        case TK_OP:       return "operator";
        case TK_NECK:     return ":-";
        case TK_QUERY:    return "?-";
        case TK_CUT:      return "!";
        case TK_SEMI:     return ";";
        case TK_ERROR:    return "<error>";
        default:          return "?";
    }
}
