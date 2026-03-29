:- initialization(main).
main :-
    X is gcd(12, 8), write(X), nl,
    Y is gcd(100, 75), write(Y), nl.
