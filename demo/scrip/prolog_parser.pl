% prolog_parser.pro --- Prolog source parser using DCG
% Reads Prolog source from stdin, prints parse tree as S-expressions.
% Milestone: M-PARSE-PROLOG
%
% Uses only builtins confirmed supported by scrip-cc -pl -jvm.
% Character classification via atom_codes + arithmetic comparisons.
%
% Output format (treebank-style S-expressions):
%   (fact (call foo 1 2))
%   (clause (call bar X Y) (, (call baz X) (call qux Y)))
%   (directive (call use_module (call library (id lists))))

:- initialization(main, main).

% ------ Character classification ---------------------------------------------------------------------------------------------------------------------------------------------------

is_digit(C)  :- C >= 48, C =< 57.
is_lower(C)  :- C >= 97, C =< 122.
is_upper(C)  :- C >= 65, C =< 90.
is_alpha(C)  :- is_lower(C) ; is_upper(C).
is_alnum(C)  :- is_alpha(C) ; is_digit(C) ; C =:= 95.  % _
is_space(C)  :- C =< 32.
is_graphic(C) :-
    member(C, [35,38,42,43,45,46,47,58,60,61,62,63,64,92,94,126]).
    % # & * + - . / : < = > ? @ \ ^ ~

code_digit(C, D) :- D is C - 48.

% ------ Tokeniser ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

tokenise(Str, Toks) :-
    atom_codes(Str, Codes),
    lex(Codes, Toks).

lex([], []) :- !.
lex([C|Cs], Toks) :-
    is_space(C), !,
    lex(Cs, Toks).
lex([37|Cs], Toks) :-  % % comment
    !, skip_line(Cs, Rest),
    lex(Rest, Toks).
lex([39|Cs], [tok(atom,A)|Toks]) :-  % ' quoted atom
    !, lex_squote(Cs, Codes, Rest),
    atom_codes(A, Codes),
    lex(Rest, Toks).
lex([34|Cs], [tok(str,S)|Toks]) :-  % " string
    !, lex_dquote(Cs, Codes, Rest),
    atom_codes(S, Codes),
    lex(Rest, Toks).
lex([C|Cs], [tok(int,N)|Toks]) :-
    is_digit(C), !,
    lex_digits([C|Cs], Ds, Rest),
    number_codes(N, Ds),
    lex(Rest, Toks).
lex([C|Cs], [tok(T,A)|Toks]) :-
    ( is_upper(C) ; C =:= 95 ), !,
    lex_alnum([C|Cs], Codes, Rest),
    atom_codes(A, Codes),
    ( A = '_' -> T = anon ; T = var ),
    lex(Rest, Toks).
lex([C|Cs], [tok(atom,A)|Toks]) :-
    is_lower(C), !,
    lex_alnum([C|Cs], Codes, Rest),
    atom_codes(A, Codes),
    lex(Rest, Toks).
lex([C|Cs], [tok(op,A)|Toks]) :-
    is_graphic(C), !,
    lex_graphic([C|Cs], Codes, Rest),
    atom_codes(A, Codes),
    lex(Rest, Toks).
lex([C|Cs], [tok(punct,P)|Toks]) :-
    member(C, [40,41,44,91,93,123,125,124]),  % ( ) , [ ] { } |
    !, char_code(P, C),
    lex(Cs, Toks).
lex([_|Cs], Toks) :- lex(Cs, Toks).  % skip unknown

skip_line([], []).
skip_line([10|Cs], Cs) :- !.
skip_line([_|Cs], Rest) :- skip_line(Cs, Rest).

lex_digits([C|Cs], [C|Ds], Rest) :- is_digit(C), !, lex_digits(Cs, Ds, Rest).
lex_digits(Cs, [], Cs).

lex_alnum([C|Cs], [C|Rest], Tail) :- is_alnum(C), !, lex_alnum(Cs, Rest, Tail).
lex_alnum(Cs, [], Cs).

lex_graphic([C|Cs], [C|Rest], Tail) :- is_graphic(C), !, lex_graphic(Cs, Rest, Tail).
lex_graphic(Cs, [], Cs).

lex_squote([39|Cs], [], Cs) :- !.
lex_squote([92,C|Cs], [C|Rest], Tail) :- !, lex_squote(Cs, Rest, Tail).
lex_squote([C|Cs], [C|Rest], Tail) :- lex_squote(Cs, Rest, Tail).

lex_dquote([34|Cs], [], Cs) :- !.
lex_dquote([92,C|Cs], [C|Rest], Tail) :- !, lex_dquote(Cs, Rest, Tail).
lex_dquote([C|Cs], [C|Rest], Tail) :- lex_dquote(Cs, Rest, Tail).

% ------ Operator table ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

op_info(':-',   1200, xfx). op_info('-->',  1200, xfx).
op_info(':-',   1200, fx).  op_info('?-',   1200, fx).
op_info(';',    1100, xfy). op_info('|',    1100, xfy).
op_info('->',   1050, xfy).
op_info(',',    1000, xfy).
op_info('\\+',  900,  fy).
op_info('=',    700,  xfx). op_info('\\=',  700,  xfx).
op_info('==',   700,  xfx). op_info('\\==', 700,  xfx).
op_info('is',   700,  xfx). op_info('=..',  700,  xfx).
op_info('<',    700,  xfx). op_info('>',    700,  xfx).
op_info('=<',   700,  xfx). op_info('>=',   700,  xfx).
op_info('=:=',  700,  xfx). op_info('=\\=', 700,  xfx).
op_info(':',    600,  xfy).
op_info('+',    500,  yfx). op_info('-',    500,  yfx).
op_info('*',    400,  yfx). op_info('/',    400,  yfx).
op_info('//',   400,  yfx). op_info('mod',  400,  yfx).
op_info('**',   200,  xfx).
op_info('^',    200,  xfy).
op_info('-',    200,  fy).

% ------ Term parser ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

parse_term(MaxP, Toks, Tree, Rest) :-
    parse_primary(Toks, T0, R0),
    parse_ops(MaxP, 0, T0, R0, Tree, Rest).

parse_ops(MaxP, MinP, Left, [tok(_,Op)|Toks], Tree, Rest) :-
    op_info(Op, P, Type),
    P > MinP, P =< MaxP,
    ( member(Type, [yfx, xfx]) -> RPrec is P - 1 ; RPrec = P ),
    parse_term(RPrec, Toks, Right, R1), !,
    parse_ops(MaxP, P, op2(Op,Left,Right), R1, Tree, Rest).
parse_ops(_, _, T, R, T, R).

parse_primary([tok(_,Op)|Toks], op1(Op,Arg), Rest) :-
    op_info(Op, P, T), member(T, [fx, fy]), !,
    ( T = fx -> AP is P - 1 ; AP = P ),
    parse_term(AP, Toks, Arg, Rest).
parse_primary([tok(atom,F), tok(punct,'(')|Toks], call(F,Args), Rest) :- !,
    parse_arglist(Toks, Args, Rest).
parse_primary([tok(punct,'(')|Toks], Tree, Rest) :- !,
    parse_term(1200, Toks, Tree, [tok(punct,')')|Rest]).
parse_primary([tok(punct,'[')|Toks], Tree, Rest) :- !,
    parse_list(Toks, Tree, Rest).
parse_primary([tok(punct,'{')|Toks], braces(T), Rest) :- !,
    parse_term(1200, Toks, T, [tok(punct,'}')|Rest]).
parse_primary([tok(atom,A)|Rest],  atom(A),  Rest).
parse_primary([tok(var,V)|Rest],   var(V),   Rest).
parse_primary([tok(anon,_)|Rest],  var('_'), Rest).
parse_primary([tok(int,N)|Rest],   int(N),   Rest).
parse_primary([tok(str,S)|Rest],   str(S),   Rest).

parse_arglist([tok(punct,')')|Rest], [], Rest) :- !.
parse_arglist(Toks, [A|As], Rest) :-
    parse_term(999, Toks, A, R1), !,
    ( R1 = [tok(punct,',')|R2] -> parse_arglist(R2, As, Rest)
    ; As = [], R1 = [tok(punct,')')|Rest] ).

parse_list([tok(punct,']')|Rest], atom('[]'), Rest) :- !.
parse_list(Toks, lst(H,T), Rest) :-
    parse_term(999, Toks, H, R1), !,
    ( R1 = [tok(punct,',')|R2] ->
        parse_list(R2, T, Rest)
    ; R1 = [tok(punct,'|')|R2] ->
        parse_term(999, R2, T, [tok(punct,']')|Rest])
    ; R1 = [tok(punct,']')|Rest], T = atom('[]')
    ).

% ------ Clause parser ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

parse_clause([tok(_,'.')|Rest], empty, Rest) :- !.
parse_clause(Toks, Tree, Rest) :-
    parse_term(1200, Toks, T, [tok(_,'.')|Rest]),
    to_clause(T, Tree).

to_clause(op2(':-', H, B), clause(H, B)) :- !.
to_clause(op2('-->', H, B), dcg(H, B))   :- !.
to_clause(op1(':-', D), directive(D))    :- !.
to_clause(op1('?-', Q), query(Q))        :- !.
to_clause(F, fact(F)).

% ------ S-expression printer ---------------------------------------------------------------------------------------------------------------------------------------------------------------

print_sx(atom(A))       :- write(A).
print_sx(var(V))        :- write(V).
print_sx(int(N))        :- write(N).
print_sx(str(S))        :- write('"'), write(S), write('"').
print_sx(atom('[]'))    :- write('[]').
print_sx(lst(H,T))      :-
    write('(list '), print_sx(H), write(' '), print_sx(T), write(')').
print_sx(op2(Op,L,R))   :-
    write('('), write(Op), write(' '),
    print_sx(L), write(' '), print_sx(R), write(')').
print_sx(op1(Op,A))     :-
    write('('), write(Op), write(' '), print_sx(A), write(')').
print_sx(call(F,[]))    :- write('(call '), write(F), write(')').
print_sx(call(F,Args))  :-
    write('(call '), write(F),
    print_sx_list(Args),
    write(')').
print_sx(braces(T))     :- write('(braces '), print_sx(T), write(')').
print_sx(fact(T))       :- write('(fact '), print_sx(T), write(')').
print_sx(clause(H,B))   :-
    write('(clause '), print_sx(H), write(' '), print_sx(B), write(')').
print_sx(dcg(H,B))      :-
    write('(dcg '), print_sx(H), write(' '), print_sx(B), write(')').
print_sx(directive(D))  :- write('(directive '), print_sx(D), write(')').
print_sx(query(Q))      :- write('(query '), print_sx(Q), write(')').
print_sx(empty)         :- write('(empty)').

print_sx_list([]).
print_sx_list([H|T]) :- write(' '), print_sx(H), print_sx_list(T).

% ------ Read all input ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

read_all(S) :-
    read_all_lines('', S).

read_all_lines(Acc, S) :-
    ( read_line_to_string(user_input, L), L \= end_of_file ->
        atom_concat(Acc, '\n', A1),
        atom_concat(A1, L, A2),
        read_all_lines(A2, S)
    ; S = Acc ).

% ------ Main ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

main :-
    set_width,
    read_all(Src),
    tokenise(Src, Toks),
    parse_loop(Toks).

parse_loop([]) :- !.
parse_loop(Toks) :-
    ( parse_clause(Toks, Tree, Rest) ->
        ( Tree \= empty -> pp_top(Tree) ; true ),
        parse_loop(Rest)
    ;   Toks = [_|Rest],
        parse_loop(Rest)
    ).

set_width :-
    ( current_prolog_flag(argv, Args),
      member(A, Args),
      atom_concat('--width=', W, A),
      atom_number(W, N), integer(N), N > 0
    -> retractall(max_width(_)), assert(max_width(N))
    ; true ).

% ── Pretty-printer (replaces print_sx) ───────────────────────────────────────
% max_width/1: configurable line width, default 120
:- dynamic max_width/1.
max_width(120).

% sx_to_atom(+Tree, -Atom): render tree as flat atom
sx_flat(atom(A), A) :- !.
sx_flat(var(V),  V) :- !.
sx_flat(int(N),  S) :- !, number_codes(N,Cs), atom_codes(S,Cs).
sx_flat(float(F),S) :- !, number_codes(F,Cs), atom_codes(S,Cs).
sx_flat(str(S),  Q) :- !, atom_concat('"', S, T), atom_concat(T, '"', Q).
sx_flat(atom('[]'), '[]') :- !.
sx_flat(lst(H,T), R) :- !,
    sx_flat(H, HS), sx_flat(T, TS),
    atom_concat('(list ', HS, A), atom_concat(A, ' ', B), atom_concat(B, TS, C),
    atom_concat(C, ')', R).
sx_flat(op2(Op,L,R2), S) :- !,
    sx_flat(L, LS), sx_flat(R2, RS),
    atomic_list_concat(['(', Op, ' ', LS, ' ', RS, ')'], S).
sx_flat(op1(Op,A), S) :- !,
    sx_flat(A, AS),
    atomic_list_concat(['(', Op, ' ', AS, ')'], S).
sx_flat(call(F,[]), S) :- !, atomic_list_concat(['(call ', F, ')'], S).
sx_flat(call(F,Args), S) :- !,
    maplist(sx_flat, Args, AS),
    atomic_list_concat(AS, ' ', ArgStr),
    atomic_list_concat(['(call ', F, ' ', ArgStr, ')'], S).
sx_flat(braces(T), S) :- !, sx_flat(T, TS), atom_concat('(braces ', TS, A), atom_concat(A, ')', S).
sx_flat(fact(T), S)       :- !, sx_flat(T, TS), atom_concat('(fact ', TS, A), atom_concat(A, ')', S).
sx_flat(clause(H,B), S)   :- !, sx_flat(H, HS), sx_flat(B, BS),
    atomic_list_concat(['(clause ', HS, ' ', BS, ')'], S).
sx_flat(dcg(H,B), S)      :- !, sx_flat(H, HS), sx_flat(B, BS),
    atomic_list_concat(['(dcg ', HS, ' ', BS, ')'], S).
sx_flat(directive(D), S)  :- !, sx_flat(D, DS), atom_concat('(directive ', DS, A), atom_concat(A, ')', S).
sx_flat(query(Q), S)      :- !, sx_flat(Q, QS), atom_concat('(query ', QS, A), atom_concat(A, ')', S).
sx_flat(empty, '(empty)') :- !.

% sx_tag(+Tree, -Tag, -Children): decompose into head label + child trees
sx_tag(lst(H,T),    list,      [H,T])     :- !.
sx_tag(op2(Op,L,R), Op,        [L,R])     :- !.
sx_tag(op1(Op,A),   Op,        [A])       :- !.
sx_tag(call(F,As),  call,      [atom(F)|As]) :- !.
sx_tag(braces(T),   braces,    [T])       :- !.
sx_tag(fact(T),     fact,      [T])       :- !.
sx_tag(clause(H,B), clause,    [H,B])     :- !.
sx_tag(dcg(H,B),    dcg,       [H,B])     :- !.
sx_tag(directive(D),directive, [D])       :- !.
sx_tag(query(Q),    query,     [Q])       :- !.
sx_tag(empty,       empty,     [])        :- !.

% pp(+Tree, +Indent, +Col) — pretty-print, tracking current column
pp(Tree, Indent, Col) :-
    sx_flat(Tree, Flat), !,
    atom_length(Flat, Len),
    max_width(W),
    ( Col + Len =< W ->
        write(Flat)                          % fits on this line
    ;
        ( sx_tag(Tree, Tag, []) ->
            write('('), write(Tag), write(')')  % leaf compound, no children
        ; sx_tag(Tree, Tag, Children) ->
            write('('), write(Tag),
            atom_length(Tag, TLen),
            Col2 is Col + 1 + TLen,
            Indent2 is Indent + 2,
            pp_children(Children, Indent2, Col2)
        ;
            write(Flat)                      % fallback
        )
    ).

pp_children([], _, _) :- write(')').
pp_children([C|Cs], Indent, Col) :-
    sx_flat(C, CF), atom_length(CF, CL),
    max_width(W),
    ( Col + 1 + CL =< W ->
        write(' '), pp(C, Indent, Col + 1),
        Col2 is Col + 1 + CL,
        pp_children_rest(Cs, Indent, Col2)
    ;
        nl, write_indent(Indent),
        pp(C, Indent, Indent),
        sx_flat(C, _), atom_length(CF, CL2),
        Col2 is Indent + CL2,
        pp_children_rest(Cs, Indent, Col2)
    ).

pp_children_rest([], _, _) :- write(')').
pp_children_rest([C|Cs], Indent, Col) :-
    sx_flat(C, CF), atom_length(CF, CL),
    max_width(W),
    ( Col + 1 + CL =< W ->
        write(' '), pp(C, Indent, Col + 1),
        Col2 is Col + 1 + CL,
        pp_children_rest(Cs, Indent, Col2)
    ;
        nl, write_indent(Indent),
        pp(C, Indent, Indent),
        Col2 is Indent + CL,
        pp_children_rest(Cs, Indent, Col2)
    ).

write_indent(0) :- !.
write_indent(N) :- N > 0, write(' '), N1 is N - 1, write_indent(N1).

% Replace print_sx calls in parse_loop
pp_top(Tree) :- pp(Tree, 0, 0), nl.
