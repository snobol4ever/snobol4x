% coverage_pl_nodes.pl — exercises every Prolog IR node kind
% Covers: E_CLAUSE E_CHOICE E_UNIFY E_CUT E_FNC E_QLIT E_ILIT E_FLIT
%         E_VART E_ADD E_SUB E_MPY E_DIV E_TRAIL_MARK E_TRAIL_UNWIND

% E_CLAUSE + E_CHOICE — predicate with multiple clauses (choice point)
color(red).
color(green).
color(blue).

% E_UNIFY — unification
unify_test(X, X).

% E_CUT — cut
first_color(X) :- color(X), !.

% E_FNC — builtin call (write/1, nl/0, is/2)
% E_ILIT — integer literal
% E_ADD E_SUB E_MPY E_DIV — arithmetic
arith_test :-
    X is 3 + 4,
    Y is 10 - 3,
    Z is 3 * 4,
    W is 10 / 2,
    write(X), nl,
    write(Y), nl,
    write(Z), nl,
    write(W), nl.

% E_QLIT — atom literal
atom_test :-
    X = hello,
    write(X), nl.

% E_FLIT — float literal
float_test :-
    X is 1.5 + 0.5,
    write(X), nl.

% E_VART — variable
var_test(X) :-
    write(X), nl.

% E_TRAIL_MARK + E_TRAIL_UNWIND — backtracking exercises the trail
trail_test :-
    color(X),
    write(X), nl,
    fail.
trail_test.

:- write(start), nl.
:- arith_test.
:- atom_test.
:- float_test.
:- var_test(world).
:- first_color(C), write(C), nl.
:- unify_test(hello, hello), write(unified), nl.
:- trail_test.
:- write(done), nl.
