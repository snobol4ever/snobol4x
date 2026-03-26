:- initialization(main).
main :-
    term_to_atom(foo(1,2), A), write(A), nl,
    term_to_atom(hello, B), write(B), nl,
    term_to_atom([1,2,3], C), write(C), nl.
