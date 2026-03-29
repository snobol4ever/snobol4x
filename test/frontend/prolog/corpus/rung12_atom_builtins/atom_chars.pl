:- initialization(main).
main :-
    atom_chars(hi, Cs),
    write(Cs), nl,
    atom_chars(A, [w,o,r,l,d]),
    write(A), nl.
