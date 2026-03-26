:- initialization(main).
main :-
    term_string(point(3,4), S), write(S), nl,
    term_string(42, S2), write(S2), nl.
