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
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         SNOBOL4_STYPE
/* Substitute the variable and function names.  */
#define yyparse         snobol4_parse
#define yylex           snobol4_lex
#define yyerror         snobol4_error
#define yydebug         snobol4_debug
#define yynerrs         snobol4_nerrs

/* First part of user prologue.  */
#line 28 "snobol4_grammar.y"

#include "scrip_cc.h"
#include "snobol4_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { Lex *lx; EXPR_t **result; } ParseParm;
static Lex *g_lx;

#line 89 "snobol4_grammar.tab.c"

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

#include "snobol4_grammar.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_TK_IDENT = 3,                   /* TK_IDENT  */
  YYSYMBOL_TK_END = 4,                     /* TK_END  */
  YYSYMBOL_TK_INT = 5,                     /* TK_INT  */
  YYSYMBOL_TK_REAL = 6,                    /* TK_REAL  */
  YYSYMBOL_TK_STR = 7,                     /* TK_STR  */
  YYSYMBOL_TK_KEYWORD = 8,                 /* TK_KEYWORD  */
  YYSYMBOL_TK_PLUS = 9,                    /* TK_PLUS  */
  YYSYMBOL_TK_MINUS = 10,                  /* TK_MINUS  */
  YYSYMBOL_TK_STAR = 11,                   /* TK_STAR  */
  YYSYMBOL_TK_SLASH = 12,                  /* TK_SLASH  */
  YYSYMBOL_TK_PCT = 13,                    /* TK_PCT  */
  YYSYMBOL_TK_CARET = 14,                  /* TK_CARET  */
  YYSYMBOL_TK_BANG = 15,                   /* TK_BANG  */
  YYSYMBOL_TK_STARSTAR = 16,               /* TK_STARSTAR  */
  YYSYMBOL_TK_AMP = 17,                    /* TK_AMP  */
  YYSYMBOL_TK_AT = 18,                     /* TK_AT  */
  YYSYMBOL_TK_TILDE = 19,                  /* TK_TILDE  */
  YYSYMBOL_TK_DOLLAR = 20,                 /* TK_DOLLAR  */
  YYSYMBOL_TK_DOT = 21,                    /* TK_DOT  */
  YYSYMBOL_TK_HASH = 22,                   /* TK_HASH  */
  YYSYMBOL_TK_PIPE = 23,                   /* TK_PIPE  */
  YYSYMBOL_TK_EQ = 24,                     /* TK_EQ  */
  YYSYMBOL_TK_QMARK = 25,                  /* TK_QMARK  */
  YYSYMBOL_TK_COMMA = 26,                  /* TK_COMMA  */
  YYSYMBOL_TK_LPAREN = 27,                 /* TK_LPAREN  */
  YYSYMBOL_TK_RPAREN = 28,                 /* TK_RPAREN  */
  YYSYMBOL_TK_LBRACKET = 29,               /* TK_LBRACKET  */
  YYSYMBOL_TK_RBRACKET = 30,               /* TK_RBRACKET  */
  YYSYMBOL_TK_LANGLE = 31,                 /* TK_LANGLE  */
  YYSYMBOL_TK_RANGLE = 32,                 /* TK_RANGLE  */
  YYSYMBOL_YYACCEPT = 33,                  /* $accept  */
  YYSYMBOL_top = 34,                       /* top  */
  YYSYMBOL_expr = 35,                      /* expr  */
  YYSYMBOL_expr0 = 36,                     /* expr0  */
  YYSYMBOL_expr2 = 37,                     /* expr2  */
  YYSYMBOL_expr3 = 38,                     /* expr3  */
  YYSYMBOL_expr4 = 39,                     /* expr4  */
  YYSYMBOL_expr5 = 40,                     /* expr5  */
  YYSYMBOL_expr6 = 41,                     /* expr6  */
  YYSYMBOL_expr7 = 42,                     /* expr7  */
  YYSYMBOL_expr8 = 43,                     /* expr8  */
  YYSYMBOL_expr9 = 44,                     /* expr9  */
  YYSYMBOL_expr10 = 45,                    /* expr10  */
  YYSYMBOL_expr11 = 46,                    /* expr11  */
  YYSYMBOL_expr12 = 47,                    /* expr12  */
  YYSYMBOL_expr13 = 48,                    /* expr13  */
  YYSYMBOL_expr14 = 49,                    /* expr14  */
  YYSYMBOL_expr15 = 50,                    /* expr15  */
  YYSYMBOL_exprlist = 51,                  /* exprlist  */
  YYSYMBOL_exprlist_ne = 52,               /* exprlist_ne  */
  YYSYMBOL_expr17 = 53                     /* expr17  */
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
typedef yytype_int8 yy_state_t;

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
         || (defined SNOBOL4_STYPE_IS_TRIVIAL && SNOBOL4_STYPE_IS_TRIVIAL)))

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
#define YYFINAL  64
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   136

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  33
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  21
/* YYNRULES -- Number of rules.  */
#define YYNRULES  72
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  118

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   287


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
      25,    26,    27,    28,    29,    30,    31,    32
};

#if SNOBOL4_DEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    57,    57,    58,    61,    65,    66,    67,    72,    73,
      78,    83,    88,    93,    98,    99,   104,   105,   106,   111,
     112,   117,   118,   123,   124,   129,   130,   135,   136,   137,
     138,   143,   144,   145,   150,   151,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   178,   182,   186,   191,   192,   195,
     196,   197,   202,   203,   207,   208,   212,   216,   217,   218,
     219,   220,   221
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if SNOBOL4_DEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "TK_IDENT", "TK_END",
  "TK_INT", "TK_REAL", "TK_STR", "TK_KEYWORD", "TK_PLUS", "TK_MINUS",
  "TK_STAR", "TK_SLASH", "TK_PCT", "TK_CARET", "TK_BANG", "TK_STARSTAR",
  "TK_AMP", "TK_AT", "TK_TILDE", "TK_DOLLAR", "TK_DOT", "TK_HASH",
  "TK_PIPE", "TK_EQ", "TK_QMARK", "TK_COMMA", "TK_LPAREN", "TK_RPAREN",
  "TK_LBRACKET", "TK_RBRACKET", "TK_LANGLE", "TK_RANGLE", "$accept", "top",
  "expr", "expr0", "expr2", "expr3", "expr4", "expr5", "expr6", "expr7",
  "expr8", "expr9", "expr10", "expr11", "expr12", "expr13", "expr14",
  "expr15", "exprlist", "exprlist_ne", "expr17", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-60)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
     109,    22,    23,   -60,   -60,   -60,   -60,   109,   109,   109,
     109,   109,   109,   109,   109,   109,   109,   109,   109,   109,
     109,   109,   109,   109,    65,    33,   -60,   -60,     3,    28,
     109,    34,    16,    31,    42,    44,    43,   -60,    15,   -60,
      38,    14,   -60,   109,   109,   -60,   -60,   -60,   -60,   -60,
     -60,   -60,   -60,   -60,   -60,   -60,   -60,   -60,   -60,   -60,
     -60,   -60,   -60,     6,   -60,   109,   109,   109,   109,    34,
     109,   109,   109,   109,   109,   109,   109,   109,   109,   109,
     109,   109,   109,   109,   109,   -60,    30,    35,    32,   109,
     -60,    28,   -60,   -60,   109,    16,    31,    31,    42,    44,
      43,   -60,   -60,   -60,   -60,   -60,   -60,   -60,    29,    59,
     -60,   109,   -60,    18,   -60,   -60,   -60,   -60
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,    67,    68,    71,    72,    70,    69,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     2,     4,     7,     9,
      11,    13,    15,    18,    20,    22,    24,    26,    30,    33,
      35,    53,    56,    58,    58,    40,    41,    42,    47,    46,
      51,    45,    52,    39,    36,    37,    43,    44,    48,    50,
      49,    38,    64,     0,     1,     0,     0,     0,     0,    12,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    58,    58,    61,     0,    57,     0,     0,
      62,     8,     5,     6,    10,    14,    16,    17,    19,    21,
      23,    25,    27,    28,    29,    31,    32,    34,     0,     0,
      65,    60,    66,     0,    54,    55,    59,    63
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -60,   -60,   -60,     0,   -60,    -3,    -5,   -29,    -6,   -24,
      21,    24,    20,   -39,   -60,   -59,    -4,   -60,   -42,     7,
     -60
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,    25,    26,    85,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    86,    87,
      42
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      27,    69,    88,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      65,   105,   106,   107,    63,    71,    72,    66,    67,    77,
      78,    79,    89,    64,    90,    80,    81,   101,   102,   103,
     104,   108,   109,    83,   111,    84,   117,    96,    97,    43,
      44,    68,    70,    73,    74,    75,    76,    82,   110,   114,
     112,   111,    91,    94,    95,    69,    92,    93,     1,     2,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,   115,    24,    62,    98,   100,   113,     0,    99,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   116,     1,     2,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,     0,    24
};

static const yytype_int8 yycheck[] =
{
       0,    30,    44,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      17,    80,    81,    82,    24,     9,    10,    24,    25,    14,
      15,    16,    26,     0,    28,    20,    21,    76,    77,    78,
      79,    83,    84,    29,    26,    31,    28,    71,    72,    27,
      27,    23,    18,    22,    12,    11,    13,    19,    28,    30,
      28,    26,    65,    68,    70,    94,    66,    67,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    32,    27,    28,    73,    75,    89,    -1,    74,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   111,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    -1,    27
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    27,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    53,    27,    27,    49,    49,    49,    49,    49,
      49,    49,    49,    49,    49,    49,    49,    49,    49,    49,
      49,    49,    28,    36,     0,    17,    24,    25,    23,    40,
      18,     9,    10,    22,    12,    11,    13,    14,    15,    16,
      20,    21,    19,    29,    31,    36,    51,    52,    51,    26,
      28,    38,    36,    36,    39,    41,    42,    42,    43,    44,
      45,    46,    46,    46,    46,    48,    48,    48,    51,    51,
      28,    26,    28,    52,    30,    32,    36,    28
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    33,    34,    34,    35,    36,    36,    36,    37,    37,
      38,    38,    39,    39,    40,    40,    41,    41,    41,    42,
      42,    43,    43,    44,    44,    45,    45,    46,    46,    46,
      46,    47,    47,    47,    48,    48,    49,    49,    49,    49,
      49,    49,    49,    49,    49,    49,    49,    49,    49,    49,
      49,    49,    49,    49,    50,    50,    50,    51,    51,    52,
      52,    52,    53,    53,    53,    53,    53,    53,    53,    53,
      53,    53,    53
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     1,     3,     3,     1,     3,     1,
       3,     1,     2,     1,     3,     1,     3,     3,     1,     3,
       1,     3,     1,     3,     1,     3,     1,     3,     3,     3,
       1,     3,     3,     1,     3,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     1,     4,     4,     1,     1,     0,     3,
       2,     1,     3,     5,     2,     4,     4,     1,     1,     1,
       1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = SNOBOL4_EMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == SNOBOL4_EMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (yyparse_param, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use SNOBOL4_error or SNOBOL4_UNDEF. */
#define YYERRCODE SNOBOL4_UNDEF


/* Enable debugging if requested.  */
#if SNOBOL4_DEBUG

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
                  Kind, Value, yyparse_param); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, void *yyparse_param)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yyparse_param);
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, void *yyparse_param)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep, yyparse_param);
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
                 int yyrule, void *yyparse_param)
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
                       &yyvsp[(yyi + 1) - (yynrhs)], yyparse_param);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule, yyparse_param); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !SNOBOL4_DEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !SNOBOL4_DEBUG */


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
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, void *yyparse_param)
{
  YY_USE (yyvaluep);
  YY_USE (yyparse_param);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (void *yyparse_param)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

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

  yychar = SNOBOL4_EMPTY; /* Cause a token to be read.  */

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
  if (yychar == SNOBOL4_EMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval);
    }

  if (yychar <= SNOBOL4_EOF)
    {
      yychar = SNOBOL4_EOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == SNOBOL4_error)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = SNOBOL4_UNDEF;
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
  yychar = SNOBOL4_EMPTY;
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
  case 2: /* top: expr  */
#line 57 "snobol4_grammar.y"
                  { *(((ParseParm*)yyparse_param)->result) = (yyvsp[0].expr); }
#line 1217 "snobol4_grammar.tab.c"
    break;

  case 3: /* top: %empty  */
#line 58 "snobol4_grammar.y"
                  { *(((ParseParm*)yyparse_param)->result) = NULL; }
#line 1223 "snobol4_grammar.tab.c"
    break;

  case 4: /* expr: expr0  */
#line 61 "snobol4_grammar.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 1229 "snobol4_grammar.tab.c"
    break;

  case 5: /* expr0: expr2 TK_EQ expr0  */
#line 65 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_ASSIGN,         (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1235 "snobol4_grammar.tab.c"
    break;

  case 6: /* expr0: expr2 TK_QMARK expr0  */
#line 66 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_CAPT_COND_ASGN, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1241 "snobol4_grammar.tab.c"
    break;

  case 7: /* expr0: expr2  */
#line 67 "snobol4_grammar.y"
                             { (yyval.expr) = (yyvsp[0].expr); }
#line 1247 "snobol4_grammar.tab.c"
    break;

  case 8: /* expr2: expr2 TK_AMP expr3  */
#line 72 "snobol4_grammar.y"
                          { (yyval.expr) = expr_binary(E_OPSYN, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1253 "snobol4_grammar.tab.c"
    break;

  case 9: /* expr2: expr3  */
#line 73 "snobol4_grammar.y"
                          { (yyval.expr) = (yyvsp[0].expr); }
#line 1259 "snobol4_grammar.tab.c"
    break;

  case 10: /* expr3: expr3 TK_PIPE expr4  */
#line 79 "snobol4_grammar.y"
        {
            if ((yyvsp[-2].expr)->kind==E_ALT) { expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr)); (yyval.expr)=(yyvsp[-2].expr); }
            else { EXPR_t*a=expr_new(E_ALT); expr_add_child(a,(yyvsp[-2].expr)); expr_add_child(a,(yyvsp[0].expr)); (yyval.expr)=a; }
        }
#line 1268 "snobol4_grammar.tab.c"
    break;

  case 11: /* expr3: expr4  */
#line 83 "snobol4_grammar.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 1274 "snobol4_grammar.tab.c"
    break;

  case 12: /* expr4: expr4 expr5  */
#line 89 "snobol4_grammar.y"
        {
            if ((yyvsp[-1].expr)->kind==E_SEQ) { expr_add_child((yyvsp[-1].expr),(yyvsp[0].expr)); (yyval.expr)=(yyvsp[-1].expr); }
            else { EXPR_t*s=expr_new(E_SEQ); expr_add_child(s,(yyvsp[-1].expr)); expr_add_child(s,(yyvsp[0].expr)); (yyval.expr)=s; }
        }
#line 1283 "snobol4_grammar.tab.c"
    break;

  case 13: /* expr4: expr5  */
#line 93 "snobol4_grammar.y"
             { (yyval.expr) = (yyvsp[0].expr); }
#line 1289 "snobol4_grammar.tab.c"
    break;

  case 14: /* expr5: expr5 TK_AT expr6  */
#line 98 "snobol4_grammar.y"
                         { (yyval.expr) = expr_binary(E_CAPT_CURSOR, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1295 "snobol4_grammar.tab.c"
    break;

  case 15: /* expr5: expr6  */
#line 99 "snobol4_grammar.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 1301 "snobol4_grammar.tab.c"
    break;

  case 16: /* expr6: expr6 TK_PLUS expr7  */
#line 104 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_ADD, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1307 "snobol4_grammar.tab.c"
    break;

  case 17: /* expr6: expr6 TK_MINUS expr7  */
#line 105 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_SUB, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1313 "snobol4_grammar.tab.c"
    break;

  case 18: /* expr6: expr7  */
#line 106 "snobol4_grammar.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1319 "snobol4_grammar.tab.c"
    break;

  case 19: /* expr7: expr7 TK_HASH expr8  */
#line 111 "snobol4_grammar.y"
                           { (yyval.expr) = expr_binary(E_MUL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1325 "snobol4_grammar.tab.c"
    break;

  case 20: /* expr7: expr8  */
#line 112 "snobol4_grammar.y"
                           { (yyval.expr) = (yyvsp[0].expr); }
#line 1331 "snobol4_grammar.tab.c"
    break;

  case 21: /* expr8: expr8 TK_SLASH expr9  */
#line 117 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_DIV, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1337 "snobol4_grammar.tab.c"
    break;

  case 22: /* expr8: expr9  */
#line 118 "snobol4_grammar.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1343 "snobol4_grammar.tab.c"
    break;

  case 23: /* expr9: expr9 TK_STAR expr10  */
#line 123 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_MUL, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1349 "snobol4_grammar.tab.c"
    break;

  case 24: /* expr9: expr10  */
#line 124 "snobol4_grammar.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1355 "snobol4_grammar.tab.c"
    break;

  case 25: /* expr10: expr10 TK_PCT expr11  */
#line 129 "snobol4_grammar.y"
                            { (yyval.expr) = expr_binary(E_DIV, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1361 "snobol4_grammar.tab.c"
    break;

  case 26: /* expr10: expr11  */
#line 130 "snobol4_grammar.y"
                            { (yyval.expr) = (yyvsp[0].expr); }
#line 1367 "snobol4_grammar.tab.c"
    break;

  case 27: /* expr11: expr12 TK_CARET expr11  */
#line 135 "snobol4_grammar.y"
                                 { (yyval.expr) = expr_binary(E_POW, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1373 "snobol4_grammar.tab.c"
    break;

  case 28: /* expr11: expr12 TK_BANG expr11  */
#line 136 "snobol4_grammar.y"
                                 { (yyval.expr) = expr_binary(E_POW, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1379 "snobol4_grammar.tab.c"
    break;

  case 29: /* expr11: expr12 TK_STARSTAR expr11  */
#line 137 "snobol4_grammar.y"
                                 { (yyval.expr) = expr_binary(E_POW, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1385 "snobol4_grammar.tab.c"
    break;

  case 30: /* expr11: expr12  */
#line 138 "snobol4_grammar.y"
                                 { (yyval.expr) = (yyvsp[0].expr); }
#line 1391 "snobol4_grammar.tab.c"
    break;

  case 31: /* expr12: expr12 TK_DOLLAR expr13  */
#line 143 "snobol4_grammar.y"
                               { (yyval.expr) = expr_binary(E_CAPT_IMMED_ASGN, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1397 "snobol4_grammar.tab.c"
    break;

  case 32: /* expr12: expr12 TK_DOT expr13  */
#line 144 "snobol4_grammar.y"
                               { (yyval.expr) = expr_binary(E_CAPT_COND_ASGN,  (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1403 "snobol4_grammar.tab.c"
    break;

  case 33: /* expr12: expr13  */
#line 145 "snobol4_grammar.y"
                               { (yyval.expr) = (yyvsp[0].expr); }
#line 1409 "snobol4_grammar.tab.c"
    break;

  case 34: /* expr13: expr14 TK_TILDE expr13  */
#line 150 "snobol4_grammar.y"
                              { (yyval.expr) = expr_binary(E_CAPT_COND_ASGN, (yyvsp[-2].expr), (yyvsp[0].expr)); }
#line 1415 "snobol4_grammar.tab.c"
    break;

  case 35: /* expr13: expr14  */
#line 151 "snobol4_grammar.y"
                              { (yyval.expr) = (yyvsp[0].expr); }
#line 1421 "snobol4_grammar.tab.c"
    break;

  case 36: /* expr14: TK_AT expr14  */
#line 156 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_CAPT_CURSOR,  (yyvsp[0].expr)); }
#line 1427 "snobol4_grammar.tab.c"
    break;

  case 37: /* expr14: TK_TILDE expr14  */
#line 157 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_INDIRECT,     (yyvsp[0].expr)); }
#line 1433 "snobol4_grammar.tab.c"
    break;

  case 38: /* expr14: TK_QMARK expr14  */
#line 158 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_INTERROGATE,  (yyvsp[0].expr)); }
#line 1439 "snobol4_grammar.tab.c"
    break;

  case 39: /* expr14: TK_AMP expr14  */
#line 159 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_OPSYN,        (yyvsp[0].expr)); }
#line 1445 "snobol4_grammar.tab.c"
    break;

  case 40: /* expr14: TK_PLUS expr14  */
#line 160 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_PLS,          (yyvsp[0].expr)); }
#line 1451 "snobol4_grammar.tab.c"
    break;

  case 41: /* expr14: TK_MINUS expr14  */
#line 161 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_MNS,          (yyvsp[0].expr)); }
#line 1457 "snobol4_grammar.tab.c"
    break;

  case 42: /* expr14: TK_STAR expr14  */
#line 162 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_DEFER,        (yyvsp[0].expr)); }
#line 1463 "snobol4_grammar.tab.c"
    break;

  case 43: /* expr14: TK_DOLLAR expr14  */
#line 163 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_INDIRECT,     (yyvsp[0].expr)); }
#line 1469 "snobol4_grammar.tab.c"
    break;

  case 44: /* expr14: TK_DOT expr14  */
#line 164 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_NAME,         (yyvsp[0].expr)); }
#line 1475 "snobol4_grammar.tab.c"
    break;

  case 45: /* expr14: TK_BANG expr14  */
#line 165 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_POW,          (yyvsp[0].expr)); }
#line 1481 "snobol4_grammar.tab.c"
    break;

  case 46: /* expr14: TK_PCT expr14  */
#line 166 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_DIV,          (yyvsp[0].expr)); }
#line 1487 "snobol4_grammar.tab.c"
    break;

  case 47: /* expr14: TK_SLASH expr14  */
#line 167 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_DIV,          (yyvsp[0].expr)); }
#line 1493 "snobol4_grammar.tab.c"
    break;

  case 48: /* expr14: TK_HASH expr14  */
#line 168 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_MUL,          (yyvsp[0].expr)); }
#line 1499 "snobol4_grammar.tab.c"
    break;

  case 49: /* expr14: TK_EQ expr14  */
#line 169 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_ASSIGN,       (yyvsp[0].expr)); }
#line 1505 "snobol4_grammar.tab.c"
    break;

  case 50: /* expr14: TK_PIPE expr14  */
#line 170 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_ALT,          (yyvsp[0].expr)); }
#line 1511 "snobol4_grammar.tab.c"
    break;

  case 51: /* expr14: TK_CARET expr14  */
#line 171 "snobol4_grammar.y"
                        { (yyval.expr) = expr_unary(E_POW,          (yyvsp[0].expr)); }
#line 1517 "snobol4_grammar.tab.c"
    break;

  case 52: /* expr14: TK_STARSTAR expr14  */
#line 172 "snobol4_grammar.y"
                         { (yyval.expr) = expr_unary(E_DEFER,       (yyvsp[0].expr)); }
#line 1523 "snobol4_grammar.tab.c"
    break;

  case 53: /* expr14: expr15  */
#line 173 "snobol4_grammar.y"
                         { (yyval.expr) = (yyvsp[0].expr); }
#line 1529 "snobol4_grammar.tab.c"
    break;

  case 54: /* expr15: expr15 TK_LBRACKET exprlist TK_RBRACKET  */
#line 179 "snobol4_grammar.y"
        { EXPR_t*idx=expr_new(E_IDX); expr_add_child(idx,(yyvsp[-3].expr));
          for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++) expr_add_child(idx,(yyvsp[-1].expr)->children[i]);
          free((yyvsp[-1].expr)->children); free((yyvsp[-1].expr)); (yyval.expr)=idx; }
#line 1537 "snobol4_grammar.tab.c"
    break;

  case 55: /* expr15: expr15 TK_LANGLE exprlist TK_RANGLE  */
#line 183 "snobol4_grammar.y"
        { EXPR_t*idx=expr_new(E_IDX); expr_add_child(idx,(yyvsp[-3].expr));
          for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++) expr_add_child(idx,(yyvsp[-1].expr)->children[i]);
          free((yyvsp[-1].expr)->children); free((yyvsp[-1].expr)); (yyval.expr)=idx; }
#line 1545 "snobol4_grammar.tab.c"
    break;

  case 56: /* expr15: expr17  */
#line 186 "snobol4_grammar.y"
              { (yyval.expr) = (yyvsp[0].expr); }
#line 1551 "snobol4_grammar.tab.c"
    break;

  case 57: /* exprlist: exprlist_ne  */
#line 191 "snobol4_grammar.y"
                    { (yyval.expr) = (yyvsp[0].expr); }
#line 1557 "snobol4_grammar.tab.c"
    break;

  case 58: /* exprlist: %empty  */
#line 192 "snobol4_grammar.y"
                    { (yyval.expr) = expr_new(E_NUL); }
#line 1563 "snobol4_grammar.tab.c"
    break;

  case 59: /* exprlist_ne: exprlist_ne TK_COMMA expr0  */
#line 195 "snobol4_grammar.y"
                                  { expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr)); (yyval.expr)=(yyvsp[-2].expr); }
#line 1569 "snobol4_grammar.tab.c"
    break;

  case 60: /* exprlist_ne: exprlist_ne TK_COMMA  */
#line 196 "snobol4_grammar.y"
                                  { expr_add_child((yyvsp[-1].expr),expr_new(E_NUL)); (yyval.expr)=(yyvsp[-1].expr); }
#line 1575 "snobol4_grammar.tab.c"
    break;

  case 61: /* exprlist_ne: expr0  */
#line 197 "snobol4_grammar.y"
             { EXPR_t*l=expr_new(E_NUL); expr_add_child(l,(yyvsp[0].expr)); (yyval.expr)=l; }
#line 1581 "snobol4_grammar.tab.c"
    break;

  case 62: /* expr17: TK_LPAREN expr0 TK_RPAREN  */
#line 202 "snobol4_grammar.y"
                                                       { (yyval.expr) = (yyvsp[-1].expr); }
#line 1587 "snobol4_grammar.tab.c"
    break;

  case 63: /* expr17: TK_LPAREN expr0 TK_COMMA exprlist_ne TK_RPAREN  */
#line 204 "snobol4_grammar.y"
        { EXPR_t*a=expr_new(E_ALT); expr_add_child(a,(yyvsp[-3].expr));
          for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++) expr_add_child(a,(yyvsp[-1].expr)->children[i]);
          free((yyvsp[-1].expr)->children); free((yyvsp[-1].expr)); (yyval.expr)=a; }
#line 1595 "snobol4_grammar.tab.c"
    break;

  case 64: /* expr17: TK_LPAREN TK_RPAREN  */
#line 207 "snobol4_grammar.y"
                                                       { (yyval.expr) = expr_new(E_NUL); }
#line 1601 "snobol4_grammar.tab.c"
    break;

  case 65: /* expr17: TK_IDENT TK_LPAREN exprlist TK_RPAREN  */
#line 209 "snobol4_grammar.y"
        { EXPR_t*e=expr_new(E_FNC); e->sval=(char*)(yyvsp[-3].tok).sval;
          for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++) expr_add_child(e,(yyvsp[-1].expr)->children[i]);
          free((yyvsp[-1].expr)->children); free((yyvsp[-1].expr)); (yyval.expr)=e; }
#line 1609 "snobol4_grammar.tab.c"
    break;

  case 66: /* expr17: TK_END TK_LPAREN exprlist TK_RPAREN  */
#line 213 "snobol4_grammar.y"
        { EXPR_t*e=expr_new(E_FNC); e->sval=(char*)(yyvsp[-3].tok).sval;
          for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++) expr_add_child(e,(yyvsp[-1].expr)->children[i]);
          free((yyvsp[-1].expr)->children); free((yyvsp[-1].expr)); (yyval.expr)=e; }
#line 1617 "snobol4_grammar.tab.c"
    break;

  case 67: /* expr17: TK_IDENT  */
#line 216 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_VAR);     e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1623 "snobol4_grammar.tab.c"
    break;

  case 68: /* expr17: TK_END  */
#line 217 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_VAR);     e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1629 "snobol4_grammar.tab.c"
    break;

  case 69: /* expr17: TK_KEYWORD  */
#line 218 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_KEYWORD); e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1635 "snobol4_grammar.tab.c"
    break;

  case 70: /* expr17: TK_STR  */
#line 219 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_QLIT);    e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1641 "snobol4_grammar.tab.c"
    break;

  case 71: /* expr17: TK_INT  */
#line 220 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_ILIT);    e->ival=(yyvsp[0].tok).ival;         (yyval.expr)=e; }
#line 1647 "snobol4_grammar.tab.c"
    break;

  case 72: /* expr17: TK_REAL  */
#line 221 "snobol4_grammar.y"
                  { EXPR_t*e=expr_new(E_FLIT);    e->dval=(yyvsp[0].tok).dval;         (yyval.expr)=e; }
#line 1653 "snobol4_grammar.tab.c"
    break;


#line 1657 "snobol4_grammar.tab.c"

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
  yytoken = yychar == SNOBOL4_EMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (yyparse_param, YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= SNOBOL4_EOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == SNOBOL4_EOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, yyparse_param);
          yychar = SNOBOL4_EMPTY;
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
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yyparse_param);
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
  yyerror (yyparse_param, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != SNOBOL4_EMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, yyparse_param);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yyparse_param);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 224 "snobol4_grammar.y"


int snobol4_lex(YYSTYPE *yylval_param, void *yyparse_param) {
    (void)yyparse_param;
    Token t = lex_next(g_lx);
    yylval_param->tok = t;
    switch (t.kind) {
        case T_IDENT:     return TK_IDENT;
        case T_END:       return TK_END;
        case T_INT:       return TK_INT;
        case T_REAL:      return TK_REAL;
        case T_STR:       return TK_STR;
        case T_KEYWORD:   return TK_KEYWORD;
        /* binary */
        case T_PLUS:      return TK_PLUS;
        case T_MINUS:     return TK_MINUS;
        case T_STAR:      return TK_STAR;
        case T_SLASH:     return TK_SLASH;
        case T_PCT:       return TK_PCT;
        case T_CARET:     return TK_CARET;
        case T_BANG:      return TK_BANG;
        case T_STARSTAR:  return TK_STARSTAR;
        case T_AMP:       return TK_AMP;
        case T_AT:        return TK_AT;
        case T_TILDE:     return TK_TILDE;
        case T_DOLLAR:    return TK_DOLLAR;
        case T_DOT:       return TK_DOT;
        case T_HASH:      return TK_HASH;
        case T_PIPE:      return TK_PIPE;
        case T_EQ:        return TK_EQ;
        case T_QMARK:     return TK_QMARK;
        /* unary */
        /* structural */
        case T_COMMA:     return TK_COMMA;
        case T_LPAREN:    return TK_LPAREN;
        case T_RPAREN:    return TK_RPAREN;
        case T_LBRACKET:  return TK_LBRACKET;
        case T_RBRACKET:  return TK_RBRACKET;
        case T_LANGLE:    return TK_LANGLE;
        case T_RANGLE:    return TK_RANGLE;
        default:          return 0;
    }
}

void snobol4_error(void *param, const char *msg) {
    (void)param;
    sno_error(g_lx ? g_lx->lineno : 0, "parse error: %s", msg);
}

static EXPR_t *parse_expr_lx(Lex *lx) {
    EXPR_t *result = NULL;
    ParseParm p = { lx, &result };
    g_lx = lx;
    snobol4_parse(&p);
    return result;
}

static void fixup_val_tree(EXPR_t *e) {
    if (!e) return;
    if (e->kind == E_SEQ) e->kind = E_CAT;
    for (int i=0; i<e->nchildren; i++) fixup_val_tree(e->children[i]);
}

static int repl_is_pat_tree(EXPR_t *e) {
    if (!e) return 0;
    switch (e->kind) {
        case E_ARB: case E_ARBNO:
        case E_CAPT_COND_ASGN: case E_CAPT_IMMED_ASGN:
        case E_CAPT_CURSOR: case E_DEFER: return 1;
        default: break;
    }
    for (int i=0; i<e->nchildren; i++)
        if (repl_is_pat_tree(e->children[i])) return 1;
    return 0;
}

static char *parse_goto_label(Lex *lx) {
    Token t = lex_peek(lx);
    TokKind open=t.kind, close;
    if      (open==T_LPAREN) close=T_RPAREN;
    else if (open==T_LANGLE) close=T_RANGLE;
    else return NULL;
    lex_next(lx); t=lex_peek(lx);
    char *label=NULL;
    if (t.kind==T_IDENT||t.kind==T_KEYWORD||t.kind==T_END) {
        lex_next(lx); label=(char*)t.sval;
    } else if (t.kind==T_DOLLAR) {
        lex_next(lx);
        if (lex_peek(lx).kind==T_LPAREN) {
            /* Reconstruct $(expr) text from token svals */
            int depth=1; lex_next(lx);
            char ebuf[512]; int epos=0;
            while (!lex_at_end(lx)&&depth>0) {
                Token tok=lex_next(lx);
                if (tok.kind==T_LPAREN) depth++;
                else if (tok.kind==T_RPAREN){depth--;if(!depth) break;}
                char tmp[64]; int tlen=tok_to_chars(&tok,tmp,sizeof tmp);
                if(epos+tlen<(int)sizeof(ebuf)-1){memcpy(ebuf+epos,tmp,tlen);epos+=tlen;}
            }
            ebuf[epos]='\0';
            char *buf=malloc(12+epos+1);
            memcpy(buf,"$COMPUTED:",10); memcpy(buf+10,ebuf,epos); buf[10+epos]='\0';
            label=buf;
        } else if (lex_peek(lx).kind==T_STR) {
            Token n2=lex_next(lx); const char *lit=n2.sval?n2.sval:"";
            char *buf=malloc(12+strlen(lit)+4); sprintf(buf,"$COMPUTED:'%s'",lit); label=buf;
        } else {
            Token n2=lex_next(lx); char buf[512];
            snprintf(buf,sizeof buf,"$%s",n2.sval?n2.sval:"?"); label=strdup(buf);
        }
    } else if (t.kind==T_LPAREN) {
        int depth=1; lex_next(lx);
        while (!lex_at_end(lx)&&depth>0){Token tok=lex_next(lx);if(tok.kind==T_LPAREN)depth++;else if(tok.kind==T_RPAREN)depth--;}
        label=strdup("$COMPUTED");
    }
    if (lex_peek(lx).kind==close) lex_next(lx);
    return label;
}

static SnoGoto *parse_goto_field(const char *goto_str, int lineno) {
    if (!goto_str||!*goto_str) return NULL;
    Lex lx={0}; lex_open_str(&lx,goto_str,(int)strlen(goto_str),lineno);
    SnoGoto *g=sgoto_new();
    while (!lex_at_end(&lx)) {
        Token t=lex_peek(&lx);
        if (t.kind==T_IDENT&&t.sval) {
            if (strcasecmp(t.sval,"S")==0){lex_next(&lx);g->onsuccess=parse_goto_label(&lx);continue;}
            if (strcasecmp(t.sval,"F")==0){lex_next(&lx);g->onfailure=parse_goto_label(&lx);continue;}
        }
        if (t.kind==T_LPAREN||t.kind==T_LANGLE){g->uncond=parse_goto_label(&lx);continue;}
        sno_error(lineno,"unexpected token in goto field"); lex_next(&lx);
    }
    if (!g->onsuccess&&!g->onfailure&&!g->uncond){free(g);return NULL;}
    return g;
}

int tok_to_chars(Token *tk, char *buf, int bufsz) {
    int n=0;
#define OUT(c) do{if(n<bufsz-1)buf[n++]=(c);}while(0)
    switch(tk->kind){
        case T_IDENT: case T_END:
            if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-1){memcpy(buf+n,tk->sval,l);n+=l;}}break;
        case T_KEYWORD:
            OUT('&');if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-2){memcpy(buf+n,tk->sval,l);n+=l;}}break;
        case T_STR:{const char*sv=tk->sval?tk->sval:"";char q=strchr(sv,'\'')?'"':'\'';
            OUT(q);int l=(int)strlen(sv);if(l<bufsz-n-2){memcpy(buf+n,sv,l);n+=l;}OUT(q);break;}
        case T_INT:  n+=snprintf(buf+n,bufsz-n,"%ld",tk->ival);break;
        case T_REAL:
            if(tk->sval){int l=(int)strlen(tk->sval);if(l<bufsz-n-1){memcpy(buf+n,tk->sval,l);n+=l;}}
            else n+=snprintf(buf+n,bufsz-n,"%g",tk->dval);break;
        /* binary ops get surrounding spaces to preserve spacing for re-lex */
        case T_PLUS:     OUT(' ');OUT('+');OUT(' ');break;
        case T_MINUS:    OUT(' ');OUT('-');OUT(' ');break;
        case T_STAR:     OUT(' ');OUT('*');OUT(' ');break;
        case T_SLASH:    OUT(' ');OUT('/');OUT(' ');break;
        case T_PCT:      OUT(' ');OUT('%');OUT(' ');break;
        case T_CARET:    OUT(' ');OUT('^');OUT(' ');break;
        case T_BANG:     OUT(' ');OUT('!');OUT(' ');break;
        case T_STARSTAR: OUT(' ');OUT('*');OUT('*');OUT(' ');break;
        case T_AMP:      OUT(' ');OUT('&');OUT(' ');break;
        case T_AT:       OUT(' ');OUT('@');OUT(' ');break;
        case T_TILDE:    OUT(' ');OUT('~');OUT(' ');break;
        case T_DOLLAR:   OUT(' ');OUT('$');OUT(' ');break;
        case T_DOT:      OUT(' ');OUT('.');OUT(' ');break;
        case T_HASH:     OUT(' ');OUT('#');OUT(' ');break;
        case T_PIPE:     OUT(' ');OUT('|');OUT(' ');break;
        case T_EQ:       OUT(' ');OUT('=');OUT(' ');break;
        case T_QMARK:    OUT(' ');OUT('?');OUT(' ');break;
        /* unary ops — no surrounding spaces */
        case T_COMMA:    OUT(',');break;
        case T_LPAREN:   OUT('(');break; case T_RPAREN:   OUT(')');break;
        case T_LBRACKET: OUT('[');break; case T_RBRACKET: OUT(']');break;
        case T_LANGLE:   OUT('<');break; case T_RANGLE:   OUT('>');break;
        default:break;
    }
#undef OUT
    buf[n]='\0'; return n;
}

Program *parse_program_tokens(Lex *stream) {
    Program *prog=calloc(1,sizeof*prog);
    while (1) {
        Token t=lex_peek(stream);
        if (t.kind==T_EOF) break;
        char *label=NULL,*goto_str=NULL;
        int is_end=0,lineno=t.lineno;
        if (t.kind==T_LABEL){
            lex_next(stream); label=(char*)t.sval; is_end=(int)t.ival; lineno=t.lineno;
            t=lex_peek(stream);
        }
        if (t.kind==T_STMT_END){
            lex_next(stream);
            if(label||is_end){
                STMT_t*s2=stmt_new(); s2->lineno=lineno;
                if(label) s2->label=strdup(label); s2->is_end=is_end;
                if(!prog->head) prog->head=prog->tail=s2; else{prog->tail->next=s2;prog->tail=s2;}
                prog->nstmts++;
            }
            continue;
        }
        if (t.kind==T_EOF){
            if(label||is_end){
                STMT_t*s2=stmt_new(); s2->lineno=lineno;
                if(label) s2->label=strdup(label); s2->is_end=is_end;
                if(!prog->head) prog->head=prog->tail=s2; else{prog->tail->next=s2;prog->tail=s2;}
                prog->nstmts++;
            }
            break;
        }
        char bbuf[8192]; int bpos=0;
        while(1){
            t=lex_peek(stream);
            if(t.kind==T_GOTO||t.kind==T_STMT_END||t.kind==T_EOF) break;
            char tmp[256]; int tlen=tok_to_chars(&t,tmp,sizeof tmp);
            if(bpos+tlen<8190){memcpy(bbuf+bpos,tmp,tlen);bpos+=tlen;}
            lex_next(stream);
        }
        bbuf[bpos]='\0';
        if(lex_peek(stream).kind==T_GOTO){goto_str=(char*)lex_peek(stream).sval;lex_next(stream);}
        if(lex_peek(stream).kind==T_STMT_END) lex_next(stream);

        if(label&&strcasecmp(label,"EXPORT")==0){
            Lex blx={0}; lex_open_str(&blx,bbuf,bpos,lineno);
            Token nt=lex_peek(&blx);
            if((nt.kind==T_IDENT||nt.kind==T_END)&&nt.sval){
                ExportEntry*e=calloc(1,sizeof*e); e->name=strdup(nt.sval);
                for(char*p=e->name;*p;p++) *p=(char)toupper((unsigned char)*p);
                e->next=prog->exports; prog->exports=e;
            }
            continue;
        }
        if(label&&strcasecmp(label,"IMPORT")==0){
            ImportEntry*e=calloc(1,sizeof*e);
            char*dot1=strchr(bbuf,'.');
            if(dot1){char*dot2=strchr(dot1+1,'.');
                if(dot2){e->lang=strndup(bbuf,(size_t)(dot1-bbuf));e->name=strndup(dot1+1,(size_t)(dot2-dot1-1));e->method=strdup(dot2+1);}
                else{e->lang=strdup("");e->name=strndup(bbuf,(size_t)(dot1-bbuf));e->method=strdup(dot1+1);}}
            else{e->lang=strdup("");e->name=strdup(bbuf);e->method=strdup(bbuf);}
            e->next=prog->imports; prog->imports=e; continue;
        }

        STMT_t*s=stmt_new(); s->lineno=lineno;
        if(label) s->label=strdup(label);
        s->go=parse_goto_field(goto_str,lineno);
        if(is_end){if(!s->label) s->label=strdup("END"); s->is_end=1;}
        else if(bpos>0){
            /* Split body at top-level '=' (depth 0) to find:
             *   subject [pattern] = replacement   or   subject = replacement
             * Find the last top-level '=' that is not inside parens/brackets.
             * Everything before is subject [pattern]; everything after is replacement.
             * Then split subject/pattern: subject ends at first top-level non-unary
             * token after an atom — handled by parse_expr_lx consuming expr14 only.
             * Simpler: run parse_expr_lx on full body up to '='; the yacc grammar
             * will consume subject+pattern as an expr naturally since '=' at top
             * level is the assign operator — but we don't want that.
             *
             * Correct split: scan bbuf for top-level '=' character.
             */
            int eq_pos = -1, depth = 0;
            for(int i=0;i<bpos;i++){
                char c=bbuf[i];
                if(c=='('||c=='['||c=='<') depth++;
                else if(c==')'||c==']'||c=='>') depth--;
                else if(c=='='&&depth==0){ eq_pos=i; break; }
            }
            if(eq_pos>=0){
                /* subject+pattern before '=', replacement after */
                char subpat[8192]; int splen=eq_pos;
                while(splen>0&&(bbuf[splen-1]==' '||bbuf[splen-1]=='\t')) splen--;
                memcpy(subpat,bbuf,splen); subpat[splen]='\0';
                char repl[8192]; int rlen=bpos-eq_pos-1;
                while(rlen>0&&(bbuf[eq_pos+1]==' '||bbuf[eq_pos+1]=='\t')){eq_pos++;rlen--;}
                if(rlen>0) memcpy(repl,bbuf+eq_pos+1,rlen); repl[rlen>0?rlen:0]='\0';
                s->has_eq=1;
                /* parse subject+pattern: subject is expr14-level atom,
                 * remainder is pattern (expr3-level).  Run full parser —
                 * the grammar will parse it as a concat (E_SEQ) of subject and pattern. */
                Lex blx={0}; lex_open_str(&blx,subpat,splen,lineno);
                EXPR_t *sp=parse_expr_lx(&blx);
                lex_destroy(&blx);
                /* If sp is E_SEQ, first child is subject, rest form pattern */
                if(sp && sp->kind==E_SEQ && sp->nchildren>=2){
                    s->subject=sp->children[0];
                    if(sp->nchildren==2){ s->pattern=sp->children[1]; }
                    else {
                        EXPR_t *seq=expr_new(E_SEQ);
                        for(int i=1;i<sp->nchildren;i++) expr_add_child(seq,sp->children[i]);
                        s->pattern=seq;
                    }
                    free(sp->children); free(sp);
                } else {
                    s->subject=sp;
                }
                fixup_val_tree(s->subject);
                if(rlen>0){
                    Lex rlx={0}; lex_open_str(&rlx,repl,rlen,lineno);
                    s->replacement=parse_expr_lx(&rlx);
                    lex_destroy(&rlx);
                    if(s->replacement&&!repl_is_pat_tree(s->replacement))
                        fixup_val_tree(s->replacement);
                }
            } else {
                /* No '=': subject [pattern] only */
                Lex blx={0}; lex_open_str(&blx,bbuf,bpos,lineno);
                EXPR_t *sp=parse_expr_lx(&blx);
                lex_destroy(&blx);
                if(sp && sp->kind==E_SEQ && sp->nchildren>=2){
                    s->subject=sp->children[0];
                    if(sp->nchildren==2){ s->pattern=sp->children[1]; }
                    else {
                        EXPR_t *seq=expr_new(E_SEQ);
                        for(int i=1;i<sp->nchildren;i++) expr_add_child(seq,sp->children[i]);
                        s->pattern=seq;
                    }
                    free(sp->children); free(sp);
                } else {
                    s->subject=sp;
                }
                fixup_val_tree(s->subject);
            }
        }
        s->next=NULL;
        if(!prog->head) prog->head=prog->tail=s; else{prog->tail->next=s;prog->tail=s;}
        prog->nstmts++;
    }
    return prog;
}

Program *parse_program(LineArray *lines) {
    (void)lines;
    fprintf(stderr,"parse_program(LineArray*): not used with one-pass lexer\n");
    return calloc(1,sizeof(Program));
}

EXPR_t *parse_expr_from_str(const char *src) {
    if(!src||!*src) return NULL;
    Lex lx={0}; lex_open_str(&lx,src,(int)strlen(src),0);
    return parse_expr_lx(&lx);
}
