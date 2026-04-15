/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         RAKU_YYSTYPE
/* Substitute the variable and function names.  */
#define yyparse         raku_yyparse
#define yylex           raku_yylex
#define yyerror         raku_yyerror
#define yydebug         raku_yydebug
#define yynerrs         raku_yynerrs
#define yylval          raku_yylval
#define yychar          raku_yychar

/* First part of user prologue.  */
#line 21 "raku.y"

#include "../../ir/ir.h"
#include "../snobol4/scrip_cc.h"
#include "raku.tab.h"   /* pulls in ExprList from %code requires */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  raku_yylex(void);
extern int  raku_get_lineno(void);
void raku_yyerror(const char *msg) {
    fprintf(stderr, "raku parse error line %d: %s\n", raku_get_lineno(), msg);
}

/*--------------------------------------------------------------------
 * ExprList helpers
 *--------------------------------------------------------------------*/
static ExprList *exprlist_new(void) {
    ExprList *l = calloc(1, sizeof *l);
    if (!l) { fprintf(stderr, "raku: OOM\n"); exit(1); }
    return l;
}
static ExprList *exprlist_append(ExprList *l, EXPR_t *e) {
    if (l->count >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = realloc(l->items, l->cap * sizeof(EXPR_t *));
        if (!l->items) { fprintf(stderr, "raku: OOM\n"); exit(1); }
    }
    l->items[l->count++] = e;
    return l;
}
static void exprlist_free(ExprList *l) { if (l) { free(l->items); free(l); } }

/*--------------------------------------------------------------------
 * Build helpers (logic from raku_lower.c, inlined for direct IR)
 *--------------------------------------------------------------------*/
static const char *strip_sigil(const char *s) {
    if (s && (s[0]=='$'||s[0]=='@'||s[0]=='%')) return s+1;
    return s;
}
static EXPR_t *leaf_sval(EKind k, const char *s) {
    EXPR_t *e = expr_new(k); e->sval = intern(s); return e;
}
static EXPR_t *var_node(const char *name) {
    return leaf_sval(E_VAR, strip_sigil(name));
}
/* make_call: E_FNC + children[0]=E_VAR(name) for icn_interp_eval layout */
static EXPR_t *make_call(const char *name) {
    EXPR_t *e = leaf_sval(E_FNC, name);
    EXPR_t *n = expr_new(E_VAR); n->sval = intern(name);
    expr_add_child(e, n);
    return e;
}
/* make_seq: ExprList → E_SEQ_EXPR, frees list */
static EXPR_t *make_seq(ExprList *stmts) {
    EXPR_t *seq = expr_new(E_SEQ_EXPR);
    if (stmts) {
        for (int i = 0; i < stmts->count; i++) expr_add_child(seq, stmts->items[i]);
        exprlist_free(stmts);
    }
    return seq;
}
/* lower_interp_str: "hello $var" → left-associative E_CAT chain */
static EXPR_t *lower_interp_str(const char *s) {
    int len = s ? (int)strlen(s) : 0;
    EXPR_t *result = NULL;
    char litbuf[4096]; int litpos = 0, i = 0;
    while (i < len) {
        if (s[i]=='$' && i+1<len &&
            (s[i+1]=='_'||(s[i+1]>='A'&&s[i+1]<='Z')||(s[i+1]>='a'&&s[i+1]<='z'))) {
            if (litpos>0) { litbuf[litpos]='\0';
                EXPR_t *lit=leaf_sval(E_QLIT,litbuf);
                result=result?expr_binary(E_CAT,result,lit):lit; litpos=0; }
            i++;
            char vname[256]; int vlen=0;
            while (i<len&&(s[i]=='_'||(s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')))
                { if(vlen<255) vname[vlen++]=s[i]; i++; }
            vname[vlen]='\0';
            EXPR_t *var=leaf_sval(E_VAR,vname);
            result=result?expr_binary(E_CAT,result,var):var;
        } else { if(litpos<4095) litbuf[litpos++]=s[i]; i++; }
    }
    if (litpos>0) { litbuf[litpos]='\0';
        EXPR_t *lit=leaf_sval(E_QLIT,litbuf);
        result=result?expr_binary(E_CAT,result,lit):lit; }
    return result ? result : leaf_sval(E_QLIT,"");
}
/* make_for_range: for lo..hi -> $v body → explicit while-loop */
static EXPR_t *make_for_range(EXPR_t *lo, EXPR_t *hi, const char *vname, EXPR_t *body_seq) {
    EXPR_t *init = expr_binary(E_ASSIGN, leaf_sval(E_VAR,vname), lo);
    EXPR_t *cond = expr_binary(E_LE, leaf_sval(E_VAR,vname), hi);
    EXPR_t *one  = expr_new(E_ILIT); one->ival = 1;
    EXPR_t *incr = expr_binary(E_ADD, leaf_sval(E_VAR,vname), one);
    expr_add_child(body_seq, expr_binary(E_ASSIGN, leaf_sval(E_VAR,vname), incr));
    EXPR_t *wloop = expr_binary(E_WHILE, cond, body_seq);
    EXPR_t *seq   = expr_new(E_SEQ_EXPR);
    expr_add_child(seq, init); expr_add_child(seq, wloop);
    return seq;
}

/*--------------------------------------------------------------------
 * Program output
 *--------------------------------------------------------------------*/
Program *raku_prog_result = NULL;

static void add_proc(EXPR_t *e) {
    if (!e) return;
    if (!raku_prog_result) raku_prog_result = calloc(1, sizeof(Program));
    STMT_t *st = calloc(1, sizeof(STMT_t));
    st->subject = e; st->lineno = 0; st->lang = LANG_RAKU;
    if (!raku_prog_result->head) raku_prog_result->head = raku_prog_result->tail = st;
    else { raku_prog_result->tail->next = st; raku_prog_result->tail = st; }
    raku_prog_result->nstmts++;
}

/* SUB_TAG: sentinel bit to distinguish sub defs from body stmts in stmt_list */
#define SUB_TAG 0x40000000


#line 199 "raku.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "raku.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_LIT_INT = 3,                    /* LIT_INT  */
  YYSYMBOL_LIT_FLOAT = 4,                  /* LIT_FLOAT  */
  YYSYMBOL_LIT_STR = 5,                    /* LIT_STR  */
  YYSYMBOL_LIT_INTERP_STR = 6,             /* LIT_INTERP_STR  */
  YYSYMBOL_VAR_SCALAR = 7,                 /* VAR_SCALAR  */
  YYSYMBOL_VAR_ARRAY = 8,                  /* VAR_ARRAY  */
  YYSYMBOL_VAR_HASH = 9,                   /* VAR_HASH  */
  YYSYMBOL_IDENT = 10,                     /* IDENT  */
  YYSYMBOL_KW_MY = 11,                     /* KW_MY  */
  YYSYMBOL_KW_SAY = 12,                    /* KW_SAY  */
  YYSYMBOL_KW_PRINT = 13,                  /* KW_PRINT  */
  YYSYMBOL_KW_IF = 14,                     /* KW_IF  */
  YYSYMBOL_KW_ELSE = 15,                   /* KW_ELSE  */
  YYSYMBOL_KW_ELSIF = 16,                  /* KW_ELSIF  */
  YYSYMBOL_KW_WHILE = 17,                  /* KW_WHILE  */
  YYSYMBOL_KW_FOR = 18,                    /* KW_FOR  */
  YYSYMBOL_KW_SUB = 19,                    /* KW_SUB  */
  YYSYMBOL_KW_GATHER = 20,                 /* KW_GATHER  */
  YYSYMBOL_KW_TAKE = 21,                   /* KW_TAKE  */
  YYSYMBOL_KW_RETURN = 22,                 /* KW_RETURN  */
  YYSYMBOL_KW_GIVEN = 23,                  /* KW_GIVEN  */
  YYSYMBOL_KW_WHEN = 24,                   /* KW_WHEN  */
  YYSYMBOL_KW_DEFAULT = 25,                /* KW_DEFAULT  */
  YYSYMBOL_KW_EXISTS = 26,                 /* KW_EXISTS  */
  YYSYMBOL_KW_DELETE = 27,                 /* KW_DELETE  */
  YYSYMBOL_KW_UNLESS = 28,                 /* KW_UNLESS  */
  YYSYMBOL_KW_UNTIL = 29,                  /* KW_UNTIL  */
  YYSYMBOL_KW_REPEAT = 30,                 /* KW_REPEAT  */
  YYSYMBOL_OP_RANGE = 31,                  /* OP_RANGE  */
  YYSYMBOL_OP_RANGE_EX = 32,               /* OP_RANGE_EX  */
  YYSYMBOL_OP_ARROW = 33,                  /* OP_ARROW  */
  YYSYMBOL_OP_EQ = 34,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 35,                     /* OP_NE  */
  YYSYMBOL_OP_LE = 36,                     /* OP_LE  */
  YYSYMBOL_OP_GE = 37,                     /* OP_GE  */
  YYSYMBOL_OP_SEQ = 38,                    /* OP_SEQ  */
  YYSYMBOL_OP_SNE = 39,                    /* OP_SNE  */
  YYSYMBOL_OP_AND = 40,                    /* OP_AND  */
  YYSYMBOL_OP_OR = 41,                     /* OP_OR  */
  YYSYMBOL_OP_BIND = 42,                   /* OP_BIND  */
  YYSYMBOL_OP_DIV = 43,                    /* OP_DIV  */
  YYSYMBOL_44_ = 44,                       /* '='  */
  YYSYMBOL_45_ = 45,                       /* '!'  */
  YYSYMBOL_46_ = 46,                       /* '<'  */
  YYSYMBOL_47_ = 47,                       /* '>'  */
  YYSYMBOL_48_ = 48,                       /* '~'  */
  YYSYMBOL_49_ = 49,                       /* '+'  */
  YYSYMBOL_50_ = 50,                       /* '-'  */
  YYSYMBOL_51_ = 51,                       /* '*'  */
  YYSYMBOL_52_ = 52,                       /* '/'  */
  YYSYMBOL_53_ = 53,                       /* '%'  */
  YYSYMBOL_UMINUS = 54,                    /* UMINUS  */
  YYSYMBOL_55_ = 55,                       /* ';'  */
  YYSYMBOL_56_ = 56,                       /* '['  */
  YYSYMBOL_57_ = 57,                       /* ']'  */
  YYSYMBOL_58_ = 58,                       /* '{'  */
  YYSYMBOL_59_ = 59,                       /* '}'  */
  YYSYMBOL_60_ = 60,                       /* '('  */
  YYSYMBOL_61_ = 61,                       /* ')'  */
  YYSYMBOL_62_ = 62,                       /* ','  */
  YYSYMBOL_YYACCEPT = 63,                  /* $accept  */
  YYSYMBOL_program = 64,                   /* program  */
  YYSYMBOL_stmt_list = 65,                 /* stmt_list  */
  YYSYMBOL_stmt = 66,                      /* stmt  */
  YYSYMBOL_if_stmt = 67,                   /* if_stmt  */
  YYSYMBOL_while_stmt = 68,                /* while_stmt  */
  YYSYMBOL_unless_stmt = 69,               /* unless_stmt  */
  YYSYMBOL_until_stmt = 70,                /* until_stmt  */
  YYSYMBOL_repeat_stmt = 71,               /* repeat_stmt  */
  YYSYMBOL_for_stmt = 72,                  /* for_stmt  */
  YYSYMBOL_given_stmt = 73,                /* given_stmt  */
  YYSYMBOL_when_list = 74,                 /* when_list  */
  YYSYMBOL_sub_decl = 75,                  /* sub_decl  */
  YYSYMBOL_param_list = 76,                /* param_list  */
  YYSYMBOL_block = 77,                     /* block  */
  YYSYMBOL_expr = 78,                      /* expr  */
  YYSYMBOL_cmp_expr = 79,                  /* cmp_expr  */
  YYSYMBOL_range_expr = 80,                /* range_expr  */
  YYSYMBOL_add_expr = 81,                  /* add_expr  */
  YYSYMBOL_mul_expr = 82,                  /* mul_expr  */
  YYSYMBOL_unary_expr = 83,                /* unary_expr  */
  YYSYMBOL_postfix_expr = 84,              /* postfix_expr  */
  YYSYMBOL_call_expr = 85,                 /* call_expr  */
  YYSYMBOL_arg_list = 86,                  /* arg_list  */
  YYSYMBOL_atom = 87                       /* atom  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined RAKU_YYSTYPE_IS_TRIVIAL && RAKU_YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   379

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  63
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  25
/* YYNRULES -- Number of rules.  */
#define YYNRULES  101
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  239

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   299


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    45,     2,     2,     2,    53,     2,     2,
      60,    61,    51,    49,    62,    50,     2,    52,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    55,
      46,    44,    47,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    56,     2,    57,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    58,     2,    59,    48,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    54
};

#if RAKU_YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   187,   187,   217,   218,   222,   224,   226,   229,   231,
     233,   235,   237,   239,   241,   243,   245,   247,   249,   251,
     253,   256,   259,   262,   265,   268,   269,   270,   271,   272,
     273,   274,   275,   276,   280,   282,   284,   289,   295,   297,
     303,   309,   315,   326,   332,   347,   365,   366,   375,   383,
     392,   393,   397,   401,   402,   403,   407,   408,   409,   410,
     411,   412,   413,   414,   415,   416,   417,   421,   422,   423,
     427,   428,   429,   430,   434,   435,   436,   437,   438,   442,
     443,   444,   447,   450,   455,   456,   460,   461,   465,   466,
     467,   468,   469,   470,   471,   472,   474,   476,   478,   480,
     482,   483
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if RAKU_YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "LIT_INT", "LIT_FLOAT",
  "LIT_STR", "LIT_INTERP_STR", "VAR_SCALAR", "VAR_ARRAY", "VAR_HASH",
  "IDENT", "KW_MY", "KW_SAY", "KW_PRINT", "KW_IF", "KW_ELSE", "KW_ELSIF",
  "KW_WHILE", "KW_FOR", "KW_SUB", "KW_GATHER", "KW_TAKE", "KW_RETURN",
  "KW_GIVEN", "KW_WHEN", "KW_DEFAULT", "KW_EXISTS", "KW_DELETE",
  "KW_UNLESS", "KW_UNTIL", "KW_REPEAT", "OP_RANGE", "OP_RANGE_EX",
  "OP_ARROW", "OP_EQ", "OP_NE", "OP_LE", "OP_GE", "OP_SEQ", "OP_SNE",
  "OP_AND", "OP_OR", "OP_BIND", "OP_DIV", "'='", "'!'", "'<'", "'>'",
  "'~'", "'+'", "'-'", "'*'", "'/'", "'%'", "UMINUS", "';'", "'['", "']'",
  "'{'", "'}'", "'('", "')'", "','", "$accept", "program", "stmt_list",
  "stmt", "if_stmt", "while_stmt", "unless_stmt", "until_stmt",
  "repeat_stmt", "for_stmt", "given_stmt", "when_list", "sub_decl",
  "param_list", "block", "expr", "cmp_expr", "range_expr", "add_expr",
  "mul_expr", "unary_expr", "postfix_expr", "call_expr", "arg_list",
  "atom", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-39)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -39,     6,   261,   -39,   -39,   -39,   -39,   -39,    -5,   -36,
     -37,   -38,    49,   289,   289,   -12,    18,   289,    26,    22,
     289,    91,   289,    75,    79,    29,    30,    22,   319,   319,
     289,   -39,   -39,   -39,   -39,   -39,   -39,   -39,   -39,   -39,
      36,     1,   -39,   208,    -8,   -39,   -39,   -39,   -39,   289,
     289,    82,   289,    59,    58,    61,    66,    23,    70,    37,
     -34,    48,    63,   289,   289,   -25,    56,   -39,   -39,    67,
     -39,    68,    69,   -33,   -30,   289,   289,   -39,   -39,   -39,
     -39,    60,   -39,   319,   319,   319,   319,   319,   319,   319,
     319,   319,   319,   319,   319,   319,   319,   319,   319,   319,
     319,   319,    71,    72,    77,    73,   -39,   -39,     9,   289,
     289,   289,   -29,   -21,   -15,   289,   289,   115,   289,   -39,
     -39,    74,    76,   124,   -39,    -6,   203,   -39,   -39,   -39,
     123,   289,   128,   289,    78,    81,   -39,     4,     4,     4,
       4,     4,     4,     4,     4,     4,     4,     4,     4,    -8,
      -8,    -8,   -39,   -39,   -39,   -39,   -39,    90,    96,    99,
     -39,   289,    89,    92,    98,   289,   -39,   289,   -39,   289,
     -39,   -39,    88,   107,    97,    22,    22,    22,   -39,    22,
      25,   -39,   -22,   108,   100,   110,   104,    22,    22,   289,
     289,   289,   -39,   -39,   -39,   -39,   125,   126,   129,   -39,
     -39,   -39,   150,   -39,   -39,   -39,    22,   176,   289,    22,
     -39,   -39,   -39,   130,   134,   175,   -39,   136,   137,   138,
     -39,   -39,   -39,    -7,   -39,   -39,    22,   135,   -39,   -39,
      22,   -39,   -39,   -39,   -39,   -39,   -39,   -39,   -39
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     2,     1,    88,    89,    90,    91,    92,    93,
      94,   100,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     4,    26,    27,    30,    31,    32,    28,    29,    33,
       0,    55,    66,    69,    73,    78,    81,    82,    85,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    92,    93,
      94,     0,     0,     0,     0,     0,     0,     3,    54,     0,
      18,     0,     0,     0,     0,     0,     0,    41,    92,    80,
      79,     0,    25,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    84,    86,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    14,
      15,     0,     0,     0,    43,     0,     0,    16,    17,    46,
       0,     0,     0,     0,     0,     0,   101,    56,    57,    67,
      68,    58,    59,    62,    63,    64,    65,    60,    61,    72,
      70,    71,    77,    74,    75,    76,    19,    95,    96,    97,
      83,     0,     0,     0,     0,     0,    11,     0,    12,     0,
      13,    53,     0,     0,     0,     0,     0,     0,    50,     0,
       0,    52,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    87,     5,     6,     7,     0,     0,     0,    95,
      96,    97,    34,    37,    42,    49,     0,     0,     0,     0,
      44,    98,    99,     0,     0,    38,    40,     0,     0,     0,
       8,     9,    10,     0,    48,    51,     0,     0,    23,    24,
       0,    20,    21,    22,    36,    35,    47,    45,    39
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -39,   -39,   131,   -39,   -28,   -39,   -39,   -39,   -39,   -39,
     -39,   -39,   -39,   -39,   -27,    -3,   -39,   -39,    84,   -14,
     -24,   -39,   -39,   -39,   -39
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,    31,    32,    33,    34,    35,    36,    37,
      38,   182,    39,   180,    68,    40,    41,    42,    43,    44,
      45,    46,    47,   108,    48
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      77,   178,   208,   209,    79,    80,     3,    15,   123,    51,
      61,    62,   117,   130,    65,   165,   132,    69,    71,    72,
      50,    52,    53,   167,   118,   131,   166,    81,   133,   169,
     112,   113,   114,    67,   168,    98,    66,   210,   124,    49,
     170,    83,    84,    99,   100,   101,   102,   103,    63,   105,
     107,    67,    95,    96,    97,   179,    54,    55,    56,    57,
     121,   122,     4,     5,     6,     7,    58,    59,    60,    11,
     160,   161,   134,   135,   152,   153,   154,   155,    64,    19,
      67,   149,   150,   151,    73,    23,   206,   207,    74,    75,
      76,    82,   104,   116,     4,     5,     6,     7,    58,    59,
      60,    11,   109,   119,    28,   110,   162,   163,   164,    29,
     111,    19,   171,   172,   115,   174,   125,    23,   120,    30,
     106,   136,   127,   128,   158,   173,   156,   129,   184,   157,
     186,   177,   159,   183,   189,   175,    28,   176,   185,   187,
     190,    29,   188,   191,   193,   199,    70,   194,   202,   203,
     204,    30,   205,   195,   200,   211,   201,   213,   192,   212,
     215,   216,   196,   214,   197,   223,   198,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   224,
     220,   221,   227,   225,   222,   228,   217,   218,   219,   229,
     230,   231,   232,   233,   237,   234,   235,     0,   126,   236,
       0,     0,     0,   238,     0,   226,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,     0,     0,
      16,    17,    18,    19,    20,    21,    22,     0,     0,    23,
      24,    25,    26,    27,     0,     0,     0,     0,     0,    85,
      86,     0,    87,    88,    89,    90,    91,    92,    28,     0,
       0,     0,     0,    29,    93,    94,    95,    96,    97,     0,
       0,     0,   181,    30,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,     0,     0,    16,    17,
      18,    19,    20,    21,    22,     0,     0,    23,    24,    25,
      26,    27,     4,     5,     6,     7,    58,    59,    60,    11,
       0,     0,     0,     0,     0,     0,    28,     0,     0,    19,
       0,    29,     0,     0,     0,    23,     0,     0,     0,     0,
       0,    30,     4,     5,     6,     7,    78,    59,    60,    11,
       0,     0,     0,     0,    28,     0,     0,     0,     0,    29,
       0,     0,     0,     0,     0,    23,     0,     0,     0,    30,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    28,     0,     0,     0,     0,    29,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    30
};

static const yytype_int16 yycheck[] =
{
      27,     7,    24,    25,    28,    29,     0,    14,    33,    46,
      13,    14,    46,    46,    17,    44,    46,    20,    21,    22,
      56,    58,    60,    44,    58,    58,    55,    30,    58,    44,
       7,     8,     9,    58,    55,    43,    10,    59,    65,    44,
      55,    40,    41,    51,    52,    53,    49,    50,    60,    52,
      53,    58,    48,    49,    50,    61,     7,     8,     9,    10,
      63,    64,     3,     4,     5,     6,     7,     8,     9,    10,
      61,    62,    75,    76,    98,    99,   100,   101,    60,    20,
      58,    95,    96,    97,     9,    26,    61,    62,     9,    60,
      60,    55,    10,    56,     3,     4,     5,     6,     7,     8,
       9,    10,    44,    55,    45,    44,   109,   110,   111,    50,
      44,    20,   115,   116,    44,   118,    60,    26,    55,    60,
      61,    61,    55,    55,    47,    10,    55,    58,   131,    57,
     133,     7,    59,    10,    44,    61,    45,    61,    10,    61,
      44,    50,    61,    44,    55,    57,    55,    55,   175,   176,
     177,    60,   179,    55,    47,    47,    59,    47,   161,    59,
     187,   188,   165,    59,   167,    15,   169,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,   206,
      55,    55,   209,     7,    55,    55,   189,   190,   191,    55,
      15,    55,    55,    55,    59,   223,   223,    -1,    67,   226,
      -1,    -1,    -1,   230,    -1,   208,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    -1,    -1,
      17,    18,    19,    20,    21,    22,    23,    -1,    -1,    26,
      27,    28,    29,    30,    -1,    -1,    -1,    -1,    -1,    31,
      32,    -1,    34,    35,    36,    37,    38,    39,    45,    -1,
      -1,    -1,    -1,    50,    46,    47,    48,    49,    50,    -1,
      -1,    -1,    59,    60,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    -1,    -1,    17,    18,
      19,    20,    21,    22,    23,    -1,    -1,    26,    27,    28,
      29,    30,     3,     4,     5,     6,     7,     8,     9,    10,
      -1,    -1,    -1,    -1,    -1,    -1,    45,    -1,    -1,    20,
      -1,    50,    -1,    -1,    -1,    26,    -1,    -1,    -1,    -1,
      -1,    60,     3,     4,     5,     6,     7,     8,     9,    10,
      -1,    -1,    -1,    -1,    45,    -1,    -1,    -1,    -1,    50,
      -1,    -1,    -1,    -1,    -1,    26,    -1,    -1,    -1,    60,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    45,    -1,    -1,    -1,    -1,    50,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    60
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    64,    65,     0,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    17,    18,    19,    20,
      21,    22,    23,    26,    27,    28,    29,    30,    45,    50,
      60,    66,    67,    68,    69,    70,    71,    72,    73,    75,
      78,    79,    80,    81,    82,    83,    84,    85,    87,    44,
      56,    46,    58,    60,     7,     8,     9,    10,     7,     8,
       9,    78,    78,    60,    60,    78,    10,    58,    77,    78,
      55,    78,    78,     9,     9,    60,    60,    77,     7,    83,
      83,    78,    55,    40,    41,    31,    32,    34,    35,    36,
      37,    38,    39,    46,    47,    48,    49,    50,    43,    51,
      52,    53,    78,    78,    10,    78,    61,    78,    86,    44,
      44,    44,     7,     8,     9,    44,    56,    46,    58,    55,
      55,    78,    78,    33,    77,    60,    65,    55,    55,    58,
      46,    58,    46,    58,    78,    78,    61,    81,    81,    81,
      81,    81,    81,    81,    81,    81,    81,    81,    81,    82,
      82,    82,    83,    83,    83,    83,    55,    57,    47,    59,
      61,    62,    78,    78,    78,    44,    55,    44,    55,    44,
      55,    78,    78,    10,    78,    61,    61,     7,     7,    61,
      76,    59,    74,    10,    78,    10,    78,    61,    61,    44,
      44,    44,    78,    55,    55,    55,    78,    78,    78,    57,
      47,    59,    77,    77,    77,    77,    61,    62,    24,    25,
      59,    47,    59,    47,    59,    77,    77,    78,    78,    78,
      55,    55,    55,    15,    77,     7,    78,    77,    55,    55,
      15,    55,    55,    55,    67,    77,    77,    59,    77
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    63,    64,    65,    65,    66,    66,    66,    66,    66,
      66,    66,    66,    66,    66,    66,    66,    66,    66,    66,
      66,    66,    66,    66,    66,    66,    66,    66,    66,    66,
      66,    66,    66,    66,    67,    67,    67,    68,    69,    69,
      70,    71,    72,    72,    73,    73,    74,    74,    75,    75,
      76,    76,    77,    78,    78,    78,    79,    79,    79,    79,
      79,    79,    79,    79,    79,    79,    79,    80,    80,    80,
      81,    81,    81,    81,    82,    82,    82,    82,    82,    83,
      83,    83,    84,    85,    85,    85,    86,    86,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     5,     5,     5,     6,     6,
       6,     4,     4,     4,     3,     3,     3,     3,     2,     4,
       7,     7,     7,     6,     6,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     5,     7,     7,     5,     5,     7,
       5,     2,     5,     3,     5,     7,     0,     4,     6,     5,
       1,     3,     3,     3,     2,     1,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     1,     3,     3,     1,
       3,     3,     3,     1,     3,     3,     3,     3,     1,     2,
       2,     1,     1,     4,     3,     1,     1,     3,     1,     1,
       1,     1,     1,     1,     1,     4,     4,     4,     5,     5,
       1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = RAKU_YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == RAKU_YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use RAKU_YYerror or RAKU_YYUNDEF. */
#define YYERRCODE RAKU_YYUNDEF


/* Enable debugging if requested.  */
#if RAKU_YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !RAKU_YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !RAKU_YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = RAKU_YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == RAKU_YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= RAKU_YYEOF)
    {
      yychar = RAKU_YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == RAKU_YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = RAKU_YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = RAKU_YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: stmt_list  */
#line 188 "raku.y"
        {
            ExprList *all = (yyvsp[0].list);
            /* Partition: subs (ival & SUB_TAG) vs body stmts */
            if (all) {
                /* Pass 1: emit sub defs */
                for (int i = 0; i < all->count; i++) {
                    EXPR_t *e = all->items[i];
                    if (!e || !(e->kind==E_FNC && (e->ival & SUB_TAG))) continue;
                    e->ival &= ~SUB_TAG;   /* restore real nparams */
                    add_proc(e);
                    all->items[i] = NULL;  /* mark consumed */
                }
                /* Pass 2: wrap remaining body stmts in synthetic "main" E_FNC */
                int has_body = 0;
                for (int i = 0; i < all->count; i++) if (all->items[i]) { has_body=1; break; }
                if (has_body) {
                    EXPR_t *mf = leaf_sval(E_FNC, "main"); mf->ival = 0;
                    EXPR_t *mn = expr_new(E_VAR); mn->sval = intern("main");
                    expr_add_child(mf, mn);
                    for (int i = 0; i < all->count; i++)
                        if (all->items[i]) expr_add_child(mf, all->items[i]);
                    add_proc(mf);
                }
                exprlist_free(all);
            }
        }
#line 1478 "raku.tab.c"
    break;

  case 3: /* stmt_list: %empty  */
#line 217 "raku.y"
                     { (yyval.list) = exprlist_new(); }
#line 1484 "raku.tab.c"
    break;

  case 4: /* stmt_list: stmt_list stmt  */
#line 218 "raku.y"
                     { (yyval.list) = exprlist_append((yyvsp[-1].list), (yyvsp[0].node)); }
#line 1490 "raku.tab.c"
    break;

  case 5: /* stmt: KW_MY VAR_SCALAR '=' expr ';'  */
#line 223 "raku.y"
        { (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1496 "raku.tab.c"
    break;

  case 6: /* stmt: KW_MY VAR_ARRAY '=' expr ';'  */
#line 225 "raku.y"
        { (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1502 "raku.tab.c"
    break;

  case 7: /* stmt: KW_MY VAR_HASH '=' expr ';'  */
#line 227 "raku.y"
        { (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1508 "raku.tab.c"
    break;

  case 8: /* stmt: KW_MY IDENT VAR_SCALAR '=' expr ';'  */
#line 230 "raku.y"
        { free((yyvsp[-4].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1514 "raku.tab.c"
    break;

  case 9: /* stmt: KW_MY IDENT VAR_ARRAY '=' expr ';'  */
#line 232 "raku.y"
        { free((yyvsp[-4].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1520 "raku.tab.c"
    break;

  case 10: /* stmt: KW_MY IDENT VAR_HASH '=' expr ';'  */
#line 234 "raku.y"
        { free((yyvsp[-4].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-3].sval)), (yyvsp[-1].node)); }
#line 1526 "raku.tab.c"
    break;

  case 11: /* stmt: KW_MY IDENT VAR_SCALAR ';'  */
#line 236 "raku.y"
        { free((yyvsp[-2].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-1].sval)), leaf_sval(E_QLIT, "")); }
#line 1532 "raku.tab.c"
    break;

  case 12: /* stmt: KW_MY IDENT VAR_ARRAY ';'  */
#line 238 "raku.y"
        { free((yyvsp[-2].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-1].sval)), leaf_sval(E_QLIT, "")); }
#line 1538 "raku.tab.c"
    break;

  case 13: /* stmt: KW_MY IDENT VAR_HASH ';'  */
#line 240 "raku.y"
        { free((yyvsp[-2].sval)); (yyval.node) = expr_binary(E_ASSIGN, var_node((yyvsp[-1].sval)), leaf_sval(E_QLIT, "")); }
#line 1544 "raku.tab.c"
    break;

  case 14: /* stmt: KW_SAY expr ';'  */
#line 242 "raku.y"
        { EXPR_t *c=make_call("write"); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 1550 "raku.tab.c"
    break;

  case 15: /* stmt: KW_PRINT expr ';'  */
#line 244 "raku.y"
        { EXPR_t *c=make_call("writes"); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 1556 "raku.tab.c"
    break;

  case 16: /* stmt: KW_TAKE expr ';'  */
#line 246 "raku.y"
        { (yyval.node)=expr_unary(E_SUSPEND,(yyvsp[-1].node)); }
#line 1562 "raku.tab.c"
    break;

  case 17: /* stmt: KW_RETURN expr ';'  */
#line 248 "raku.y"
        { EXPR_t *r=expr_new(E_RETURN); expr_add_child(r,(yyvsp[-1].node)); (yyval.node)=r; }
#line 1568 "raku.tab.c"
    break;

  case 18: /* stmt: KW_RETURN ';'  */
#line 250 "raku.y"
        { (yyval.node)=expr_new(E_RETURN); }
#line 1574 "raku.tab.c"
    break;

  case 19: /* stmt: VAR_SCALAR '=' expr ';'  */
#line 252 "raku.y"
        { (yyval.node)=expr_binary(E_ASSIGN,var_node((yyvsp[-3].sval)),(yyvsp[-1].node)); }
#line 1580 "raku.tab.c"
    break;

  case 20: /* stmt: VAR_ARRAY '[' expr ']' '=' expr ';'  */
#line 254 "raku.y"
        { EXPR_t *c=make_call("arr_set");
          expr_add_child(c,var_node((yyvsp[-6].sval))); expr_add_child(c,(yyvsp[-4].node)); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 1587 "raku.tab.c"
    break;

  case 21: /* stmt: VAR_HASH '<' IDENT '>' '=' expr ';'  */
#line 257 "raku.y"
        { EXPR_t *c=make_call("hash_set");
          expr_add_child(c,var_node((yyvsp[-6].sval))); expr_add_child(c,leaf_sval(E_QLIT,(yyvsp[-4].sval))); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 1594 "raku.tab.c"
    break;

  case 22: /* stmt: VAR_HASH '{' expr '}' '=' expr ';'  */
#line 260 "raku.y"
        { EXPR_t *c=make_call("hash_set");
          expr_add_child(c,var_node((yyvsp[-6].sval))); expr_add_child(c,(yyvsp[-4].node)); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 1601 "raku.tab.c"
    break;

  case 23: /* stmt: KW_DELETE VAR_HASH '<' IDENT '>' ';'  */
#line 263 "raku.y"
        { EXPR_t *c=make_call("hash_delete");
          expr_add_child(c,var_node((yyvsp[-4].sval))); expr_add_child(c,leaf_sval(E_QLIT,(yyvsp[-2].sval))); (yyval.node)=c; }
#line 1608 "raku.tab.c"
    break;

  case 24: /* stmt: KW_DELETE VAR_HASH '{' expr '}' ';'  */
#line 266 "raku.y"
        { EXPR_t *c=make_call("hash_delete");
          expr_add_child(c,var_node((yyvsp[-4].sval))); expr_add_child(c,(yyvsp[-2].node)); (yyval.node)=c; }
#line 1615 "raku.tab.c"
    break;

  case 25: /* stmt: expr ';'  */
#line 268 "raku.y"
                        { (yyval.node)=(yyvsp[-1].node); }
#line 1621 "raku.tab.c"
    break;

  case 26: /* stmt: if_stmt  */
#line 269 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1627 "raku.tab.c"
    break;

  case 27: /* stmt: while_stmt  */
#line 270 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1633 "raku.tab.c"
    break;

  case 28: /* stmt: for_stmt  */
#line 271 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1639 "raku.tab.c"
    break;

  case 29: /* stmt: given_stmt  */
#line 272 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1645 "raku.tab.c"
    break;

  case 30: /* stmt: unless_stmt  */
#line 273 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1651 "raku.tab.c"
    break;

  case 31: /* stmt: until_stmt  */
#line 274 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1657 "raku.tab.c"
    break;

  case 32: /* stmt: repeat_stmt  */
#line 275 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1663 "raku.tab.c"
    break;

  case 33: /* stmt: sub_decl  */
#line 276 "raku.y"
                        { (yyval.node)=(yyvsp[0].node); }
#line 1669 "raku.tab.c"
    break;

  case 34: /* if_stmt: KW_IF '(' expr ')' block  */
#line 281 "raku.y"
        { EXPR_t *e=expr_new(E_IF); expr_add_child(e,(yyvsp[-2].node)); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1675 "raku.tab.c"
    break;

  case 35: /* if_stmt: KW_IF '(' expr ')' block KW_ELSE block  */
#line 283 "raku.y"
        { EXPR_t *e=expr_new(E_IF); expr_add_child(e,(yyvsp[-4].node)); expr_add_child(e,(yyvsp[-2].node)); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1681 "raku.tab.c"
    break;

  case 36: /* if_stmt: KW_IF '(' expr ')' block KW_ELSE if_stmt  */
#line 285 "raku.y"
        { EXPR_t *e=expr_new(E_IF); expr_add_child(e,(yyvsp[-4].node)); expr_add_child(e,(yyvsp[-2].node)); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1687 "raku.tab.c"
    break;

  case 37: /* while_stmt: KW_WHILE '(' expr ')' block  */
#line 290 "raku.y"
        { (yyval.node)=expr_binary(E_WHILE,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1693 "raku.tab.c"
    break;

  case 38: /* unless_stmt: KW_UNLESS '(' expr ')' block  */
#line 296 "raku.y"
        { EXPR_t *e=expr_new(E_IF); expr_add_child(e,expr_unary(E_NOT,(yyvsp[-2].node))); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1699 "raku.tab.c"
    break;

  case 39: /* unless_stmt: KW_UNLESS '(' expr ')' block KW_ELSE block  */
#line 298 "raku.y"
        { EXPR_t *e=expr_new(E_IF); expr_add_child(e,expr_unary(E_NOT,(yyvsp[-4].node))); expr_add_child(e,(yyvsp[-2].node)); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1705 "raku.tab.c"
    break;

  case 40: /* until_stmt: KW_UNTIL '(' expr ')' block  */
#line 304 "raku.y"
        { EXPR_t *e=expr_new(E_UNTIL); expr_add_child(e,(yyvsp[-2].node)); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1711 "raku.tab.c"
    break;

  case 41: /* repeat_stmt: KW_REPEAT block  */
#line 310 "raku.y"
        { EXPR_t *e=expr_new(E_REPEAT); expr_add_child(e,(yyvsp[0].node)); (yyval.node)=e; }
#line 1717 "raku.tab.c"
    break;

  case 42: /* for_stmt: KW_FOR expr OP_ARROW VAR_SCALAR block  */
#line 316 "raku.y"
        { EXPR_t *iter=(yyvsp[-3].node); const char *vname=strip_sigil((yyvsp[-1].sval));
          if (iter->kind==E_TO) {
              /* range case: lo=children[0], hi=children[1] */
              (yyval.node) = make_for_range(iter->children[0], iter->children[1], vname, (yyvsp[0].node));
          } else {
              EXPR_t *gen=(iter->kind==E_VAR)?expr_unary(E_ITERATE,iter):iter;
              /* RK-16: store loop-variable name in gen->sval so icn_drive can bind it */
              gen->sval = (char *)vname;
              (yyval.node)=expr_binary(E_EVERY,gen,(yyvsp[0].node));
          } }
#line 1732 "raku.tab.c"
    break;

  case 43: /* for_stmt: KW_FOR expr block  */
#line 327 "raku.y"
        { EXPR_t *gen=((yyvsp[-1].node)->kind==E_VAR)?expr_unary(E_ITERATE,(yyvsp[-1].node)):(yyvsp[-1].node);
          (yyval.node)=expr_binary(E_EVERY,gen,(yyvsp[0].node)); }
#line 1739 "raku.tab.c"
    break;

  case 44: /* given_stmt: KW_GIVEN expr '{' when_list '}'  */
#line 333 "raku.y"
        { /* RK-18d: E_CASE[ topic, cmpnode0, val0, body0, cmpnode1, val1, body1, ... ]
           * cmp kind stored in separate E_ILIT node (ival=EKind) to avoid corrupting val->ival. */
          EXPR_t *ec=expr_new(E_CASE);
          expr_add_child(ec,(yyvsp[-3].node));
          ExprList *whens=(yyvsp[-1].list);
          for(int i=0;i<whens->count;i++){
              EXPR_t *pair=whens->items[i];
              EKind cmp=(EKind)(pair->ival);
              EXPR_t *val=pair->children[0], *body=pair->children[1];
              EXPR_t *cn=expr_new(E_ILIT); cn->ival=(long)cmp;
              expr_add_child(ec,cn); expr_add_child(ec,val); expr_add_child(ec,body);
          }
          exprlist_free(whens);
          (yyval.node)=ec; }
#line 1758 "raku.tab.c"
    break;

  case 45: /* given_stmt: KW_GIVEN expr '{' when_list KW_DEFAULT block '}'  */
#line 348 "raku.y"
        { /* RK-18d: E_CASE with default: E_NUL cmpnode + E_NUL val + body at end. */
          EXPR_t *ec=expr_new(E_CASE);
          expr_add_child(ec,(yyvsp[-5].node));
          ExprList *whens=(yyvsp[-3].list);
          for(int i=0;i<whens->count;i++){
              EXPR_t *pair=whens->items[i];
              EKind cmp=(EKind)(pair->ival);
              EXPR_t *val=pair->children[0], *body=pair->children[1];
              EXPR_t *cn=expr_new(E_ILIT); cn->ival=(long)cmp;
              expr_add_child(ec,cn); expr_add_child(ec,val); expr_add_child(ec,body);
          }
          exprlist_free(whens);
          expr_add_child(ec,expr_new(E_NUL)); expr_add_child(ec,expr_new(E_NUL)); expr_add_child(ec,(yyvsp[-1].node));
          (yyval.node)=ec; }
#line 1777 "raku.tab.c"
    break;

  case 46: /* when_list: %empty  */
#line 365 "raku.y"
                   { (yyval.list)=exprlist_new(); }
#line 1783 "raku.tab.c"
    break;

  case 47: /* when_list: when_list KW_WHEN expr block  */
#line 367 "raku.y"
        { EKind cmp=((yyvsp[-1].node)->kind==E_QLIT)?E_LEQ:E_EQ;
          EXPR_t *pair=expr_new(E_SEQ_EXPR);
          pair->ival=(long)cmp;
          expr_add_child(pair,(yyvsp[-1].node)); expr_add_child(pair,(yyvsp[0].node));
          (yyval.list)=exprlist_append((yyvsp[-3].list),pair); }
#line 1793 "raku.tab.c"
    break;

  case 48: /* sub_decl: KW_SUB IDENT '(' param_list ')' block  */
#line 376 "raku.y"
        { ExprList *params=(yyvsp[-2].list); int np=params?params->count:0;
          EXPR_t *e=leaf_sval(E_FNC,(yyvsp[-4].sval)); e->ival=(long)np|SUB_TAG;
          EXPR_t *nn=expr_new(E_VAR); nn->sval=intern((yyvsp[-4].sval)); expr_add_child(e,nn);
          if(params){ for(int i=0;i<np;i++) expr_add_child(e,params->items[i]); exprlist_free(params); }
          EXPR_t *body=(yyvsp[0].node);
          for(int i=0;i<body->nchildren;i++) expr_add_child(e,body->children[i]);
          (yyval.node)=e; }
#line 1805 "raku.tab.c"
    break;

  case 49: /* sub_decl: KW_SUB IDENT '(' ')' block  */
#line 384 "raku.y"
        { EXPR_t *e=leaf_sval(E_FNC,(yyvsp[-3].sval)); e->ival=(long)0|SUB_TAG;
          EXPR_t *nn=expr_new(E_VAR); nn->sval=intern((yyvsp[-3].sval)); expr_add_child(e,nn);
          EXPR_t *body=(yyvsp[0].node);
          for(int i=0;i<body->nchildren;i++) expr_add_child(e,body->children[i]);
          (yyval.node)=e; }
#line 1815 "raku.tab.c"
    break;

  case 50: /* param_list: VAR_SCALAR  */
#line 392 "raku.y"
                             { (yyval.list)=exprlist_append(exprlist_new(),var_node((yyvsp[0].sval))); }
#line 1821 "raku.tab.c"
    break;

  case 51: /* param_list: param_list ',' VAR_SCALAR  */
#line 393 "raku.y"
                                { (yyval.list)=exprlist_append((yyvsp[-2].list),var_node((yyvsp[0].sval))); }
#line 1827 "raku.tab.c"
    break;

  case 52: /* block: '{' stmt_list '}'  */
#line 397 "raku.y"
                         { (yyval.node)=make_seq((yyvsp[-1].list)); }
#line 1833 "raku.tab.c"
    break;

  case 53: /* expr: VAR_SCALAR '=' expr  */
#line 401 "raku.y"
                           { (yyval.node)=expr_binary(E_ASSIGN,var_node((yyvsp[-2].sval)),(yyvsp[0].node)); }
#line 1839 "raku.tab.c"
    break;

  case 54: /* expr: KW_GATHER block  */
#line 402 "raku.y"
                           { (yyval.node)=expr_unary(E_ITERATE,(yyvsp[0].node)); }
#line 1845 "raku.tab.c"
    break;

  case 55: /* expr: cmp_expr  */
#line 403 "raku.y"
                           { (yyval.node)=(yyvsp[0].node); }
#line 1851 "raku.tab.c"
    break;

  case 56: /* cmp_expr: cmp_expr OP_AND add_expr  */
#line 407 "raku.y"
                                { (yyval.node)=expr_binary(E_SEQ,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1857 "raku.tab.c"
    break;

  case 57: /* cmp_expr: cmp_expr OP_OR add_expr  */
#line 408 "raku.y"
                                { (yyval.node)=expr_binary(E_ALT,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1863 "raku.tab.c"
    break;

  case 58: /* cmp_expr: add_expr OP_EQ add_expr  */
#line 409 "raku.y"
                                { (yyval.node)=expr_binary(E_EQ,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1869 "raku.tab.c"
    break;

  case 59: /* cmp_expr: add_expr OP_NE add_expr  */
#line 410 "raku.y"
                                { (yyval.node)=expr_binary(E_NE,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1875 "raku.tab.c"
    break;

  case 60: /* cmp_expr: add_expr '<' add_expr  */
#line 411 "raku.y"
                                { (yyval.node)=expr_binary(E_LT,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1881 "raku.tab.c"
    break;

  case 61: /* cmp_expr: add_expr '>' add_expr  */
#line 412 "raku.y"
                                { (yyval.node)=expr_binary(E_GT,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1887 "raku.tab.c"
    break;

  case 62: /* cmp_expr: add_expr OP_LE add_expr  */
#line 413 "raku.y"
                                { (yyval.node)=expr_binary(E_LE,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1893 "raku.tab.c"
    break;

  case 63: /* cmp_expr: add_expr OP_GE add_expr  */
#line 414 "raku.y"
                                { (yyval.node)=expr_binary(E_GE,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1899 "raku.tab.c"
    break;

  case 64: /* cmp_expr: add_expr OP_SEQ add_expr  */
#line 415 "raku.y"
                                { (yyval.node)=expr_binary(E_LEQ,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1905 "raku.tab.c"
    break;

  case 65: /* cmp_expr: add_expr OP_SNE add_expr  */
#line 416 "raku.y"
                                { (yyval.node)=expr_binary(E_LNE,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1911 "raku.tab.c"
    break;

  case 66: /* cmp_expr: range_expr  */
#line 417 "raku.y"
                               { (yyval.node)=(yyvsp[0].node); }
#line 1917 "raku.tab.c"
    break;

  case 67: /* range_expr: add_expr OP_RANGE add_expr  */
#line 421 "raku.y"
                                    { (yyval.node)=expr_binary(E_TO,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1923 "raku.tab.c"
    break;

  case 68: /* range_expr: add_expr OP_RANGE_EX add_expr  */
#line 422 "raku.y"
                                    { (yyval.node)=expr_binary(E_TO,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1929 "raku.tab.c"
    break;

  case 69: /* range_expr: add_expr  */
#line 423 "raku.y"
                                    { (yyval.node)=(yyvsp[0].node); }
#line 1935 "raku.tab.c"
    break;

  case 70: /* add_expr: add_expr '+' mul_expr  */
#line 427 "raku.y"
                             { (yyval.node)=expr_binary(E_ADD,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1941 "raku.tab.c"
    break;

  case 71: /* add_expr: add_expr '-' mul_expr  */
#line 428 "raku.y"
                             { (yyval.node)=expr_binary(E_SUB,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1947 "raku.tab.c"
    break;

  case 72: /* add_expr: add_expr '~' mul_expr  */
#line 429 "raku.y"
                             { (yyval.node)=expr_binary(E_CAT,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1953 "raku.tab.c"
    break;

  case 73: /* add_expr: mul_expr  */
#line 430 "raku.y"
                             { (yyval.node)=(yyvsp[0].node); }
#line 1959 "raku.tab.c"
    break;

  case 74: /* mul_expr: mul_expr '*' unary_expr  */
#line 434 "raku.y"
                                  { (yyval.node)=expr_binary(E_MUL,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1965 "raku.tab.c"
    break;

  case 75: /* mul_expr: mul_expr '/' unary_expr  */
#line 435 "raku.y"
                                  { (yyval.node)=expr_binary(E_DIV,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1971 "raku.tab.c"
    break;

  case 76: /* mul_expr: mul_expr '%' unary_expr  */
#line 436 "raku.y"
                                  { (yyval.node)=expr_binary(E_MOD,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1977 "raku.tab.c"
    break;

  case 77: /* mul_expr: mul_expr OP_DIV unary_expr  */
#line 437 "raku.y"
                                  { (yyval.node)=expr_binary(E_DIV,(yyvsp[-2].node),(yyvsp[0].node)); }
#line 1983 "raku.tab.c"
    break;

  case 78: /* mul_expr: unary_expr  */
#line 438 "raku.y"
                                  { (yyval.node)=(yyvsp[0].node); }
#line 1989 "raku.tab.c"
    break;

  case 79: /* unary_expr: '-' unary_expr  */
#line 442 "raku.y"
                                   { (yyval.node)=expr_unary(E_MNS,(yyvsp[0].node)); }
#line 1995 "raku.tab.c"
    break;

  case 80: /* unary_expr: '!' unary_expr  */
#line 443 "raku.y"
                                   { (yyval.node)=expr_unary(E_NOT,(yyvsp[0].node)); }
#line 2001 "raku.tab.c"
    break;

  case 81: /* unary_expr: postfix_expr  */
#line 444 "raku.y"
                                   { (yyval.node)=(yyvsp[0].node); }
#line 2007 "raku.tab.c"
    break;

  case 82: /* postfix_expr: call_expr  */
#line 447 "raku.y"
                         { (yyval.node)=(yyvsp[0].node); }
#line 2013 "raku.tab.c"
    break;

  case 83: /* call_expr: IDENT '(' arg_list ')'  */
#line 451 "raku.y"
        { EXPR_t *e=make_call((yyvsp[-3].sval));
          ExprList *args=(yyvsp[-1].list);
          if(args){ for(int i=0;i<args->count;i++) expr_add_child(e,args->items[i]); exprlist_free(args); }
          (yyval.node)=e; }
#line 2022 "raku.tab.c"
    break;

  case 84: /* call_expr: IDENT '(' ')'  */
#line 455 "raku.y"
                     { (yyval.node)=make_call((yyvsp[-2].sval)); }
#line 2028 "raku.tab.c"
    break;

  case 85: /* call_expr: atom  */
#line 456 "raku.y"
                     { (yyval.node)=(yyvsp[0].node); }
#line 2034 "raku.tab.c"
    break;

  case 86: /* arg_list: expr  */
#line 460 "raku.y"
                        { (yyval.list)=exprlist_append(exprlist_new(),(yyvsp[0].node)); }
#line 2040 "raku.tab.c"
    break;

  case 87: /* arg_list: arg_list ',' expr  */
#line 461 "raku.y"
                        { (yyval.list)=exprlist_append((yyvsp[-2].list),(yyvsp[0].node)); }
#line 2046 "raku.tab.c"
    break;

  case 88: /* atom: LIT_INT  */
#line 465 "raku.y"
                      { EXPR_t *e=expr_new(E_ILIT); e->ival=(yyvsp[0].ival); (yyval.node)=e; }
#line 2052 "raku.tab.c"
    break;

  case 89: /* atom: LIT_FLOAT  */
#line 466 "raku.y"
                      { EXPR_t *e=expr_new(E_FLIT); e->dval=(yyvsp[0].dval); (yyval.node)=e; }
#line 2058 "raku.tab.c"
    break;

  case 90: /* atom: LIT_STR  */
#line 467 "raku.y"
                      { (yyval.node)=leaf_sval(E_QLIT,(yyvsp[0].sval)); }
#line 2064 "raku.tab.c"
    break;

  case 91: /* atom: LIT_INTERP_STR  */
#line 468 "raku.y"
                      { (yyval.node)=lower_interp_str((yyvsp[0].sval)); }
#line 2070 "raku.tab.c"
    break;

  case 92: /* atom: VAR_SCALAR  */
#line 469 "raku.y"
                      { (yyval.node)=var_node((yyvsp[0].sval)); }
#line 2076 "raku.tab.c"
    break;

  case 93: /* atom: VAR_ARRAY  */
#line 470 "raku.y"
                      { (yyval.node)=var_node((yyvsp[0].sval)); }
#line 2082 "raku.tab.c"
    break;

  case 94: /* atom: VAR_HASH  */
#line 471 "raku.y"
                      { (yyval.node)=var_node((yyvsp[0].sval)); }
#line 2088 "raku.tab.c"
    break;

  case 95: /* atom: VAR_ARRAY '[' expr ']'  */
#line 473 "raku.y"
        { EXPR_t *c=make_call("arr_get"); expr_add_child(c,var_node((yyvsp[-3].sval))); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 2094 "raku.tab.c"
    break;

  case 96: /* atom: VAR_HASH '<' IDENT '>'  */
#line 475 "raku.y"
        { EXPR_t *c=make_call("hash_get"); expr_add_child(c,var_node((yyvsp[-3].sval))); expr_add_child(c,leaf_sval(E_QLIT,(yyvsp[-1].sval))); (yyval.node)=c; }
#line 2100 "raku.tab.c"
    break;

  case 97: /* atom: VAR_HASH '{' expr '}'  */
#line 477 "raku.y"
        { EXPR_t *c=make_call("hash_get"); expr_add_child(c,var_node((yyvsp[-3].sval))); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 2106 "raku.tab.c"
    break;

  case 98: /* atom: KW_EXISTS VAR_HASH '<' IDENT '>'  */
#line 479 "raku.y"
        { EXPR_t *c=make_call("hash_exists"); expr_add_child(c,var_node((yyvsp[-3].sval))); expr_add_child(c,leaf_sval(E_QLIT,(yyvsp[-1].sval))); (yyval.node)=c; }
#line 2112 "raku.tab.c"
    break;

  case 99: /* atom: KW_EXISTS VAR_HASH '{' expr '}'  */
#line 481 "raku.y"
        { EXPR_t *c=make_call("hash_exists"); expr_add_child(c,var_node((yyvsp[-3].sval))); expr_add_child(c,(yyvsp[-1].node)); (yyval.node)=c; }
#line 2118 "raku.tab.c"
    break;

  case 100: /* atom: IDENT  */
#line 482 "raku.y"
                      { (yyval.node)=var_node((yyvsp[0].sval)); }
#line 2124 "raku.tab.c"
    break;

  case 101: /* atom: '(' expr ')'  */
#line 483 "raku.y"
                      { (yyval.node)=(yyvsp[-1].node); }
#line 2130 "raku.tab.c"
    break;


#line 2134 "raku.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == RAKU_YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= RAKU_YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == RAKU_YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = RAKU_YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != RAKU_YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 486 "raku.y"


/* ── Parse entry (sets up flex buffer and calls yyparse) ─────────────── */
extern void *raku_yy_scan_string(const char *);
extern void  raku_yy_delete_buffer(void *);

Program *raku_parse_string(const char *src) {
    raku_prog_result = NULL;
    void *buf = raku_yy_scan_string(src);
    raku_yyparse();
    raku_yy_delete_buffer(buf);
    return raku_prog_result;
}
