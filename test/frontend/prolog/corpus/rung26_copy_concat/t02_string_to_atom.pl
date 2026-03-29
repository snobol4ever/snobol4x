:- initialization(main).
main :-
    string_to_atom(hello, A), write(A), nl,
    string_to_atom(S, world), write(S), nl.
