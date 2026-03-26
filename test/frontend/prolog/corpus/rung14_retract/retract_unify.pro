:- assertz(age(alice, 30)).
:- assertz(age(bob, 25)).

main :-
    retract(age(bob, X)),
    write(X), nl.
