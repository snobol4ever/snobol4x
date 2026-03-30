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




/* First part of user prologue.  */
#line 1 "rebus.y"

/*
 * rebus.y  —  Bison grammar for the Rebus language (Griswold TR 84-9)
 *
 * Grammar structure (top-down):
 *
 *   program   ::= decl* EOF
 *   decl      ::= function_decl | record_decl
 *
 *   record_decl   ::= 'record' IDENT '(' idlist ')'
 *   function_decl ::= 'function' IDENT '(' idlist ')'
 *                       ['local' idlist ';']
 *                       ['initial' stmt ';']
 *                       stmt*
 *                     'end'
 *
 *   stmt  ::= expr_stmt | if_stmt | unless_stmt | while_stmt | until_stmt
 *           | repeat_stmt | for_stmt | case_stmt
 *           | 'exit' | 'next' | 'fail' | 'stop'
 *           | 'return' [expr]
 *           | match_stmt | replace_stmt | repln_stmt
 *           | compound_stmt
 *
 *   expr  ::= assignment | comparison | concat | arith | unary | primary
 *
 * Semicolons are inserted by the lexer (virtual semicolons after certain
 * tokens at end-of-line).  The grammar uses ';' as the statement terminator.
 *
 * Operator precedence (low → high, matches Rebus/Icon):
 *   := :=: ||:= +:= -:=         (assignments, right-assoc)
 *   |                            (pattern alternation)
 *   || &                         (string/pattern concat)
 *   < <= > >= = ~= == ~== << <<= >> >>=  (comparisons)
 *   + -                          (addition, left-assoc)
 *   * / %                        (multiplication, left-assoc)
 *   ^ **                         (exponentiation, right-assoc)
 *   unary: - + ~ \ / ! @ $ .    (right-assoc, high)
 *
 * Pattern operators:
 *   expr ? pat            (match)
 *   expr ? pat <- expr    (replace)
 *   expr ?- pat           (replace with empty)
 *   pat . var             (conditional capture)
 *   pat $ var             (immediate capture)
 */

#include "rebus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RProgram *prog;
extern RProgram *rebus_parsed_program;
extern int       rebus_nerrors;

/* ---- helper: make binary expr ---- */
static RExpr *rbinop(REKind k, RExpr *l, RExpr *r, int lineno) {
    RExpr *e = rexpr_new(k, lineno);
    e->left  = l;
    e->right = r;
    return e;
}

/* ---- helpers for growing arrays ---- */
typedef struct { char **a; int n, cap; } SAL;  /* string array list */
static SAL *sal_new(void) {
    SAL *s = calloc(1, sizeof *s);
    s->cap = 4; s->a = malloc(4 * sizeof(char *));
    return s;
}
static void sal_push(SAL *s, char *v) {
    if (s->n >= s->cap) { s->cap *= 2; s->a = realloc(s->a, s->cap * sizeof(char *)); }
    s->a[s->n++] = v;
}

typedef struct { RExpr **a; int n, cap; } EAL;  /* expr array list */
static EAL *eal_new(void) {
    EAL *e = calloc(1, sizeof *e);
    e->cap = 4; e->a = malloc(4 * sizeof(RExpr *));
    return e;
}
static void eal_push(EAL *e, RExpr *v) {
    if (e->n >= e->cap) { e->cap *= 2; e->a = realloc(e->a, e->cap * sizeof(RExpr *)); }
    e->a[e->n++] = v;
}

typedef struct { RStmt **a; int n, cap; } STAL; /* stmt array list */
static STAL *stal_new(void) {
    STAL *s = calloc(1, sizeof *s);
    s->cap = 8; s->a = malloc(8 * sizeof(RStmt *));
    return s;
}
static void stal_push(STAL *s, RStmt *v) {
    if (s->n >= s->cap) { s->cap *= 2; s->a = realloc(s->a, s->cap * sizeof(RStmt *)); }
    s->a[s->n++] = v;
}

extern int  yylex(void);
extern void yyerror(const char *);
extern int  yylineno;

#line 173 "rebus.tab.c"

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

#include "rebus.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_IDENT = 3,                    /* T_IDENT  */
  YYSYMBOL_T_STR = 4,                      /* T_STR  */
  YYSYMBOL_T_KEYWORD = 5,                  /* T_KEYWORD  */
  YYSYMBOL_T_INT = 6,                      /* T_INT  */
  YYSYMBOL_T_REAL = 7,                     /* T_REAL  */
  YYSYMBOL_T_CASE = 8,                     /* T_CASE  */
  YYSYMBOL_T_DEFAULT = 9,                  /* T_DEFAULT  */
  YYSYMBOL_T_DO = 10,                      /* T_DO  */
  YYSYMBOL_T_ELSE = 11,                    /* T_ELSE  */
  YYSYMBOL_T_END = 12,                     /* T_END  */
  YYSYMBOL_T_EXIT = 13,                    /* T_EXIT  */
  YYSYMBOL_T_FAIL = 14,                    /* T_FAIL  */
  YYSYMBOL_T_FOR = 15,                     /* T_FOR  */
  YYSYMBOL_T_FROM = 16,                    /* T_FROM  */
  YYSYMBOL_T_FUNCTION = 17,                /* T_FUNCTION  */
  YYSYMBOL_T_BY = 18,                      /* T_BY  */
  YYSYMBOL_T_IF = 19,                      /* T_IF  */
  YYSYMBOL_T_INITIAL = 20,                 /* T_INITIAL  */
  YYSYMBOL_T_LOCAL = 21,                   /* T_LOCAL  */
  YYSYMBOL_T_NEXT = 22,                    /* T_NEXT  */
  YYSYMBOL_T_OF = 23,                      /* T_OF  */
  YYSYMBOL_T_RECORD = 24,                  /* T_RECORD  */
  YYSYMBOL_T_REPEAT = 25,                  /* T_REPEAT  */
  YYSYMBOL_T_RETURN = 26,                  /* T_RETURN  */
  YYSYMBOL_T_STOP = 27,                    /* T_STOP  */
  YYSYMBOL_T_THEN = 28,                    /* T_THEN  */
  YYSYMBOL_T_TO = 29,                      /* T_TO  */
  YYSYMBOL_T_UNLESS = 30,                  /* T_UNLESS  */
  YYSYMBOL_T_UNTIL = 31,                   /* T_UNTIL  */
  YYSYMBOL_T_WHILE = 32,                   /* T_WHILE  */
  YYSYMBOL_T_ASSIGN = 33,                  /* T_ASSIGN  */
  YYSYMBOL_T_EXCHANGE = 34,                /* T_EXCHANGE  */
  YYSYMBOL_T_ADDASSIGN = 35,               /* T_ADDASSIGN  */
  YYSYMBOL_T_SUBASSIGN = 36,               /* T_SUBASSIGN  */
  YYSYMBOL_T_CATASSIGN = 37,               /* T_CATASSIGN  */
  YYSYMBOL_T_QUESTMINUS = 38,              /* T_QUESTMINUS  */
  YYSYMBOL_T_ARROW = 39,                   /* T_ARROW  */
  YYSYMBOL_T_STRCAT = 40,                  /* T_STRCAT  */
  YYSYMBOL_T_STARSTAR = 41,                /* T_STARSTAR  */
  YYSYMBOL_T_NE = 42,                      /* T_NE  */
  YYSYMBOL_T_GE = 43,                      /* T_GE  */
  YYSYMBOL_T_LE = 44,                      /* T_LE  */
  YYSYMBOL_T_SEQ = 45,                     /* T_SEQ  */
  YYSYMBOL_T_SNE = 46,                     /* T_SNE  */
  YYSYMBOL_T_SGT = 47,                     /* T_SGT  */
  YYSYMBOL_T_SGE = 48,                     /* T_SGE  */
  YYSYMBOL_T_SLT = 49,                     /* T_SLT  */
  YYSYMBOL_T_SLE = 50,                     /* T_SLE  */
  YYSYMBOL_T_PLUSCOLON = 51,               /* T_PLUSCOLON  */
  YYSYMBOL_LOWER_THAN_ELSE = 52,           /* LOWER_THAN_ELSE  */
  YYSYMBOL_53_ = 53,                       /* '|'  */
  YYSYMBOL_54_ = 54,                       /* '&'  */
  YYSYMBOL_55_ = 55,                       /* '<'  */
  YYSYMBOL_56_ = 56,                       /* '>'  */
  YYSYMBOL_57_ = 57,                       /* '='  */
  YYSYMBOL_58_ = 58,                       /* '+'  */
  YYSYMBOL_59_ = 59,                       /* '-'  */
  YYSYMBOL_60_ = 60,                       /* '*'  */
  YYSYMBOL_61_ = 61,                       /* '/'  */
  YYSYMBOL_62_ = 62,                       /* '%'  */
  YYSYMBOL_63_ = 63,                       /* '^'  */
  YYSYMBOL_UMINUS = 64,                    /* UMINUS  */
  YYSYMBOL_UPLUS = 65,                     /* UPLUS  */
  YYSYMBOL_UTILDE = 66,                    /* UTILDE  */
  YYSYMBOL_UBACK = 67,                     /* UBACK  */
  YYSYMBOL_USLASH = 68,                    /* USLASH  */
  YYSYMBOL_UBANG = 69,                     /* UBANG  */
  YYSYMBOL_UAT = 70,                       /* UAT  */
  YYSYMBOL_UDOLLAR = 71,                   /* UDOLLAR  */
  YYSYMBOL_UDOT = 72,                      /* UDOT  */
  YYSYMBOL_73_ = 73,                       /* ';'  */
  YYSYMBOL_74_ = 74,                       /* '('  */
  YYSYMBOL_75_ = 75,                       /* ')'  */
  YYSYMBOL_76_ = 76,                       /* ','  */
  YYSYMBOL_77_ = 77,                       /* '?'  */
  YYSYMBOL_78_ = 78,                       /* '{'  */
  YYSYMBOL_79_ = 79,                       /* '}'  */
  YYSYMBOL_80_ = 80,                       /* ':'  */
  YYSYMBOL_81_ = 81,                       /* '~'  */
  YYSYMBOL_82_ = 82,                       /* '\\'  */
  YYSYMBOL_83_ = 83,                       /* '!'  */
  YYSYMBOL_84_ = 84,                       /* '@'  */
  YYSYMBOL_85_ = 85,                       /* '$'  */
  YYSYMBOL_86_ = 86,                       /* '.'  */
  YYSYMBOL_87_ = 87,                       /* '['  */
  YYSYMBOL_88_ = 88,                       /* ']'  */
  YYSYMBOL_YYACCEPT = 89,                  /* $accept  */
  YYSYMBOL_program = 90,                   /* program  */
  YYSYMBOL_decl_list = 91,                 /* decl_list  */
  YYSYMBOL_decl = 92,                      /* decl  */
  YYSYMBOL_opt_semi = 93,                  /* opt_semi  */
  YYSYMBOL_record_decl = 94,               /* record_decl  */
  YYSYMBOL_function_decl = 95,             /* function_decl  */
  YYSYMBOL_opt_params = 96,                /* opt_params  */
  YYSYMBOL_opt_locals = 97,                /* opt_locals  */
  YYSYMBOL_opt_initial = 98,               /* opt_initial  */
  YYSYMBOL_stmt_list = 99,                 /* stmt_list  */
  YYSYMBOL_stmt_list_ne = 100,             /* stmt_list_ne  */
  YYSYMBOL_idlist_ne = 101,                /* idlist_ne  */
  YYSYMBOL_opt_idlist = 102,               /* opt_idlist  */
  YYSYMBOL_stmt = 103,                     /* stmt  */
  YYSYMBOL_expr_as_stmt = 104,             /* expr_as_stmt  */
  YYSYMBOL_compound_stmt = 105,            /* compound_stmt  */
  YYSYMBOL_stmt_body = 106,                /* stmt_body  */
  YYSYMBOL_if_stmt = 107,                  /* if_stmt  */
  YYSYMBOL_unless_stmt = 108,              /* unless_stmt  */
  YYSYMBOL_while_stmt = 109,               /* while_stmt  */
  YYSYMBOL_until_stmt = 110,               /* until_stmt  */
  YYSYMBOL_repeat_stmt = 111,              /* repeat_stmt  */
  YYSYMBOL_for_stmt = 112,                 /* for_stmt  */
  YYSYMBOL_case_stmt = 113,                /* case_stmt  */
  YYSYMBOL_caselist = 114,                 /* caselist  */
  YYSYMBOL_caseclause = 115,               /* caseclause  */
  YYSYMBOL_expr = 116,                     /* expr  */
  YYSYMBOL_assign_expr = 117,              /* assign_expr  */
  YYSYMBOL_alt_expr = 118,                 /* alt_expr  */
  YYSYMBOL_cat_expr = 119,                 /* cat_expr  */
  YYSYMBOL_cmp_expr = 120,                 /* cmp_expr  */
  YYSYMBOL_add_expr = 121,                 /* add_expr  */
  YYSYMBOL_mul_expr = 122,                 /* mul_expr  */
  YYSYMBOL_pow_expr = 123,                 /* pow_expr  */
  YYSYMBOL_unary_expr = 124,               /* unary_expr  */
  YYSYMBOL_postfix_expr = 125,             /* postfix_expr  */
  YYSYMBOL_primary = 126,                  /* primary  */
  YYSYMBOL_pat_expr = 127,                 /* pat_expr  */
  YYSYMBOL_opt_expr = 128,                 /* opt_expr  */
  YYSYMBOL_arglist = 129,                  /* arglist  */
  YYSYMBOL_arglist_ne = 130                /* arglist_ne  */
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
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

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
#define YYLAST   271

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  89
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  42
/* YYNRULES -- Number of rules.  */
#define YYNRULES  129
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  239

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   316


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
       2,     2,     2,    83,     2,     2,    85,    62,    54,     2,
      74,    75,    60,    58,    76,    59,    86,    61,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    80,    73,
      55,    57,    56,    77,    84,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    87,    82,    88,    63,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    78,    53,    79,    81,     2,     2,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    64,    65,
      66,    67,    68,    69,    70,    71,    72
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   186,   186,   190,   191,   203,   204,   208,   209,   215,
     216,   224,   246,   269,   270,   274,   275,   279,   280,   281,
     287,   288,   292,   293,   294,   301,   308,   316,   317,   321,
     322,   330,   331,   332,   333,   334,   335,   336,   337,   338,
     339,   340,   341,   342,   346,   351,   355,   359,   363,   371,
     386,   391,   399,   410,   421,   431,   442,   452,   462,   476,
     486,   487,   492,   496,   503,   526,   531,   532,   533,   534,
     535,   536,   541,   542,   547,   548,   549,   554,   555,   556,
     557,   558,   559,   560,   561,   562,   563,   564,   565,   566,
     571,   572,   573,   578,   579,   580,   581,   586,   587,   588,
     593,   594,   595,   596,   597,   598,   599,   600,   604,   605,
     610,   611,   628,   638,   652,   656,   664,   665,   666,   667,
     668,   669,   675,   680,   681,   686,   687,   691,   692,   693
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "T_IDENT", "T_STR",
  "T_KEYWORD", "T_INT", "T_REAL", "T_CASE", "T_DEFAULT", "T_DO", "T_ELSE",
  "T_END", "T_EXIT", "T_FAIL", "T_FOR", "T_FROM", "T_FUNCTION", "T_BY",
  "T_IF", "T_INITIAL", "T_LOCAL", "T_NEXT", "T_OF", "T_RECORD", "T_REPEAT",
  "T_RETURN", "T_STOP", "T_THEN", "T_TO", "T_UNLESS", "T_UNTIL", "T_WHILE",
  "T_ASSIGN", "T_EXCHANGE", "T_ADDASSIGN", "T_SUBASSIGN", "T_CATASSIGN",
  "T_QUESTMINUS", "T_ARROW", "T_STRCAT", "T_STARSTAR", "T_NE", "T_GE",
  "T_LE", "T_SEQ", "T_SNE", "T_SGT", "T_SGE", "T_SLT", "T_SLE",
  "T_PLUSCOLON", "LOWER_THAN_ELSE", "'|'", "'&'", "'<'", "'>'", "'='",
  "'+'", "'-'", "'*'", "'/'", "'%'", "'^'", "UMINUS", "UPLUS", "UTILDE",
  "UBACK", "USLASH", "UBANG", "UAT", "UDOLLAR", "UDOT", "';'", "'('",
  "')'", "','", "'?'", "'{'", "'}'", "':'", "'~'", "'\\\\'", "'!'", "'@'",
  "'$'", "'.'", "'['", "']'", "$accept", "program", "decl_list", "decl",
  "opt_semi", "record_decl", "function_decl", "opt_params", "opt_locals",
  "opt_initial", "stmt_list", "stmt_list_ne", "idlist_ne", "opt_idlist",
  "stmt", "expr_as_stmt", "compound_stmt", "stmt_body", "if_stmt",
  "unless_stmt", "while_stmt", "until_stmt", "repeat_stmt", "for_stmt",
  "case_stmt", "caselist", "caseclause", "expr", "assign_expr", "alt_expr",
  "cat_expr", "cmp_expr", "add_expr", "mul_expr", "pow_expr", "unary_expr",
  "postfix_expr", "primary", "pat_expr", "opt_expr", "arglist",
  "arglist_ne", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-40)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-45)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -40,    30,    51,   -40,   -39,    47,    66,   -40,   -40,   -40,
     -40,   -40,     0,     5,    83,    83,   -40,    22,    23,    23,
      29,    41,   113,    41,   -40,    96,   -40,   -40,    83,   107,
     -10,   172,   172,   -40,   -40,   -40,   -40,   -40,   -40,    54,
     -40,   -40,   129,   172,   -40,    41,    54,   -40,   172,   172,
     172,    54,    54,    54,    54,   172,    54,    54,    54,   147,
      54,    54,    78,   -40,    79,   -40,   -40,   -40,   -40,   -40,
     -40,   -40,   -21,   -40,    72,   -25,    99,    19,    -7,   -40,
     -15,    -4,   -40,   145,     6,    80,    79,   142,   152,   146,
     -40,   172,   -40,   -40,   153,   159,   160,   -40,   -40,   -40,
     109,   114,   -40,   -40,   -40,   -40,   -40,   -40,   -40,    54,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    54,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    54,
      54,    54,    54,    54,    54,    54,    54,    54,    54,    -1,
      -1,    54,   -40,   123,   127,    79,   -40,   134,    54,    41,
     -40,   -40,    41,    41,    41,   -40,   -40,   -40,   -40,   166,
     -40,   -40,   -40,   -40,   -40,   -25,    99,    99,    19,    19,
      19,    19,    19,    19,    19,    19,    19,    19,    19,    19,
      -7,    -7,   -40,   -40,   -40,   -40,   -40,   -40,   132,   138,
     -40,   -40,   164,   131,   -40,   -40,    37,   191,   172,   172,
     172,   172,    54,   -40,    54,    54,   -40,   148,   -34,   -40,
     163,    54,   216,   -40,   -40,   -40,   -40,   -40,   151,   172,
      37,   -40,   172,    17,    41,   -40,   -40,   -40,   -40,    41,
      54,   172,   172,   230,   -40,   -40,    41,   172,   -40
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     0,     1,     0,     0,     0,     5,     4,     8,
       7,     6,     0,     0,    13,    29,    27,     0,    14,    30,
       0,     9,     0,     9,    10,    15,    28,    11,     0,    17,
       0,     0,    20,    16,   120,   116,   119,   117,   118,     0,
      39,    41,     0,     0,    40,     9,   123,    42,     0,     0,
       0,     0,     0,     0,     0,    20,     0,     0,     0,     0,
       0,     0,     0,    31,    18,    32,    33,    34,    35,    36,
      37,    38,    45,    65,    66,    72,    74,    77,    90,    93,
      97,   100,   110,     0,     0,     0,    23,     0,     0,     0,
      44,     0,   124,    43,     0,     0,     0,   102,   101,   105,
       0,     0,   103,   104,   106,   107,   108,   109,    19,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   125,     0,
       0,   125,    12,     0,     0,    25,    22,     0,     0,     9,
      50,    56,     9,     9,     9,   121,    49,   122,    48,    46,
      67,    68,    69,    70,    71,    73,    75,    76,    79,    83,
      81,    84,    85,    88,    89,    86,    87,    80,    82,    78,
      91,    92,    94,    95,    96,    99,    98,   127,     0,   126,
     115,   114,   127,     0,    26,    24,     0,     0,     0,     0,
       0,     0,     0,   111,   129,     0,   112,     0,     0,    60,
       0,     0,    51,    53,    55,    54,    47,   128,     0,     0,
      62,    59,     0,     0,     9,   113,    64,    61,    63,     9,
       0,     0,     0,     0,    52,    57,     9,     0,    58
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -40,   -40,   -40,   -40,   -23,   -40,   -40,   -40,   -40,   -40,
     189,   -40,    34,   -40,   140,   -40,    -8,    10,   -40,   -40,
     -40,   -40,   -40,   -40,   -40,   -40,    25,   -38,    48,   -40,
     133,   -24,   141,   -30,   101,   165,   -40,    -6,   149,   -40,
     110,   -40
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,     8,    25,     9,    10,    17,    29,    32,
      83,    84,    18,    20,   150,    63,    90,   151,    65,    66,
      67,    68,    69,    70,    71,   208,   209,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,   158,    93,
     188,   189
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      27,    87,    34,    35,    36,    37,    38,   143,    92,    34,
      35,    36,    37,    38,    39,   117,   100,   109,   -21,    40,
      41,    42,    91,    64,    86,    43,   136,   229,    44,   118,
       3,    45,    46,    47,    11,   230,    48,    49,    50,   220,
      34,    35,    36,    37,    38,   221,   207,    86,   137,    19,
      12,    -2,     4,   133,   134,   135,   110,    34,    35,    36,
      37,    38,    30,    33,    51,    52,    22,    53,     5,    13,
     138,   157,   157,    54,    14,     6,   145,   131,   132,    15,
      54,   139,   140,   141,    55,   -21,    16,    56,    57,    58,
      59,    60,    61,   166,   167,    51,    52,    21,    53,    22,
     187,   180,   181,   192,    23,   111,   112,   113,   114,   115,
     197,    54,    51,    52,    24,    53,    26,    28,    56,    57,
      58,    59,    60,    61,     7,   116,   198,    31,    54,   199,
     200,   201,    88,   190,   191,    56,    57,    58,    59,    60,
      61,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     105,   108,   -44,   146,   128,   129,   130,   142,   210,   160,
     161,   162,   163,   164,   216,   147,   217,   218,   148,   153,
     154,    62,    85,   223,   149,    34,    35,    36,    37,    38,
      39,   152,   210,    89,   155,    40,    41,    42,    94,    95,
      96,    43,   233,   156,    44,    85,   194,    45,    46,    47,
     195,   231,    48,    49,    50,   202,   232,   203,   212,   213,
     214,   215,   196,   237,   204,   205,    97,    98,    99,   206,
     211,   102,   103,   104,   144,   106,   107,   224,   219,   226,
      51,    52,   228,    53,   182,   183,   184,   185,   186,   225,
     236,   234,   235,   222,   101,   227,    54,   238,     0,   165,
      55,   193,     0,    56,    57,    58,    59,    60,    61,   159,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179
};

static const yytype_int16 yycheck[] =
{
      23,    39,     3,     4,     5,     6,     7,     1,    46,     3,
       4,     5,     6,     7,     8,    40,    54,    38,    12,    13,
      14,    15,    45,    31,    32,    19,    41,    10,    22,    54,
       0,    25,    26,    27,    73,    18,    30,    31,    32,    73,
       3,     4,     5,     6,     7,    79,     9,    55,    63,    15,
       3,     0,     1,    60,    61,    62,    77,     3,     4,     5,
       6,     7,    28,    73,    58,    59,    76,    61,    17,     3,
      74,   109,   110,    74,    74,    24,    84,    58,    59,    74,
      74,    85,    86,    87,    78,    79,     3,    81,    82,    83,
      84,    85,    86,   117,   118,    58,    59,    75,    61,    76,
     138,   131,   132,   141,    75,    33,    34,    35,    36,    37,
     148,    74,    58,    59,    73,    61,     3,    21,    81,    82,
      83,    84,    85,    86,    73,    53,   149,    20,    74,   152,
     153,   154,     3,   139,   140,    81,    82,    83,    84,    85,
      86,    42,    43,    44,    45,    46,    47,    48,    49,    50,
       3,    73,    73,    73,    55,    56,    57,    12,   196,   111,
     112,   113,   114,   115,   202,    23,   204,   205,    16,    10,
      10,    31,    32,   211,    28,     3,     4,     5,     6,     7,
       8,    28,   220,    43,    75,    13,    14,    15,    48,    49,
      50,    19,   230,    79,    22,    55,    73,    25,    26,    27,
      73,   224,    30,    31,    32,    39,   229,    75,   198,   199,
     200,   201,    78,   236,    76,    51,    51,    52,    53,    88,
      29,    56,    57,    58,    84,    60,    61,    11,    80,   219,
      58,    59,   222,    61,   133,   134,   135,   136,   137,    88,
      10,   231,   232,    80,    55,   220,    74,   237,    -1,   116,
      78,   141,    -1,    81,    82,    83,    84,    85,    86,   110,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    90,    91,     0,     1,    17,    24,    73,    92,    94,
      95,    73,     3,     3,    74,    74,     3,    96,   101,   101,
     102,    75,    76,    75,    73,    93,     3,    93,    21,    97,
     101,    20,    98,    73,     3,     4,     5,     6,     7,     8,
      13,    14,    15,    19,    22,    25,    26,    27,    30,    31,
      32,    58,    59,    61,    74,    78,    81,    82,    83,    84,
      85,    86,   103,   104,   105,   107,   108,   109,   110,   111,
     112,   113,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,    99,   100,   103,   105,   116,     3,   103,
     105,    93,   116,   128,   103,   103,   103,   124,   124,   124,
     116,    99,   124,   124,   124,     3,   124,   124,    73,    38,
      77,    33,    34,    35,    36,    37,    53,    40,    54,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    55,    56,
      57,    58,    59,    60,    61,    62,    41,    63,    74,    85,
      86,    87,    12,     1,   103,   105,    73,    23,    16,    28,
     103,   106,    28,    10,    10,    75,    79,   116,   127,   127,
     117,   117,   117,   117,   117,   119,   120,   120,   121,   121,
     121,   121,   121,   121,   121,   121,   121,   121,   121,   121,
     122,   122,   123,   123,   123,   123,   123,   116,   129,   130,
     126,   126,   116,   129,    73,    73,    78,   116,    93,    93,
      93,    93,    39,    75,    76,    51,    88,     9,   114,   115,
     116,    29,   106,   106,   106,   106,   116,   116,   116,    80,
      73,    79,    80,   116,    11,    88,   106,   115,   106,    10,
      18,    93,    93,   116,   106,   106,    10,    93,   106
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    89,    90,    91,    91,    91,    91,    92,    92,    93,
      93,    94,    95,    96,    96,    97,    97,    98,    98,    98,
      99,    99,   100,   100,   100,   100,   100,   101,   101,   102,
     102,   103,   103,   103,   103,   103,   103,   103,   103,   103,
     103,   103,   103,   103,   103,   104,   104,   104,   104,   105,
     106,   107,   107,   108,   109,   110,   111,   112,   112,   113,
     114,   114,   114,   115,   115,   116,   117,   117,   117,   117,
     117,   117,   118,   118,   119,   119,   119,   120,   120,   120,
     120,   120,   120,   120,   120,   120,   120,   120,   120,   120,
     121,   121,   121,   122,   122,   122,   122,   123,   123,   123,
     124,   124,   124,   124,   124,   124,   124,   124,   124,   124,
     125,   125,   125,   125,   125,   125,   126,   126,   126,   126,
     126,   126,   127,   128,   128,   129,   129,   130,   130,   130
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     3,     1,     1,     0,
       1,     6,    10,     0,     1,     0,     3,     0,     2,     3,
       0,     1,     2,     1,     3,     2,     3,     1,     3,     0,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     1,     1,     3,     5,     3,     3,
       1,     5,     8,     5,     5,     5,     3,     9,    11,     6,
       1,     3,     2,     3,     3,     1,     1,     3,     3,     3,
       3,     3,     1,     3,     1,     3,     3,     1,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       1,     3,     3,     1,     3,     3,     3,     1,     3,     3,
       1,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       1,     4,     4,     6,     3,     3,     1,     1,     1,     1,
       1,     3,     1,     0,     1,     0,     1,     1,     3,     2
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
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
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

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
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


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

  yychar = YYEMPTY; /* Cause a token to be read.  */

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
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
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
  yychar = YYEMPTY;
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
  case 2: /* program: decl_list  */
#line 186 "rebus.y"
                            { /* rebus_parsed_program already set */ }
#line 1468 "rebus.tab.c"
    break;

  case 4: /* decl_list: decl_list decl  */
#line 191 "rebus.y"
                            {
            if ((yyvsp[0].decl)) {
                (yyvsp[0].decl)->next = NULL;
                if (!prog->decls) prog->decls = (yyvsp[0].decl);
                else {
                    RDecl *d = prog->decls;
                    while (d->next) d = d->next;
                    d->next = (yyvsp[0].decl);
                }
                prog->ndecls++;
            }
        }
#line 1485 "rebus.tab.c"
    break;

  case 5: /* decl_list: decl_list ';'  */
#line 203 "rebus.y"
                            { /* empty statement at top level */ }
#line 1491 "rebus.tab.c"
    break;

  case 6: /* decl_list: decl_list error ';'  */
#line 204 "rebus.y"
                            { yyerrok; }
#line 1497 "rebus.tab.c"
    break;

  case 7: /* decl: function_decl  */
#line 208 "rebus.y"
                            { (yyval.decl) = (yyvsp[0].decl); }
#line 1503 "rebus.tab.c"
    break;

  case 8: /* decl: record_decl  */
#line 209 "rebus.y"
                            { (yyval.decl) = (yyvsp[0].decl); }
#line 1509 "rebus.tab.c"
    break;

  case 11: /* record_decl: T_RECORD T_IDENT '(' opt_idlist ')' opt_semi  */
#line 225 "rebus.y"
        {
            RDecl *d   = rdecl_new(RD_RECORD, yylineno);
            d->name    = (yyvsp[-4].sval);
            SAL *sl    = (yyvsp[-2].sal);
            d->fields  = sl->a;
            d->nfields = sl->n;
            free(sl);
            (yyval.decl) = d;
        }
#line 1523 "rebus.tab.c"
    break;

  case 12: /* function_decl: T_FUNCTION T_IDENT '(' opt_params ')' opt_semi opt_locals opt_initial stmt_list T_END  */
#line 251 "rebus.y"
        {
            RDecl *d    = rdecl_new(RD_FUNCTION, yylineno);
            d->name     = (yyvsp[-8].sval);
            SAL *ps     = (SAL*)(yyvsp[-6].sal);
            d->params   = ps->a;
            d->nparams  = ps->n;
            free(ps);
            SAL *ls     = (SAL*)(yyvsp[-3].sal);
            d->locals   = ls->a;
            d->nlocals  = ls->n;
            free(ls);
            d->initial  = (yyvsp[-2].stmt);
            d->body     = (yyvsp[-1].stmt);
            (yyval.decl) = d;
        }
#line 1543 "rebus.tab.c"
    break;

  case 13: /* opt_params: %empty  */
#line 269 "rebus.y"
                    { (yyval.sal) = (void*)sal_new(); }
#line 1549 "rebus.tab.c"
    break;

  case 14: /* opt_params: idlist_ne  */
#line 270 "rebus.y"
                    { (yyval.sal) = (yyvsp[0].sal); }
#line 1555 "rebus.tab.c"
    break;

  case 15: /* opt_locals: %empty  */
#line 274 "rebus.y"
                               { (yyval.sal) = (void*)sal_new(); }
#line 1561 "rebus.tab.c"
    break;

  case 16: /* opt_locals: T_LOCAL idlist_ne ';'  */
#line 275 "rebus.y"
                              { (yyval.sal) = (yyvsp[-1].sal); }
#line 1567 "rebus.tab.c"
    break;

  case 17: /* opt_initial: %empty  */
#line 279 "rebus.y"
                               { (yyval.stmt) = NULL; }
#line 1573 "rebus.tab.c"
    break;

  case 18: /* opt_initial: T_INITIAL compound_stmt  */
#line 280 "rebus.y"
                                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1579 "rebus.tab.c"
    break;

  case 19: /* opt_initial: T_INITIAL stmt ';'  */
#line 281 "rebus.y"
                                            { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1585 "rebus.tab.c"
    break;

  case 20: /* stmt_list: %empty  */
#line 287 "rebus.y"
                    { (yyval.stmt) = NULL; }
#line 1591 "rebus.tab.c"
    break;

  case 21: /* stmt_list: stmt_list_ne  */
#line 288 "rebus.y"
                    { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1597 "rebus.tab.c"
    break;

  case 22: /* stmt_list_ne: stmt ';'  */
#line 292 "rebus.y"
                                { (yyval.stmt) = (yyvsp[-1].stmt); }
#line 1603 "rebus.tab.c"
    break;

  case 23: /* stmt_list_ne: compound_stmt  */
#line 293 "rebus.y"
                                { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1609 "rebus.tab.c"
    break;

  case 24: /* stmt_list_ne: stmt_list_ne stmt ';'  */
#line 294 "rebus.y"
                                {
            if (!(yyvsp[-2].stmt)) { (yyval.stmt) = (yyvsp[-1].stmt); }
            else {
                RStmt *t = (yyvsp[-2].stmt); while (t->next) t = t->next;
                t->next = (yyvsp[-1].stmt); (yyval.stmt) = (yyvsp[-2].stmt);
            }
        }
#line 1621 "rebus.tab.c"
    break;

  case 25: /* stmt_list_ne: stmt_list_ne compound_stmt  */
#line 301 "rebus.y"
                                 {
            if (!(yyvsp[-1].stmt)) { (yyval.stmt) = (yyvsp[0].stmt); }
            else {
                RStmt *t = (yyvsp[-1].stmt); while (t->next) t = t->next;
                t->next = (yyvsp[0].stmt); (yyval.stmt) = (yyvsp[-1].stmt);
            }
        }
#line 1633 "rebus.tab.c"
    break;

  case 26: /* stmt_list_ne: stmt_list_ne error ';'  */
#line 308 "rebus.y"
                                { yyerrok; (yyval.stmt) = (yyvsp[-2].stmt); }
#line 1639 "rebus.tab.c"
    break;

  case 27: /* idlist_ne: T_IDENT  */
#line 316 "rebus.y"
                            { SAL *s = sal_new(); sal_push(s, (yyvsp[0].sval)); (yyval.sal) = s; }
#line 1645 "rebus.tab.c"
    break;

  case 28: /* idlist_ne: idlist_ne ',' T_IDENT  */
#line 317 "rebus.y"
                            { sal_push((yyvsp[-2].sal), (yyvsp[0].sval)); (yyval.sal) = (yyvsp[-2].sal); }
#line 1651 "rebus.tab.c"
    break;

  case 29: /* opt_idlist: %empty  */
#line 321 "rebus.y"
                    { (yyval.sal) = sal_new(); }
#line 1657 "rebus.tab.c"
    break;

  case 30: /* opt_idlist: idlist_ne  */
#line 322 "rebus.y"
                    { (yyval.sal) = (yyvsp[0].sal); }
#line 1663 "rebus.tab.c"
    break;

  case 31: /* stmt: expr_as_stmt  */
#line 330 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1669 "rebus.tab.c"
    break;

  case 32: /* stmt: if_stmt  */
#line 331 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1675 "rebus.tab.c"
    break;

  case 33: /* stmt: unless_stmt  */
#line 332 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1681 "rebus.tab.c"
    break;

  case 34: /* stmt: while_stmt  */
#line 333 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1687 "rebus.tab.c"
    break;

  case 35: /* stmt: until_stmt  */
#line 334 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1693 "rebus.tab.c"
    break;

  case 36: /* stmt: repeat_stmt  */
#line 335 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1699 "rebus.tab.c"
    break;

  case 37: /* stmt: for_stmt  */
#line 336 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1705 "rebus.tab.c"
    break;

  case 38: /* stmt: case_stmt  */
#line 337 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1711 "rebus.tab.c"
    break;

  case 39: /* stmt: T_EXIT  */
#line 338 "rebus.y"
                            { (yyval.stmt) = rstmt_new(RS_EXIT,   yylineno); }
#line 1717 "rebus.tab.c"
    break;

  case 40: /* stmt: T_NEXT  */
#line 339 "rebus.y"
                            { (yyval.stmt) = rstmt_new(RS_NEXT,   yylineno); }
#line 1723 "rebus.tab.c"
    break;

  case 41: /* stmt: T_FAIL  */
#line 340 "rebus.y"
                            { (yyval.stmt) = rstmt_new(RS_FAIL,   yylineno); }
#line 1729 "rebus.tab.c"
    break;

  case 42: /* stmt: T_STOP  */
#line 341 "rebus.y"
                            { (yyval.stmt) = rstmt_new(RS_STOP,   yylineno); }
#line 1735 "rebus.tab.c"
    break;

  case 43: /* stmt: T_RETURN opt_expr  */
#line 342 "rebus.y"
                            {
            RStmt *s = rstmt_new(RS_RETURN, yylineno);
            s->retval = (yyvsp[0].expr); (yyval.stmt) = s;
        }
#line 1744 "rebus.tab.c"
    break;

  case 44: /* stmt: compound_stmt  */
#line 346 "rebus.y"
                            { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1750 "rebus.tab.c"
    break;

  case 45: /* expr_as_stmt: expr  */
#line 351 "rebus.y"
                                    {
            RStmt *s = rstmt_new(RS_EXPR, (yyvsp[0].expr)->lineno);
            s->expr = (yyvsp[0].expr); (yyval.stmt) = s;
        }
#line 1759 "rebus.tab.c"
    break;

  case 46: /* expr_as_stmt: expr '?' pat_expr  */
#line 355 "rebus.y"
                                    {
            RStmt *s = rstmt_new(RS_MATCH, (yyvsp[-2].expr)->lineno);
            s->expr = (yyvsp[-2].expr); s->pat = (yyvsp[0].expr); (yyval.stmt) = s;
        }
#line 1768 "rebus.tab.c"
    break;

  case 47: /* expr_as_stmt: expr '?' pat_expr T_ARROW expr  */
#line 359 "rebus.y"
                                     {
            RStmt *s = rstmt_new(RS_REPLACE, (yyvsp[-4].expr)->lineno);
            s->expr = (yyvsp[-4].expr); s->pat = (yyvsp[-2].expr); s->repl = (yyvsp[0].expr); (yyval.stmt) = s;
        }
#line 1777 "rebus.tab.c"
    break;

  case 48: /* expr_as_stmt: expr T_QUESTMINUS pat_expr  */
#line 363 "rebus.y"
                                    {
            RStmt *s = rstmt_new(RS_REPLN, (yyvsp[-2].expr)->lineno);
            s->expr = (yyvsp[-2].expr); s->pat = (yyvsp[0].expr); (yyval.stmt) = s;
        }
#line 1786 "rebus.tab.c"
    break;

  case 49: /* compound_stmt: '{' stmt_list '}'  */
#line 371 "rebus.y"
                            {
            /* collect the linked-list into a RS_COMPOUND */
            RStmt *s = rstmt_new(RS_COMPOUND, yylineno);
            /* count and store */
            int n = 0; RStmt *t = (yyvsp[-1].stmt); while (t) { n++; t = t->next; }
            s->stmts  = malloc(n * sizeof(RStmt *));
            s->nstmts = n;
            t = (yyvsp[-1].stmt);
            for (int i = 0; i < n; i++) { s->stmts[i] = t; t = t->next; }
            (yyval.stmt) = s;
        }
#line 1802 "rebus.tab.c"
    break;

  case 50: /* stmt_body: stmt  */
#line 386 "rebus.y"
                    { (yyval.stmt) = (yyvsp[0].stmt); }
#line 1808 "rebus.tab.c"
    break;

  case 51: /* if_stmt: T_IF stmt T_THEN opt_semi stmt_body  */
#line 392 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_IF, yylineno);
            s->body  = (yyvsp[-3].stmt);
            s->alt   = (yyvsp[0].stmt);
            s->repl  = NULL;
            (yyval.stmt) = s;
        }
#line 1820 "rebus.tab.c"
    break;

  case 52: /* if_stmt: T_IF stmt T_THEN opt_semi stmt_body T_ELSE opt_semi stmt_body  */
#line 400 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_IF, yylineno);
            s->body  = (yyvsp[-6].stmt);
            s->alt   = (yyvsp[-3].stmt);
            s->repl  = (RExpr*)(yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1832 "rebus.tab.c"
    break;

  case 53: /* unless_stmt: T_UNLESS stmt T_THEN opt_semi stmt_body  */
#line 411 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_UNLESS, yylineno);
            s->body  = (yyvsp[-3].stmt);
            s->alt   = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1843 "rebus.tab.c"
    break;

  case 54: /* while_stmt: T_WHILE stmt T_DO opt_semi stmt_body  */
#line 422 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_WHILE, yylineno);
            s->body  = (yyvsp[-3].stmt);
            s->alt   = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1854 "rebus.tab.c"
    break;

  case 55: /* until_stmt: T_UNTIL stmt T_DO opt_semi stmt_body  */
#line 432 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_UNTIL, yylineno);
            s->body  = (yyvsp[-3].stmt);
            s->alt   = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1865 "rebus.tab.c"
    break;

  case 56: /* repeat_stmt: T_REPEAT opt_semi stmt_body  */
#line 443 "rebus.y"
        {
            RStmt *s = rstmt_new(RS_REPEAT, yylineno);
            s->alt   = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1875 "rebus.tab.c"
    break;

  case 57: /* for_stmt: T_FOR T_IDENT T_FROM expr T_TO expr T_DO opt_semi stmt_body  */
#line 453 "rebus.y"
        {
            RStmt *s    = rstmt_new(RS_FOR, yylineno);
            s->for_var  = (yyvsp[-7].sval);
            s->for_from = (yyvsp[-5].expr);
            s->for_to   = (yyvsp[-3].expr);
            s->for_by   = NULL;
            s->alt      = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1889 "rebus.tab.c"
    break;

  case 58: /* for_stmt: T_FOR T_IDENT T_FROM expr T_TO expr T_BY expr T_DO opt_semi stmt_body  */
#line 463 "rebus.y"
        {
            RStmt *s    = rstmt_new(RS_FOR, yylineno);
            s->for_var  = (yyvsp[-9].sval);
            s->for_from = (yyvsp[-7].expr);
            s->for_to   = (yyvsp[-5].expr);
            s->for_by   = (yyvsp[-3].expr);
            s->alt      = (yyvsp[0].stmt);
            (yyval.stmt) = s;
        }
#line 1903 "rebus.tab.c"
    break;

  case 59: /* case_stmt: T_CASE expr T_OF '{' caselist '}'  */
#line 477 "rebus.y"
        {
            RStmt *s      = rstmt_new(RS_CASE, yylineno);
            s->case_expr  = (yyvsp[-4].expr);
            s->cases      = (yyvsp[-1].rcase);
            (yyval.stmt) = s;
        }
#line 1914 "rebus.tab.c"
    break;

  case 60: /* caselist: caseclause  */
#line 486 "rebus.y"
                            { (yyval.rcase) = (yyvsp[0].rcase); }
#line 1920 "rebus.tab.c"
    break;

  case 61: /* caselist: caselist ';' caseclause  */
#line 487 "rebus.y"
                              {
            /* append $3 to end of $1 */
            RCase *c = (yyvsp[-2].rcase); while (c->next) c = c->next;
            c->next = (yyvsp[0].rcase); (yyval.rcase) = (yyvsp[-2].rcase);
        }
#line 1930 "rebus.tab.c"
    break;

  case 62: /* caselist: caselist ';'  */
#line 492 "rebus.y"
                            { (yyval.rcase) = (yyvsp[-1].rcase); }
#line 1936 "rebus.tab.c"
    break;

  case 63: /* caseclause: expr ':' stmt_body  */
#line 497 "rebus.y"
        {
            RCase *c  = rcase_new(yylineno);
            c->guard  = (yyvsp[-2].expr);
            c->body   = (yyvsp[0].stmt);
            (yyval.rcase) = c;
        }
#line 1947 "rebus.tab.c"
    break;

  case 64: /* caseclause: T_DEFAULT ':' stmt_body  */
#line 504 "rebus.y"
        {
            RCase *c     = rcase_new(yylineno);
            c->is_default = 1;
            c->body       = (yyvsp[0].stmt);
            (yyval.rcase) = c;
        }
#line 1958 "rebus.tab.c"
    break;

  case 65: /* expr: assign_expr  */
#line 526 "rebus.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1964 "rebus.tab.c"
    break;

  case 66: /* assign_expr: alt_expr  */
#line 531 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1970 "rebus.tab.c"
    break;

  case 67: /* assign_expr: alt_expr T_ASSIGN assign_expr  */
#line 532 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_ASSIGN,    (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 1976 "rebus.tab.c"
    break;

  case 68: /* assign_expr: alt_expr T_EXCHANGE assign_expr  */
#line 533 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_EXCHANGE,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 1982 "rebus.tab.c"
    break;

  case 69: /* assign_expr: alt_expr T_ADDASSIGN assign_expr  */
#line 534 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_ADDASSIGN, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 1988 "rebus.tab.c"
    break;

  case 70: /* assign_expr: alt_expr T_SUBASSIGN assign_expr  */
#line 535 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SUBASSIGN, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 1994 "rebus.tab.c"
    break;

  case 71: /* assign_expr: alt_expr T_CATASSIGN assign_expr  */
#line 536 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_CATASSIGN, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2000 "rebus.tab.c"
    break;

  case 72: /* alt_expr: cat_expr  */
#line 541 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2006 "rebus.tab.c"
    break;

  case 73: /* alt_expr: alt_expr '|' cat_expr  */
#line 542 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_ALT,    (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2012 "rebus.tab.c"
    break;

  case 74: /* cat_expr: cmp_expr  */
#line 547 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2018 "rebus.tab.c"
    break;

  case 75: /* cat_expr: cat_expr T_STRCAT cmp_expr  */
#line 548 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_STRCAT, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2024 "rebus.tab.c"
    break;

  case 76: /* cat_expr: cat_expr '&' cmp_expr  */
#line 549 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_PATCAT, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2030 "rebus.tab.c"
    break;

  case 77: /* cmp_expr: add_expr  */
#line 554 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2036 "rebus.tab.c"
    break;

  case 78: /* cmp_expr: cmp_expr '=' add_expr  */
#line 555 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_EQ,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2042 "rebus.tab.c"
    break;

  case 79: /* cmp_expr: cmp_expr T_NE add_expr  */
#line 556 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_NE,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2048 "rebus.tab.c"
    break;

  case 80: /* cmp_expr: cmp_expr '<' add_expr  */
#line 557 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_LT,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2054 "rebus.tab.c"
    break;

  case 81: /* cmp_expr: cmp_expr T_LE add_expr  */
#line 558 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_LE,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2060 "rebus.tab.c"
    break;

  case 82: /* cmp_expr: cmp_expr '>' add_expr  */
#line 559 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_GT,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2066 "rebus.tab.c"
    break;

  case 83: /* cmp_expr: cmp_expr T_GE add_expr  */
#line 560 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_GE,  (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2072 "rebus.tab.c"
    break;

  case 84: /* cmp_expr: cmp_expr T_SEQ add_expr  */
#line 561 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SEQ, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2078 "rebus.tab.c"
    break;

  case 85: /* cmp_expr: cmp_expr T_SNE add_expr  */
#line 562 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SNE, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2084 "rebus.tab.c"
    break;

  case 86: /* cmp_expr: cmp_expr T_SLT add_expr  */
#line 563 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SLT, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2090 "rebus.tab.c"
    break;

  case 87: /* cmp_expr: cmp_expr T_SLE add_expr  */
#line 564 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SLE, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2096 "rebus.tab.c"
    break;

  case 88: /* cmp_expr: cmp_expr T_SGT add_expr  */
#line 565 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SGT, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2102 "rebus.tab.c"
    break;

  case 89: /* cmp_expr: cmp_expr T_SGE add_expr  */
#line 566 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SGE, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2108 "rebus.tab.c"
    break;

  case 90: /* add_expr: mul_expr  */
#line 571 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2114 "rebus.tab.c"
    break;

  case 91: /* add_expr: add_expr '+' mul_expr  */
#line 572 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_ADD, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2120 "rebus.tab.c"
    break;

  case 92: /* add_expr: add_expr '-' mul_expr  */
#line 573 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_SUB, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2126 "rebus.tab.c"
    break;

  case 93: /* mul_expr: pow_expr  */
#line 578 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2132 "rebus.tab.c"
    break;

  case 94: /* mul_expr: mul_expr '*' pow_expr  */
#line 579 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_MUL, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2138 "rebus.tab.c"
    break;

  case 95: /* mul_expr: mul_expr '/' pow_expr  */
#line 580 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_DIV, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2144 "rebus.tab.c"
    break;

  case 96: /* mul_expr: mul_expr '%' pow_expr  */
#line 581 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_MOD, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2150 "rebus.tab.c"
    break;

  case 97: /* pow_expr: unary_expr  */
#line 586 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2156 "rebus.tab.c"
    break;

  case 98: /* pow_expr: unary_expr '^' pow_expr  */
#line 587 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_POW, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2162 "rebus.tab.c"
    break;

  case 99: /* pow_expr: unary_expr T_STARSTAR pow_expr  */
#line 588 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_POW, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno); }
#line 2168 "rebus.tab.c"
    break;

  case 100: /* unary_expr: postfix_expr  */
#line 593 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2174 "rebus.tab.c"
    break;

  case 101: /* unary_expr: '-' unary_expr  */
#line 594 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_NEG,   NULL, (yyvsp[0].expr), yylineno); }
#line 2180 "rebus.tab.c"
    break;

  case 102: /* unary_expr: '+' unary_expr  */
#line 595 "rebus.y"
                                            { (yyval.expr) = rbinop(RE_POS,   NULL, (yyvsp[0].expr), yylineno); }
#line 2186 "rebus.tab.c"
    break;

  case 103: /* unary_expr: '~' unary_expr  */
#line 596 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_NOT,   NULL, (yyvsp[0].expr), yylineno); }
#line 2192 "rebus.tab.c"
    break;

  case 104: /* unary_expr: '\\' unary_expr  */
#line 597 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_NOT,   NULL, (yyvsp[0].expr), yylineno); }
#line 2198 "rebus.tab.c"
    break;

  case 105: /* unary_expr: '/' unary_expr  */
#line 598 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_VALUE, NULL, (yyvsp[0].expr), yylineno); }
#line 2204 "rebus.tab.c"
    break;

  case 106: /* unary_expr: '!' unary_expr  */
#line 599 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_BANG,  NULL, (yyvsp[0].expr), yylineno); }
#line 2210 "rebus.tab.c"
    break;

  case 107: /* unary_expr: '@' T_IDENT  */
#line 600 "rebus.y"
                                            {
            RExpr *e = rexpr_new(RE_CURSOR, yylineno);
            e->sval = (yyvsp[0].sval); (yyval.expr) = e;
        }
#line 2219 "rebus.tab.c"
    break;

  case 108: /* unary_expr: '$' unary_expr  */
#line 604 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_DEREF,  NULL, (yyvsp[0].expr), yylineno); }
#line 2225 "rebus.tab.c"
    break;

  case 109: /* unary_expr: '.' unary_expr  */
#line 605 "rebus.y"
                                           { (yyval.expr) = rbinop(RE_COND,   NULL, (yyvsp[0].expr), yylineno); }
#line 2231 "rebus.tab.c"
    break;

  case 110: /* postfix_expr: primary  */
#line 610 "rebus.y"
                                            { (yyval.expr) = (yyvsp[0].expr); }
#line 2237 "rebus.tab.c"
    break;

  case 111: /* postfix_expr: postfix_expr '(' arglist ')'  */
#line 612 "rebus.y"
        {
            /* function call or pattern constructor */
            EAL *al = (yyvsp[-1].eal);
            /* If left is RE_VAR, convert to RE_CALL */
            RExpr *e = rexpr_new(RE_CALL, (yyvsp[-3].expr)->lineno);
            if ((yyvsp[-3].expr)->kind == RE_VAR) {
                e->sval = (yyvsp[-3].expr)->sval; free((yyvsp[-3].expr));
            } else {
                /* complex callee: store in left */
                e->left = (yyvsp[-3].expr);
            }
            e->args  = al->a;
            e->nargs = al->n;
            free(al);
            (yyval.expr) = e;
        }
#line 2258 "rebus.tab.c"
    break;

  case 112: /* postfix_expr: postfix_expr '[' arglist ']'  */
#line 629 "rebus.y"
        {
            EAL *al = (yyvsp[-1].eal);
            RExpr *e = rexpr_new(RE_SUB_IDX, (yyvsp[-3].expr)->lineno);
            e->left  = (yyvsp[-3].expr);
            e->args  = al->a;
            e->nargs = al->n;
            free(al);
            (yyval.expr) = e;
        }
#line 2272 "rebus.tab.c"
    break;

  case 113: /* postfix_expr: postfix_expr '[' expr T_PLUSCOLON expr ']'  */
#line 639 "rebus.y"
        {
            /* a[i +: n]  — substring starting at i, length n */
            RExpr *range = rexpr_new(RE_RANGE, (yyvsp[-3].expr)->lineno);
            range->left  = (yyvsp[-3].expr);
            range->right = (yyvsp[-1].expr);
            RExpr *e = rexpr_new(RE_SUB_IDX, (yyvsp[-5].expr)->lineno);
            e->left  = (yyvsp[-5].expr);
            e->args  = malloc(1 * sizeof(RExpr *));
            e->args[0] = range;
            e->nargs = 1;
            (yyval.expr) = e;
        }
#line 2289 "rebus.tab.c"
    break;

  case 114: /* postfix_expr: postfix_expr '.' primary  */
#line 653 "rebus.y"
        {
            (yyval.expr) = rbinop(RE_COND, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno);
        }
#line 2297 "rebus.tab.c"
    break;

  case 115: /* postfix_expr: postfix_expr '$' primary  */
#line 657 "rebus.y"
        {
            (yyval.expr) = rbinop(RE_IMM, (yyvsp[-2].expr), (yyvsp[0].expr), yylineno);
        }
#line 2305 "rebus.tab.c"
    break;

  case 116: /* primary: T_STR  */
#line 664 "rebus.y"
                    { RExpr *e = rexpr_new(RE_STR,     yylineno); e->sval = (yyvsp[0].sval); (yyval.expr) = e; }
#line 2311 "rebus.tab.c"
    break;

  case 117: /* primary: T_INT  */
#line 665 "rebus.y"
                    { RExpr *e = rexpr_new(RE_INT,     yylineno); e->ival = (yyvsp[0].ival); (yyval.expr) = e; }
#line 2317 "rebus.tab.c"
    break;

  case 118: /* primary: T_REAL  */
#line 666 "rebus.y"
                    { RExpr *e = rexpr_new(RE_REAL,    yylineno); e->dval = (yyvsp[0].dval); (yyval.expr) = e; }
#line 2323 "rebus.tab.c"
    break;

  case 119: /* primary: T_KEYWORD  */
#line 667 "rebus.y"
                    { RExpr *e = rexpr_new(RE_KEYWORD, yylineno); e->sval = (yyvsp[0].sval); (yyval.expr) = e; }
#line 2329 "rebus.tab.c"
    break;

  case 120: /* primary: T_IDENT  */
#line 668 "rebus.y"
                    { RExpr *e = rexpr_new(RE_VAR,     yylineno); e->sval = (yyvsp[0].sval); (yyval.expr) = e; }
#line 2335 "rebus.tab.c"
    break;

  case 121: /* primary: '(' expr ')'  */
#line 669 "rebus.y"
                    { (yyval.expr) = (yyvsp[-1].expr); }
#line 2341 "rebus.tab.c"
    break;

  case 122: /* pat_expr: expr  */
#line 675 "rebus.y"
                { (yyval.expr) = (yyvsp[0].expr); }
#line 2347 "rebus.tab.c"
    break;

  case 123: /* opt_expr: %empty  */
#line 680 "rebus.y"
                    { (yyval.expr) = NULL; }
#line 2353 "rebus.tab.c"
    break;

  case 124: /* opt_expr: expr  */
#line 681 "rebus.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 2359 "rebus.tab.c"
    break;

  case 125: /* arglist: %empty  */
#line 686 "rebus.y"
                    { (yyval.eal) = eal_new(); }
#line 2365 "rebus.tab.c"
    break;

  case 126: /* arglist: arglist_ne  */
#line 687 "rebus.y"
                    { (yyval.eal) = (yyvsp[0].eal); }
#line 2371 "rebus.tab.c"
    break;

  case 127: /* arglist_ne: expr  */
#line 691 "rebus.y"
                                { EAL *al = eal_new(); eal_push(al, (yyvsp[0].expr)); (yyval.eal) = al; }
#line 2377 "rebus.tab.c"
    break;

  case 128: /* arglist_ne: arglist_ne ',' expr  */
#line 692 "rebus.y"
                                { eal_push((yyvsp[-2].eal), (yyvsp[0].expr)); (yyval.eal) = (yyvsp[-2].eal); }
#line 2383 "rebus.tab.c"
    break;

  case 129: /* arglist_ne: arglist_ne ','  */
#line 693 "rebus.y"
                                { eal_push((yyvsp[-1].eal), NULL); (yyval.eal) = (yyvsp[-1].eal); }
#line 2389 "rebus.tab.c"
    break;


#line 2393 "rebus.tab.c"

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
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
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

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
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
  if (yychar != YYEMPTY)
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

#line 696 "rebus.y"


void rebus_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    rebus_parsed_program = prog;
}
