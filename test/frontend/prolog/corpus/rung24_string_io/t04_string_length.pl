:- initialization(main).
main :-
    string_length("hello", N), write(N), nl,
    string_length(abcde, M), write(M), nl.
