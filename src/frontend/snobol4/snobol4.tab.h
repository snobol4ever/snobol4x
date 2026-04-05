/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_SNOBOL4_SNOBOL4_TAB_H_INCLUDED
# define YY_SNOBOL4_SNOBOL4_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef SNOBOL4_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define SNOBOL4_DEBUG 1
#  else
#   define SNOBOL4_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define SNOBOL4_DEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined SNOBOL4_DEBUG */
#if SNOBOL4_DEBUG
extern int snobol4_debug;
#endif
/* "%code requires" blocks.  */
#line 1 "snobol4.y"

#include "scrip_cc.h"
#include "snobol4.h"

#line 62 "snobol4.tab.h"

/* Token kinds.  */
#ifndef SNOBOL4_TOKENTYPE
# define SNOBOL4_TOKENTYPE
  enum snobol4_tokentype
  {
    SNOBOL4_EMPTY = -2,
    SNOBOL4_EOF = 0,               /* "end of file"  */
    SNOBOL4_error = 256,           /* error  */
    SNOBOL4_UNDEF = 257,           /* "invalid token"  */
    VARTYP = 258,                 /* VARTYP  */
    FNCTYP = 259,              /* FNCTYP  */
    T_KEYWORD = 260,               /* T_KEYWORD  */
    T_END = 261,                   /* T_END  */
    ILITYP = 262,                   /* ILITYP  */
    FLITYP = 263,                  /* FLITYP  */
    QLITYP = 264,                   /* QLITYP  */
    T_LABEL = 265,                 /* T_LABEL  */
    SGOTYP = 266,                /* SGOTYP  */
    FGOTYP = 267,                /* FGOTYP  */
    T_GOTO_LPAREN = 268,           /* T_GOTO_LPAREN  */
    T_GOTO_RPAREN = 269,           /* T_GOTO_RPAREN  */
    EOSTYP = 270,              /* EOSTYP  */
    EQTYP = 271,            /* EQTYP  */
    BIQSFN = 272,                 /* BIQSFN  */
    ORFN = 273,           /* ORFN  */
    ADDFN = 274,              /* ADDFN  */
    SUBFN = 275,           /* SUBFN  */
    MPYFN = 276,        /* MPYFN  */
    DIVFN = 277,              /* DIVFN  */
    EXPFN = 278,        /* EXPFN  */
    DOLFN = 279,      /* DOLFN  */
    NAMFN = 280,           /* NAMFN  */
    T_AMPERSAND = 281,             /* T_AMPERSAND  */
    T_AT_SIGN = 282,               /* T_AT_SIGN  */
    T_POUND = 283,                 /* T_POUND  */
    T_PERCENT = 284,               /* T_PERCENT  */
    T_TILDE = 285,                 /* T_TILDE  */
    ATFN = 286,            /* ATFN  */
    T_UN_TILDE = 287,              /* T_UN_TILDE  */
    T_UN_QUESTION_MARK = 288,      /* T_UN_QUESTION_MARK  */
    T_UN_AMPERSAND = 289,          /* T_UN_AMPERSAND  */
    PLSFN = 290,               /* PLSFN  */
    MNSFN = 291,              /* MNSFN  */
    STRFN = 292,           /* STRFN  */
    INDFN = 293,        /* INDFN  */
    DOTFN = 294,             /* DOTFN  */
    T_UN_EXCLAMATION = 295,        /* T_UN_EXCLAMATION  */
    T_UN_PERCENT = 296,            /* T_UN_PERCENT  */
    T_UN_SLASH = 297,              /* T_UN_SLASH  */
    T_UN_POUND = 298,              /* T_UN_POUND  */
    T_UN_EQUAL = 299,              /* T_UN_EQUAL  */
    T_UN_VERTICAL_BAR = 300,       /* T_UN_VERTICAL_BAR  */
    CATFN = 301,                /* CATFN  */
    CMATYP = 302,                 /* CMATYP  */
    T_LPAREN = 303,                /* T_LPAREN  */
    T_RPAREN = 304,                /* T_RPAREN  */
    T_LBRACK = 305,                /* T_LBRACK  */
    RBTYP = 306,                /* RBTYP  */
    T_LANGLE = 307,                /* T_LANGLE  */
    T_RANGLE = 308                 /* T_RANGLE  */
  };
  typedef enum snobol4_tokentype snobol4_token_kind_t;
#endif

/* Value type.  */
#if ! defined SNOBOL4_STYPE && ! defined SNOBOL4_STYPE_IS_DECLARED
union SNOBOL4_STYPE
{
#line 20 "snobol4.y"
 EXPR_t *expr; Token tok; SnoGoto *go; 

#line 135 "snobol4.tab.h"

};
typedef union SNOBOL4_STYPE SNOBOL4_STYPE;
# define SNOBOL4_STYPE_IS_TRIVIAL 1
# define SNOBOL4_STYPE_IS_DECLARED 1
#endif




int snobol4_parse (void *yyparse_param);


#endif /* !YY_SNOBOL4_SNOBOL4_TAB_H_INCLUDED  */
