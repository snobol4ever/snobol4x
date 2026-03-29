:- initialization(main).
main :-
    atom_concat(foo, bar, R),
    write(R), nl,
    atom_concat(hello, ' world', R2),
    write(R2), nl.
