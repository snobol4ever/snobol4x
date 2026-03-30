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

#ifndef YY_YY_REBUS_TAB_H_INCLUDED
# define YY_YY_REBUS_TAB_H_INCLUDED
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
    T_IDENT = 258,                 /* T_IDENT  */
    T_STR = 259,                   /* T_STR  */
    T_KEYWORD = 260,               /* T_KEYWORD  */
    T_INT = 261,                   /* T_INT  */
    T_REAL = 262,                  /* T_REAL  */
    T_CASE = 263,                  /* T_CASE  */
    T_DEFAULT = 264,               /* T_DEFAULT  */
    T_DO = 265,                    /* T_DO  */
    T_ELSE = 266,                  /* T_ELSE  */
    T_END = 267,                   /* T_END  */
    T_EXIT = 268,                  /* T_EXIT  */
    T_FAIL = 269,                  /* T_FAIL  */
    T_FOR = 270,                   /* T_FOR  */
    T_FROM = 271,                  /* T_FROM  */
    T_FUNCTION = 272,              /* T_FUNCTION  */
    T_BY = 273,                    /* T_BY  */
    T_IF = 274,                    /* T_IF  */
    T_INITIAL = 275,               /* T_INITIAL  */
    T_LOCAL = 276,                 /* T_LOCAL  */
    T_NEXT = 277,                  /* T_NEXT  */
    T_OF = 278,                    /* T_OF  */
    T_RECORD = 279,                /* T_RECORD  */
    T_REPEAT = 280,                /* T_REPEAT  */
    T_RETURN = 281,                /* T_RETURN  */
    T_STOP = 282,                  /* T_STOP  */
    T_THEN = 283,                  /* T_THEN  */
    T_TO = 284,                    /* T_TO  */
    T_UNLESS = 285,                /* T_UNLESS  */
    T_UNTIL = 286,                 /* T_UNTIL  */
    T_WHILE = 287,                 /* T_WHILE  */
    T_ASSIGN = 288,                /* T_ASSIGN  */
    T_EXCHANGE = 289,              /* T_EXCHANGE  */
    T_ADDASSIGN = 290,             /* T_ADDASSIGN  */
    T_SUBASSIGN = 291,             /* T_SUBASSIGN  */
    T_CATASSIGN = 292,             /* T_CATASSIGN  */
    T_QUESTMINUS = 293,            /* T_QUESTMINUS  */
    T_ARROW = 294,                 /* T_ARROW  */
    T_STRCAT = 295,                /* T_STRCAT  */
    T_STARSTAR = 296,              /* T_STARSTAR  */
    T_NE = 297,                    /* T_NE  */
    T_GE = 298,                    /* T_GE  */
    T_LE = 299,                    /* T_LE  */
    T_SEQ = 300,                   /* T_SEQ  */
    T_SNE = 301,                   /* T_SNE  */
    T_SGT = 302,                   /* T_SGT  */
    T_SGE = 303,                   /* T_SGE  */
    T_SLT = 304,                   /* T_SLT  */
    T_SLE = 305,                   /* T_SLE  */
    T_PLUSCOLON = 306,             /* T_PLUSCOLON  */
    LOWER_THAN_ELSE = 307,         /* LOWER_THAN_ELSE  */
    UMINUS = 308,                  /* UMINUS  */
    UPLUS = 309,                   /* UPLUS  */
    UTILDE = 310,                  /* UTILDE  */
    UBACK = 311,                   /* UBACK  */
    USLASH = 312,                  /* USLASH  */
    UBANG = 313,                   /* UBANG  */
    UAT = 314,                     /* UAT  */
    UDOLLAR = 315,                 /* UDOLLAR  */
    UDOT = 316                     /* UDOT  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 103 "rebus.y"

    char       *sval;
    long        ival;
    double      dval;
    RExpr      *expr;
    RStmt      *stmt;
    RDecl      *decl;
    RCase      *rcase;
    void       *sal;    /* SAL* */
    void       *eal;    /* EAL* */
    void       *stal;   /* STAL* */

#line 138 "rebus.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_REBUS_TAB_H_INCLUDED  */
