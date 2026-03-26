:- initialization(main).
main :-
    number_string(42, S), write(S), nl,
    number_string(N, "99"), write(N), nl.
