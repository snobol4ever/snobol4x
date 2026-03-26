:- initialization(main).
main :- char_type('A', upper(L)), write(L), nl,
        char_type(b, lower(U)), write(U), nl.
