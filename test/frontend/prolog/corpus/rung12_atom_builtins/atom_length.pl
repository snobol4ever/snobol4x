:- initialization(main).
main :-
    atom_length(hello, N),
    write(N), nl,
    atom_length('', Z),
    write(Z), nl.
