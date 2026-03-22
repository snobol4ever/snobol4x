% rung04_arith — is/2, arithmetic, comparisons
% Expected output: 6  true  false
:- initialization(main).
main :-
    X is 2 * 3,
    write(X), nl,
    ( 3 < 5 -> write(true) ; write(false) ), nl,
    ( 5 < 3 -> write(true) ; write(false) ), nl.
