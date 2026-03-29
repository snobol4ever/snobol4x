:- initialization(main).
main :-
    X is float_integer_part(3.7), write(X), nl,
    Y is float_fractional_part(3.7), write(Y), nl,
    Z is float(5), write(Z), nl.
