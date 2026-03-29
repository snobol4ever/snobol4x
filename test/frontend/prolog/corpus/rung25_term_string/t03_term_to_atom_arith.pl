:- initialization(main).
main :-
    term_to_atom(1+2, A), write(A), nl,
    term_to_atom(f(a,b,c), B), write(B), nl.
