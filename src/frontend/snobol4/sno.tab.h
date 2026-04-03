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

#ifndef YY_YY_SNO_TAB_H_INCLUDED
# define YY_YY_SNO_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    LABEL = 258,                   /* LABEL  */
    IDENT = 259,                   /* IDENT  */
    KEYWORD = 260,                 /* KEYWORD  */
    STR = 261,                     /* STR  */
    END_LABEL = 262,               /* END_LABEL  */
    INT = 263,                     /* INT  */
    REAL = 264,                    /* REAL  */
    EQ = 265,                      /* EQ  */
    COLON = 266,                   /* COLON  */
    LPAREN = 267,                  /* LPAREN  */
    RPAREN = 268,                  /* RPAREN  */
    LBRACKET = 269,                /* LBRACKET  */
    RBRACKET = 270,                /* RBRACKET  */
    STARSTAR = 271,                /* STARSTAR  */
    CARET = 272,                   /* CARET  */
    PLUS = 273,                    /* PLUS  */
    MINUS = 274,                   /* MINUS  */
    STAR = 275,                    /* STAR  */
    SLASH = 276,                   /* SLASH  */
    PIPE = 277,                    /* PIPE  */
    DOT = 278,                     /* DOT  */
    DOLLAR = 279,                  /* DOLLAR  */
    AMP = 280,                     /* AMP  */
    COMMA = 281,                   /* COMMA  */
    AT = 282,                      /* AT  */
    SGOTO = 283,                   /* SGOTO  */
    FGOTO = 284,                   /* FGOTO  */
    NEWLINE = 285,                 /* NEWLINE  */
    __ = 286                       /* __  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 52 "sno.y"

    char   *sval;
    long    ival;
    double  dval;
    struct Expr    *expr;
    void           *go;
    struct Stmt    *stmt;
    void           *al;

#line 105 "sno.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_SNO_TAB_H_INCLUDED  */
