:- initialization(main).
main :-
    upcase_atom(hello, U),
    write(U), nl,
    downcase_atom('WORLD', D),
    write(D), nl,
    atom_length(abcde, N),
    write(N), nl.
