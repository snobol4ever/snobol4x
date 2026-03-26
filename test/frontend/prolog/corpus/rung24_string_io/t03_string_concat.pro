:- initialization(main).
main :-
    string_concat("foo", "bar", S), write(S), nl,
    string_concat(hello, ' world', S2), write(S2), nl.
