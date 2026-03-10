#!/usr/bin/env python3
"""Rename all yy* symbols in a Bison-generated C file to avoid collisions."""
import re, sys
src, prefix, dst = sys.argv[1], sys.argv[2], sys.argv[3]
t = open(src).read()
for s in ['yyparse','yylex','yyerror','yychar','yylval','yynerrs','yydebug',
          'yyin','yyout','yytext','yywrap']:
    t = re.sub(r'\b' + s + r'\b', prefix + '_' + s, t)
open(dst, 'w').write(t)
print(f"  renamed {src} -> {dst} (prefix={prefix}_)")
