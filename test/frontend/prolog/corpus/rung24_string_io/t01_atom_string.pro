:- initialization(main).
main :-
    atom_string(hello, S), write(S), nl,
    atom_string(A, "world"), write(A), nl,
    atom_string(42, S2), write(S2), nl.
