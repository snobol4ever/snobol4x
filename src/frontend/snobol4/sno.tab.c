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
#line 1 "sno.y"

/*
 * sno.y — SNOBOL4 bison grammar for snoc
 *
 * Whitespace design (from beauty.sno S4_expression.sno):
 *
 *   __  — raw token: any run of spaces/tabs (lexer always emits this)
 *   __  — mandatory whitespace: one or more __ tokens
 *   _   — optional whitespace (gray): __ or empty
 *
 * Binary operators require __ on both sides:  expr __ OP __ term
 * Concat requires __:                          expr __ term
 * Unary operators need no leading space:       OP term
 * Inside parens/brackets _ (gray) is used:     LPAREN _ expr _ RPAREN
 *
 * Statement structure mirrors beauty.sno snoStmt:
 *   label __ subject __ pattern = replacement : goto
 *   The first __ separates label from subject,
 *   the second __ separates subject from pattern.
 *   Subject is a full expr (snoExpr14 level — unary prefix).
 *   Pattern is expr (snoExpr1 level — everything).
 *
 * This eliminates all lexer lookahead, bstack, PAT_BUILTIN, SUBJ tricks.
 * The grammar expresses what was previously smuggled into the lexer.
 */

#include "scrip_cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Program *prog;
extern Program *parsed_program;

typedef struct { Expr **a; int n, cap; } AL;
static AL *al_new(void) {
    AL *al = calloc(1,sizeof *al); al->cap=4; al->a=malloc(4*sizeof(Expr*)); return al;
}
static void al_push(AL *al, Expr *e) {
    if (al->n >= al->cap) { al->cap*=2; al->a=realloc(al->a,al->cap*sizeof(Expr*)); }
    al->a[al->n++] = e;
}
static Expr *binop(EKind k, Expr *l, Expr *r) {
    Expr *e=expr_new(k); e->left=l; e->right=r; return e;
}

extern int  yylex(void);
extern void yyerror(const char *);
extern int  lineno_stmt;

#line 122 "sno.tab.c"

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

#include "sno.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_LABEL = 3,                      /* LABEL  */
  YYSYMBOL_IDENT = 4,                      /* IDENT  */
  YYSYMBOL_KEYWORD = 5,                    /* KEYWORD  */
  YYSYMBOL_STR = 6,                        /* STR  */
  YYSYMBOL_END_LABEL = 7,                  /* END_LABEL  */
  YYSYMBOL_INT = 8,                        /* INT  */
  YYSYMBOL_REAL = 9,                       /* REAL  */
  YYSYMBOL_EQ = 10,                        /* EQ  */
  YYSYMBOL_COLON = 11,                     /* COLON  */
  YYSYMBOL_LPAREN = 12,                    /* LPAREN  */
  YYSYMBOL_RPAREN = 13,                    /* RPAREN  */
  YYSYMBOL_LBRACKET = 14,                  /* LBRACKET  */
  YYSYMBOL_RBRACKET = 15,                  /* RBRACKET  */
  YYSYMBOL_STARSTAR = 16,                  /* STARSTAR  */
  YYSYMBOL_CARET = 17,                     /* CARET  */
  YYSYMBOL_PLUS = 18,                      /* PLUS  */
  YYSYMBOL_MINUS = 19,                     /* MINUS  */
  YYSYMBOL_STAR = 20,                      /* STAR  */
  YYSYMBOL_SLASH = 21,                     /* SLASH  */
  YYSYMBOL_PIPE = 22,                      /* PIPE  */
  YYSYMBOL_DOT = 23,                       /* DOT  */
  YYSYMBOL_DOLLAR = 24,                    /* DOLLAR  */
  YYSYMBOL_AMP = 25,                       /* AMP  */
  YYSYMBOL_COMMA = 26,                     /* COMMA  */
  YYSYMBOL_AT = 27,                        /* AT  */
  YYSYMBOL_SGOTO = 28,                     /* SGOTO  */
  YYSYMBOL_FGOTO = 29,                     /* FGOTO  */
  YYSYMBOL_NEWLINE = 30,                   /* NEWLINE  */
  YYSYMBOL___ = 31,                        /* __  */
  YYSYMBOL_YYACCEPT = 32,                  /* $accept  */
  YYSYMBOL_program = 33,                   /* program  */
  YYSYMBOL_stmtlist = 34,                  /* stmtlist  */
  YYSYMBOL_line = 35,                      /* line  */
  YYSYMBOL__ = 36,                         /* _  */
  YYSYMBOL_stmt = 37,                      /* stmt  */
  YYSYMBOL_opt_label = 38,                 /* opt_label  */
  YYSYMBOL_opt_repl = 39,                  /* opt_repl  */
  YYSYMBOL_opt_goto = 40,                  /* opt_goto  */
  YYSYMBOL_expr = 41,                      /* expr  */
  YYSYMBOL_concat_expr = 42,               /* concat_expr  */
  YYSYMBOL_unary_expr = 43,                /* unary_expr  */
  YYSYMBOL_postfix_expr = 44,              /* postfix_expr  */
  YYSYMBOL_primary = 45,                   /* primary  */
  YYSYMBOL_arglist = 46,                   /* arglist  */
  YYSYMBOL_arglist_ne = 47,                /* arglist_ne  */
  YYSYMBOL_goto_clauses = 48,              /* goto_clauses  */
  YYSYMBOL_gclause_s = 49,                 /* gclause_s  */
  YYSYMBOL_gclause_f = 50,                 /* gclause_f  */
  YYSYMBOL_gclause_u = 51,                 /* gclause_u  */
  YYSYMBOL_glabel = 52                     /* glabel  */
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
#define YYLAST   421

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  32
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  21
/* YYNRULES -- Number of rules.  */
#define YYNRULES  74
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  158

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   286


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
      25,    26,    27,    28,    29,    30,    31
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    84,    84,    87,    88,    92,   100,   109,   110,   115,
     115,   136,   138,   141,   144,   148,   152,   158,   158,   161,
     162,   165,   165,   183,   184,   185,   186,   187,   188,   189,
     190,   191,   192,   193,   194,   195,   200,   201,   206,   207,
     208,   209,   210,   211,   212,   213,   214,   215,   216,   221,
     222,   229,   230,   231,   232,   233,   236,   237,   238,   246,
     247,   251,   252,   256,   257,   258,   259,   260,   261,   264,
     265,   266,   269,   270,   271
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
  "\"end of file\"", "error", "\"invalid token\"", "LABEL", "IDENT",
  "KEYWORD", "STR", "END_LABEL", "INT", "REAL", "EQ", "COLON", "LPAREN",
  "RPAREN", "LBRACKET", "RBRACKET", "STARSTAR", "CARET", "PLUS", "MINUS",
  "STAR", "SLASH", "PIPE", "DOT", "DOLLAR", "AMP", "COMMA", "AT", "SGOTO",
  "FGOTO", "NEWLINE", "__", "$accept", "program", "stmtlist", "line", "_",
  "stmt", "opt_label", "opt_repl", "opt_goto", "expr", "concat_expr",
  "unary_expr", "postfix_expr", "primary", "arglist", "arglist_ne",
  "goto_clauses", "gclause_s", "gclause_f", "gclause_u", "glabel", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-72)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-23)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -72,     8,    75,   -72,   -19,   -72,    -7,   -72,   -72,    14,
       1,   -72,   -72,   -72,    63,   394,   -17,   -17,   -72,    35,
      33,   -72,   -72,   -72,    36,   -72,   -72,   -72,   -72,   -17,
     394,   394,   394,   394,   394,   394,   394,   394,   394,   394,
      -3,    41,   -72,    44,    53,   -17,   -72,   -72,   -72,   -17,
     394,   -72,   -72,   -72,   -72,   -72,   -72,   -72,   -72,   -72,
     -72,    42,   394,    55,   -72,   -17,   -17,   -17,    48,   394,
     -11,    43,   -72,   394,   -13,     0,   -17,   394,    48,    48,
     -72,   -72,    73,   -17,   -72,   -17,   -10,   -17,    70,   370,
     -72,   -72,    42,   -72,    63,   -17,   -17,   -17,   -72,    71,
      72,    69,   394,   -72,    65,    66,   130,   154,   178,   202,
     226,   250,   274,   298,   322,   346,   -72,   -13,    59,    83,
      86,    89,   -72,   -72,   -17,   -17,   394,   394,   394,   394,
     394,   394,   394,   394,   394,   394,   394,   394,   -72,   -72,
     -72,   -72,   394,    -4,   -72,   -72,   -72,   -72,   -72,   -72,
     -72,   -72,   -72,   -72,   -72,   -72,   -72,   -72
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     0,     1,     0,    18,     0,     7,     4,     0,
      11,     8,     6,     5,     9,     0,     9,     9,    10,     0,
       9,    63,    64,    65,    56,    54,    51,    52,    53,     9,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       9,    38,    49,     0,     0,     9,    66,    67,    68,     9,
       0,    46,    39,    40,    41,    48,    45,    43,    42,    47,
      44,    19,    10,     0,    13,     9,     9,     9,     0,    59,
       9,    23,    36,     0,     9,     9,     9,    59,     0,     0,
      72,    73,     0,     9,    61,     9,    60,     9,     0,     0,
      20,    14,    19,    15,     9,     9,     9,     9,    74,     0,
       0,     0,     0,    57,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    37,     9,     9,     0,
       0,     0,    71,    55,     9,     9,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    16,    50,
      69,    70,     0,     0,    24,    32,    31,    27,    28,    29,
      30,    25,    33,    34,    26,    35,    62,    58
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -72,   -72,   -72,   -72,   -16,   -72,   -72,    15,   -71,    -5,
     -72,     4,   -72,   -72,    56,    18,    46,   -18,   -15,   -14,
     -53
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     1,     2,     8,    19,     9,    10,    74,    64,    84,
      71,    72,    41,    42,    85,    86,    20,    21,    22,    23,
      83
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      43,    44,    46,    91,    93,    47,    48,    61,     3,   157,
      92,    11,    14,    50,    18,    87,    -9,   -21,    18,    40,
      18,    18,   124,    12,    63,    96,    97,   -21,    62,    68,
     -21,    18,    15,    69,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    13,    70,   138,    45,    49,    77,
      78,    79,    80,    81,    88,    65,    66,    75,    63,    63,
      94,    16,    17,   -12,    18,    67,    76,    99,    90,   100,
     101,   102,    82,    73,    89,    -2,     4,    98,     5,   119,
     120,   121,     6,   103,   122,   123,   -17,    16,    17,   -22,
      18,    16,    17,   116,    18,   124,   126,   127,   139,   140,
      46,    63,   141,    47,    48,     7,   -17,   117,   142,   143,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
     125,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,    95,    24,    25,    26,   156,    27,    28,
     118,     0,    29,     0,     0,     0,     0,    30,    31,    32,
      33,    34,    35,    36,    37,    38,     0,    39,    24,    25,
      26,   128,    27,    28,     0,     0,    29,     0,     0,     0,
       0,    30,    31,    32,    33,    34,    35,    36,    37,    38,
       0,    39,    24,    25,    26,   129,    27,    28,     0,     0,
      29,     0,     0,     0,     0,    30,    31,    32,    33,    34,
      35,    36,    37,    38,     0,    39,    24,    25,    26,   130,
      27,    28,     0,     0,    29,     0,     0,     0,     0,    30,
      31,    32,    33,    34,    35,    36,    37,    38,     0,    39,
      24,    25,    26,   131,    27,    28,     0,     0,    29,     0,
       0,     0,     0,    30,    31,    32,    33,    34,    35,    36,
      37,    38,     0,    39,    24,    25,    26,   132,    27,    28,
       0,     0,    29,     0,     0,     0,     0,    30,    31,    32,
      33,    34,    35,    36,    37,    38,     0,    39,    24,    25,
      26,   133,    27,    28,     0,     0,    29,     0,     0,     0,
       0,    30,    31,    32,    33,    34,    35,    36,    37,    38,
       0,    39,    24,    25,    26,   134,    27,    28,     0,     0,
      29,     0,     0,     0,     0,    30,    31,    32,    33,    34,
      35,    36,    37,    38,     0,    39,    24,    25,    26,   135,
      27,    28,     0,     0,    29,     0,     0,     0,     0,    30,
      31,    32,    33,    34,    35,    36,    37,    38,     0,    39,
      24,    25,    26,   136,    27,    28,     0,     0,    29,     0,
       0,     0,     0,    30,    31,    32,    33,    34,    35,    36,
      37,    38,     0,    39,    24,    25,    26,   137,    27,    28,
     104,     0,    29,     0,     0,     0,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,     0,   115,    24,    25,
      26,     0,    27,    28,     0,     0,    29,     0,     0,     0,
       0,    30,    31,    32,    33,    34,    35,    36,    37,    38,
       0,    39
};

static const yytype_int16 yycheck[] =
{
      16,    17,    20,    74,    75,    20,    20,    10,     0,    13,
      10,    30,    11,    29,    31,    26,    26,    30,    31,    15,
      31,    31,    26,    30,    40,    78,    79,    30,    31,    45,
      30,    31,    31,    49,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    30,    50,   117,    12,    12,    65,
      66,    67,     4,     5,    70,    14,    12,    62,    74,    75,
      76,    28,    29,    30,    31,    12,    11,    83,    73,    85,
      86,    87,    24,    31,    31,     0,     1,     4,     3,    95,
      96,    97,     7,    13,    13,    13,    11,    28,    29,    30,
      31,    28,    29,    89,    31,    26,    31,    31,    15,    13,
     118,   117,    13,   118,   118,    30,    31,    92,   124,   125,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     102,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,    77,     4,     5,     6,   142,     8,     9,
      94,    -1,    12,    -1,    -1,    -1,    -1,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    -1,    27,     4,     5,
       6,    31,     8,     9,    -1,    -1,    12,    -1,    -1,    -1,
      -1,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      -1,    27,     4,     5,     6,    31,     8,     9,    -1,    -1,
      12,    -1,    -1,    -1,    -1,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    -1,    27,     4,     5,     6,    31,
       8,     9,    -1,    -1,    12,    -1,    -1,    -1,    -1,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    -1,    27,
       4,     5,     6,    31,     8,     9,    -1,    -1,    12,    -1,
      -1,    -1,    -1,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    -1,    27,     4,     5,     6,    31,     8,     9,
      -1,    -1,    12,    -1,    -1,    -1,    -1,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    -1,    27,     4,     5,
       6,    31,     8,     9,    -1,    -1,    12,    -1,    -1,    -1,
      -1,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      -1,    27,     4,     5,     6,    31,     8,     9,    -1,    -1,
      12,    -1,    -1,    -1,    -1,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    -1,    27,     4,     5,     6,    31,
       8,     9,    -1,    -1,    12,    -1,    -1,    -1,    -1,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    -1,    27,
       4,     5,     6,    31,     8,     9,    -1,    -1,    12,    -1,
      -1,    -1,    -1,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    -1,    27,     4,     5,     6,    31,     8,     9,
      10,    -1,    12,    -1,    -1,    -1,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    -1,    27,     4,     5,
       6,    -1,     8,     9,    -1,    -1,    12,    -1,    -1,    -1,
      -1,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      -1,    27
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    33,    34,     0,     1,     3,     7,    30,    35,    37,
      38,    30,    30,    30,    11,    31,    28,    29,    31,    36,
      48,    49,    50,    51,     4,     5,     6,     8,     9,    12,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    27,
      43,    44,    45,    36,    36,    12,    49,    50,    51,    12,
      36,    43,    43,    43,    43,    43,    43,    43,    43,    43,
      43,    10,    31,    36,    40,    14,    12,    12,    36,    36,
      41,    42,    43,    31,    39,    41,    11,    36,    36,    36,
       4,     5,    24,    52,    41,    46,    47,    26,    36,    31,
      41,    40,    10,    40,    36,    46,    52,    52,     4,    36,
      36,    36,    36,    13,    10,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    27,    43,    39,    48,    36,
      36,    36,    13,    13,    26,    47,    31,    31,    31,    31,
      31,    31,    31,    31,    31,    31,    31,    31,    40,    15,
      13,    13,    36,    36,    41,    41,    41,    41,    41,    41,
      41,    41,    41,    41,    41,    41,    41,    13
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    32,    33,    34,    34,    35,    35,    35,    35,    36,
      36,    37,    37,    37,    37,    37,    37,    38,    38,    39,
      39,    40,    40,    41,    41,    41,    41,    41,    41,    41,
      41,    41,    41,    41,    41,    41,    42,    42,    43,    43,
      43,    43,    43,    43,    43,    43,    43,    43,    43,    44,
      44,    45,    45,    45,    45,    45,    45,    45,    45,    46,
      46,    47,    47,    48,    48,    48,    48,    48,    48,    49,
      50,    51,    52,    52,    52
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     2,     2,     1,     2,     0,
       1,     1,     3,     4,     6,     6,     8,     0,     1,     0,
       2,     0,     4,     1,     5,     5,     5,     5,     5,     5,
       5,     5,     5,     5,     5,     5,     1,     3,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     1,
       6,     1,     1,     1,     1,     6,     1,     5,     8,     0,
       1,     1,     5,     1,     1,     1,     2,     2,     2,     7,
       7,     6,     1,     1,     2
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
  case 5: /* line: stmt NEWLINE  */
#line 92 "sno.y"
                   {
            if ((yyvsp[-1].stmt)) {
                (yyvsp[-1].stmt)->next = NULL;
                if (!prog->head) prog->head = prog->tail = (yyvsp[-1].stmt);
                else { prog->tail->next = (yyvsp[-1].stmt); prog->tail = (yyvsp[-1].stmt); }
                prog->nstmts++;
            }
        }
#line 1317 "sno.tab.c"
    break;

  case 6: /* line: END_LABEL NEWLINE  */
#line 100 "sno.y"
                        {
            Stmt *s = stmt_new();
            s->label = strdup("END");
            s->is_end = 1;
            s->lineno = lineno_stmt;
            if (!prog->head) prog->head = prog->tail = s;
            else { prog->tail->next = s; prog->tail = s; }
            prog->nstmts++;
        }
#line 1331 "sno.tab.c"
    break;

  case 7: /* line: NEWLINE  */
#line 109 "sno.y"
                        { }
#line 1337 "sno.tab.c"
    break;

  case 8: /* line: error NEWLINE  */
#line 110 "sno.y"
                        { yyerrok; }
#line 1343 "sno.tab.c"
    break;

  case 11: /* stmt: opt_label  */
#line 137 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[0].sval); s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1349 "sno.tab.c"
    break;

  case 12: /* stmt: opt_label COLON goto_clauses  */
#line 139 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[-2].sval); s->go=(SnoGoto*)(yyvsp[0].go);
          s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1356 "sno.tab.c"
    break;

  case 13: /* stmt: opt_label __ unary_expr opt_goto  */
#line 142 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[-3].sval); s->subject=(yyvsp[-1].expr);
          s->go=(SnoGoto*)(yyvsp[0].go); s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1363 "sno.tab.c"
    break;

  case 14: /* stmt: opt_label __ unary_expr EQ opt_repl opt_goto  */
#line 145 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[-5].sval); s->subject=(yyvsp[-3].expr);
          s->replacement=(yyvsp[-1].expr); s->go=(SnoGoto*)(yyvsp[0].go);
          s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1371 "sno.tab.c"
    break;

  case 15: /* stmt: opt_label __ unary_expr __ expr opt_goto  */
#line 149 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[-5].sval); s->subject=(yyvsp[-3].expr);
          s->pattern=(yyvsp[-1].expr); s->go=(SnoGoto*)(yyvsp[0].go);
          s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1379 "sno.tab.c"
    break;

  case 16: /* stmt: opt_label __ unary_expr __ expr EQ opt_repl opt_goto  */
#line 153 "sno.y"
        { Stmt *s=stmt_new(); s->label=(yyvsp[-7].sval); s->subject=(yyvsp[-5].expr);
          s->pattern=(yyvsp[-3].expr); s->replacement=(yyvsp[-1].expr); s->go=(SnoGoto*)(yyvsp[0].go);
          s->lineno=lineno_stmt; (yyval.stmt)=s; }
#line 1387 "sno.tab.c"
    break;

  case 17: /* opt_label: %empty  */
#line 158 "sno.y"
                        { (yyval.sval)=NULL; }
#line 1393 "sno.tab.c"
    break;

  case 18: /* opt_label: LABEL  */
#line 158 "sno.y"
                                             { (yyval.sval)=(yyvsp[0].sval); }
#line 1399 "sno.tab.c"
    break;

  case 19: /* opt_repl: %empty  */
#line 161 "sno.y"
                   { Expr *e=expr_new(E_NULL); (yyval.expr)=e; }
#line 1405 "sno.tab.c"
    break;

  case 20: /* opt_repl: __ expr  */
#line 162 "sno.y"
                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1411 "sno.tab.c"
    break;

  case 21: /* opt_goto: %empty  */
#line 165 "sno.y"
                       { (yyval.go)=NULL; }
#line 1417 "sno.tab.c"
    break;

  case 22: /* opt_goto: _ COLON _ goto_clauses  */
#line 165 "sno.y"
                                                             { (yyval.go)=(yyvsp[0].go); }
#line 1423 "sno.tab.c"
    break;

  case 23: /* expr: concat_expr  */
#line 183 "sno.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1429 "sno.tab.c"
    break;

  case 24: /* expr: concat_expr __ EQ __ expr  */
#line 184 "sno.y"
                                                { (yyval.expr)=binop(E_ASSIGN,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1435 "sno.tab.c"
    break;

  case 25: /* expr: concat_expr __ PIPE __ expr  */
#line 185 "sno.y"
                                                { (yyval.expr)=binop(E_ALT,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1441 "sno.tab.c"
    break;

  case 26: /* expr: concat_expr __ AMP __ expr  */
#line 186 "sno.y"
                                                { (yyval.expr)=binop(E_REDUCE,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1447 "sno.tab.c"
    break;

  case 27: /* expr: concat_expr __ PLUS __ expr  */
#line 187 "sno.y"
                                                { (yyval.expr)=binop(E_ADD,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1453 "sno.tab.c"
    break;

  case 28: /* expr: concat_expr __ MINUS __ expr  */
#line 188 "sno.y"
                                                { (yyval.expr)=binop(E_SUB,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1459 "sno.tab.c"
    break;

  case 29: /* expr: concat_expr __ STAR __ expr  */
#line 189 "sno.y"
                                                { (yyval.expr)=binop(E_MUL,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1465 "sno.tab.c"
    break;

  case 30: /* expr: concat_expr __ SLASH __ expr  */
#line 190 "sno.y"
                                                { (yyval.expr)=binop(E_DIV,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1471 "sno.tab.c"
    break;

  case 31: /* expr: concat_expr __ CARET __ expr  */
#line 191 "sno.y"
                                                { (yyval.expr)=binop(E_POW,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1477 "sno.tab.c"
    break;

  case 32: /* expr: concat_expr __ STARSTAR __ expr  */
#line 192 "sno.y"
                                                { (yyval.expr)=binop(E_POW,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1483 "sno.tab.c"
    break;

  case 33: /* expr: concat_expr __ DOT __ expr  */
#line 193 "sno.y"
                                                { (yyval.expr)=binop(E_COND,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1489 "sno.tab.c"
    break;

  case 34: /* expr: concat_expr __ DOLLAR __ expr  */
#line 194 "sno.y"
                                                { (yyval.expr)=binop(E_IMM,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1495 "sno.tab.c"
    break;

  case 35: /* expr: concat_expr __ AT __ expr  */
#line 195 "sno.y"
                                                { (yyval.expr)=binop(E_AT,(yyvsp[-4].expr),(yyvsp[0].expr)); }
#line 1501 "sno.tab.c"
    break;

  case 36: /* concat_expr: unary_expr  */
#line 200 "sno.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1507 "sno.tab.c"
    break;

  case 37: /* concat_expr: concat_expr __ unary_expr  */
#line 201 "sno.y"
                                                { (yyval.expr)=binop(E_CONCAT,(yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1513 "sno.tab.c"
    break;

  case 38: /* unary_expr: postfix_expr  */
#line 206 "sno.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1519 "sno.tab.c"
    break;

  case 39: /* unary_expr: PLUS unary_expr  */
#line 207 "sno.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1525 "sno.tab.c"
    break;

  case 40: /* unary_expr: MINUS unary_expr  */
#line 208 "sno.y"
                                                { (yyval.expr)=binop(E_NEG,NULL,(yyvsp[0].expr)); }
#line 1531 "sno.tab.c"
    break;

  case 41: /* unary_expr: STAR unary_expr  */
#line 209 "sno.y"
                                                { (yyval.expr)=binop(E_DEREF,NULL,(yyvsp[0].expr)); }
#line 1537 "sno.tab.c"
    break;

  case 42: /* unary_expr: DOLLAR unary_expr  */
#line 210 "sno.y"
                                                { (yyval.expr)=binop(E_DEREF,NULL,(yyvsp[0].expr)); }
#line 1543 "sno.tab.c"
    break;

  case 43: /* unary_expr: DOT unary_expr  */
#line 211 "sno.y"
                                                { (yyval.expr)=binop(E_COND,NULL,(yyvsp[0].expr)); }
#line 1549 "sno.tab.c"
    break;

  case 44: /* unary_expr: AT unary_expr  */
#line 212 "sno.y"
                                                { Expr *e=expr_new(E_AT); e->right=(yyvsp[0].expr); (yyval.expr)=e; }
#line 1555 "sno.tab.c"
    break;

  case 45: /* unary_expr: PIPE unary_expr  */
#line 213 "sno.y"
                                                { (yyval.expr)=binop(E_ALT,NULL,(yyvsp[0].expr)); }
#line 1561 "sno.tab.c"
    break;

  case 46: /* unary_expr: CARET unary_expr  */
#line 214 "sno.y"
                                                { (yyval.expr)=binop(E_POW,NULL,(yyvsp[0].expr)); }
#line 1567 "sno.tab.c"
    break;

  case 47: /* unary_expr: AMP unary_expr  */
#line 215 "sno.y"
                                                { (yyval.expr)=binop(E_REDUCE,NULL,(yyvsp[0].expr)); }
#line 1573 "sno.tab.c"
    break;

  case 48: /* unary_expr: SLASH unary_expr  */
#line 216 "sno.y"
                                                { (yyval.expr)=binop(E_DIV,NULL,(yyvsp[0].expr)); }
#line 1579 "sno.tab.c"
    break;

  case 49: /* postfix_expr: primary  */
#line 221 "sno.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1585 "sno.tab.c"
    break;

  case 50: /* postfix_expr: postfix_expr LBRACKET _ arglist _ RBRACKET  */
#line 222 "sno.y"
                                                 {
        AL *al=(yyvsp[-2].al); Expr *e=expr_new(E_INDEX);
        e->left=(yyvsp[-5].expr); e->args=al->a; e->nargs=al->n; free(al); (yyval.expr)=e; }
#line 1593 "sno.tab.c"
    break;

  case 51: /* primary: STR  */
#line 229 "sno.y"
                { Expr *e=expr_new(E_STR);  e->sval=(yyvsp[0].sval); (yyval.expr)=e; }
#line 1599 "sno.tab.c"
    break;

  case 52: /* primary: INT  */
#line 230 "sno.y"
                { Expr *e=expr_new(E_INT);  e->ival=(yyvsp[0].ival); (yyval.expr)=e; }
#line 1605 "sno.tab.c"
    break;

  case 53: /* primary: REAL  */
#line 231 "sno.y"
                { Expr *e=expr_new(E_REAL); e->dval=(yyvsp[0].dval); (yyval.expr)=e; }
#line 1611 "sno.tab.c"
    break;

  case 54: /* primary: KEYWORD  */
#line 232 "sno.y"
                { Expr *e=expr_new(E_KEYWORD); e->sval=(yyvsp[0].sval); (yyval.expr)=e; }
#line 1617 "sno.tab.c"
    break;

  case 55: /* primary: IDENT LPAREN _ arglist _ RPAREN  */
#line 234 "sno.y"
        { AL *al=(yyvsp[-2].al); Expr *e=expr_new(E_CALL);
          e->sval=(yyvsp[-5].sval); e->args=al->a; e->nargs=al->n; free(al); (yyval.expr)=e; }
#line 1624 "sno.tab.c"
    break;

  case 56: /* primary: IDENT  */
#line 236 "sno.y"
                { Expr *e=expr_new(E_VAR); e->sval=(yyvsp[0].sval); (yyval.expr)=e; }
#line 1630 "sno.tab.c"
    break;

  case 57: /* primary: LPAREN _ expr _ RPAREN  */
#line 237 "sno.y"
                                           { (yyval.expr)=(yyvsp[-2].expr); }
#line 1636 "sno.tab.c"
    break;

  case 58: /* primary: LPAREN _ expr COMMA _ arglist_ne _ RPAREN  */
#line 239 "sno.y"
        { /* (a, b, c) — alternation grouping */
          AL *al=(yyvsp[-2].al); Expr *e=(yyvsp[-5].expr);
          for (int i=0;i<al->n;i++) e=binop(E_ALT,e,al->a[i]);
          free(al->a); free(al); (yyval.expr)=e; }
#line 1645 "sno.tab.c"
    break;

  case 59: /* arglist: %empty  */
#line 246 "sno.y"
                    { (yyval.al)=al_new(); }
#line 1651 "sno.tab.c"
    break;

  case 60: /* arglist: arglist_ne  */
#line 247 "sno.y"
                    { (yyval.al)=(yyvsp[0].al); }
#line 1657 "sno.tab.c"
    break;

  case 61: /* arglist_ne: expr  */
#line 251 "sno.y"
                                    { AL *al=al_new(); al_push(al,(yyvsp[0].expr)); (yyval.al)=al; }
#line 1663 "sno.tab.c"
    break;

  case 62: /* arglist_ne: arglist_ne _ COMMA _ expr  */
#line 252 "sno.y"
                                    { al_push((yyvsp[-4].al),(yyvsp[0].expr)); (yyval.al)=(yyvsp[-4].al); }
#line 1669 "sno.tab.c"
    break;

  case 63: /* goto_clauses: gclause_s  */
#line 256 "sno.y"
                    { SnoGoto *g=sgoto_new(); g->onsuccess=(yyvsp[0].sval); (yyval.go)=g; }
#line 1675 "sno.tab.c"
    break;

  case 64: /* goto_clauses: gclause_f  */
#line 257 "sno.y"
                    { SnoGoto *g=sgoto_new(); g->onfailure=(yyvsp[0].sval); (yyval.go)=g; }
#line 1681 "sno.tab.c"
    break;

  case 65: /* goto_clauses: gclause_u  */
#line 258 "sno.y"
                    { SnoGoto *g=sgoto_new(); g->uncond=(yyvsp[0].sval); (yyval.go)=g; }
#line 1687 "sno.tab.c"
    break;

  case 66: /* goto_clauses: goto_clauses gclause_s  */
#line 259 "sno.y"
                             { ((SnoGoto*)(yyvsp[-1].go))->onsuccess=(yyvsp[0].sval); (yyval.go)=(yyvsp[-1].go); }
#line 1693 "sno.tab.c"
    break;

  case 67: /* goto_clauses: goto_clauses gclause_f  */
#line 260 "sno.y"
                             { ((SnoGoto*)(yyvsp[-1].go))->onfailure=(yyvsp[0].sval); (yyval.go)=(yyvsp[-1].go); }
#line 1699 "sno.tab.c"
    break;

  case 68: /* goto_clauses: goto_clauses gclause_u  */
#line 261 "sno.y"
                             { ((SnoGoto*)(yyvsp[-1].go))->uncond=(yyvsp[0].sval); (yyval.go)=(yyvsp[-1].go); }
#line 1705 "sno.tab.c"
    break;

  case 69: /* gclause_s: SGOTO _ LPAREN _ glabel _ RPAREN  */
#line 264 "sno.y"
                                              { (yyval.sval)=(yyvsp[-2].sval); }
#line 1711 "sno.tab.c"
    break;

  case 70: /* gclause_f: FGOTO _ LPAREN _ glabel _ RPAREN  */
#line 265 "sno.y"
                                              { (yyval.sval)=(yyvsp[-2].sval); }
#line 1717 "sno.tab.c"
    break;

  case 71: /* gclause_u: _ LPAREN _ glabel _ RPAREN  */
#line 266 "sno.y"
                                              { (yyval.sval)=(yyvsp[-2].sval); }
#line 1723 "sno.tab.c"
    break;

  case 72: /* glabel: IDENT  */
#line 269 "sno.y"
                    { (yyval.sval)=(yyvsp[0].sval); }
#line 1729 "sno.tab.c"
    break;

  case 73: /* glabel: KEYWORD  */
#line 270 "sno.y"
                    { (yyval.sval)=(yyvsp[0].sval); }
#line 1735 "sno.tab.c"
    break;

  case 74: /* glabel: DOLLAR IDENT  */
#line 271 "sno.y"
                    { char *buf=malloc(strlen((yyvsp[0].sval))+3);
                      sprintf(buf,"$%s",(yyvsp[0].sval)); (yyval.sval)=buf; }
#line 1742 "sno.tab.c"
    break;


#line 1746 "sno.tab.c"

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

#line 275 "sno.y"


void sno_parse_init(void) {
    prog = calloc(1, sizeof *prog);
    parsed_program = prog;
}
