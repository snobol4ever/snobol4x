% prolog_recognizer.pro -- Prolog source recognizer, SNOBOL4 BEAUTY paradigm
% No separate lexer. DCG rules on char-code lists mirror BNF.
% nPush/nInc/nTop/nPop manage a counter stack (nb_setval/nb_getval).
% Shift/Reduce build the tree on a value stack.

:- use_module(library(lists)).
:- discontiguous r_op_token//1.

%% Stack initialisation

stack_init :-
    nb_setval(val_stack, []),
    nb_setval(ctr_stack, []).

%% Counter stack

nPush :- nb_getval(ctr_stack, S), nb_setval(ctr_stack, [0|S]).
nInc  :- nb_getval(ctr_stack, [H|T]), H1 is H+1, nb_setval(ctr_stack, [H1|T]).
nTop(N) :- nb_getval(ctr_stack, [N|_]).
nPop  :- nb_getval(ctr_stack, [_|T]), nb_setval(ctr_stack, T).

%% Value stack

shift(Tag, Val) :-
    nb_getval(val_stack, S),
    nb_setval(val_stack, [node(Tag,Val,[])|S]).

reduce(Tag, N) :-
    nb_getval(val_stack, S),
    length(Kids0, N),
    append(Kids0, Rest, S),
    reverse(Kids0, Kids),
    nb_setval(val_stack, [node(Tag,'',Kids)|Rest]).

%% Tree dump

tdump(node(Tag,'',  []), I) :- !, write_pad(I), format("(~w)~n", [Tag]).
tdump(node(Tag, V,  []), I) :- !,
    write_pad(I),
    ( V == '' -> format("(~w)~n",[Tag]) ; format("(~w ~q)~n",[Tag,V]) ).
tdump(node(Tag, _, Kids), I) :-
    write_pad(I), format("(~w~n",[Tag]),
    I1 is I+2, maplist(tdump_i(I1), Kids),
    write_pad(I), write(')'), nl.
tdump_i(I,N) :- tdump(N,I).

write_pad(0) :- !.
write_pad(N) :- N>0, write(' '), N1 is N-1, write_pad(N1).

%% Character helpers

is_space(C)   :- code_type(C, space).
is_lower(C)   :- code_type(C, lower(_)).
is_upper(C)   :- code_type(C, upper(_)).
is_digit(C)   :- code_type(C, digit(_)).
is_alnumul(C) :- ( code_type(C,alnum) ; C =:= 0'_ ), !.

%% Whitespace and comments

ws --> [].
ws --> [C], { is_space(C) }, !, ws.
ws --> [0'%], !, skip_line, ws.

skip_line --> [0'\n], !.
skip_line --> [], !.
skip_line --> [_], !, skip_line.

%% Helpers

kw(Word) -->
    ws, { atom_codes(Word,Cs) }, Cs, peek_not_alnum.

peek_not_alnum, [C] --> [C], { is_alnumul(C) }, !, { fail }.
peek_not_alnum --> [].

punct(S) --> ws, { atom_codes(S,Cs) }, Cs.

%% Identifiers

lc_ident(Name) -->
    ws, [C], { is_lower(C) }, ident_rest(R), { atom_codes(Name,[C|R]) }.

var_ident(Name) -->
    ws, [C], { is_upper(C) ; C =:= 0'_ }, ident_rest(R), { atom_codes(Name,[C|R]) }.

ident_rest([C|R]) --> [C], { is_alnumul(C) }, !, ident_rest(R).
ident_rest([]) --> [].

%% Number literals

integer_lit(V) -->
    ws, [C], { is_digit(C) }, digit_rest(R), { number_codes(V,[C|R]) }.

digit_rest([C|R]) --> [C], { is_digit(C) }, !, digit_rest(R).
digit_rest([]) --> [].

float_lit(V) -->
    ws, [C], { is_digit(C) }, digit_rest(IR),
    [0'.], [D], { is_digit(D) }, digit_rest(FR),
    { append([C|IR], [0'.|[D|FR]], All), number_codes(V,All) }.

%% Quoted atom

atom_lit(A) -->
    ws, [0''], quoted_atom_cs(Cs), { atom_codes(A,Cs) }.

quoted_atom_cs([0'',0''|R]) --> [0'',0''], !, quoted_atom_cs(R).
quoted_atom_cs([])     --> [0''], !.
quoted_atom_cs([C|R])  --> [C], { C =\= 0'' }, !, quoted_atom_cs(R).
quoted_atom_cs([])     --> [].

%% String literal

string_lit(S) -->
    ws, [0'"], string_cs(Cs), { atom_codes(S,Cs) }.

string_cs([0'\\,C|R]) --> [0'\\], [C], !, string_cs(R).
string_cs([])         --> [0'"], !.
string_cs([C|R])      --> [C], { C =\= 0'" }, !, string_cs(R).
string_cs([])         --> [].

%% Prolog operator table (renamed op_def to avoid clash with built-in op/3)

op_def(':-',  1200, xfx). op_def('-->',  1200, xfx).
op_def('?-',  1200, fx).  op_def(':-',   1100, fx).
op_def(';',   1100, xfy). op_def('->',   1050, xfy).
op_def(',',   1000, xfy). op_def('\\+',   900, fy).
op_def('=',    700, xfx). op_def('\\=',   700, xfx).
op_def('==',   700, xfx). op_def('\\==',  700, xfx).
op_def('is',   700, xfx). op_def('<',     700, xfx).
op_def('>',    700, xfx). op_def('=<',    700, xfx).
op_def('>=',   700, xfx). op_def('=:=',   700, xfx).
op_def('=\\=', 700, xfx). op_def('=..',   700, xfx).
op_def('@<',   700, xfx). op_def('@>',    700, xfx).
op_def('@=<',  700, xfx). op_def('@>=',   700, xfx).
op_def(':',    600, xfy). op_def('+',     500, yfx).
op_def('-',    500, yfx). op_def('/\\',   500, yfx).
op_def('\\/',  500, yfx). op_def('xor',   400, yfx).
op_def('*',    400, yfx). op_def('/',     400, yfx).
op_def('//',   400, yfx). op_def('<<',    400, yfx).
op_def('>>',   400, yfx). op_def('mod',   400, yfx).
op_def('rem',  400, yfx). op_def('div',   400, yfx).
op_def('**',   200, xfx). op_def('^',     200, xfy).
op_def('-',    200, fy).  op_def('+',     200, fy).

is_infix(T)  :- member(T, [xfx,xfy,yfx]).
is_prefix(T) :- member(T, [fx,fy]).

rp_for(xfx, P, R) :- R is P-1.
rp_for(xfy, P, P).
rp_for(yfx, P, R) :- R is P-1.

%% Term recognizer

r_term(MaxP) --> r_primary, r_term_rest(MaxP).

r_term_rest(MaxP) -->
    ws, r_op_token(Op),
    { op_def(Op,Prec,Type), is_infix(Type), Prec =< MaxP,
      rp_for(Type,Prec,RP) }, !,
    r_term(RP),
    { reduce(Op,2) },
    r_term_rest(MaxP).
r_term_rest(_) --> [].

r_primary -->
    punct('('), !, r_term(1200), punct(')').

r_primary -->
    punct('['), !, r_list_body, punct(']').

r_primary -->
    punct('{'), !,
    ( r_term(1200), { reduce('{}',1) } ; { shift('{}','') } ),
    punct('}').

r_primary -->
    ws, r_op_token(Op),
    { op_def(Op,Prec,Type), is_prefix(Type),
      ( Type=fx -> ArgP is Prec-1 ; ArgP=Prec ) }, !,
    r_term(ArgP), { reduce(Op,1) }.

r_primary --> float_lit(V),   !, { shift(float,V) }.
r_primary --> integer_lit(V), !, { shift(int,V) }.
r_primary --> string_lit(S),  !, { shift(str,S) }.
r_primary --> atom_lit(A),    !, r_maybe_args(A).
r_primary --> var_ident(N),   !, { shift(var,N) }.
r_primary --> ws, [33], !,      { shift(atom,'!') }.    % cut
r_primary -->
    lc_ident(A),
    { \+ member(A,[is,mod,rem,div,xor]) }, !,
    r_maybe_args(A).

r_maybe_args(F) -->
    ws, [0'(], !,
    { shift(atom,F), nPush }, r_arglist, { nTop(N), nPop },
    ws, [0')],
    { M is N+1, reduce(call,M) }.
r_maybe_args(F) --> { shift(atom,F) }.

r_arglist -->
    r_term(999), !, { nInc }, r_arglist_rest.
r_arglist --> [].

r_arglist_rest -->
    punct(','), !, r_term(999), { nInc }, r_arglist_rest.
r_arglist_rest --> [].

r_list_body -->
    r_term(999), !,
    { nPush, nInc }, r_list_rest.
r_list_body --> { shift(atom,'[]') }.

r_list_rest -->
    punct(','), !, r_term(999), { nInc }, r_list_rest.
r_list_rest -->
    punct('|'), !, r_term(999),
    { nTop(N), nPop, M is N+1, reduce('.',M) }.
r_list_rest -->
    { nTop(N), nPop, reduce(list,N) }.

%% Operator token scanner

op_char(0'=). op_char(0'<). op_char(0'>). op_char(0'+). op_char(0'-).
op_char(0'*). op_char(0'/). op_char(0'\\). op_char(0'@). op_char(0'^).
op_char(0'~). op_char(0'&). op_char(0'?). op_char(0':). op_char(0'|).

r_op_token(Op) -->
    ws, [C], { op_char(C) }, op_sym_rest(R),
    { atom_codes(Op,[C|R]), op_def(Op,_,_) }.

% Punctuation operators not matched by symbol scanner
r_op_token(Op) --> ws, { member(Op-C, [(','-44),(';'-59)]) }, [C].

op_sym_rest([C|R]) --> [C], { op_char(C) }, !, op_sym_rest(R).
op_sym_rest([]) --> [].

r_op_token(Op) -->
    ws, [C], { is_lower(C) }, ident_rest(R), peek_not_alnum,
    { atom_codes(Op,[C|R]), op_def(Op,_,_) }.



%% Clause

r_clause -->
    ws, r_term(1200), ws, [0'.],
    ( [C], { is_space(C) } ; \+ [_] ), !.

%% Compiland

compiland_loop(Acc, N, Codes, Rest) :-
    Codes = [_|_],
    nb_getval(val_stack, SnapV), nb_getval(ctr_stack, SnapC),
    ( phrase(r_clause, Codes, Codes1)
    -> Acc1 is Acc+1,
       compiland_loop(Acc1, N, Codes1, Rest)
    ;  nb_setval(val_stack, SnapV), nb_setval(ctr_stack, SnapC),
       skip_past_dot(Codes, Codes2),
       compiland_loop(Acc, N, Codes2, Rest)
    ).
compiland_loop(N, N, [], []).

skip_past_dot([0'.|Rest], Rest) :- !.
skip_past_dot([_|T], Rest) :- skip_past_dot(T, Rest).
skip_past_dot([], []).

%% Input

read_stream_codes(S, [C|R]) :-
    get_code(S, C), C =\= -1, !, read_stream_codes(S, R).
read_stream_codes(_, []).

%% Main

main :-
    stack_init,
    read_stream_codes(current_input, Codes),
    compiland_loop(0, N, Codes, _),
    reduce(compiland, N),
    nb_getval(val_stack, [Tree|_]),
    tdump(Tree, 0).

:- initialization(main, main).
