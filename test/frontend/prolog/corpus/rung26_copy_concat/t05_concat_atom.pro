:- initialization(main).
main :-
    concat_atom([foo, bar, baz], A), write(A), nl.
