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
#line 20 "raku.y"

#include "raku_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  raku_yylex(void);
extern int  raku_get_lineno(void);
void raku_yyerror(const char *msg) {
    fprintf(stderr, "raku parse error line %d: %s\n", raku_get_lineno(), msg);
}

/* Root of the parsed program — filled by the start rule */
RakuNode *raku_parse_result = NULL;

#line 95 "raku.tab.c"

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
  YYSYMBOL_OP_RANGE = 26,                  /* OP_RANGE  */
  YYSYMBOL_OP_RANGE_EX = 27,               /* OP_RANGE_EX  */
  YYSYMBOL_OP_ARROW = 28,                  /* OP_ARROW  */
  YYSYMBOL_OP_EQ = 29,                     /* OP_EQ  */
  YYSYMBOL_OP_NE = 30,                     /* OP_NE  */
  YYSYMBOL_OP_LE = 31,                     /* OP_LE  */
  YYSYMBOL_OP_GE = 32,                     /* OP_GE  */
  YYSYMBOL_OP_SEQ = 33,                    /* OP_SEQ  */
  YYSYMBOL_OP_SNE = 34,                    /* OP_SNE  */
  YYSYMBOL_OP_AND = 35,                    /* OP_AND  */
  YYSYMBOL_OP_OR = 36,                     /* OP_OR  */
  YYSYMBOL_OP_BIND = 37,                   /* OP_BIND  */
  YYSYMBOL_OP_DIV = 38,                    /* OP_DIV  */
  YYSYMBOL_39_ = 39,                       /* '='  */
  YYSYMBOL_40_ = 40,                       /* '!'  */
  YYSYMBOL_41_ = 41,                       /* '<'  */
  YYSYMBOL_42_ = 42,                       /* '>'  */
  YYSYMBOL_43_ = 43,                       /* '~'  */
  YYSYMBOL_44_ = 44,                       /* '+'  */
  YYSYMBOL_45_ = 45,                       /* '-'  */
  YYSYMBOL_46_ = 46,                       /* '*'  */
  YYSYMBOL_47_ = 47,                       /* '/'  */
  YYSYMBOL_48_ = 48,                       /* '%'  */
  YYSYMBOL_UMINUS = 49,                    /* UMINUS  */
  YYSYMBOL_50_ = 50,                       /* ';'  */
  YYSYMBOL_51_ = 51,                       /* '['  */
  YYSYMBOL_52_ = 52,                       /* ']'  */
  YYSYMBOL_53_ = 53,                       /* '{'  */
  YYSYMBOL_54_ = 54,                       /* '}'  */
  YYSYMBOL_55_ = 55,                       /* '('  */
  YYSYMBOL_56_ = 56,                       /* ')'  */
  YYSYMBOL_57_ = 57,                       /* ','  */
  YYSYMBOL_YYACCEPT = 58,                  /* $accept  */
  YYSYMBOL_program = 59,                   /* program  */
  YYSYMBOL_stmt_list = 60,                 /* stmt_list  */
  YYSYMBOL_stmt = 61,                      /* stmt  */
  YYSYMBOL_if_stmt = 62,                   /* if_stmt  */
  YYSYMBOL_while_stmt = 63,                /* while_stmt  */
  YYSYMBOL_for_stmt = 64,                  /* for_stmt  */
  YYSYMBOL_given_stmt = 65,                /* given_stmt  */
  YYSYMBOL_when_list = 66,                 /* when_list  */
  YYSYMBOL_sub_decl = 67,                  /* sub_decl  */
  YYSYMBOL_param_list = 68,                /* param_list  */
  YYSYMBOL_block = 69,                     /* block  */
  YYSYMBOL_expr = 70,                      /* expr  */
  YYSYMBOL_cmp_expr = 71,                  /* cmp_expr  */
  YYSYMBOL_range_expr = 72,                /* range_expr  */
  YYSYMBOL_add_expr = 73,                  /* add_expr  */
  YYSYMBOL_mul_expr = 74,                  /* mul_expr  */
  YYSYMBOL_unary_expr = 75,                /* unary_expr  */
  YYSYMBOL_postfix_expr = 76,              /* postfix_expr  */
  YYSYMBOL_call_expr = 77,                 /* call_expr  */
  YYSYMBOL_arg_list = 78,                  /* arg_list  */
  YYSYMBOL_atom = 79                       /* atom  */
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
#define YYLAST   289

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  58
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  22
/* YYNRULES -- Number of rules.  */
#define YYNRULES  83
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  188

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   294


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
       2,     2,     2,    40,     2,     2,     2,    48,     2,     2,
      55,    56,    46,    44,    57,    45,     2,    47,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    50,
      41,    39,    42,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    51,     2,    52,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    53,     2,    54,    43,     2,     2,     2,
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
      35,    36,    37,    38,    49
};

#if RAKU_YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    84,    84,    89,    90,    95,    97,    99,   101,   103,
     105,   107,   109,   111,   113,   115,   118,   120,   122,   123,
     124,   125,   126,   131,   133,   135,   140,   147,   149,   156,
     158,   163,   164,   169,   171,   176,   178,   184,   190,   191,
     192,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   210,   211,   212,   216,   217,   218,   219,   223,
     224,   225,   226,   227,   231,   232,   233,   237,   241,   242,
     243,   247,   248,   253,   254,   255,   256,   257,   258,   259,
     260,   262,   263,   264
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
  "KW_GIVEN", "KW_WHEN", "KW_DEFAULT", "OP_RANGE", "OP_RANGE_EX",
  "OP_ARROW", "OP_EQ", "OP_NE", "OP_LE", "OP_GE", "OP_SEQ", "OP_SNE",
  "OP_AND", "OP_OR", "OP_BIND", "OP_DIV", "'='", "'!'", "'<'", "'>'",
  "'~'", "'+'", "'-'", "'*'", "'/'", "'%'", "UMINUS", "';'", "'['", "']'",
  "'{'", "'}'", "'('", "')'", "','", "$accept", "program", "stmt_list",
  "stmt", "if_stmt", "while_stmt", "for_stmt", "given_stmt", "when_list",
  "sub_decl", "param_list", "block", "expr", "cmp_expr", "range_expr",
  "add_expr", "mul_expr", "unary_expr", "postfix_expr", "call_expr",
  "arg_list", "atom", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-54)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -54,    30,   194,   -54,   -54,   -54,   -54,   -54,     0,    -7,
     -26,     4,    45,   215,   215,    25,    26,   215,    38,    -3,
     215,    69,   215,   125,   125,   215,   -54,   -54,   -54,   -54,
     -54,   -54,   -10,   -19,   -54,   232,    -1,   -54,   -54,   -54,
     -54,   215,   215,    77,   215,    15,    51,    52,    53,    57,
      47,   -15,    49,    50,   215,   215,   -17,    46,   -54,   -54,
      54,   -54,    55,    58,   -54,   -54,   -54,    56,   -54,   125,
     125,   125,   125,   125,   125,   125,   125,   125,   125,   125,
     125,   125,   125,   125,   125,   125,   125,   125,    60,    61,
      64,    48,   -54,   -54,   -23,   215,   215,   215,   215,   215,
      93,   215,   -54,   -54,    65,    66,   100,   -54,    -5,   141,
     -54,   -54,   -54,   -54,    13,    13,    13,    13,    13,    13,
      13,    13,    13,    13,    13,    13,    -1,    -1,    -1,   -54,
     -54,   -54,   -54,   -54,    78,    81,    88,   -54,   215,    86,
      87,    89,   -54,   104,    74,    84,    -3,    -3,    -3,   -54,
      -3,     8,   -54,   -11,   215,   215,   215,   -54,   -54,   -54,
     -54,   -54,   -54,   -54,   151,   -54,   -54,   -54,    -3,   101,
     215,    -3,   -54,    90,   117,   118,    -4,   -54,   -54,    -3,
     115,   -54,   -54,   -54,   -54,   -54,   -54,   -54
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     2,     1,    73,    74,    75,    76,    77,    78,
       0,    82,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     4,    18,    19,    20,
      21,    22,     0,    40,    51,    54,    58,    63,    66,    67,
      70,     0,     0,     0,     0,     0,     0,     0,     0,    77,
      78,     0,     0,     0,     0,     0,     0,     0,     3,    39,
       0,    12,     0,     0,    77,    65,    64,     0,    17,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    69,    71,     0,     0,     0,     0,     0,     0,
       0,     0,     8,     9,     0,     0,     0,    28,     0,     0,
      10,    11,    31,    83,    41,    42,    52,    53,    43,    44,
      47,    48,    49,    50,    45,    46,    57,    55,    56,    62,
      59,    60,    61,    13,    79,    80,    81,    68,     0,     0,
       0,     0,    38,     0,     0,     0,     0,     0,     0,    35,
       0,     0,    37,     0,     0,     0,     0,    72,     5,     6,
       7,    79,    80,    81,    23,    26,    27,    34,     0,     0,
       0,     0,    29,     0,     0,     0,     0,    33,    36,     0,
       0,    14,    15,    16,    25,    24,    32,    30
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -54,   -54,   113,   -54,    -2,   -54,   -54,   -54,   -54,   -54,
     -54,   -53,   -13,   -54,   -54,   209,   -20,   -18,   -54,   -54,
     -54,   -54
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,    26,    27,    28,    29,    30,   153,    31,
     151,    59,    32,    33,    34,    35,    36,    37,    38,    39,
      94,    40
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      52,    53,   149,   107,    56,    65,    66,    60,    62,    63,
      15,   106,    67,   170,   171,    43,    69,    70,     4,     5,
       6,     7,    49,    50,    51,    11,   100,    44,    88,    89,
       3,    91,    93,   137,   138,    19,    58,    84,   101,    41,
      68,   104,   105,   172,    42,    85,    86,    87,    57,    58,
      58,   150,    46,    47,    48,    23,    81,    82,    83,    45,
      24,   126,   127,   128,   168,   169,   129,   130,   131,   132,
      25,    92,     4,     5,     6,     7,    49,    50,    51,    11,
      54,    55,   139,   140,   141,   142,   143,    90,   145,    19,
      95,    96,    97,   164,   165,   166,    98,   167,    99,   102,
     103,   108,   136,   144,   110,   111,   135,   148,   178,    23,
     133,   112,   113,   134,    24,   177,   162,   154,   180,    61,
     155,   146,   147,   185,    25,   157,   186,   156,     4,     5,
       6,     7,    64,    50,    51,    11,   158,   159,   163,   160,
     181,   173,   174,   175,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,   161,   179,    16,    17,
      18,    19,    20,    21,    22,    23,   176,   182,   183,   187,
      24,   109,     0,     0,   184,     0,     0,     0,     0,     0,
      25,    23,     0,     0,     0,     0,    24,     0,     0,     0,
       0,     0,     0,     0,     0,   152,    25,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,     0,
       0,    16,    17,    18,    19,    20,    21,    22,     4,     5,
       6,     7,    49,    50,    51,    11,     0,     0,     0,     0,
       0,     0,     0,     0,    23,    19,     0,     0,     0,    24,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    25,
       0,     0,     0,     0,     0,    23,     0,     0,    71,    72,
      24,    73,    74,    75,    76,    77,    78,     0,     0,     0,
      25,     0,     0,    79,    80,    81,    82,    83,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125
};

static const yytype_int16 yycheck[] =
{
      13,    14,     7,    56,    17,    23,    24,    20,    21,    22,
      14,    28,    25,    24,    25,    41,    35,    36,     3,     4,
       5,     6,     7,     8,     9,    10,    41,    53,    41,    42,
       0,    44,    45,    56,    57,    20,    53,    38,    53,    39,
      50,    54,    55,    54,    51,    46,    47,    48,    10,    53,
      53,    56,     7,     8,     9,    40,    43,    44,    45,    55,
      45,    81,    82,    83,    56,    57,    84,    85,    86,    87,
      55,    56,     3,     4,     5,     6,     7,     8,     9,    10,
      55,    55,    95,    96,    97,    98,    99,    10,   101,    20,
      39,    39,    39,   146,   147,   148,    39,   150,    51,    50,
      50,    55,    54,    10,    50,    50,    42,     7,     7,    40,
      50,    53,    56,    52,    45,   168,    42,    39,   171,    50,
      39,    56,    56,   176,    55,   138,   179,    39,     3,     4,
       5,     6,     7,     8,     9,    10,    50,    50,    54,    50,
      50,   154,   155,   156,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    52,   170,    17,    18,
      19,    20,    21,    22,    23,    40,    15,    50,    50,    54,
      45,    58,    -1,    -1,   176,    -1,    -1,    -1,    -1,    -1,
      55,    40,    -1,    -1,    -1,    -1,    45,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    54,    55,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    -1,
      -1,    17,    18,    19,    20,    21,    22,    23,     3,     4,
       5,     6,     7,     8,     9,    10,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    40,    20,    -1,    -1,    -1,    45,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      -1,    -1,    -1,    -1,    -1,    40,    -1,    -1,    26,    27,
      45,    29,    30,    31,    32,    33,    34,    -1,    -1,    -1,
      55,    -1,    -1,    41,    42,    43,    44,    45,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    59,    60,     0,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    17,    18,    19,    20,
      21,    22,    23,    40,    45,    55,    61,    62,    63,    64,
      65,    67,    70,    71,    72,    73,    74,    75,    76,    77,
      79,    39,    51,    41,    53,    55,     7,     8,     9,     7,
       8,     9,    70,    70,    55,    55,    70,    10,    53,    69,
      70,    50,    70,    70,     7,    75,    75,    70,    50,    35,
      36,    26,    27,    29,    30,    31,    32,    33,    34,    41,
      42,    43,    44,    45,    38,    46,    47,    48,    70,    70,
      10,    70,    56,    70,    78,    39,    39,    39,    39,    51,
      41,    53,    50,    50,    70,    70,    28,    69,    55,    60,
      50,    50,    53,    56,    73,    73,    73,    73,    73,    73,
      73,    73,    73,    73,    73,    73,    74,    74,    74,    75,
      75,    75,    75,    50,    52,    42,    54,    56,    57,    70,
      70,    70,    70,    70,    10,    70,    56,    56,     7,     7,
      56,    68,    54,    66,    39,    39,    39,    70,    50,    50,
      50,    52,    42,    54,    69,    69,    69,    69,    56,    57,
      24,    25,    54,    70,    70,    70,    15,    69,     7,    70,
      69,    50,    50,    50,    62,    69,    69,    54
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    58,    59,    60,    60,    61,    61,    61,    61,    61,
      61,    61,    61,    61,    61,    61,    61,    61,    61,    61,
      61,    61,    61,    62,    62,    62,    63,    64,    64,    65,
      65,    66,    66,    67,    67,    68,    68,    69,    70,    70,
      70,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    72,    72,    72,    73,    73,    73,    73,    74,
      74,    74,    74,    74,    75,    75,    75,    76,    77,    77,
      77,    78,    78,    79,    79,    79,    79,    79,    79,    79,
      79,    79,    79,    79
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     5,     5,     5,     3,     3,
       3,     3,     2,     4,     7,     7,     7,     2,     1,     1,
       1,     1,     1,     5,     7,     7,     5,     5,     3,     5,
       7,     0,     4,     6,     5,     1,     3,     3,     3,     2,
       1,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     1,     3,     3,     1,     3,     3,     3,     1,     3,
       3,     3,     3,     1,     2,     2,     1,     1,     4,     3,
       1,     1,     3,     1,     1,     1,     1,     1,     1,     4,
       4,     4,     1,     3
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
#line 85 "raku.y"
        { raku_parse_result = raku_node_block((yyvsp[0].list), raku_get_lineno()); }
#line 1300 "raku.tab.c"
    break;

  case 3: /* stmt_list: %empty  */
#line 89 "raku.y"
                           { (yyval.list) = raku_list_new(); }
#line 1306 "raku.tab.c"
    break;

  case 4: /* stmt_list: stmt_list stmt  */
#line 90 "raku.y"
                           { (yyval.list) = raku_list_append((yyvsp[-1].list), (yyvsp[0].node)); }
#line 1312 "raku.tab.c"
    break;

  case 5: /* stmt: KW_MY VAR_SCALAR '=' expr ';'  */
#line 96 "raku.y"
        { (yyval.node) = raku_node_my_scalar((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1318 "raku.tab.c"
    break;

  case 6: /* stmt: KW_MY VAR_ARRAY '=' expr ';'  */
#line 98 "raku.y"
        { (yyval.node) = raku_node_my_array((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1324 "raku.tab.c"
    break;

  case 7: /* stmt: KW_MY VAR_HASH '=' expr ';'  */
#line 100 "raku.y"
        { (yyval.node) = raku_node_my_array((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1330 "raku.tab.c"
    break;

  case 8: /* stmt: KW_SAY expr ';'  */
#line 102 "raku.y"
        { (yyval.node) = raku_node_say((yyvsp[-1].node), raku_get_lineno()); }
#line 1336 "raku.tab.c"
    break;

  case 9: /* stmt: KW_PRINT expr ';'  */
#line 104 "raku.y"
        { (yyval.node) = raku_node_print((yyvsp[-1].node), raku_get_lineno()); }
#line 1342 "raku.tab.c"
    break;

  case 10: /* stmt: KW_TAKE expr ';'  */
#line 106 "raku.y"
        { (yyval.node) = raku_node_take((yyvsp[-1].node), raku_get_lineno()); }
#line 1348 "raku.tab.c"
    break;

  case 11: /* stmt: KW_RETURN expr ';'  */
#line 108 "raku.y"
        { (yyval.node) = raku_node_return((yyvsp[-1].node), raku_get_lineno()); }
#line 1354 "raku.tab.c"
    break;

  case 12: /* stmt: KW_RETURN ';'  */
#line 110 "raku.y"
        { (yyval.node) = raku_node_return(NULL, raku_get_lineno()); }
#line 1360 "raku.tab.c"
    break;

  case 13: /* stmt: VAR_SCALAR '=' expr ';'  */
#line 112 "raku.y"
        { (yyval.node) = raku_node_assign((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1366 "raku.tab.c"
    break;

  case 14: /* stmt: VAR_ARRAY '[' expr ']' '=' expr ';'  */
#line 114 "raku.y"
        { (yyval.node) = raku_node_arr_set((yyvsp[-6].sval), (yyvsp[-4].node), (yyvsp[-1].node), raku_get_lineno()); }
#line 1372 "raku.tab.c"
    break;

  case 15: /* stmt: VAR_HASH '<' IDENT '>' '=' expr ';'  */
#line 116 "raku.y"
        { RakuNode *k = raku_node_str((yyvsp[-4].sval), raku_get_lineno());
          (yyval.node) = raku_node_hash_set((yyvsp[-6].sval), k, (yyvsp[-1].node), raku_get_lineno()); }
#line 1379 "raku.tab.c"
    break;

  case 16: /* stmt: VAR_HASH '{' expr '}' '=' expr ';'  */
#line 119 "raku.y"
        { (yyval.node) = raku_node_hash_set((yyvsp[-6].sval), (yyvsp[-4].node), (yyvsp[-1].node), raku_get_lineno()); }
#line 1385 "raku.tab.c"
    break;

  case 17: /* stmt: expr ';'  */
#line 121 "raku.y"
        { (yyval.node) = raku_node_expr_stmt((yyvsp[-1].node), raku_get_lineno()); }
#line 1391 "raku.tab.c"
    break;

  case 18: /* stmt: if_stmt  */
#line 122 "raku.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1397 "raku.tab.c"
    break;

  case 19: /* stmt: while_stmt  */
#line 123 "raku.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1403 "raku.tab.c"
    break;

  case 20: /* stmt: for_stmt  */
#line 124 "raku.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1409 "raku.tab.c"
    break;

  case 21: /* stmt: given_stmt  */
#line 125 "raku.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1415 "raku.tab.c"
    break;

  case 22: /* stmt: sub_decl  */
#line 126 "raku.y"
                       { (yyval.node) = (yyvsp[0].node); }
#line 1421 "raku.tab.c"
    break;

  case 23: /* if_stmt: KW_IF '(' expr ')' block  */
#line 132 "raku.y"
        { (yyval.node) = raku_node_if((yyvsp[-2].node), (yyvsp[0].node), NULL, raku_get_lineno()); }
#line 1427 "raku.tab.c"
    break;

  case 24: /* if_stmt: KW_IF '(' expr ')' block KW_ELSE block  */
#line 134 "raku.y"
        { (yyval.node) = raku_node_if((yyvsp[-4].node), (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1433 "raku.tab.c"
    break;

  case 25: /* if_stmt: KW_IF '(' expr ')' block KW_ELSE if_stmt  */
#line 136 "raku.y"
        { (yyval.node) = raku_node_if((yyvsp[-4].node), (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1439 "raku.tab.c"
    break;

  case 26: /* while_stmt: KW_WHILE '(' expr ')' block  */
#line 141 "raku.y"
        { (yyval.node) = raku_node_while((yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1445 "raku.tab.c"
    break;

  case 27: /* for_stmt: KW_FOR expr OP_ARROW VAR_SCALAR block  */
#line 148 "raku.y"
        { (yyval.node) = raku_node_for((yyvsp[-3].node), (yyvsp[-1].sval), (yyvsp[0].node), raku_get_lineno()); }
#line 1451 "raku.tab.c"
    break;

  case 28: /* for_stmt: KW_FOR expr block  */
#line 150 "raku.y"
        { (yyval.node) = raku_node_for((yyvsp[-1].node), NULL, (yyvsp[0].node), raku_get_lineno()); }
#line 1457 "raku.tab.c"
    break;

  case 29: /* given_stmt: KW_GIVEN expr '{' when_list '}'  */
#line 157 "raku.y"
        { (yyval.node) = raku_node_given((yyvsp[-3].node), (yyvsp[-1].list), NULL, raku_get_lineno()); }
#line 1463 "raku.tab.c"
    break;

  case 30: /* given_stmt: KW_GIVEN expr '{' when_list KW_DEFAULT block '}'  */
#line 159 "raku.y"
        { (yyval.node) = raku_node_given((yyvsp[-5].node), (yyvsp[-3].list), (yyvsp[-1].node),   raku_get_lineno()); }
#line 1469 "raku.tab.c"
    break;

  case 31: /* when_list: %empty  */
#line 163 "raku.y"
                                { (yyval.list) = raku_list_new(); }
#line 1475 "raku.tab.c"
    break;

  case 32: /* when_list: when_list KW_WHEN expr block  */
#line 165 "raku.y"
        { (yyval.list) = raku_list_append((yyvsp[-3].list), raku_node_when((yyvsp[-1].node), (yyvsp[0].node), raku_get_lineno())); }
#line 1481 "raku.tab.c"
    break;

  case 33: /* sub_decl: KW_SUB IDENT '(' param_list ')' block  */
#line 170 "raku.y"
        { (yyval.node) = raku_node_sub((yyvsp[-4].sval), (yyvsp[-2].list), (yyvsp[0].node), raku_get_lineno()); }
#line 1487 "raku.tab.c"
    break;

  case 34: /* sub_decl: KW_SUB IDENT '(' ')' block  */
#line 172 "raku.y"
        { (yyval.node) = raku_node_sub((yyvsp[-3].sval), raku_list_new(), (yyvsp[0].node), raku_get_lineno()); }
#line 1493 "raku.tab.c"
    break;

  case 35: /* param_list: VAR_SCALAR  */
#line 176 "raku.y"
                                   { (yyval.list) = raku_list_append(raku_list_new(),
                                         raku_node_var_scalar((yyvsp[0].sval), raku_get_lineno())); }
#line 1500 "raku.tab.c"
    break;

  case 36: /* param_list: param_list ',' VAR_SCALAR  */
#line 178 "raku.y"
                                   { (yyval.list) = raku_list_append((yyvsp[-2].list),
                                         raku_node_var_scalar((yyvsp[0].sval), raku_get_lineno())); }
#line 1507 "raku.tab.c"
    break;

  case 37: /* block: '{' stmt_list '}'  */
#line 185 "raku.y"
        { (yyval.node) = raku_node_block((yyvsp[-1].list), raku_get_lineno()); }
#line 1513 "raku.tab.c"
    break;

  case 38: /* expr: VAR_SCALAR '=' expr  */
#line 190 "raku.y"
                                   { (yyval.node) = raku_node_assign((yyvsp[-2].sval), (yyvsp[0].node), raku_get_lineno()); }
#line 1519 "raku.tab.c"
    break;

  case 39: /* expr: KW_GATHER block  */
#line 191 "raku.y"
                                   { (yyval.node) = raku_node_gather((yyvsp[0].node), raku_get_lineno()); }
#line 1525 "raku.tab.c"
    break;

  case 40: /* expr: cmp_expr  */
#line 192 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1531 "raku.tab.c"
    break;

  case 41: /* cmp_expr: cmp_expr OP_AND add_expr  */
#line 196 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_AND, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1537 "raku.tab.c"
    break;

  case 42: /* cmp_expr: cmp_expr OP_OR add_expr  */
#line 197 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_OR,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1543 "raku.tab.c"
    break;

  case 43: /* cmp_expr: add_expr OP_EQ add_expr  */
#line 198 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_EQ,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1549 "raku.tab.c"
    break;

  case 44: /* cmp_expr: add_expr OP_NE add_expr  */
#line 199 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_NE,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1555 "raku.tab.c"
    break;

  case 45: /* cmp_expr: add_expr '<' add_expr  */
#line 200 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_LT,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1561 "raku.tab.c"
    break;

  case 46: /* cmp_expr: add_expr '>' add_expr  */
#line 201 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_GT,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1567 "raku.tab.c"
    break;

  case 47: /* cmp_expr: add_expr OP_LE add_expr  */
#line 202 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_LE,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1573 "raku.tab.c"
    break;

  case 48: /* cmp_expr: add_expr OP_GE add_expr  */
#line 203 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_GE,  (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1579 "raku.tab.c"
    break;

  case 49: /* cmp_expr: add_expr OP_SEQ add_expr  */
#line 204 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_SEQ, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1585 "raku.tab.c"
    break;

  case 50: /* cmp_expr: add_expr OP_SNE add_expr  */
#line 205 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_SNE, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1591 "raku.tab.c"
    break;

  case 51: /* cmp_expr: range_expr  */
#line 206 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1597 "raku.tab.c"
    break;

  case 52: /* range_expr: add_expr OP_RANGE add_expr  */
#line 210 "raku.y"
                                    { (yyval.node) = raku_node_binop(RK_RANGE,    (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1603 "raku.tab.c"
    break;

  case 53: /* range_expr: add_expr OP_RANGE_EX add_expr  */
#line 211 "raku.y"
                                    { (yyval.node) = raku_node_binop(RK_RANGE_EX, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1609 "raku.tab.c"
    break;

  case 54: /* range_expr: add_expr  */
#line 212 "raku.y"
                                    { (yyval.node) = (yyvsp[0].node); }
#line 1615 "raku.tab.c"
    break;

  case 55: /* add_expr: add_expr '+' mul_expr  */
#line 216 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_ADD,    (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1621 "raku.tab.c"
    break;

  case 56: /* add_expr: add_expr '-' mul_expr  */
#line 217 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_SUBTRACT,    (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1627 "raku.tab.c"
    break;

  case 57: /* add_expr: add_expr '~' mul_expr  */
#line 218 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_STRCAT, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1633 "raku.tab.c"
    break;

  case 58: /* add_expr: mul_expr  */
#line 219 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1639 "raku.tab.c"
    break;

  case 59: /* mul_expr: mul_expr '*' unary_expr  */
#line 223 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_MUL, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1645 "raku.tab.c"
    break;

  case 60: /* mul_expr: mul_expr '/' unary_expr  */
#line 224 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_DIV, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1651 "raku.tab.c"
    break;

  case 61: /* mul_expr: mul_expr '%' unary_expr  */
#line 225 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_MOD, (yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1657 "raku.tab.c"
    break;

  case 62: /* mul_expr: mul_expr OP_DIV unary_expr  */
#line 226 "raku.y"
                                   { (yyval.node) = raku_node_binop(RK_IDIV,(yyvsp[-2].node), (yyvsp[0].node), raku_get_lineno()); }
#line 1663 "raku.tab.c"
    break;

  case 63: /* mul_expr: unary_expr  */
#line 227 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1669 "raku.tab.c"
    break;

  case 64: /* unary_expr: '-' unary_expr  */
#line 231 "raku.y"
                                   { (yyval.node) = raku_node_unop(RK_NEG, (yyvsp[0].node), raku_get_lineno()); }
#line 1675 "raku.tab.c"
    break;

  case 65: /* unary_expr: '!' unary_expr  */
#line 232 "raku.y"
                                   { (yyval.node) = raku_node_unop(RK_NOT, (yyvsp[0].node), raku_get_lineno()); }
#line 1681 "raku.tab.c"
    break;

  case 66: /* unary_expr: postfix_expr  */
#line 233 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1687 "raku.tab.c"
    break;

  case 67: /* postfix_expr: call_expr  */
#line 237 "raku.y"
                                   { (yyval.node) = (yyvsp[0].node); }
#line 1693 "raku.tab.c"
    break;

  case 68: /* call_expr: IDENT '(' arg_list ')'  */
#line 241 "raku.y"
                                  { (yyval.node) = raku_node_call((yyvsp[-3].sval), (yyvsp[-1].list), raku_get_lineno()); }
#line 1699 "raku.tab.c"
    break;

  case 69: /* call_expr: IDENT '(' ')'  */
#line 242 "raku.y"
                                  { (yyval.node) = raku_node_call((yyvsp[-2].sval), raku_list_new(), raku_get_lineno()); }
#line 1705 "raku.tab.c"
    break;

  case 70: /* call_expr: atom  */
#line 243 "raku.y"
                                  { (yyval.node) = (yyvsp[0].node); }
#line 1711 "raku.tab.c"
    break;

  case 71: /* arg_list: expr  */
#line 247 "raku.y"
                                  { (yyval.list) = raku_list_append(raku_list_new(), (yyvsp[0].node)); }
#line 1717 "raku.tab.c"
    break;

  case 72: /* arg_list: arg_list ',' expr  */
#line 248 "raku.y"
                                  { (yyval.list) = raku_list_append((yyvsp[-2].list), (yyvsp[0].node)); }
#line 1723 "raku.tab.c"
    break;

  case 73: /* atom: LIT_INT  */
#line 253 "raku.y"
                                  { (yyval.node) = raku_node_int((yyvsp[0].ival),  raku_get_lineno()); }
#line 1729 "raku.tab.c"
    break;

  case 74: /* atom: LIT_FLOAT  */
#line 254 "raku.y"
                                  { (yyval.node) = raku_node_float((yyvsp[0].dval), raku_get_lineno()); }
#line 1735 "raku.tab.c"
    break;

  case 75: /* atom: LIT_STR  */
#line 255 "raku.y"
                                  { (yyval.node) = raku_node_str((yyvsp[0].sval),  raku_get_lineno()); }
#line 1741 "raku.tab.c"
    break;

  case 76: /* atom: LIT_INTERP_STR  */
#line 256 "raku.y"
                                  { (yyval.node) = raku_node_interp_str((yyvsp[0].sval), raku_get_lineno()); }
#line 1747 "raku.tab.c"
    break;

  case 77: /* atom: VAR_SCALAR  */
#line 257 "raku.y"
                                  { (yyval.node) = raku_node_var_scalar((yyvsp[0].sval), raku_get_lineno()); }
#line 1753 "raku.tab.c"
    break;

  case 78: /* atom: VAR_ARRAY  */
#line 258 "raku.y"
                                  { (yyval.node) = raku_node_var_array((yyvsp[0].sval),  raku_get_lineno()); }
#line 1759 "raku.tab.c"
    break;

  case 79: /* atom: VAR_ARRAY '[' expr ']'  */
#line 259 "raku.y"
                                  { (yyval.node) = raku_node_arr_get((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1765 "raku.tab.c"
    break;

  case 80: /* atom: VAR_HASH '<' IDENT '>'  */
#line 260 "raku.y"
                                  { RakuNode *k = raku_node_str((yyvsp[-1].sval), raku_get_lineno());
                                    (yyval.node) = raku_node_hash_get((yyvsp[-3].sval), k, raku_get_lineno()); }
#line 1772 "raku.tab.c"
    break;

  case 81: /* atom: VAR_HASH '{' expr '}'  */
#line 262 "raku.y"
                                  { (yyval.node) = raku_node_hash_get((yyvsp[-3].sval), (yyvsp[-1].node), raku_get_lineno()); }
#line 1778 "raku.tab.c"
    break;

  case 82: /* atom: IDENT  */
#line 263 "raku.y"
                                  { (yyval.node) = raku_node_ident((yyvsp[0].sval), raku_get_lineno()); }
#line 1784 "raku.tab.c"
    break;

  case 83: /* atom: '(' expr ')'  */
#line 264 "raku.y"
                                  { (yyval.node) = (yyvsp[-1].node); }
#line 1790 "raku.tab.c"
    break;


#line 1794 "raku.tab.c"

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

#line 267 "raku.y"

