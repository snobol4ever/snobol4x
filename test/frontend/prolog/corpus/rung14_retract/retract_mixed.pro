:- assertz(fact(1)).
:- assertz(fact(2)).
:- assertz(fact(3)).

main :-
    retract(fact(2)),
    fact(X),
    write(X), nl,
    fail.
main.
