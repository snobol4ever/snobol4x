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
  YYSYMBOL_T_GOTO_S = 11,                  /* T_GOTO_S  */
  YYSYMBOL_T_GOTO_F = 12,                  /* T_GOTO_F  */
  YYSYMBOL_T_GOTO_LPAREN = 13,             /* T_GOTO_LPAREN  */
  YYSYMBOL_T_GOTO_RPAREN = 14,             /* T_GOTO_RPAREN  */
  YYSYMBOL_T_STMT_END = 15,                /* T_STMT_END  */
  YYSYMBOL_T_ASSIGNMENT = 16,              /* T_ASSIGNMENT  */
  YYSYMBOL_T_MATCH = 17,                   /* T_MATCH  */
  YYSYMBOL_T_ALTERNATION = 18,             /* T_ALTERNATION  */
  YYSYMBOL_T_ADDITION = 19,                /* T_ADDITION  */
  YYSYMBOL_T_SUBTRACTION = 20,             /* T_SUBTRACTION  */
  YYSYMBOL_T_MULTIPLICATION = 21,          /* T_MULTIPLICATION  */
  YYSYMBOL_T_DIVISION = 22,                /* T_DIVISION  */
  YYSYMBOL_T_EXPONENTIATION = 23,          /* T_EXPONENTIATION  */
  YYSYMBOL_T_IMMEDIATE_ASSIGN = 24,        /* T_IMMEDIATE_ASSIGN  */
  YYSYMBOL_T_COND_ASSIGN = 25,             /* T_COND_ASSIGN  */
  YYSYMBOL_T_AMPERSAND = 26,               /* T_AMPERSAND  */
  YYSYMBOL_T_AT_SIGN = 27,                 /* T_AT_SIGN  */
  YYSYMBOL_T_POUND = 28,                   /* T_POUND  */
  YYSYMBOL_T_PERCENT = 29,                 /* T_PERCENT  */
  YYSYMBOL_T_TILDE = 30,                   /* T_TILDE  */
  YYSYMBOL_T_UN_AT_SIGN = 31,              /* T_UN_AT_SIGN  */
  YYSYMBOL_T_UN_TILDE = 32,                /* T_UN_TILDE  */
  YYSYMBOL_T_UN_QUESTION_MARK = 33,        /* T_UN_QUESTION_MARK  */
  YYSYMBOL_T_UN_AMPERSAND = 34,            /* T_UN_AMPERSAND  */
  YYSYMBOL_T_UN_PLUS = 35,                 /* T_UN_PLUS  */
  YYSYMBOL_T_UN_MINUS = 36,                /* T_UN_MINUS  */
  YYSYMBOL_T_UN_ASTERISK = 37,             /* T_UN_ASTERISK  */
  YYSYMBOL_T_UN_DOLLAR_SIGN = 38,          /* T_UN_DOLLAR_SIGN  */
  YYSYMBOL_T_UN_PERIOD = 39,               /* T_UN_PERIOD  */
  YYSYMBOL_T_UN_EXCLAMATION = 40,          /* T_UN_EXCLAMATION  */
  YYSYMBOL_T_UN_PERCENT = 41,              /* T_UN_PERCENT  */
  YYSYMBOL_T_UN_SLASH = 42,                /* T_UN_SLASH  */
  YYSYMBOL_T_UN_POUND = 43,                /* T_UN_POUND  */
  YYSYMBOL_T_UN_EQUAL = 44,                /* T_UN_EQUAL  */
  YYSYMBOL_T_UN_VERTICAL_BAR = 45,         /* T_UN_VERTICAL_BAR  */
  YYSYMBOL_T_CONCAT = 46,                  /* T_CONCAT  */
  YYSYMBOL_T_COMMA = 47,                   /* T_COMMA  */
  YYSYMBOL_T_LPAREN = 48,                  /* T_LPAREN  */
  YYSYMBOL_T_RPAREN = 49,                  /* T_RPAREN  */
  YYSYMBOL_T_LBRACK = 50,                  /* T_LBRACK  */
  YYSYMBOL_T_RBRACK = 51,                  /* T_RBRACK  */
  YYSYMBOL_T_LANGLE = 52,                  /* T_LANGLE  */
  YYSYMBOL_T_RANGLE = 53,                  /* T_RANGLE  */
  YYSYMBOL_YYACCEPT = 54,                  /* $accept  */
  YYSYMBOL_top = 55,                       /* top  */
  YYSYMBOL_program = 56,                   /* program  */
  YYSYMBOL_stmt = 57,                      /* stmt  */
  YYSYMBOL_unlabeled_stmt = 58,            /* unlabeled_stmt  */
  YYSYMBOL_opt_subject = 59,               /* opt_subject  */
  YYSYMBOL_opt_pattern = 60,               /* opt_pattern  */
  YYSYMBOL_opt_repl = 61,                  /* opt_repl  */
  YYSYMBOL_goto_atom = 62,                 /* goto_atom  */
  YYSYMBOL_goto_expr = 63,                 /* goto_expr  */
  YYSYMBOL_goto_label_expr = 64,           /* goto_label_expr  */
  YYSYMBOL_opt_goto = 65,                  /* opt_goto  */
  YYSYMBOL_expr0 = 66,                     /* expr0  */
  YYSYMBOL_expr2 = 67,                     /* expr2  */
  YYSYMBOL_expr3 = 68,                     /* expr3  */
  YYSYMBOL_expr4 = 69,                     /* expr4  */
  YYSYMBOL_expr5 = 70,                     /* expr5  */
  YYSYMBOL_expr6 = 71,                     /* expr6  */
  YYSYMBOL_expr7 = 72,                     /* expr7  */
  YYSYMBOL_expr8 = 73,                     /* expr8  */
  YYSYMBOL_expr9 = 74,                     /* expr9  */
  YYSYMBOL_expr10 = 75,                    /* expr10  */
  YYSYMBOL_expr11 = 76,                    /* expr11  */
  YYSYMBOL_expr12 = 77,                    /* expr12  */
  YYSYMBOL_expr13 = 78,                    /* expr13  */
  YYSYMBOL_expr14 = 79,                    /* expr14  */
  YYSYMBOL_expr15 = 80,                    /* expr15  */
  YYSYMBOL_exprlist = 81,                  /* exprlist  */
  YYSYMBOL_exprlist_ne = 82,               /* exprlist_ne  */
  YYSYMBOL_expr17 = 83                     /* expr17  */
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
static void     sno4_stmt_commit_go(void*,Token,EXPR_t*,EXPR_t*,int,EXPR_t*,SnoGoto*);
static void     fixup_val(EXPR_t*);
static int      is_pat(EXPR_t*);
static EXPR_t  *parse_expr(Lex*);

#line 206 "snobol4.tab.c"

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
#define YYFINAL  67
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   224

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  54
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  30
/* YYNRULES -- Number of rules.  */
#define YYNRULES  98
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  168

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   308


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
      45,    46,    47,    48,    49,    50,    51,    52,    53
};

#if SNOBOL4_DEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    46,    46,    47,    49,    49,    50,    51,    52,    55,
      56,    58,    59,    61,    62,    64,    65,    66,    72,    73,
      74,    75,    77,    78,    83,    84,    85,    86,    87,    88,
      91,    92,    97,   100,   105,   108,   112,   113,   114,   116,
     117,   119,   120,   122,   123,   125,   126,   128,   129,   130,
     132,   133,   135,   136,   138,   139,   141,   142,   144,   145,
     147,   148,   149,   151,   152,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   171,   172,   173,   175,   176,   178,   179,   180,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191
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
  "T_LABEL", "T_GOTO_S", "T_GOTO_F", "T_GOTO_LPAREN", "T_GOTO_RPAREN",
  "T_STMT_END", "T_ASSIGNMENT", "T_MATCH", "T_ALTERNATION", "T_ADDITION",
  "T_SUBTRACTION", "T_MULTIPLICATION", "T_DIVISION", "T_EXPONENTIATION",
  "T_IMMEDIATE_ASSIGN", "T_COND_ASSIGN", "T_AMPERSAND", "T_AT_SIGN",
  "T_POUND", "T_PERCENT", "T_TILDE", "T_UN_AT_SIGN", "T_UN_TILDE",
  "T_UN_QUESTION_MARK", "T_UN_AMPERSAND", "T_UN_PLUS", "T_UN_MINUS",
  "T_UN_ASTERISK", "T_UN_DOLLAR_SIGN", "T_UN_PERIOD", "T_UN_EXCLAMATION",
  "T_UN_PERCENT", "T_UN_SLASH", "T_UN_POUND", "T_UN_EQUAL",
  "T_UN_VERTICAL_BAR", "T_CONCAT", "T_COMMA", "T_LPAREN", "T_RPAREN",
  "T_LBRACK", "T_RBRACK", "T_LANGLE", "T_RANGLE", "$accept", "top",
  "program", "stmt", "unlabeled_stmt", "opt_subject", "opt_pattern",
  "opt_repl", "goto_atom", "goto_expr", "goto_label_expr", "opt_goto",
  "expr0", "expr2", "expr3", "expr4", "expr5", "expr6", "expr7", "expr8",
  "expr9", "expr10", "expr11", "expr12", "expr13", "expr14", "expr15",
  "exprlist", "exprlist_ne", "expr17", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-95)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-41)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      77,   -95,   -32,   -95,   -95,   -95,   -95,   -95,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   142,    20,    96,   -95,   -95,    15,
      -3,     0,   -19,    29,    31,    18,    48,    43,    46,   -95,
      19,   -95,    58,   -16,   -95,   161,    15,     7,   -95,   -95,
     -95,   -95,   -95,   -95,   -95,   -95,   -95,   -95,   -95,   -95,
     -95,   -95,   -95,   -95,    10,    -4,    60,   -95,   -95,   161,
      42,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   161,   -95,    27,
      44,    42,   161,   161,   -95,   161,   161,   -95,    76,    76,
       3,   -95,    75,    15,    60,    60,   -19,    29,    31,    18,
      18,    48,    43,    46,   -95,   -95,   -95,   -95,   -95,    41,
      40,   -95,   161,    80,    15,    16,   -95,   -95,    85,   112,
      93,   110,   128,    12,   -95,    42,   -95,   -95,   -95,   -95,
      42,   -95,    76,    76,   -95,   -95,   -95,   129,   138,    26,
     111,   139,   -95,   -95,   -95,   -95,   -95,   -95,   -95,   -95,
     -95,    -6,   -95,   -95,   141,    26,   -95,   -95
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
      12,    93,     0,    95,    94,    97,    98,    96,    12,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    12,     5,     8,    17,
       0,    11,    42,    44,    46,    49,    51,    53,    55,    57,
      59,    62,    64,    80,    83,    85,    17,     0,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    91,     0,    38,    40,     1,     4,    16,
      35,    14,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    85,    85,    88,     0,
      84,    35,    14,     0,    89,     0,     0,    15,     0,     0,
       0,    30,     0,    17,    13,    39,    41,    43,    45,    47,
      48,    50,    52,    54,    56,    58,    60,    61,    63,     0,
       0,    92,    87,     0,    17,     0,    36,    37,    32,    34,
       0,     0,     0,     0,     9,    35,    81,    82,    86,     6,
      35,    90,     0,     0,    24,    26,    25,     0,     0,     0,
       0,     0,    31,    33,    27,    29,    19,    20,    21,    18,
      22,     0,    10,     7,     0,     0,    28,    23
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -95,   -95,   -95,   127,   -95,   148,    65,   -45,    -7,   -95,
     -94,   -88,   -24,    11,     2,    86,    87,    88,   -38,    82,
      83,    91,   -15,   -95,   -23,   201,   -95,   -18,    79,   -95
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    25,    26,    27,    28,    29,   103,    70,   160,   161,
     101,   102,    88,    65,    66,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    89,    90,    44
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      64,    91,    31,   123,   128,   129,   130,   131,   164,   132,
      31,    30,    95,    96,    71,   147,    45,   -40,    73,    47,
      67,   148,    72,    72,    92,   149,   -40,    74,    31,   156,
     157,    69,   158,    72,    86,   159,    87,    30,   109,   110,
     165,   133,    82,    83,    84,    97,    78,   150,   152,   153,
      76,    77,   151,    98,    99,   100,    75,    93,   135,    94,
     116,   117,   118,   122,    80,   141,   114,   115,   119,   120,
      79,   126,   127,   104,   105,    81,   121,    -3,    73,   140,
       1,     2,     3,     4,     5,     6,     7,     8,    85,   100,
     134,   122,   136,   137,   104,   139,    -2,   142,   138,     1,
       2,     3,     4,     5,     6,     7,     8,   144,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,   143,   145,    24,   162,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,   146,   154,    24,     1,     2,     3,     4,     5,
       6,     7,   155,    68,   163,   166,    46,   124,   167,   106,
     111,   107,   112,   108,     1,     2,     3,     4,     5,     6,
       7,   113,   125,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,     0,     0,
      24,    63,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,     0,     0,    24,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62
};

static const yytype_int16 yycheck[] =
{
      24,    46,     0,    91,    98,    99,     3,     4,    14,     6,
       8,     0,    16,    17,    17,     3,    48,    17,    18,     8,
       0,     9,    26,    26,    17,    13,    26,    46,    26,     3,
       4,    16,     6,    26,    50,     9,    52,    26,    76,    77,
      46,    38,    23,    24,    25,    69,    28,   135,   142,   143,
      19,    20,   140,    11,    12,    13,    27,    47,   103,    49,
      83,    84,    85,    47,    21,    49,    81,    82,    86,    87,
      22,    95,    96,    71,    72,    29,    49,     0,    18,   124,
       3,     4,     5,     6,     7,     8,     9,    10,    30,    13,
      15,    47,    51,    53,    92,    15,     0,    12,   122,     3,
       4,     5,     6,     7,     8,     9,    10,    14,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    11,    14,    48,    15,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    14,    14,    48,     3,     4,     5,     6,     7,
       8,     9,    14,    26,    15,    14,     8,    92,   165,    73,
      78,    74,    79,    75,     3,     4,     5,     6,     7,     8,
       9,    80,    93,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    -1,    -1,
      48,    49,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    -1,    -1,    48,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    48,    55,    56,    57,    58,    59,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    83,    48,    59,    67,    79,    79,
      79,    79,    79,    79,    79,    79,    79,    79,    79,    79,
      79,    79,    79,    49,    66,    67,    68,     0,    57,    16,
      61,    17,    26,    18,    46,    27,    19,    20,    28,    22,
      21,    29,    23,    24,    25,    30,    50,    52,    66,    81,
      82,    61,    17,    47,    49,    16,    17,    66,    11,    12,
      13,    64,    65,    60,    68,    68,    69,    70,    71,    72,
      72,    73,    74,    75,    76,    76,    78,    78,    78,    81,
      81,    49,    47,    65,    60,    82,    66,    66,    64,    64,
       3,     4,     6,    38,    15,    61,    51,    53,    66,    15,
      61,    49,    12,    11,    14,    14,    14,     3,     9,    13,
      65,    65,    64,    64,    14,    14,     3,     4,     6,     9,
      62,    63,    15,    15,    14,    46,    14,    62
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    54,    55,    55,    56,    56,    57,    57,    57,    58,
      58,    59,    59,    60,    60,    61,    61,    61,    62,    62,
      62,    62,    63,    63,    64,    64,    64,    64,    64,    64,
      65,    65,    65,    65,    65,    65,    66,    66,    66,    67,
      67,    68,    68,    69,    69,    70,    70,    71,    71,    71,
      72,    72,    73,    73,    74,    74,    75,    75,    76,    76,
      77,    77,    77,    78,    78,    79,    79,    79,    79,    79,
      79,    79,    79,    79,    79,    79,    79,    79,    79,    79,
      79,    80,    80,    80,    81,    81,    82,    82,    82,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     1,     5,     7,     1,     4,
       6,     1,     0,     1,     0,     2,     1,     0,     1,     1,
       1,     1,     1,     3,     3,     3,     3,     4,     6,     4,
       1,     4,     2,     4,     2,     0,     3,     3,     1,     3,
       1,     3,     1,     3,     1,     3,     1,     3,     3,     1,
       3,     1,     3,     1,     3,     1,     3,     1,     3,     1,
       3,     3,     1,     3,     1,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       1,     4,     4,     1,     1,     0,     3,     2,     1,     3,
       5,     2,     4,     1,     1,     1,     1,     1,     1
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
#line 46 "snobol4.y"
                                                                                                    { }
#line 1297 "snobol4.tab.c"
    break;

  case 3: /* top: %empty  */
#line 47 "snobol4.y"
                                                                                                    { }
#line 1303 "snobol4.tab.c"
    break;

  case 6: /* stmt: T_LABEL opt_subject opt_repl opt_goto T_STMT_END  */
#line 50 "snobol4.y"
                                                                                     { Token l=(yyvsp[-4].tok); sno4_stmt_commit_go(yyparse_param,l,(yyvsp[-3].expr),NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].go)); }
#line 1309 "snobol4.tab.c"
    break;

  case 7: /* stmt: T_LABEL expr2 T_MATCH opt_pattern opt_repl opt_goto T_STMT_END  */
#line 51 "snobol4.y"
                                                                                     { Token l=(yyvsp[-6].tok); EXPR_t*sc=expr_binary(E_SCAN,(yyvsp[-5].expr),(yyvsp[-3].expr)); sno4_stmt_commit_go(yyparse_param,l,sc,NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].go)); }
#line 1315 "snobol4.tab.c"
    break;

  case 9: /* unlabeled_stmt: opt_subject opt_repl opt_goto T_STMT_END  */
#line 55 "snobol4.y"
                                                                                     { Token l; l.sval=NULL;l.ival=0;l.lineno=0;l.kind=0; sno4_stmt_commit_go(yyparse_param,l,(yyvsp[-3].expr),NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].go)); }
#line 1321 "snobol4.tab.c"
    break;

  case 10: /* unlabeled_stmt: expr2 T_MATCH opt_pattern opt_repl opt_goto T_STMT_END  */
#line 56 "snobol4.y"
                                                                                     { Token l; l.sval=NULL;l.ival=0;l.lineno=0;l.kind=0; EXPR_t*sc=expr_binary(E_SCAN,(yyvsp[-5].expr),(yyvsp[-3].expr)); sno4_stmt_commit_go(yyparse_param,l,sc,NULL,((yyvsp[-2].expr)!=NULL),(yyvsp[-2].expr),(yyvsp[-1].go)); }
#line 1327 "snobol4.tab.c"
    break;

  case 11: /* opt_subject: expr3  */
#line 58 "snobol4.y"
                                                                                                  { (yyval.expr)=(yyvsp[0].expr); }
#line 1333 "snobol4.tab.c"
    break;

  case 12: /* opt_subject: %empty  */
#line 59 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1339 "snobol4.tab.c"
    break;

  case 13: /* opt_pattern: expr3  */
#line 61 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1345 "snobol4.tab.c"
    break;

  case 14: /* opt_pattern: %empty  */
#line 62 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1351 "snobol4.tab.c"
    break;

  case 15: /* opt_repl: T_ASSIGNMENT expr0  */
#line 64 "snobol4.y"
                                                                                                  { (yyval.expr)=(yyvsp[0].expr); }
#line 1357 "snobol4.tab.c"
    break;

  case 16: /* opt_repl: T_ASSIGNMENT  */
#line 65 "snobol4.y"
                                                                                                   { EXPR_t*e=expr_new(E_QLIT);e->sval=strdup("");(yyval.expr)=e; }
#line 1363 "snobol4.tab.c"
    break;

  case 17: /* opt_repl: %empty  */
#line 66 "snobol4.y"
                                                                                                   { (yyval.expr)=NULL; }
#line 1369 "snobol4.tab.c"
    break;

  case 18: /* goto_atom: T_STR  */
#line 72 "snobol4.y"
                     { EXPR_t*e=expr_new(E_QLIT);  e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1375 "snobol4.tab.c"
    break;

  case 19: /* goto_atom: T_IDENT  */
#line 73 "snobol4.y"
                      { EXPR_t*e=expr_new(E_VAR);   e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1381 "snobol4.tab.c"
    break;

  case 20: /* goto_atom: T_FUNCTION  */
#line 74 "snobol4.y"
                        { EXPR_t*e=expr_new(E_VAR); e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1387 "snobol4.tab.c"
    break;

  case 21: /* goto_atom: T_END  */
#line 75 "snobol4.y"
                      { EXPR_t*e=expr_new(E_VAR);   e->sval=(char*)(yyvsp[0].tok).sval; (yyval.expr)=e; }
#line 1393 "snobol4.tab.c"
    break;

  case 22: /* goto_expr: goto_atom  */
#line 77 "snobol4.y"
                                                { (yyval.expr)=(yyvsp[0].expr); }
#line 1399 "snobol4.tab.c"
    break;

  case 23: /* goto_expr: goto_expr T_CONCAT goto_atom  */
#line 78 "snobol4.y"
                                                { if((yyvsp[-2].expr)->kind==E_SEQ){expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr);}else{EXPR_t*s=expr_new(E_SEQ);expr_add_child(s,(yyvsp[-2].expr));expr_add_child(s,(yyvsp[0].expr));(yyval.expr)=s;} }
#line 1405 "snobol4.tab.c"
    break;

  case 24: /* goto_label_expr: T_GOTO_LPAREN T_IDENT T_GOTO_RPAREN  */
#line 83 "snobol4.y"
                                                              { (yyval.go)=sgoto_new(); (yyval.go)->uncond=strdup((yyvsp[-1].tok).sval); }
#line 1411 "snobol4.tab.c"
    break;

  case 25: /* goto_label_expr: T_GOTO_LPAREN T_END T_GOTO_RPAREN  */
#line 84 "snobol4.y"
                                                              { (yyval.go)=sgoto_new(); (yyval.go)->uncond=strdup((yyvsp[-1].tok).sval); }
#line 1417 "snobol4.tab.c"
    break;

  case 26: /* goto_label_expr: T_GOTO_LPAREN T_FUNCTION T_GOTO_RPAREN  */
#line 85 "snobol4.y"
                                                              { (yyval.go)=sgoto_new(); (yyval.go)->uncond=strdup((yyvsp[-1].tok).sval); }
#line 1423 "snobol4.tab.c"
    break;

  case 27: /* goto_label_expr: T_GOTO_LPAREN T_UN_DOLLAR_SIGN T_IDENT T_GOTO_RPAREN  */
#line 86 "snobol4.y"
                                                                  { (yyval.go)=sgoto_new(); char buf[512]; snprintf(buf,sizeof buf,"$%s",(yyvsp[-1].tok).sval); (yyval.go)->uncond=strdup(buf); }
#line 1429 "snobol4.tab.c"
    break;

  case 28: /* goto_label_expr: T_GOTO_LPAREN T_UN_DOLLAR_SIGN T_GOTO_LPAREN goto_expr T_GOTO_RPAREN T_GOTO_RPAREN  */
#line 87 "snobol4.y"
                                                                                                { (yyval.go)=sgoto_new(); (yyval.go)->computed_uncond_expr=((yyvsp[-2].expr)); }
#line 1435 "snobol4.tab.c"
    break;

  case 29: /* goto_label_expr: T_GOTO_LPAREN T_UN_DOLLAR_SIGN T_STR T_GOTO_RPAREN  */
#line 88 "snobol4.y"
                                                                { (yyval.go)=sgoto_new(); EXPR_t*e=expr_new(E_QLIT); e->sval=strdup((yyvsp[-1].tok).sval); (yyval.go)->computed_uncond_expr=e; }
#line 1441 "snobol4.tab.c"
    break;

  case 30: /* opt_goto: goto_label_expr  */
#line 91 "snobol4.y"
                                                              { (yyval.go)=(yyvsp[0].go); }
#line 1447 "snobol4.tab.c"
    break;

  case 31: /* opt_goto: T_GOTO_S goto_label_expr T_GOTO_F goto_label_expr  */
#line 92 "snobol4.y"
                                                               {
               (yyval.go)=sgoto_new();
               (yyval.go)->onsuccess=(yyvsp[-2].go)->uncond; (yyval.go)->computed_success_expr=(yyvsp[-2].go)->computed_uncond_expr; free((yyvsp[-2].go));
               (yyval.go)->onfailure=(yyvsp[0].go)->uncond; (yyval.go)->computed_failure_expr=(yyvsp[0].go)->computed_uncond_expr; free((yyvsp[0].go));
             }
#line 1457 "snobol4.tab.c"
    break;

  case 32: /* opt_goto: T_GOTO_S goto_label_expr  */
#line 97 "snobol4.y"
                                      {
               (yyval.go)=sgoto_new(); (yyval.go)->onsuccess=(yyvsp[0].go)->uncond; (yyval.go)->computed_success_expr=(yyvsp[0].go)->computed_uncond_expr; free((yyvsp[0].go));
             }
#line 1465 "snobol4.tab.c"
    break;

  case 33: /* opt_goto: T_GOTO_F goto_label_expr T_GOTO_S goto_label_expr  */
#line 100 "snobol4.y"
                                                               {
               (yyval.go)=sgoto_new();
               (yyval.go)->onfailure=(yyvsp[-2].go)->uncond; (yyval.go)->computed_failure_expr=(yyvsp[-2].go)->computed_uncond_expr; free((yyvsp[-2].go));
               (yyval.go)->onsuccess=(yyvsp[0].go)->uncond; (yyval.go)->computed_success_expr=(yyvsp[0].go)->computed_uncond_expr; free((yyvsp[0].go));
             }
#line 1475 "snobol4.tab.c"
    break;

  case 34: /* opt_goto: T_GOTO_F goto_label_expr  */
#line 105 "snobol4.y"
                                      {
               (yyval.go)=sgoto_new(); (yyval.go)->onfailure=(yyvsp[0].go)->uncond; (yyval.go)->computed_failure_expr=(yyvsp[0].go)->computed_uncond_expr; free((yyvsp[0].go));
             }
#line 1483 "snobol4.tab.c"
    break;

  case 35: /* opt_goto: %empty  */
#line 108 "snobol4.y"
                                                               { (yyval.go)=NULL; }
#line 1489 "snobol4.tab.c"
    break;

  case 36: /* expr0: expr2 T_ASSIGNMENT expr0  */
#line 112 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_ASSIGN,          (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1495 "snobol4.tab.c"
    break;

  case 37: /* expr0: expr2 T_MATCH expr0  */
#line 113 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_SCAN,            (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1501 "snobol4.tab.c"
    break;

  case 38: /* expr0: expr2  */
#line 114 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1507 "snobol4.tab.c"
    break;

  case 39: /* expr2: expr2 T_AMPERSAND expr3  */
#line 116 "snobol4.y"
                                                                                                  { EXPR_t*_e=expr_binary(E_OPSYN,(yyvsp[-2].expr),(yyvsp[0].expr)); _e->sval=strdup("&"); (yyval.expr)=_e; }
#line 1513 "snobol4.tab.c"
    break;

  case 40: /* expr2: expr3  */
#line 117 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1519 "snobol4.tab.c"
    break;

  case 41: /* expr3: expr3 T_ALTERNATION expr4  */
#line 119 "snobol4.y"
                                                                                                  { if((yyvsp[-2].expr)->kind==E_ALT){expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr);}else{EXPR_t*a=expr_new(E_ALT);expr_add_child(a,(yyvsp[-2].expr));expr_add_child(a,(yyvsp[0].expr));(yyval.expr)=a;} }
#line 1525 "snobol4.tab.c"
    break;

  case 42: /* expr3: expr4  */
#line 120 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1531 "snobol4.tab.c"
    break;

  case 43: /* expr4: expr4 T_CONCAT expr5  */
#line 122 "snobol4.y"
                                                                                                            { if((yyvsp[-2].expr)->kind==E_SEQ){expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr);}else{EXPR_t*s=expr_new(E_SEQ);expr_add_child(s,(yyvsp[-2].expr));expr_add_child(s,(yyvsp[0].expr));(yyval.expr)=s;} }
#line 1537 "snobol4.tab.c"
    break;

  case 44: /* expr4: expr5  */
#line 123 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1543 "snobol4.tab.c"
    break;

  case 45: /* expr5: expr5 T_AT_SIGN expr6  */
#line 125 "snobol4.y"
                                                                                                  { EXPR_t*_e=expr_binary(E_OPSYN,(yyvsp[-2].expr),(yyvsp[0].expr)); _e->sval=strdup("@"); (yyval.expr)=_e; }
#line 1549 "snobol4.tab.c"
    break;

  case 46: /* expr5: expr6  */
#line 126 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1555 "snobol4.tab.c"
    break;

  case 47: /* expr6: expr6 T_ADDITION expr7  */
#line 128 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_ADD,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1561 "snobol4.tab.c"
    break;

  case 48: /* expr6: expr6 T_SUBTRACTION expr7  */
#line 129 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_SUB,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1567 "snobol4.tab.c"
    break;

  case 49: /* expr6: expr7  */
#line 130 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1573 "snobol4.tab.c"
    break;

  case 50: /* expr7: expr7 T_POUND expr8  */
#line 132 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_MUL,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1579 "snobol4.tab.c"
    break;

  case 51: /* expr7: expr8  */
#line 133 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1585 "snobol4.tab.c"
    break;

  case 52: /* expr8: expr8 T_DIVISION expr9  */
#line 135 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_DIV,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1591 "snobol4.tab.c"
    break;

  case 53: /* expr8: expr9  */
#line 136 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1597 "snobol4.tab.c"
    break;

  case 54: /* expr9: expr9 T_MULTIPLICATION expr10  */
#line 138 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_MUL,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1603 "snobol4.tab.c"
    break;

  case 55: /* expr9: expr10  */
#line 139 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1609 "snobol4.tab.c"
    break;

  case 56: /* expr10: expr10 T_PERCENT expr11  */
#line 141 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_DIV,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1615 "snobol4.tab.c"
    break;

  case 57: /* expr10: expr11  */
#line 142 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1621 "snobol4.tab.c"
    break;

  case 58: /* expr11: expr12 T_EXPONENTIATION expr11  */
#line 144 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_POW,             (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1627 "snobol4.tab.c"
    break;

  case 59: /* expr11: expr12  */
#line 145 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1633 "snobol4.tab.c"
    break;

  case 60: /* expr12: expr12 T_IMMEDIATE_ASSIGN expr13  */
#line 147 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_IMMED_ASGN,(yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1639 "snobol4.tab.c"
    break;

  case 61: /* expr12: expr12 T_COND_ASSIGN expr13  */
#line 148 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_binary(E_CAPT_COND_ASGN, (yyvsp[-2].expr),(yyvsp[0].expr)); }
#line 1645 "snobol4.tab.c"
    break;

  case 62: /* expr12: expr13  */
#line 149 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1651 "snobol4.tab.c"
    break;

  case 63: /* expr13: expr14 T_TILDE expr13  */
#line 151 "snobol4.y"
                                                                                                  { EXPR_t*_e=expr_binary(E_OPSYN,(yyvsp[-2].expr),(yyvsp[0].expr)); _e->sval=strdup("~"); (yyval.expr)=_e; }
#line 1657 "snobol4.tab.c"
    break;

  case 64: /* expr13: expr14  */
#line 152 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1663 "snobol4.tab.c"
    break;

  case 65: /* expr14: T_UN_AT_SIGN expr14  */
#line 154 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_CAPT_CURSOR,     (yyvsp[0].expr)); }
#line 1669 "snobol4.tab.c"
    break;

  case 66: /* expr14: T_UN_TILDE expr14  */
#line 155 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_NOT,             (yyvsp[0].expr)); }
#line 1675 "snobol4.tab.c"
    break;

  case 67: /* expr14: T_UN_QUESTION_MARK expr14  */
#line 156 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_INTERROGATE,     (yyvsp[0].expr)); }
#line 1681 "snobol4.tab.c"
    break;

  case 68: /* expr14: T_UN_AMPERSAND expr14  */
#line 157 "snobol4.y"
                                                                                                  { EXPR_t*_e=expr_unary(E_OPSYN,(yyvsp[0].expr)); _e->sval=strdup("&"); (yyval.expr)=_e; }
#line 1687 "snobol4.tab.c"
    break;

  case 69: /* expr14: T_UN_PLUS expr14  */
#line 158 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_PLS,             (yyvsp[0].expr)); }
#line 1693 "snobol4.tab.c"
    break;

  case 70: /* expr14: T_UN_MINUS expr14  */
#line 159 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_MNS,             (yyvsp[0].expr)); }
#line 1699 "snobol4.tab.c"
    break;

  case 71: /* expr14: T_UN_ASTERISK expr14  */
#line 160 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DEFER,           (yyvsp[0].expr)); }
#line 1705 "snobol4.tab.c"
    break;

  case 72: /* expr14: T_UN_DOLLAR_SIGN expr14  */
#line 161 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_INDIRECT,        (yyvsp[0].expr)); }
#line 1711 "snobol4.tab.c"
    break;

  case 73: /* expr14: T_UN_PERIOD expr14  */
#line 162 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_NAME,            (yyvsp[0].expr)); }
#line 1717 "snobol4.tab.c"
    break;

  case 74: /* expr14: T_UN_EXCLAMATION expr14  */
#line 163 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_POW,             (yyvsp[0].expr)); }
#line 1723 "snobol4.tab.c"
    break;

  case 75: /* expr14: T_UN_PERCENT expr14  */
#line 164 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DIV,             (yyvsp[0].expr)); }
#line 1729 "snobol4.tab.c"
    break;

  case 76: /* expr14: T_UN_SLASH expr14  */
#line 165 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_DIV,             (yyvsp[0].expr)); }
#line 1735 "snobol4.tab.c"
    break;

  case 77: /* expr14: T_UN_POUND expr14  */
#line 166 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_MUL,             (yyvsp[0].expr)); }
#line 1741 "snobol4.tab.c"
    break;

  case 78: /* expr14: T_UN_EQUAL expr14  */
#line 167 "snobol4.y"
                                                                                                  { (yyval.expr)=expr_unary(E_ASSIGN,          (yyvsp[0].expr)); }
#line 1747 "snobol4.tab.c"
    break;

  case 79: /* expr14: T_UN_VERTICAL_BAR expr14  */
#line 168 "snobol4.y"
                                                                                                  { EXPR_t*_e=expr_unary(E_OPSYN,(yyvsp[0].expr)); _e->sval=strdup("|"); (yyval.expr)=_e; }
#line 1753 "snobol4.tab.c"
    break;

  case 80: /* expr14: expr15  */
#line 169 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1759 "snobol4.tab.c"
    break;

  case 81: /* expr15: expr15 T_LBRACK exprlist T_RBRACK  */
#line 171 "snobol4.y"
                                                                                                { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,(yyvsp[-3].expr));for(int j=0;j<(yyvsp[-1].expr)->nchildren;j++)expr_add_child(i,(yyvsp[-1].expr)->children[j]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=i; }
#line 1765 "snobol4.tab.c"
    break;

  case 82: /* expr15: expr15 T_LANGLE exprlist T_RANGLE  */
#line 172 "snobol4.y"
                                                                                                { EXPR_t*i=expr_new(E_IDX);expr_add_child(i,(yyvsp[-3].expr));for(int j=0;j<(yyvsp[-1].expr)->nchildren;j++)expr_add_child(i,(yyvsp[-1].expr)->children[j]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=i; }
#line 1771 "snobol4.tab.c"
    break;

  case 83: /* expr15: expr17  */
#line 173 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1777 "snobol4.tab.c"
    break;

  case 84: /* exprlist: exprlist_ne  */
#line 175 "snobol4.y"
                                                                                                   { (yyval.expr)=(yyvsp[0].expr); }
#line 1783 "snobol4.tab.c"
    break;

  case 85: /* exprlist: %empty  */
#line 176 "snobol4.y"
                                                                                                   { (yyval.expr)=expr_new(E_NUL); }
#line 1789 "snobol4.tab.c"
    break;

  case 86: /* exprlist_ne: exprlist_ne T_COMMA expr0  */
#line 178 "snobol4.y"
                                                                                                  { expr_add_child((yyvsp[-2].expr),(yyvsp[0].expr));(yyval.expr)=(yyvsp[-2].expr); }
#line 1795 "snobol4.tab.c"
    break;

  case 87: /* exprlist_ne: exprlist_ne T_COMMA  */
#line 179 "snobol4.y"
                                                                                                  { expr_add_child((yyvsp[-1].expr),expr_new(E_NUL));(yyval.expr)=(yyvsp[-1].expr); }
#line 1801 "snobol4.tab.c"
    break;

  case 88: /* exprlist_ne: expr0  */
#line 180 "snobol4.y"
                                                                                                   { EXPR_t*l=expr_new(E_NUL);expr_add_child(l,(yyvsp[0].expr));(yyval.expr)=l; }
#line 1807 "snobol4.tab.c"
    break;

  case 89: /* expr17: T_LPAREN expr0 T_RPAREN  */
#line 182 "snobol4.y"
                                                                                                { (yyval.expr)=(yyvsp[-1].expr); }
#line 1813 "snobol4.tab.c"
    break;

  case 90: /* expr17: T_LPAREN expr0 T_COMMA exprlist_ne T_RPAREN  */
#line 183 "snobol4.y"
                                                                                               { EXPR_t*a=expr_new(E_ALT);expr_add_child(a,(yyvsp[-3].expr));for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++)expr_add_child(a,(yyvsp[-1].expr)->children[i]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=a; }
#line 1819 "snobol4.tab.c"
    break;

  case 91: /* expr17: T_LPAREN T_RPAREN  */
#line 184 "snobol4.y"
                                                                                                { (yyval.expr)=expr_new(E_NUL); }
#line 1825 "snobol4.tab.c"
    break;

  case 92: /* expr17: T_FUNCTION T_LPAREN exprlist T_RPAREN  */
#line 185 "snobol4.y"
                                                                                               { EXPR_t*e=expr_new(E_FNC);e->sval=(char*)(yyvsp[-3].tok).sval;for(int i=0;i<(yyvsp[-1].expr)->nchildren;i++)expr_add_child(e,(yyvsp[-1].expr)->children[i]);free((yyvsp[-1].expr)->children);free((yyvsp[-1].expr));(yyval.expr)=e; }
#line 1831 "snobol4.tab.c"
    break;

  case 93: /* expr17: T_IDENT  */
#line 186 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1837 "snobol4.tab.c"
    break;

  case 94: /* expr17: T_END  */
#line 187 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_VAR);    e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1843 "snobol4.tab.c"
    break;

  case 95: /* expr17: T_KEYWORD  */
#line 188 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_KEYWORD);e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1849 "snobol4.tab.c"
    break;

  case 96: /* expr17: T_STR  */
#line 189 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_QLIT);   e->sval=(char*)(yyvsp[0].tok).sval;(yyval.expr)=e; }
#line 1855 "snobol4.tab.c"
    break;

  case 97: /* expr17: T_INT  */
#line 190 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_ILIT);   e->ival=(yyvsp[0].tok).ival;(yyval.expr)=e; }
#line 1861 "snobol4.tab.c"
    break;

  case 98: /* expr17: T_REAL  */
#line 191 "snobol4.y"
                                                                                                  { EXPR_t*e=expr_new(E_FLIT);   e->dval=(yyvsp[0].tok).dval;(yyval.expr)=e; }
#line 1867 "snobol4.tab.c"
    break;


#line 1871 "snobol4.tab.c"

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

#line 193 "snobol4.y"

int snobol4_lex(YYSTYPE *yylval_param, void *yyparse_param) {
    (void)yyparse_param; Token t=lex_next(g_lx); yylval_param->tok=t;
    if (getenv("SNO_TOK_TRACE"))
        fprintf(stderr,"[TOK %d sval=%s ival=%ld]\n",t.kind,t.sval?t.sval:"",t.ival);
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
static void sno4_stmt_commit_go(void*,Token,EXPR_t*,EXPR_t*,int,EXPR_t*,SnoGoto*);
/* DYN-59: sno4_stmt_commit_go — takes SnoGoto* directly from grammar.
 * Replaces sno4_stmt_commit + goto_field()/goto_label() re-lexing. */
static void sno4_stmt_commit_go(void *param,Token lbl,EXPR_t *subj,EXPR_t *pat,int has_eq,EXPR_t *repl,SnoGoto *go){
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
        if(first->kind==E_VAR || first->kind==E_KEYWORD || first->kind==E_QLIT) {
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
    s->go=go;
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

/* parse_expr_pat_from_str — parse a bare expression string using the bison
 * parser by wrapping it as a pattern slot: "_SNO4PAT_ src"
 * The bison grammar parses the RHS as a pattern EXPR_t in pattern context.
 * Returns EXPR_t* (shared IR) or NULL on parse failure.
 * Used by _eval_str_impl_fn in scrip.c to replace the CMPILE path. */
EXPR_t *parse_expr_pat_from_str(const char *src) {
    if (!src || !*src) return NULL;
    /* Wrap as "_SNO_DUMMY_ <src>" — bison sees a subject (_SNO_DUMMY_)
     * followed by a pattern expression (src), placing src in s->pattern.
     * This is the only way to get the bison parser to parse a bare
     * expression string; parse_expr_from_str returns subject only and
     * fails on standalone pattern expressions. */
    char buf[8192];
    int n = snprintf(buf, sizeof buf, "_SNO_DUMMY_ %s", src);
    if (n <= 0 || n >= (int)sizeof buf) return NULL;
    Lex lx = {0};
    lex_open_str(&lx, buf, n, 0);
    Program *prog = calloc(1, sizeof *prog);
    PP p = {prog, NULL};
    g_lx = &lx;
    snobol4_parse(&p);
    if (!prog->head) return NULL;
    STMT_t *s = prog->head;
    /* Pattern slot has our expression; subject is the dummy var */
    if (s->pattern) return s->pattern;
    return NULL;
}
