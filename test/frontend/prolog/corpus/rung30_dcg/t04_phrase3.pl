:- initialization(main).

ab --> [a], [b].

main :-
    phrase(ab, [a,b,c,d], Rest),
    write(Rest), nl.
