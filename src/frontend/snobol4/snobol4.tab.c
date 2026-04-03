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
#define YYPURE 2

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

#include "snobol4.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_IDENT = 3,                    /* T_IDENT  */
  YYSYMBOL_T_FUNCTION = 4,                 /* T_FUNCTION  */
  YYSYMBOL_T_KEYWORD = 5,                  /* T_KEYWORD  */
  YYSYMBOL_T_END = 6,                      /* T_END  */
  YYSYMBOL_T_INT = 7,                      /* T_INT  */
  YYSYMBOL_T_REAL = 8,                     /* T_REAL  */
  YYSYMBOL_T_STR = 9,                      /* T_STR  */
  YYSYMBOL_T_LABEL = 10,                   /* T_LABEL  */
  YYSYMBOL_T_GOTO = 11,                    /* T_GOTO  */
  YYSYMBOL_T_STMT_END = 12,                /* T_STMT_END  */
  YYSYMBOL_T_ASSIGNMENT = 13,              /* T_ASSIGNMENT  */
  YYSYMBOL_T_MATCH = 14,                   /* T_MATCH  */
  YYSYMBOL_T_ALTERNATION = 15,             /* T_ALTERNATION  */
  YYSYMBOL_T_ADDITION = 16,                /* T_ADDITION  */
  YYSYMBOL_T_SUBTRACTION = 17,             /* T_SUBTRACTION  */
  YYSYMBOL_T_MULTIPLICATION = 18,          /* T_MULTIPLICATION  */
  YYSYMBOL_T_DIVISION = 19,                /* T_DIVISION  */
  YYSYMBOL_T_EXPONENTIATION = 20,          /* T_EXPONENTIATION  */
  YYSYMBOL_T_IMMEDIATE_ASSIGN = 21,        /* T_IMMEDIATE_ASSIGN  */
  YYSYMBOL_T_COND_ASSIGN = 22,             /* T_COND_ASSIGN  */
  YYSYMBOL_T_AMPERSAND = 23,               /* T_AMPERSAND  */
  YYSYMBOL_T_AT_SIGN = 24,                 /* T_AT_SIGN  */
  YYSYMBOL_T_POUND = 25,                   /* T_POUND  */
  YYSYMBOL_T_PERCENT = 26,                 /* T_PERCENT  */
  YYSYMBOL_T_TILDE = 27,                   /* T_TILDE  */
  YYSYMBOL_T_UN_AT_SIGN = 28,              /* T_UN_AT_SIGN  */
  YYSYMBOL_T_UN_TILDE = 29,                /* T_UN_TILDE  */
  YYSYMBOL_T_UN_QUESTION_MARK = 30,        /* T_UN_QUESTION_MARK  */
  YYSYMBOL_T_UN_AMPERSAND = 31,            /* T_UN_AMPERSAND  */
  YYSYMBOL_T_UN_PLUS = 32,                 /* T_UN_PLUS  */
  YYSYMBOL_T_UN_MINUS = 33,                /* T_UN_MINUS  */
  YYSYMBOL_T_UN_ASTERISK = 34,             /* T_UN_ASTERISK  */
  YYSYMBOL_T_UN_DOLLAR_SIGN = 35,          /* T_UN_DOLLAR_SIGN  */
  YYSYMBOL_T_UN_PERIOD = 36,               /* T_UN_PERIOD  */
  YYSYMBOL_T_UN_EXCLAMATION = 37,          /* T_UN_EXCLAMATION  */
  YYSYMBOL_T_UN_PERCENT = 38,              /* T_UN_PERCENT  */
  YYSYMBOL_T_UN_SLASH = 39,                /* T_UN_SLASH  */
  YYSYMBOL_T_UN_POUND = 40,                /* T_UN_POUND  */
  YYSYMBOL_T_UN_EQUAL = 41,                /* T_UN_EQUAL  */
  YYSYMBOL_T_UN_VERTICAL_BAR = 42,         /* T_UN_VERTICAL_BAR  */
  YYSYMBOL_T_CONCAT = 43,                  /* T_CONCAT  */
  YYSYMBOL_T_COMMA = 44,                   /* T_COMMA  */
  YYSYMBOL_T_LPAREN = 45,                  /* T_LPAREN  */
  YYSYMBOL_T_RPAREN = 46,                  /* T_RPAREN  */
  YYSYMBOL_T_LBRACK = 47,                  /* T_LBRACK  */
  YYSYMBOL_T_RBRACK = 48,                  /* T_RBRACK  */
  YYSYMBOL_T_LANGLE = 49,                  /* T_LANGLE  */
  YYSYMBOL_T_RANGLE = 50,                  /* T_RANGLE  */
  YYSYMBOL_YYACCEPT = 51,                  /* $accept  */
  YYSYMBOL_top = 52,                       /* top  */
  YYSYMBOL_program = 53,                   /* program  */
  YYSYMBOL_stmt = 54,                      /* stmt  */
  YYSYMBOL_opt_label = 55,                 /* opt_label  */
  YYSYMBOL_opt_subject = 56,               /* opt_subject  */
  YYSYMBOL_opt_pattern = 57,               /* opt_pattern  */
  YYSYMBOL_opt_repl = 58,                  /* opt_repl  */
  YYSYMBOL_opt_goto = 59,                  /* opt_goto  */
  YYSYMBOL_expr0 = 60,                     /* expr0  */
  YYSYMBOL_expr2 = 61,                     /* expr2  */
  YYSYMBOL_expr3 = 62,                     /* expr3  */
  YYSYMBOL_expr4 = 63,                     /* expr4  */
  YYSYMBOL_expr5 = 64,                     /* expr5  */
  YYSYMBOL_expr6 = 65,                     /* expr6  */
  YYSYMBOL_expr7 = 66,                     /* expr7  */
  YYSYMBOL_expr8 = 67,                     /* expr8  */
  YYSYMBOL_expr9 = 68,                     /* expr9  */
  YYSYMBOL_expr10 = 69,                    /* expr10  */
  YYSYMBOL_expr11 = 70,                    /* expr11  */
  YYSYMBOL_expr12 = 71,                    /* expr12  */
  YYSYMBOL_expr13 = 72,                    /* expr13  */
  YYSYMBOL_expr14 = 73,                    /* expr14  */
  YYSYMBOL_expr15 = 74,                    /* expr15  */
  YYSYMBOL_exprlist = 75,                  /* exprlist  */
  YYSYMBOL_exprlist_ne = 76,               /* exprlist_ne  */
  YYSYMBOL_expr17 = 77                     /* expr17  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;



/* Unqualified %code blocks.  */
#line 5 "snobol4.y"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct { Program *prog; EXPR_t **result; } PP;
static Lex     *g_lx;
static void     sno4_stmt_commit(void*,Token,EXPR_t*,EXPR_t*,int,EXPR_t*,Token);
static void     fixup_val(EXPR_t*);
static int      is_pat(EXPR_t*);
static char    *goto_label(Lex*);
static SnoGoto *goto_field(const char*,int);
static EXPR_t  *parse_expr(Lex*);

#line 202 "snobol4.tab.c"

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
#define YYFINAL  6
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   133

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  51
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  27
/* YYNRULES -- Number of rules.  */
#define YYNRULES  79
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  127

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   305


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
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50
};

#if SNOBOL4_DEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    47,    47,    49,    49,    50,    51,    53,    54,    56,
      57,    59,    60,    62,    63,    65,    66,    70,    71,    72,
      74,    75,    77,    78,    80,    81,    83,    84,    86,    87,
      88,    90,    91,    93,    94,    96,    97,    99,   100,   102,
     103,   105,   106,   107,   109,   110,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   129,   130,   131,   133,   134,   136,   137,   138,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149
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
  "\"end of file\"", "error", "\"invalid token\"", "T_IDENT",
  "T_FUNCTION", "T_KEYWORD", "T_END", "T_INT", "T_REAL", "T_STR",
  "T_LABEL", "T_GOTO", "T_STMT_END", "T_ASSIGNMENT", "T_MATCH",
  "T_ALTERNATION", "T_ADDITION", "T_SUBTRACTION", "T_MULTIPLICATION",
  "T_DIVISION", "T_EXPONENTIATION", "T_IMMEDIATE_ASSIGN", "T_COND_ASSIGN",
  "T_AMPERSAND", "T_AT_SIGN", "T_POUND", "T_PERCENT", "T_TILDE",
  "T_UN_AT_SIGN", "T_UN_TILDE", "T_UN_QUESTION_MARK", "T_UN_AMPERSAND",
  "T_UN_PLUS", "T_UN_MINUS", "T_UN_ASTERISK", "T_UN_DOLLAR_SIGN",
  "T_UN_PERIOD", "T_UN_EXCLAMATION", "T_UN_PERCENT", "T_UN_SLASH",
  "T_UN_POUND", "T_UN_EQUAL", "T_UN_VERTICAL_BAR", "T_CONCAT", "T_COMMA",
  "T_LPAREN", "T_RPAREN", "T_LBRACK", "T_RBRACK", "T_LANGLE", "T_RANGLE",
  "$accept", "top", "program", "stmt", "opt_label", "opt_subject",
  "opt_pattern", "opt_repl", "opt_goto", "expr0", "expr2", "expr3",
  "expr4", "expr5", "expr6", "expr7", "expr8", "expr9", "expr10", "expr11",
  "expr12", "expr13", "expr14", "expr15", "exprlist", "exprlist_ne",
  "expr17", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-75)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-22)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      44,   -75,    84,    53,   -75,    64,   -75,   -75,   -75,    19,
     -75,   -75,   -75,   -75,   -75,    64,    64,    64,    64,    64,
      64,    64,    64,    64,    64,    64,    64,    64,    64,    64,
      10,    74,    63,    60,    45,    65,    -6,    66,    71,    89,
      82,   -75,   -16,   -75,    83,    11,   -75,    64,   -75,   -75,
     -75,   -75,   -75,   -75,   -75,   -75,   -75,   -75,   -75,   -75,
     -75,   -75,   -75,   -75,    13,   -11,    96,    64,   101,    64,
      64,    64,    64,    64,    64,    64,    64,    64,    64,    64,
      64,    64,    64,    64,    64,    64,   -75,    67,    70,    64,
     -75,    64,    64,   -75,   -75,   103,    74,    96,    96,    45,
      65,    -6,    66,    66,    71,    89,    82,   -75,   -75,   -75,
     -75,   -75,    68,    69,   -75,    64,    32,   -75,   -75,   -75,
     101,   -75,   -75,   -75,   -75,   105,   -75
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       8,     7,     0,     8,     4,    10,     1,     3,    74,     0,
      76,    75,    78,    79,    77,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    14,     0,     9,    23,    25,    27,    30,    32,    34,
      36,    38,    40,    43,    45,    61,    64,    66,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    72,     0,    19,    21,     0,    16,    12,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    66,    66,    69,     0,    65,     0,
      70,     0,     0,    13,    15,     0,    14,    11,    20,    22,
      24,    26,    28,    29,    31,    33,    35,    37,    39,    41,
      42,    44,     0,     0,    73,    68,     0,    17,    18,     5,
      16,    62,    63,    67,    71,     0,     6
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -75,   -75,   -75,   115,   -75,   -75,   -75,    24,     1,   -30,
     117,    -4,    52,    54,    51,   -39,    49,    50,    55,     0,
     -75,   -74,     5,   -75,    -3,    39,   -75
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     2,     3,     4,     5,    31,    96,    68,    95,    86,
      65,    66,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    87,    88,    46
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      64,    33,    91,    92,    80,    81,    82,   109,   110,   111,
      74,    75,    70,     8,     9,    10,    11,    12,    13,    14,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,   102,   103,    93,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    -2,     1,    30,    63,    89,    84,    90,
      85,   117,   118,     1,    47,    97,    98,     8,     9,    10,
      11,    12,    13,    14,   -21,    71,   115,    69,   124,   107,
     108,   112,   113,   -21,     6,   123,    70,    67,    72,    73,
      77,    76,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    78,    79,    30,
      83,    71,    94,   114,   115,   119,   121,   126,     7,   122,
     120,   125,    32,    99,   101,   104,   100,   105,   116,     0,
       0,     0,     0,   106
};

static const yytype_int8 yycheck[] =
{
      30,     5,    13,    14,    20,    21,    22,    81,    82,    83,
      16,    17,    23,     3,     4,     5,     6,     7,     8,     9,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    74,    75,    67,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,     0,    10,    45,    46,    44,    47,    46,
      49,    91,    92,    10,    45,    69,    70,     3,     4,     5,
       6,     7,     8,     9,    14,    15,    44,    14,    46,    79,
      80,    84,    85,    23,     0,   115,    23,    13,    43,    24,
      19,    25,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    18,    26,    45,
      27,    15,    11,    46,    44,    12,    48,    12,     3,    50,
      96,   120,     5,    71,    73,    76,    72,    77,    89,    -1,
      -1,    -1,    -1,    78
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    10,    52,    53,    54,    55,     0,    54,     3,     4,
       5,     6,     7,     8,     9,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      45,    56,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    77,    45,    73,    73,
      73,    73,    73,    73,    73,    73,    73,    73,    73,    73,
      73,    73,    73,    46,    60,    61,    62,    13,    58,    14,
      23,    15,    43,    24,    16,    17,    25,    19,    18,    26,
      20,    21,    22,    27,    47,    49,    60,    75,    76,    44,
      46,    13,    14,    60,    11,    59,    57,    62,    62,    63,
      64,    65,    66,    66,    67,    68,    69,    70,    70,    72,
      72,    72,    75,    75,    46,    44,    76,    60,    60,    12,
      58,    48,    50,    60,    46,    59,    12
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    51,    52,    53,    53,    54,    54,    55,    55,    56,
      56,    57,    57,    58,    58,    59,    59,    60,    60,    60,
      61,    61,    62,    62,    63,    63,    64,    64,    65,    65,
      65,    66,    66,    67,    67,    68,    68,    69,    69,    70,
      70,    71,    71,    71,    72,    72,    73,    73,    73,    73,
      73,    73,    73,    73,    73,    73,    73,    73,    73,    73,
      73,    73,    74,    74,    74,    75,    75,    76,    76,    76,
      77,    77,    77,    77,    77,    77,    77,    77,    77,    77
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     1,     5,     7,     1,     0,     1,
       0,     1,     0,     2,     0,     1,     0,     3,     3,     1,
       3,     1,     3,     1,     3,     1,     3,     1,     3,     3,
       1,     3,     1,     3,     1,     3,     1,     3,     1,     3,
       1,     3,     3,     1,     3,     1,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     1,     4,     4,     1,     1,     0,     3,     2,     1,
       3,     5,     2,     4,     1,     1,     1,     1,     1,     1
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
  case 2: /* top: program  */
#line 47 "snobol4.y"
                                                                                                    { }
#line 1256 "snobol4.tab.c"
    break;

  case 5: /* stmt: opt_label opt_subject opt_repl opt_goto T_STMT_END  */
#line 50 "snobol4.y"
                                                                                     { sno4_stmt_commit(yyparse_param,(yyvsp[-4].tok),(yyvsp[-3].expr),NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].tok)); }
#line 1262 "snobol4.tab.c"
    break;

  case 6: /* stmt: opt_label expr2 T_MATCH opt_pattern opt_repl opt_goto T_STMT_END  */
#line 51 "snobol4.y"
                                                                                     { EXPR_t*sc=expr_binary(E_SCAN,(yyvsp[-5].expr),(yyvsp[-3].expr)); sno4_stmt_commit(yyparse_param,(yyvsp[-6].tok),sc,NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].tok)); }
#line 1268 "snobol4.tab.c"
    break;

  case 7: /* opt_label: T_LABEL  */
#line 53 "snobol4.y"
                                                                                                  { (yyval.tok)=(yyvsp[0].tok); }
#line 1274 "snobol4.tab.c"
    break;

  case 8: /* opt_label: %empty  */
#line 54 "snobol4.y"
                                                                                                   { (yyval.tok).sval=NULL;(yyval.tok).ival=0;(yyval.tok).lineno=0;(yyval.tok).kind=0; }
#line 1280 "snobol4.tab.c"
    break;

  case 9: /* opt_subject: expr3  */
#line 56 "snobol4.y"
                                                                                                  { (yyval.expr)=(yyvsp[0].expr); }
#line 1286 "snobol4.tab.c"
    break;

  case 10: /* opt_subject: %empty  */
#line 57 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1292 "snobol4.tab.c"
    break;

  case 11: /* opt_pattern: expr3  */
#line 59 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1298 "snobol4.tab.c"
    break;

  case 12: /* opt_pattern: %empty  */
#line 60 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1304 "snobol4.tab.c"
    break;

  case 13: /* opt_repl: T_ASSIGNMENT expr0  */
#line 62 "snobol4.y"
                                                                                                  { (yyval.expr)=(yyvsp[0].expr); }
#line 1310 "snobol4.tab.c"
    break;

  case 14: /* opt_repl: %empty  */
#line 63 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1316 "snobol4.tab.c"
    break;

  case 15: /* opt_goto: T_GOTO  */
#line 65 "snobol4.y"
                                                                                                  { (yyval.tok)=(yyvsp[0].tok); }
#line 1322 "snobol4.tab.c"
    break;

  case 16: /* opt_goto: %empty  */
#line 66 "snobol4.y"
                                                                                                   { (yyval.tok).sval=NULL;(yyval.tok).ival=0;(yyval.tok).lineno=0;(yyval.tok).kind=0; }
#line 1328 "snobol4.tab.c"
    break;

  case 17: /* expr0: expr2 T_ASSIGNMENT expr0  */
#line 70 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_ASSIGN,          (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1334 "snobol4.tab.c"
    break;

  case 18: /* expr0: expr2 T_MATCH expr0  */
#line 71 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_SCAN,            (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1340 "snobol4.tab.c"
    break;

  case 19: /* expr0: expr2  */
#line 72 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1346 "snobol4.tab.c"
    break;

  case 20: /* expr2: expr2 T_AMPERSAND expr3  */
#line 74 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_OPSYN,           (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1352 "snobol4.tab.c"
    break;

  case 21: /* expr2: expr3  */
#line 75 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1358 "snobol4.tab.c"
    break;

  case 22: /* expr3: expr3 T_ALTERNATION expr4  */
#line 77 "snobol4.y"
                                                                                                  { if((yyvsp[-2].expr)->kind==E_ALT){expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr);}else{EXPR_t*a=expr_new(E_ALT);expr_add_child(a,(yyvsp[-2].expr));expr_add_child(a,(yyvsp[0].expr));(yyval.expr)=a;} }
#line 1364 "snobol4.tab.c"
    break;

  case 23: /* expr3: expr4  */
#line 78 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1370 "snobol4.tab.c"
    break;

  case 24: /* expr4: expr4 T_CONCAT expr5  */
#line 80 "snobol4.y"
                                                                                                            { if((yyvsp[-2].expr)->kind==E_SEQ){expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr);}else{EXPR_t*s=expr_new(E_SEQ);expr_add_child(s,(yyvsp[-2].expr));expr_add_child(s,(yyvsp[0].expr));(yyval.expr)=s;} }
#line 1376 "snobol4.tab.c"
    break;

  case 25: /* expr4: expr5  */
#line 81 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1382 "snobol4.tab.c"
    break;

  case 26: /* expr5: expr5 T_AT_SIGN expr6  */
#line 83 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_CURSOR,     (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1388 "snobol4.tab.c"
    break;

  case 27: /* expr5: expr6  */
#line 84 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1394 "snobol4.tab.c"
    break;

  case 28: /* expr6: expr6 T_ADDITION expr7  */
#line 86 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_ADD,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1400 "snobol4.tab.c"
    break;

  case 29: /* expr6: expr6 T_SUBTRACTION expr7  */
#line 87 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_SUB,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1406 "snobol4.tab.c"
    break;

  case 30: /* expr6: expr7  */
#line 88 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1412 "snobol4.tab.c"
    break;

  case 31: /* expr7: expr7 T_POUND expr8  */
#line 90 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_MUL,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1418 "snobol4.tab.c"
    break;

  case 32: /* expr7: expr8  */
#line 91 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1424 "snobol4.tab.c"
    break;

  case 33: /* expr8: expr8 T_DIVISION expr9  */
#line 93 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_DIV,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1430 "snobol4.tab.c"
    break;

  case 34: /* expr8: expr9  */
#line 94 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1436 "snobol4.tab.c"
    break;

  case 35: /* expr9: expr9 T_MULTIPLICATION expr10  */
#line 96 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_MUL,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1442 "snobol4.tab.c"
    break;

  case 36: /* expr9: expr10  */
#line 97 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1448 "snobol4.tab.c"
    break;

  case 37: /* expr10: expr10 T_PERCENT expr11  */
#line 99 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_DIV,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1454 "snobol4.tab.c"
    break;

  case 38: /* expr10: expr11  */
#line 100 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1460 "snobol4.tab.c"
    break;

  case 39: /* expr11: expr12 T_EXPONENTIATION expr11  */
#line 102 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_POW,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1466 "snobol4.tab.c"
    break;

  case 40: /* expr11: expr12  */
#line 103 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1472 "snobol4.tab.c"
    break;

  case 41: /* expr12: expr12 T_IMMEDIATE_ASSIGN expr13  */
#line 105 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_IMMED_ASGN,(yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1478 "snobol4.tab.c"
    break;

  case 42: /* expr12: expr12 T_COND_ASSIGN expr13  */
#line 106 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_COND_ASGN, (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1484 "snobol4.tab.c"
    break;

  case 43: /* expr12: expr13  */
#line 107 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1490 "snobol4.tab.c"
    break;

  case 44: /* expr13: expr14 T_TILDE expr13  */
#line 109 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_COND_ASGN, (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1496 "snobol4.tab.c"
    break;

  case 45: /* expr13: expr14  */
#line 110 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1502 "snobol4.tab.c"
    break;

  case 46: /* expr14: T_UN_AT_SIGN expr14  */
#line 112 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_CAPT_CURSOR,     (yyvsp[0].expr)); }
#line 1508 "snobol4.tab.c"
    break;

  case 47: /* expr14: T_UN_TILDE expr14  */
#line 113 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_INDIRECT,        (yyvsp[0].expr)); }
#line 1514 "snobol4.tab.c"
    break;

  case 48: /* expr14: T_UN_QUESTION_MARK expr14  */
#line 114 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_INTERROGATE,     (yyvsp[0].expr)); }
#line 1520 "snobol4.tab.c"
    break;

  case 49: /* expr14: T_UN_AMPERSAND expr14  */
#line 115 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_OPSYN,           (yyvsp[0].expr)); }
#line 1526 "snobol4.tab.c"
    break;

  case 50: /* expr14: T_UN_PLUS expr14  */
#line 116 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_PLS,             (yyvsp[0].expr)); }
#line 1532 "snobol4.tab.c"
    break;

  case 51: /* expr14: T_UN_MINUS expr14  */
#line 117 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_MNS,             (yyvsp[0].expr)); }
#line 1538 "snobol4.tab.c"
    break;

  case 52: /* expr14: T_UN_ASTERISK expr14  */
#line 118 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DEFER,           (yyvsp[0].expr)); }
#line 1544 "snobol4.tab.c"
    break;

  case 53: /* expr14: T_UN_DOLLAR_SIGN expr14  */
#line 119 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_INDIRECT,        (yyvsp[0].expr)); }
#line 1550 "snobol4.tab.c"
    break;

  case 54: /* expr14: T_UN_PERIOD expr14  */
#line 120 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_NAME,            (yyvsp[0].expr)); }
#line 1556 "snobol4.tab.c"
    break;

  case 55: /* expr14: T_UN_EXCLAMATION expr14  */
#line 121 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_POW,             (yyvsp[0].expr)); }
#line 1562 "snobol4.tab.c"
    break;

  case 56: /* expr14: T_UN_PERCENT expr14  */
#line 122 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DIV,             (yyvsp[0].expr)); }
#line 1568 "snobol4.tab.c"
    break;

  case 57: /* expr14: T_UN_SLASH expr14  */
#line 123 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DIV,             (yyvsp[0].expr)); }
#line 1574 "snobol4.tab.c"
    break;

  case 58: /* expr14: T_UN_POUND expr14  */
#line 124 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_MUL,             (yyvsp[0].expr)); }
#line 1580 "snobol4.tab.c"
    break;

  case 59: /* expr14: T_UN_EQUAL expr14  */
#line 125 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_ASSIGN,          (yyvsp[0].expr)); }
#line 1586 "snobol4.tab.c"
    break;

  case 60: /* expr14: T_UN_VERTICAL_BAR expr14  */
#line 126 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_ALT,             (yyvsp[0].expr)); }
#line 1592 "snobol4.tab.c"
    break;

  case 61: /* expr14: expr15  */
#line 127 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1598 "snobol4.tab.c"
    break;

  case 62: /* expr15: expr15 T_LBRACK exprlist T_RBRACK  */
#line 129 "snobol4.y"
                                                                                                { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,(yyvsp[-3].expr));for(int j=0;j<(yyvsp[-1].expr)->nchildren;j++)expr_add_child(i,(yyvsp[-1].expr)->children[j]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=i; }
#line 1604 "snobol4.tab.c"
    break;

  case 63: /* expr15: expr15 T_LANGLE exprlist T_RANGLE  */
#line 130 "snobol4.y"
                                                                                                { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,(yyvsp[-3].expr));for(int j=0;j<(yyvsp[-1].expr)->nchildren;j++)expr_add_child(i,(yyvsp[-1].expr)->children[j]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=i; }
#line 1610 "snobol4.tab.c"
    break;

  case 64: /* expr15: expr17  */
#line 131 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1616 "snobol4.tab.c"
    break;

  case 65: /* exprlist: exprlist_ne  */
#line 133 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1622 "snobol4.tab.c"
    break;

  case 66: /* exprlist: %empty  */
#line 134 "snobol4.y"
                                                                                                   { (yyval.expr)=expr_new(E_NUL); }
#line 1628 "snobol4.tab.c"
    break;

  case 67: /* exprlist_ne: exprlist_ne T_COMMA expr0  */
#line 136 "snobol4.y"
                                                                                                  { expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr); }
#line 1634 "snobol4.tab.c"
    break;

  case 68: /* exprlist_ne: exprlist_ne T_COMMA  */
#line 137 "snobol4.y"
                                                                                                  { expr_add_child((yyvsp[-1].expr),expr_new(E_NUL));(yyval.expr)=(yyvsp[-1].expr); }
#line 1640 "snobol4.tab.c"
    break;

  case 69: /* exprlist_ne: expr0  */
#line 138 "snobol4.y"
                                                                                                   { EXPR_t*l=expr_new(E_NUL);expr_add_child(l,(yyvsp[0].expr));(yyval.expr)=l; }
#line 1646 "snobol4.tab.c"
    break;

  case 70: /* expr17: T_LPAREN expr0 T_RPAREN  */
#line 140 "snobol4.y"
                                                                                                { (yyval.expr)=(yyvsp[-1].expr); }
#line 1652 "snobol4.tab.c"
    break;

  case 71: /* expr17: T_LPAREN expr0 T_COMMA exprlist_ne T_RPAREN  */
#line 141 "snobol4.y"
                                                                                               { EXPR_t*a=expr_new(E_ALT);expr_add_child(a,(yyvsp[-3].expr));for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++)expr_add_child(a,(yyvsp[-1].expr)->children[i]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=a; }
#line 1658 "snobol4.tab.c"
    break;

  case 72: /* expr17: T_LPAREN T_RPAREN  */
#line 142 "snobol4.y"
                                                                                                { (yyval.expr)=expr_new(E_NUL); }
#line 1664 "snobol4.tab.c"
    break;

  case 73: /* expr17: T_FUNCTION T_LPAREN exprlist T_RPAREN  */
#line 143 "snobol4.y"
                                                                                               { EXPR_t*e=expr_new(E_FNC);e->sval=(char*)(yyvsp[-3].tok).sval;for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++)expr_add_child(e,(yyvsp[-1].expr)->children[i]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=e; }
#line 1670 "snobol4.tab.c"
    break;

  case 74: /* expr17: T_IDENT  */
#line 144 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1676 "snobol4.tab.c"
    break;

  case 75: /* expr17: T_END  */
#line 145 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1682 "snobol4.tab.c"
    break;

  case 76: /* expr17: T_KEYWORD  */
#line 146 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_KEYWORD);e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1688 "snobol4.tab.c"
    break;

  case 77: /* expr17: T_STR  */
#line 147 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_QLIT);   e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1694 "snobol4.tab.c"
    break;

  case 78: /* expr17: T_INT  */
#line 148 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_ILIT);   e->ival=(yyvsp[0].tok).ival;(yyval.expr)=e; }
#line 1700 "snobol4.tab.c"
    break;

  case 79: /* expr17: T_REAL  */
#line 149 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_FLIT);   e->dval=(yyvsp[0].tok).dval;(yyval.expr)=e; }
#line 1706 "snobol4.tab.c"
    break;


#line 1710 "snobol4.tab.c"

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

#line 151 "snobol4.y"

int snobol4_lex(YYSTYPE *yylval_param, void *yyparse_param) {
    (void)yyparse_param; Token t=lex_next(g_lx); yylval_param->tok=t;
    return t.kind;
}
void snobol4_error(void *p,const char *msg){(void)p;sno_error(g_lx?g_lx->lineno:0,"parse error: %s",msg);}
static void fixup_val(EXPR_t *e){ (void)e; /* SNOBOL4: no-op — E_SEQ never converted to E_CAT; runtime handles both */ }
static int is_pat(EXPR_t *e){
    if(!e) return 0;
    switch(e->kind){case E_ARB:case E_ARBNO:case E_CAPT_COND_ASGN:case E_CAPT_IMMED_ASGN:case E_CAPT_CURSOR:case E_DEFER:return 1;default:break;}
    for(int i=0;i<e->nchildren;i++) if(is_pat(e->children[i])) return 1;
    return 0;
}
static char *goto_label(Lex *lx){
    Token t=lex_peek(lx); int open=t.kind,close;
    if(open==T_LPAREN) close=T_RPAREN; else if(open==T_LANGLE) close=T_RANGLE; else return NULL;
    lex_next(lx); t=lex_peek(lx); char *label=NULL;
    if(t.kind==T_IDENT||t.kind==T_FUNCTION||t.kind==T_KEYWORD||t.kind==T_END){lex_next(lx);label=(char*)t.sval;}
    else if(t.kind==T_UN_DOLLAR_SIGN){
        lex_next(lx);
        if(lex_peek(lx).kind==T_LPAREN){
            int depth=1;lex_next(lx);char eb[512];int ep=0;
            while(!lex_at_end(lx)&&depth>0){Token tok=lex_next(lx);if(tok.kind==T_LPAREN)depth++;else if(tok.kind==T_RPAREN){depth--;if(!depth)break;}if(tok.sval&&ep<510){int l=strlen(tok.sval);memcpy(eb+ep,tok.sval,l);ep+=l;}}
            eb[ep]='\0';char*buf=malloc(12+ep+1);memcpy(buf,"$COMPUTED:",10);memcpy(buf+10,eb,ep);buf[10+ep]='\0';label=buf;
        } else if(lex_peek(lx).kind==T_STR){
            Token n=lex_next(lx);const char*lit=n.sval?n.sval:"";char*buf=malloc(16+strlen(lit));sprintf(buf,"$COMPUTED:'%s'",lit);label=buf;
        } else {Token n=lex_next(lx);char buf[512];snprintf(buf,sizeof buf,"$%s",n.sval?n.sval:"?");label=strdup(buf);}
    } else if(t.kind==T_LPAREN){
        int depth=1;lex_next(lx);
        while(!lex_at_end(lx)&&depth>0){Token tok=lex_next(lx);if(tok.kind==T_LPAREN)depth++;else if(tok.kind==T_RPAREN)depth--;}
        label=strdup("$COMPUTED");
    }
    if(lex_peek(lx).kind==close) lex_next(lx);
    return label;
}
static SnoGoto *goto_field(const char *gs,int lineno){
    if(!gs||!*gs) return NULL;
    Lex lx={0};lex_open_str(&lx,gs,(int)strlen(gs),lineno);SnoGoto *g=sgoto_new();
    while(!lex_at_end(&lx)){
        Token t=lex_peek(&lx);
        if((t.kind==T_IDENT||t.kind==T_FUNCTION)&&t.sval){
            if(strcasecmp(t.sval,"S")==0){lex_next(&lx);g->onsuccess=goto_label(&lx);continue;}
            if(strcasecmp(t.sval,"F")==0){lex_next(&lx);g->onfailure=goto_label(&lx);continue;}
        }
        if(t.kind==T_LPAREN||t.kind==T_LANGLE){g->uncond=goto_label(&lx);continue;}
        sno_error(lineno,"unexpected token in goto field");lex_next(&lx);
    }
    if(!g->onsuccess&&!g->onfailure&&!g->uncond){free(g);return NULL;}
    return g;
}
static void sno4_stmt_commit(void *param,Token lbl,EXPR_t *subj,EXPR_t *pat,int has_eq,EXPR_t *repl,Token gt){
    PP *pp=(PP*)param;
    if(lbl.sval&&strcasecmp(lbl.sval,"EXPORT")==0){
        if(subj&&subj->kind==E_VAR&&subj->sval){
            ExportEntry*e=calloc(1,sizeof*e);e->name=strdup(subj->sval);
            for(char*p=e->name;*p;p++)*p=(char)toupper((unsigned char)*p);
            e->next=pp->prog->exports;pp->prog->exports=e;
        } return;
    }
    if(lbl.sval&&strcasecmp(lbl.sval,"IMPORT")==0){
        ImportEntry*e=calloc(1,sizeof*e);const char*n=subj&&subj->sval?subj->sval:"";
        char*dot1=strchr(n,'.');
        if(dot1){char*dot2=strchr(dot1+1,'.');
            if(dot2){e->lang=strndup(n,(size_t)(dot1-n));e->name=strndup(dot1+1,(size_t)(dot2-dot1-1));e->method=strdup(dot2+1);}
            else{e->lang=strdup("");e->name=strndup(n,(size_t)(dot1-n));e->method=strdup(dot1+1);}}
        else{e->lang=strdup("");e->name=strdup(n);e->method=strdup(n);}
        e->next=pp->prog->imports;pp->prog->imports=e;return;
    }
    STMT_t *s=stmt_new();s->lineno=lbl.lineno;
    if(lbl.sval){s->label=strdup(lbl.sval);s->is_end=lbl.ival||(strcasecmp(lbl.sval,"END")==0);}
    /* S=PR split: E_SCAN(subj, pat) from "X ? PAT" binary match operator */
    if(!pat && subj && subj->kind==E_SCAN && subj->nchildren==2) {
        EXPR_t *orig = subj;
        subj = orig->children[0];
        pat  = orig->children[1];
    }
    /* S=PR split: if subj is E_SEQ with first child a bare name, split into
     * subject=first_child, pattern=rest. Grammar puts everything in opt_subject. */
    if(!pat && subj && (subj->kind==E_SEQ) && subj->nchildren>=2) {
        EXPR_t *first = subj->children[0];
        if(first->kind==E_VAR || first->kind==E_KEYWORD) {
            int nc = subj->nchildren - 1;
            EXPR_t *rest;
            if(nc == 1) {
                rest = subj->children[1];
            } else {
                rest = expr_new(E_SEQ);
                for(int i=1;i<subj->nchildren;i++) expr_add_child(rest,subj->children[i]);
            }
            subj = first;
            pat  = rest;
        }
    }
    s->subject=subj; s->pattern=pat;
    if(s->subject) fixup_val(s->subject);
    if(has_eq){s->has_eq=1;s->replacement=repl;if(repl&&!is_pat(repl))fixup_val(repl);}
    s->go=goto_field(gt.sval,s->lineno);
    if(!pp->prog->head) pp->prog->head=pp->prog->tail=s; else{pp->prog->tail->next=s;pp->prog->tail=s;}
    pp->prog->nstmts++;
}
static EXPR_t *parse_expr(Lex *lx){
    Program *prog=calloc(1,sizeof*prog);PP p={prog,NULL};g_lx=lx;snobol4_parse(&p);
    return prog->head?prog->head->subject:NULL;
}
Program *parse_program_tokens(Lex *stream){
    Program *prog=calloc(1,sizeof*prog);PP p={prog,NULL};g_lx=stream;snobol4_parse(&p);return prog;
}
Program *parse_program(LineArray *lines){(void)lines;return calloc(1,sizeof(Program));}
EXPR_t *parse_expr_from_str(const char *src){
    if(!src||!*src) return NULL;Lex lx={0};lex_open_str(&lx,src,(int)strlen(src),0);return parse_expr(&lx);
}
