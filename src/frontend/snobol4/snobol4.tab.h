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
    T_IDENT = 258,                 /* T_IDENT  */
    T_FUNCTION = 259,              /* T_FUNCTION  */
    T_KEYWORD = 260,               /* T_KEYWORD  */
    T_END = 261,                   /* T_END  */
    T_INT = 262,                   /* T_INT  */
    T_REAL = 263,                  /* T_REAL  */
    T_STR = 264,                   /* T_STR  */
    T_LABEL = 265,                 /* T_LABEL  */
    T_GOTO = 266,                  /* T_GOTO  */
    T_STMT_END = 267,              /* T_STMT_END  */
    T_ASSIGNMENT = 268,            /* T_ASSIGNMENT  */
    T_MATCH = 269,                 /* T_MATCH  */
    T_ALTERNATION = 270,           /* T_ALTERNATION  */
    T_ADDITION = 271,              /* T_ADDITION  */
    T_SUBTRACTION = 272,           /* T_SUBTRACTION  */
    T_MULTIPLICATION = 273,        /* T_MULTIPLICATION  */
    T_DIVISION = 274,              /* T_DIVISION  */
    T_EXPONENTIATION = 275,        /* T_EXPONENTIATION  */
    T_IMMEDIATE_ASSIGN = 276,      /* T_IMMEDIATE_ASSIGN  */
    T_COND_ASSIGN = 277,           /* T_COND_ASSIGN  */
    T_AMPERSAND = 278,             /* T_AMPERSAND  */
    T_AT_SIGN = 279,               /* T_AT_SIGN  */
    T_POUND = 280,                 /* T_POUND  */
    T_PERCENT = 281,               /* T_PERCENT  */
    T_TILDE = 282,                 /* T_TILDE  */
    T_UN_AT_SIGN = 283,            /* T_UN_AT_SIGN  */
    T_UN_TILDE = 284,              /* T_UN_TILDE  */
    T_UN_QUESTION_MARK = 285,      /* T_UN_QUESTION_MARK  */
    T_UN_AMPERSAND = 286,          /* T_UN_AMPERSAND  */
    T_UN_PLUS = 287,               /* T_UN_PLUS  */
    T_UN_MINUS = 288,              /* T_UN_MINUS  */
    T_UN_ASTERISK = 289,           /* T_UN_ASTERISK  */
    T_UN_DOLLAR_SIGN = 290,        /* T_UN_DOLLAR_SIGN  */
    T_UN_PERIOD = 291,             /* T_UN_PERIOD  */
    T_UN_EXCLAMATION = 292,        /* T_UN_EXCLAMATION  */
    T_UN_PERCENT = 293,            /* T_UN_PERCENT  */
    T_UN_SLASH = 294,              /* T_UN_SLASH  */
    T_UN_POUND = 295,              /* T_UN_POUND  */
    T_UN_EQUAL = 296,              /* T_UN_EQUAL  */
    T_UN_VERTICAL_BAR = 297,       /* T_UN_VERTICAL_BAR  */
    T_CONCAT = 298,                /* T_CONCAT  */
    T_COMMA = 299,                 /* T_COMMA  */
    T_LPAREN = 300,                /* T_LPAREN  */
    T_RPAREN = 301,                /* T_RPAREN  */
    T_LBRACK = 302,                /* T_LBRACK  */
    T_RBRACK = 303,                /* T_RBRACK  */
    T_LANGLE = 304,                /* T_LANGLE  */
    T_RANGLE = 305                 /* T_RANGLE  */
  };
  typedef enum snobol4_tokentype snobol4_token_kind_t;
#endif

/* Value type.  */
#if ! defined SNOBOL4_STYPE && ! defined SNOBOL4_STYPE_IS_DECLARED
union SNOBOL4_STYPE
{
#line 22 "snobol4.y"
 EXPR_t *expr; Token tok; 

#line 132 "snobol4.tab.h"

};
typedef union SNOBOL4_STYPE SNOBOL4_STYPE;
# define SNOBOL4_STYPE_IS_TRIVIAL 1
# define SNOBOL4_STYPE_IS_DECLARED 1
#endif




int snobol4_parse (void *yyparse_param);


#endif /* !YY_SNOBOL4_SNOBOL4_TAB_H_INCLUDED  */
