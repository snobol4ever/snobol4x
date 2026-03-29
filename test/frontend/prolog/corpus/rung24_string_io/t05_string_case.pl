:- initialization(main).
main :-
    string_upper("hello", U), write(U), nl,
    string_lower("WORLD", L), write(L), nl,
    string_upper(foo, U2), write(U2), nl.
