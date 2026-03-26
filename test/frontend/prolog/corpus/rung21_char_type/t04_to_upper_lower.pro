:- initialization(main).
main :- char_type(a, to_upper(U)), write(U), nl,
        char_type('Z', to_lower(L)), write(L), nl.
