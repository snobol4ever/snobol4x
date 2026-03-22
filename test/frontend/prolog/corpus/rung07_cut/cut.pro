% rung07_cut — !, differ/N, closed-world negation
% Expected output: differ(a,b)=yes  differ(a,a)=no
:- initialization(main).

differ(X, X) :- !, fail.
differ(_, _).

main :-
    ( differ(a, b) -> write(yes) ; write(no) ), nl,
    ( differ(a, a) -> write(yes) ; write(no) ), nl.
