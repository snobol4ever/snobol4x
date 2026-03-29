% coverage_net_gaps.pro — exercises Prolog IR nodes missing from prolog_emit_net.c
% Covers: E_ADD E_SUB E_MPY E_DIV E_ILIT E_FLIT E_CUT E_TRAIL_MARK E_TRAIL_UNWIND E_UNIFY
% (E_QLIT E_VART E_FNC E_CLAUSE E_CHOICE already handled in prolog_emit_net.c)

:- initialization(main, main).

% E_ADD E_SUB E_MPY E_DIV — arithmetic via is/2
arith(X, Y, Sum, Diff, Prod, Quot) :-
    Sum  is X + Y,
    Diff is X - Y,
    Prod is X * Y,
    Quot is X / Y.

% E_FLIT — float literal
float_check(R) :-
    R is 3.14 * 2.0.

% E_CUT — cut in clause
max(X, Y, X) :- X >= Y, !.
max(_, Y, Y).

% E_UNIFY — =/2 unification
unify_test(X, X).

% E_TRAIL_MARK / E_TRAIL_UNWIND — exercised by any backtracking predicate
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).

main :-
    arith(10, 3, S, D, P, Q),
    write(S), nl,   % 13
    write(D), nl,   % 7
    write(P), nl,   % 30
    write(Q), nl,   % 3
    float_check(R),
    write(R), nl,   % 6.28
    max(5, 3, M),
    write(M), nl,   % 5
    unify_test(hello, V),
    write(V), nl,   % hello
    member(X, [a, b, c]),
    write(X), nl,
    fail ; true.
