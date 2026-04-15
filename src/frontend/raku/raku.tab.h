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

#ifndef YY_RAKU_YY_RAKU_TAB_H_INCLUDED
# define YY_RAKU_YY_RAKU_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef RAKU_YYDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define RAKU_YYDEBUG 1
#  else
#   define RAKU_YYDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define RAKU_YYDEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined RAKU_YYDEBUG */
#if RAKU_YYDEBUG
extern int raku_yydebug;
#endif
/* "%code requires" blocks.  */
#line 3 "raku.y"

/*
 * raku.y — Tiny-Raku Bison grammar
 *
 * FI-3: builds EXPR_t/STMT_t directly — no intermediate RakuNode AST.
 *
 * AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
 */
#include "../../ir/ir.h"
#include "../snobol4/scrip_cc.h"

typedef struct ExprList {
    EXPR_t **items;
    int      count;
    int      cap;
} ExprList;

#line 75 "raku.tab.h"

/* Token kinds.  */
#ifndef RAKU_YYTOKENTYPE
# define RAKU_YYTOKENTYPE
  enum raku_yytokentype
  {
    RAKU_YYEMPTY = -2,
    RAKU_YYEOF = 0,                /* "end of file"  */
    RAKU_YYerror = 256,            /* error  */
    RAKU_YYUNDEF = 257,            /* "invalid token"  */
    LIT_INT = 258,                 /* LIT_INT  */
    LIT_FLOAT = 259,               /* LIT_FLOAT  */
    LIT_STR = 260,                 /* LIT_STR  */
    LIT_INTERP_STR = 261,          /* LIT_INTERP_STR  */
    VAR_SCALAR = 262,              /* VAR_SCALAR  */
    VAR_ARRAY = 263,               /* VAR_ARRAY  */
    VAR_HASH = 264,                /* VAR_HASH  */
    IDENT = 265,                   /* IDENT  */
    KW_MY = 266,                   /* KW_MY  */
    KW_SAY = 267,                  /* KW_SAY  */
    KW_PRINT = 268,                /* KW_PRINT  */
    KW_IF = 269,                   /* KW_IF  */
    KW_ELSE = 270,                 /* KW_ELSE  */
    KW_ELSIF = 271,                /* KW_ELSIF  */
    KW_WHILE = 272,                /* KW_WHILE  */
    KW_FOR = 273,                  /* KW_FOR  */
    KW_SUB = 274,                  /* KW_SUB  */
    KW_GATHER = 275,               /* KW_GATHER  */
    KW_TAKE = 276,                 /* KW_TAKE  */
    KW_RETURN = 277,               /* KW_RETURN  */
    KW_GIVEN = 278,                /* KW_GIVEN  */
    KW_WHEN = 279,                 /* KW_WHEN  */
    KW_DEFAULT = 280,              /* KW_DEFAULT  */
    KW_EXISTS = 281,               /* KW_EXISTS  */
    KW_DELETE = 282,               /* KW_DELETE  */
    KW_UNLESS = 283,               /* KW_UNLESS  */
    KW_UNTIL = 284,                /* KW_UNTIL  */
    KW_REPEAT = 285,               /* KW_REPEAT  */
    OP_RANGE = 286,                /* OP_RANGE  */
    OP_RANGE_EX = 287,             /* OP_RANGE_EX  */
    OP_ARROW = 288,                /* OP_ARROW  */
    OP_EQ = 289,                   /* OP_EQ  */
    OP_NE = 290,                   /* OP_NE  */
    OP_LE = 291,                   /* OP_LE  */
    OP_GE = 292,                   /* OP_GE  */
    OP_SEQ = 293,                  /* OP_SEQ  */
    OP_SNE = 294,                  /* OP_SNE  */
    OP_AND = 295,                  /* OP_AND  */
    OP_OR = 296,                   /* OP_OR  */
    OP_BIND = 297,                 /* OP_BIND  */
    OP_DIV = 298,                  /* OP_DIV  */
    UMINUS = 299                   /* UMINUS  */
  };
  typedef enum raku_yytokentype raku_yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined RAKU_YYSTYPE && ! defined RAKU_YYSTYPE_IS_DECLARED
union RAKU_YYSTYPE
{
#line 141 "raku.y"

    long      ival;
    double    dval;
    char     *sval;
    EXPR_t   *node;
    ExprList *list;

#line 144 "raku.tab.h"

};
typedef union RAKU_YYSTYPE RAKU_YYSTYPE;
# define RAKU_YYSTYPE_IS_TRIVIAL 1
# define RAKU_YYSTYPE_IS_DECLARED 1
#endif


extern RAKU_YYSTYPE raku_yylval;


int raku_yyparse (void);


#endif /* !YY_RAKU_YY_RAKU_TAB_H_INCLUDED  */
