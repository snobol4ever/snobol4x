% rung05_backtrack — member/2, fail, multiple solutions
% Expected output: a b c (one per line)
:- initialization(main).
member(X, [X|_]).
member(X, [_|T]) :- member(X, T).
main :- member(X, [a, b, c]), write(X), nl, fail ; true.
