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

#ifndef YY_SNOBOL4_SNOBOL4_GRAMMAR_TAB_H_INCLUDED
# define YY_SNOBOL4_SNOBOL4_GRAMMAR_TAB_H_INCLUDED
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

/* Token kinds.  */
#ifndef SNOBOL4_TOKENTYPE
# define SNOBOL4_TOKENTYPE
  enum snobol4_tokentype
  {
    SNOBOL4_EMPTY = -2,
    SNOBOL4_EOF = 0,               /* "end of file"  */
    SNOBOL4_error = 256,           /* error  */
    SNOBOL4_UNDEF = 257,           /* "invalid token"  */
    TK_IDENT = 258,                /* TK_IDENT  */
    TK_END = 259,                  /* TK_END  */
    TK_INT = 260,                  /* TK_INT  */
    TK_REAL = 261,                 /* TK_REAL  */
    TK_STR = 262,                  /* TK_STR  */
    TK_KEYWORD = 263,              /* TK_KEYWORD  */
    TK_PLUS = 264,                 /* TK_PLUS  */
    TK_MINUS = 265,                /* TK_MINUS  */
    TK_STAR = 266,                 /* TK_STAR  */
    TK_SLASH = 267,                /* TK_SLASH  */
    TK_PCT = 268,                  /* TK_PCT  */
    TK_CARET = 269,                /* TK_CARET  */
    TK_BANG = 270,                 /* TK_BANG  */
    TK_STARSTAR = 271,             /* TK_STARSTAR  */
    TK_AMP = 272,                  /* TK_AMP  */
    TK_AT = 273,                   /* TK_AT  */
    TK_TILDE = 274,                /* TK_TILDE  */
    TK_DOLLAR = 275,               /* TK_DOLLAR  */
    TK_DOT = 276,                  /* TK_DOT  */
    TK_HASH = 277,                 /* TK_HASH  */
    TK_PIPE = 278,                 /* TK_PIPE  */
    TK_EQ = 279,                   /* TK_EQ  */
    TK_QMARK = 280,                /* TK_QMARK  */
    TK_COMMA = 281,                /* TK_COMMA  */
    TK_LPAREN = 282,               /* TK_LPAREN  */
    TK_RPAREN = 283,               /* TK_RPAREN  */
    TK_LBRACKET = 284,             /* TK_LBRACKET  */
    TK_RBRACKET = 285,             /* TK_RBRACKET  */
    TK_LANGLE = 286,               /* TK_LANGLE  */
    TK_RANGLE = 287                /* TK_RANGLE  */
  };
  typedef enum snobol4_tokentype snobol4_token_kind_t;
#endif

/* Value type.  */
#if ! defined SNOBOL4_STYPE && ! defined SNOBOL4_STYPE_IS_DECLARED
union SNOBOL4_STYPE
{
#line 44 "snobol4_grammar.y"
 EXPR_t *expr; Token tok; 

#line 107 "snobol4_grammar.tab.h"

};
typedef union SNOBOL4_STYPE SNOBOL4_STYPE;
# define SNOBOL4_STYPE_IS_TRIVIAL 1
# define SNOBOL4_STYPE_IS_DECLARED 1
#endif




int snobol4_parse (void *yyparse_param);


#endif /* !YY_SNOBOL4_SNOBOL4_GRAMMAR_TAB_H_INCLUDED  */
